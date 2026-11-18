#!/usr/bin/env python3
"""Loopback demonstration backend for the real VaporView Gateway v1 client."""

import argparse
import base64
from collections import deque
from datetime import datetime, timedelta, timezone
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import mimetypes
import os
from pathlib import Path, PurePosixPath
import secrets
import sqlite3
import subprocess
import tempfile
import threading
import time
from urllib.parse import parse_qs, unquote, urlsplit
import uuid
import webbrowser

MAX_JSON = 1024 * 1024
MAX_SESSION_BYTES = 256 * 1024 * 1024
MAX_FILE_BYTES = 128 * 1024 * 1024
ALLOWED_COMMANDS = {1, 3, 10}


def utc_now():
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def compact(value):
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"), allow_nan=False, sort_keys=True)


class ApiError(Exception):
    def __init__(self, status, detail):
        self.status, self.detail = status, detail


def text(value, name, maximum=128):
    if not isinstance(value, str) or not value or len(value) > maximum:
        raise ApiError(400, "invalid " + name)
    return value


def number(value, name, maximum):
    if isinstance(value, str) and value.isascii() and value.isdecimal():
        value = int(value)
    if type(value) is not int or value < 0 or value > maximum:
        raise ApiError(400, "invalid " + name)
    return value


def safe_path(root, relative):
    """No absolute paths, traversal, backslashes, drive paths, links or junctions."""
    if not isinstance(relative, str) or not relative or len(relative) > 512 or "\\" in relative or ":" in relative:
        raise ApiError(400, "invalid relative path")
    parts = PurePosixPath(relative).parts
    if relative.startswith("/") or any(part in (".", "..") for part in relative.split("/")):
        raise ApiError(400, "invalid relative path")
    root = root.resolve()
    current = root
    for part in parts:
        current = current / part
        if current.is_symlink() or (hasattr(current, "is_junction") and current.is_junction()):
            raise ApiError(400, "links are not supported")
    target = current.resolve()
    if target == root or not target.is_relative_to(root):
        raise ApiError(400, "path escapes data directory")
    return target


class DemoState:
    def __init__(self, directory, device_id="demo-aircraft", token=None, client=None):
        self.directory = Path(directory).resolve()
        self.directory.mkdir(parents=True, exist_ok=True)
        self.sessions_dir = self.directory / "sessions"
        self.sessions_dir.mkdir(exist_ok=True)
        self.device_id = device_id
        self.token = token or secrets.token_urlsafe(32)
        self.client = Path(client).resolve() if client else None
        self.url = ""
        self.lock = threading.RLock()
        self.online = True
        self.events = deque(maxlen=120)
        self.stats = dict.fromkeys(("telemetryBatches", "frames", "duplicates", "commandResults",
                                   "sessionBytes", "sessionsComplete", "rejectedRequests"), 0)
        self.verification = {"state": "idle", "detail": "等待真实 Gateway 验证", "checks": []}
        self.verify_outage = False
        self.verify_failures = 0
        self.verify_recovered = False
        self.outage_started_us = 0
        self.outage_ended_us = 0
        self.replayed_frames = 0
        self.process = None
        self.cancel_path = None
        self.verification_thread = None
        self.closing = False
        self.db = sqlite3.connect(self.directory / "gateway-demo.sqlite3", check_same_thread=False)
        self.db.row_factory = sqlite3.Row
        self.db.executescript("""
            PRAGMA journal_mode=WAL;
            PRAGMA synchronous=FULL;
            CREATE TABLE IF NOT EXISTS telemetry(id TEXT PRIMARY KEY, body TEXT NOT NULL, frames INTEGER NOT NULL);
            CREATE TABLE IF NOT EXISTS commands(id TEXT PRIMARY KEY, body TEXT NOT NULL, status TEXT NOT NULL, result TEXT);
            CREATE TABLE IF NOT EXISTS results(id TEXT PRIMARY KEY, body TEXT NOT NULL);
            CREATE TABLE IF NOT EXISTS receipts(id TEXT NOT NULL, kind TEXT NOT NULL, digest TEXT NOT NULL,
                PRIMARY KEY(id,kind));
            CREATE TABLE IF NOT EXISTS sessions(id TEXT PRIMARY KEY, complete INTEGER NOT NULL DEFAULT 0, manifest TEXT);
            CREATE TABLE IF NOT EXISTS files(session TEXT NOT NULL, path TEXT NOT NULL, size INTEGER NOT NULL,
                received INTEGER NOT NULL, PRIMARY KEY(session,path));
            CREATE TABLE IF NOT EXISTS metadata(key TEXT PRIMARY KEY, value TEXT NOT NULL);
        """)
        owner = self.db.execute("SELECT value FROM metadata WHERE key='device_id'").fetchone()
        if owner and owner[0] != device_id:
            self.db.close()
            raise ValueError("This data directory belongs to another device ID. Use a different --data-dir; do not reuse its data.")
        if not owner:
            has_data = any(self.db.execute("SELECT 1 FROM " + table + " LIMIT 1").fetchone()
                           for table in ("telemetry", "commands", "results", "receipts", "sessions", "files"))
            if has_data or any(self.sessions_dir.iterdir()):
                self.db.close()
                raise ValueError("This data directory contains unowned legacy data. Use a new --data-dir; device ownership cannot be inferred.")
            self.db.execute("INSERT INTO metadata VALUES('device_id',?)", (device_id,))
            self.db.commit()
        self.stats["telemetryBatches"], self.stats["frames"] = self.db.execute(
            "SELECT COUNT(*),COALESCE(SUM(frames),0) FROM telemetry").fetchone()
        self.stats["commandResults"] = self.db.execute("SELECT COUNT(*) FROM results").fetchone()[0]
        self.stats["sessionBytes"] = self.db.execute("SELECT COALESCE(SUM(received),0) FROM files").fetchone()[0]
        self.stats["sessionsComplete"] = self.db.execute("SELECT COUNT(*) FROM sessions WHERE complete=1").fetchone()[0]

    def close(self):
        self.closing = True
        process = self.process
        if process and process.poll() is None:
            if self.cancel_path:
                self.cancel_path.write_text("cancel", encoding="utf-8")
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.terminate()
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
        if self.verification_thread:
            self.verification_thread.join(timeout=5)
        self.db.close()

    def event(self, method, path, status, detail):
        with self.lock:
            self.events.append({"time": utc_now(), "method": method, "path": path, "status": status, "detail": detail})
            if status >= 400:
                self.stats["rejectedRequests"] += 1

    def connection(self, reveal=False):
        result = {"url": self.url, "deviceId": self.device_id}
        if reveal:
            result["token"] = self.token
        return result

    def snapshot(self):
        with self.lock:
            commands = []
            for row in self.db.execute("SELECT body,status,result FROM commands ORDER BY rowid DESC LIMIT 50"):
                value = json.loads(row["body"])
                value["status"] = row["status"]
                if row["result"]:
                    value["result"] = json.loads(row["result"])
                commands.append(value)
            sessions = []
            for row in self.db.execute("SELECT id,complete FROM sessions ORDER BY rowid DESC LIMIT 16"):
                files = [dict(item) for item in self.db.execute(
                    "SELECT path,size,received FROM files WHERE session=? ORDER BY path", (row["id"],))]
                sessions.append({"sessionId": row["id"], "complete": bool(row["complete"]),
                                 "bytes": sum(item["received"] for item in files), "files": files})
            return {"online": self.online, "deviceId": self.device_id, "stats": dict(self.stats),
                    "telemetry": [json.loads(row[0]) for row in self.db.execute(
                        "SELECT body FROM telemetry ORDER BY rowid DESC LIMIT 20")],
                    "commands": commands, "sessions": sessions, "events": list(self.events),
                    "verification": json.loads(compact(self.verification)), "connection": self.connection()}

    def create_command(self, command_id, ttl=60):
        if type(command_id) is not int or command_id not in ALLOWED_COMMANDS:
            raise ApiError(400, "only status, start recording and stop recording are supported")
        if type(ttl) is not int or not 1 <= ttl <= 86400:
            raise ApiError(400, "ttlSeconds must be between 1 and 86400")
        if self.db.execute("SELECT COUNT(*) FROM commands WHERE status='queued'").fetchone()[0] >= 32:
            raise ApiError(409, "command queue is full")
        value = {"id": str(uuid.uuid4()), "deviceId": self.device_id, "commandId": command_id,
                 "payloadBase64": "", "expiresAt": (datetime.now(timezone.utc) + timedelta(seconds=ttl)).isoformat(
                     timespec="milliseconds").replace("+00:00", "Z")}
        self.db.execute("INSERT INTO commands(id,body,status) VALUES(?,?,'queued')", (value["id"], compact(value)))
        self.db.execute("DELETE FROM commands WHERE rowid NOT IN (SELECT rowid FROM commands ORDER BY rowid DESC LIMIT 4096) AND status!='queued'")
        self.db.commit()
        return {**value, "status": "queued"}

    def gateway(self, endpoint, body, query):
        with self.lock:
            saved_stats = dict(self.stats)
            try:
                return self._gateway(endpoint, body, query)
            except Exception:
                self.db.rollback()
                self.stats = saved_stats
                raise

    def _gateway(self, endpoint, body, query):
        with self.lock:
            if not self.online:
                if self.verification["state"] == "running":
                    self.verify_failures += 1
                raise ApiError(503, "demo internet connection is offline")
            if endpoint == "commands":
                if query.get("deviceId") != [self.device_id]:
                    raise ApiError(403, "device permission denied")
                return {"commands": [json.loads(row[0]) for row in self.db.execute(
                    "SELECT body FROM commands WHERE status='queued' ORDER BY rowid LIMIT 32")]}
            if not isinstance(body, dict) or body.get("deviceId") != self.device_id:
                raise ApiError(403, "device permission denied")
            if endpoint in ("telemetry", "command-results"):
                return self._items(endpoint, body)
            if endpoint == "sessions":
                return self._session(body)
            raise ApiError(404, "unknown API")

    def _items(self, endpoint, body):
        items = body.get("items")
        if not isinstance(items, list) or not 1 <= len(items) <= 64:
            raise ApiError(400, "items must contain 1 to 64 messages")
        accepted = []
        new_frames = 0
        replayed_frames = 0
        for item in items:
            if not isinstance(item, dict):
                raise ApiError(400, "invalid item")
            message_id = text(item.get("id"), "message ID")
            encoded = compact(item)
            table = "telemetry" if endpoint == "telemetry" else "results"
            digest = hashlib.sha256(encoded.encode()).hexdigest()
            row = self.db.execute("SELECT digest FROM receipts WHERE id=? AND kind=?", (message_id, table)).fetchone()
            if row:
                if row[0] != digest:
                    raise ApiError(409, "message ID reused with different content")
                self.stats["duplicates"] += 1
                accepted.append(message_id)
                continue
            if self.db.execute("SELECT COUNT(*) FROM receipts").fetchone()[0] >= 8192:
                raise ApiError(409, "demo receipt limit reached; start a new temporary demo")
            if endpoint == "telemetry":
                frames = item.get("frames")
                if item.get("kind") != "telemetry" or not isinstance(frames, list) or not 1 <= len(frames) <= 4096:
                    raise ApiError(400, "invalid telemetry frames")
                for frame in frames:
                    if not isinstance(frame, dict):
                        raise ApiError(400, "invalid telemetry frame")
                    number(frame.get("type"), "type", 255)
                    number(frame.get("sequence"), "sequence", 65535)
                    number(frame.get("flags"), "flags", 255)
                    number(frame.get("timeUs"), "timeUs", 2**64 - 1)
                    try:
                        payload = base64.b64decode(frame.get("payloadBase64", ""), validate=True)
                    except (ValueError, TypeError):
                        raise ApiError(400, "invalid frame encoding") from None
                    if len(payload) > 256 * 1024:
                        raise ApiError(413, "frame payload too large")
                self.db.execute("INSERT INTO telemetry VALUES(?,?,?)", (message_id, encoded, len(frames)))
                self.stats["telemetryBatches"] += 1
                self.stats["frames"] += len(frames)
                new_frames += len(frames)
                if self.outage_ended_us:
                    replayed = sum(self.outage_started_us <= int(frame["timeUs"]) <= self.outage_ended_us for frame in frames)
                    replayed_frames += replayed
            else:
                if len(encoded) > 16 * 1024:
                    raise ApiError(413, "command result is too large")
                command_id = text(item.get("commandId"), "command ID")
                if item.get("kind") != "command-result" or not isinstance(item.get("status"), str):
                    raise ApiError(400, "invalid command result")
                if not self.db.execute("SELECT 1 FROM commands WHERE id=?", (command_id,)).fetchone():
                    raise ApiError(409, "unknown command")
                self.db.execute("INSERT INTO results VALUES(?,?)", (message_id, encoded))
                self.db.execute("UPDATE commands SET status=?,result=? WHERE id=?", (item["status"], encoded, command_id))
                self.stats["commandResults"] += 1
            self.db.execute("INSERT INTO receipts VALUES(?,?,?)", (message_id, table, digest))
            accepted.append(message_id)
        # Bounded inspection history; counters retain totals for this run.
        self.db.execute("DELETE FROM telemetry WHERE rowid NOT IN (SELECT rowid FROM telemetry ORDER BY rowid DESC LIMIT 200)")
        self.db.execute("DELETE FROM results WHERE rowid NOT IN (SELECT rowid FROM results ORDER BY rowid DESC LIMIT 4096)")
        self.db.commit()
        self.replayed_frames += replayed_frames
        if self.verify_failures and replayed_frames:
            self.verify_recovered = True
        if new_frames and self.verify_outage:
            self.verify_outage = False
            self.online = False
            self.outage_started_us = time.time_ns() // 1000
            timer = threading.Timer(3, self._restore_network)
            timer.daemon = True
            timer.start()
        return {"acceptedIds": accepted}

    def _restore_network(self):
        with self.lock:
            self.online = True
            self.outage_ended_us = time.time_ns() // 1000
            self.event("DEMO", "/demo/v1/network", 200, "自动恢复演示网络")

    def _session(self, body):
        session_id = text(body.get("sessionId"), "session ID")
        # Session IDs never become paths. Only their hash is used on disk.
        session_root = self.sessions_dir / hashlib.sha256(session_id.encode()).hexdigest()
        if session_root.is_symlink() or (hasattr(session_root, "is_junction") and session_root.is_junction()):
            raise ApiError(400, "linked Session directory")
        known = self.db.execute("SELECT complete,manifest FROM sessions WHERE id=?", (session_id,)).fetchone()
        if not known:
            if self.db.execute("SELECT COUNT(*) FROM sessions").fetchone()[0] >= 16:
                raise ApiError(409, "Session limit reached")
            self.db.execute("INSERT INTO sessions(id) VALUES(?)", (session_id,))
        if body.get("complete") is True:
            manifest = body.get("files")
            if not isinstance(manifest, list) or not 1 <= len(manifest) <= 4096:
                raise ApiError(400, "invalid Session manifest")
            expected = {}
            for entry in manifest:
                if not isinstance(entry, dict):
                    raise ApiError(400, "invalid manifest entry")
                relative = text(entry.get("path"), "file path", 512)
                safe_path(session_root, relative)
                if relative in expected:
                    raise ApiError(400, "duplicate manifest path")
                expected[relative] = number(entry.get("size"), "file size", MAX_FILE_BYTES)
            rows = list(self.db.execute("SELECT path,size,received FROM files WHERE session=?", (session_id,)))
            if set(expected) != {row["path"] for row in rows}:
                raise ApiError(409, "Session files are incomplete")
            for row in rows:
                target = safe_path(session_root, row["path"])
                if row["size"] != expected[row["path"]] or row["received"] != row["size"] or not target.is_file() or target.stat().st_size != row["size"]:
                    raise ApiError(409, "Session file is incomplete")
            encoded = compact(manifest)
            if known and known["complete"] and known["manifest"] != encoded:
                raise ApiError(409, "completed Session cannot change")
            self.db.execute("UPDATE sessions SET complete=1,manifest=? WHERE id=?", (encoded, session_id))
            self.db.commit()
            if not known or not known["complete"]:
                self.stats["sessionsComplete"] += 1
            return {"accepted": True}
        if body.get("complete") is not False:
            raise ApiError(400, "complete must be boolean")
        relative = text(body.get("relativePath"), "file path", 512)
        target = safe_path(session_root, relative)
        total = number(body.get("totalBytes"), "totalBytes", MAX_FILE_BYTES)
        offset = number(body.get("offset"), "offset", total)
        try:
            data = base64.b64decode(body.get("dataBase64", ""), validate=True)
        except (ValueError, TypeError):
            raise ApiError(400, "invalid chunk encoding") from None
        if len(data) > 256 * 1024 or offset + len(data) > total or (not data and total != 0):
            raise ApiError(400, "invalid chunk size")
        if body.get("chunkSha256") != hashlib.sha256(data).hexdigest():
            raise ApiError(400, "chunk hash mismatch")
        row = self.db.execute("SELECT size,received FROM files WHERE session=? AND path=?", (session_id, relative)).fetchone()
        received = row["received"] if row else 0
        if row and row["size"] != total:
            raise ApiError(409, "file size changed")
        if row and offset + len(data) <= received:
            if offset + len(data) > received or not target.is_file():
                raise ApiError(409, "overlapping chunk")
            with target.open("rb") as file:
                file.seek(offset)
                if file.read(len(data)) != data:
                    raise ApiError(409, "duplicate chunk differs")
            self.stats["duplicates"] += 1
            return {"accepted": True, "nextOffset": str(offset + len(data))}
        if offset != received or (known and known["complete"]):
            raise ApiError(409, "unexpected offset or completed Session")
        if self.stats["sessionBytes"] + len(data) > MAX_SESSION_BYTES:
            raise ApiError(413, "Session storage limit reached")
        if not row and self.db.execute("SELECT COUNT(*) FROM files WHERE session=?", (session_id,)).fetchone()[0] >= 4096:
            raise ApiError(409, "Session file limit reached")
        target.parent.mkdir(parents=True, exist_ok=True)
        safe_path(session_root, relative)
        with target.open("r+b" if target.exists() else "w+b") as file:
            file.truncate(received)
            file.seek(received)
            file.write(data)
            file.flush()
            os.fsync(file.fileno())
        self.db.execute("INSERT INTO files VALUES(?,?,?,?) ON CONFLICT(session,path) DO UPDATE SET received=excluded.received",
                        (session_id, relative, total, received + len(data)))
        self.db.commit()
        self.stats["sessionBytes"] += len(data)
        return {"accepted": True, "nextOffset": str(received + len(data))}

    def start_verification(self):
        with self.lock:
            if self.verification["state"] == "running":
                raise ApiError(409, "verification is already running")
            if not self.client or not self.client.is_file():
                raise ApiError(409, "start server with --client pointing to gateway_service_test.exe")
            self.online = True
            self.verify_outage = True
            self.verify_failures = 0
            self.verify_recovered = False
            self.outage_started_us = 0
            self.outage_ended_us = 0
            self.replayed_frames = 0
            before = dict(self.stats)
            command = self.create_command(10, 120)
            self.verification = {"state": "running", "detail": "真实 Gateway 正在连接模拟 SkyCore 与此后台", "checks": [], "startedAt": utc_now()}
            thread = threading.Thread(target=self._verification_worker, args=(before, command["id"]), daemon=True)
            self.verification_thread = thread
            thread.start()
            return dict(self.verification)

    def _verification_worker(self, before, command_id):
        report_path = self.directory / ("verification-" + uuid.uuid4().hex + ".json")
        cancel_path = self.directory / ("verification-cancel-" + uuid.uuid4().hex)
        self.cancel_path = cancel_path
        environment = os.environ.copy()
        environment.update(GATEWAY_DEMO_URL=self.url, GATEWAY_DEMO_TOKEN=self.token,
                           GATEWAY_DEMO_DEVICE=self.device_id, GATEWAY_DEMO_REPORT=str(report_path), GATEWAY_DEMO_CANCEL=str(cancel_path))
        report, failure = {}, ""
        try:
            process = subprocess.Popen([str(self.client), "--web-demo"], env=environment,
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                       creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            self.process = process
            if self.closing:
                cancel_path.write_text("cancel", encoding="utf-8")
            try:
                returncode = process.wait(timeout=60)
            except subprocess.TimeoutExpired:
                cancel_path.write_text("cancel", encoding="utf-8")
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                raise
            if report_path.is_file() and report_path.stat().st_size <= MAX_JSON:
                report = json.loads(report_path.read_text(encoding="utf-8"))
            if returncode:
                failure = "Gateway 验证客户端退出码 " + str(returncode)
        except (OSError, ValueError, subprocess.TimeoutExpired) as error:
            failure = "Gateway 客户端启动、报告或超时失败（" + type(error).__name__ + "）"
        finally:
            report_path.unlink(missing_ok=True)
            cancel_path.unlink(missing_ok=True)
        with self.lock:
            if self.closing:
                return
            row = self.db.execute("SELECT status FROM commands WHERE id=?", (command_id,)).fetchone()
            checks = [
                {"name": "真实 Gateway 客户端", "passed": bool(report.get("passed")) and not failure,
                 "detail": failure or "C++ GatewayService 客户端报告"},
                {"name": "遥测到达后台", "passed": self.stats["frames"] > before["frames"], "detail": "HTTP 接收帧计数"},
                {"name": "现有 SkyCore 链路命令 ACK", "passed": bool(row and row[0] == "acknowledged"), "detail": "读取状态命令结果"},
                {"name": "Session 完整同步", "passed": self.stats["sessionsComplete"] > before["sessionsComplete"], "detail": "分块落盘并确认完成"},
                {"name": "断网缓存及恢复", "passed": self.verify_failures > 0 and self.verify_recovered,
                 "detail": "后台返回 503；恢复后确认 %s 帧采集时间位于实际断网区间" % self.replayed_frames},
            ]
            for check in report.get("checks", []):
                if isinstance(check, dict) and isinstance(check.get("name"), str):
                    checks.append({"name": check["name"][:120], "passed": check.get("passed") is True,
                                   "detail": str(check.get("detail", ""))[:300]})
            passed = all(check["passed"] for check in checks)
            self.online = True
            self.verify_outage = False
            self.verification.update(state="passed" if passed else "failed", checks=checks, finishedAt=utc_now(),
                                     detail="真实 Gateway 往返验证通过" if passed else (failure or "部分检查未通过，请查看各项结果"))


class DemoServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, port, state, web_root=None):
        self.state = state
        self.web_root = (Path(web_root) if web_root else Path(__file__).parent / "web").resolve()
        super().__init__(("127.0.0.1", port), DemoHandler)
        state.url = "http://127.0.0.1:" + str(self.server_port)


class DemoHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_):
        pass  # Headers, tokens and payloads never go to console logs.

    def do_GET(self):
        self._handle(False)

    def do_POST(self):
        self._handle(True)

    def _json(self, status, value):
        payload = compact(value).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(payload)
        self.close_connection = True

    def _handle(self, post):
        parsed = urlsplit(self.path)
        path = unquote(parsed.path)
        state = self.server.state
        try:
            expected_host = "127.0.0.1:" + str(self.server.server_port)
            if self.headers.get("Host") != expected_host:
                raise ApiError(403, "invalid local host")
            origin = self.headers.get("Origin")
            if origin and origin != state.url:
                raise ApiError(403, "cross-origin request denied")
            if self.headers.get("Sec-Fetch-Site") == "cross-site":
                raise ApiError(403, "cross-site request denied")
            body = None
            if post:
                if self.headers.get("Content-Type", "").split(";")[0] != "application/json":
                    raise ApiError(415, "JSON request required")
                try:
                    length = int(self.headers.get("Content-Length", "-1"))
                except ValueError:
                    raise ApiError(400, "invalid content length") from None
                if not 0 <= length <= MAX_JSON:
                    raise ApiError(413, "request too large")
                self.connection.settimeout(10)
                try:
                    body = json.loads(self.rfile.read(length), parse_constant=lambda _: (_ for _ in ()).throw(ValueError()))
                except (ValueError, UnicodeDecodeError):
                    raise ApiError(400, "invalid JSON") from None
                if not isinstance(body, dict):
                    raise ApiError(400, "JSON object required")
            if path.startswith("/gateway/v1/"):
                if not secrets.compare_digest(self.headers.get("Authorization", "").encode("utf-8"),
                                              ("Bearer " + state.token).encode("utf-8")):
                    raise ApiError(401, "bearer authentication required")
                if self.headers.get("X-VaporView-Device-Id") != state.device_id:
                    raise ApiError(403, "device permission denied")
                endpoint = path.removeprefix("/gateway/v1/")
                if (endpoint == "commands") == post:
                    raise ApiError(405, "method not allowed")
                response = state.gateway(endpoint, body, parse_qs(parsed.query))
                state.event(self.command, path, 200, "Gateway v1 已确认")
                self._json(200, response)
                return
            if path == "/demo/v1/state" and not post:
                self._json(200, state.snapshot())
                return
            if path == "/demo/v1/connection" and not post:
                self._json(200, state.connection(True))
                return
            with state.lock:
                if path == "/demo/v1/commands" and post:
                    response = {"command": state.create_command(body.get("commandId"), body.get("ttlSeconds", 60))}
                elif path == "/demo/v1/network" and post:
                    if type(body.get("online")) is not bool:
                        raise ApiError(400, "online must be boolean")
                    state.online = body["online"]
                    response = {"online": state.online}
                elif path == "/demo/v1/verify" and post:
                    response = state.start_verification()
                elif path.startswith("/demo/") or post:
                    raise ApiError(404, "unknown demo API")
                else:
                    self._static(path)
                    return
            status = 202 if path.endswith("/verify") else 200
            state.event(self.command, path, status, "本机演示操作")
            self._json(status, response)
        except ApiError as error:
            state.event(self.command, path[:160], error.status, error.detail)
            self._json(error.status, {"error": error.detail})
        except (OSError, sqlite3.Error, ValueError, TypeError):
            state.event(self.command, path[:160], 500, "本机存储或请求处理失败")
            self._json(500, {"error": "local request processing failed"})

    def _static(self, path):
        if path not in ("/", "/index.html", "/app.js", "/style.css"):
            raise ApiError(404, "file not found")
        target = safe_path(self.server.web_root, "index.html" if path == "/" else path.lstrip("/"))
        if not target.is_file() or target.stat().st_size > MAX_JSON:
            raise ApiError(404, "file not found")
        payload = target.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", (mimetypes.guess_type(target.name)[0] or "application/octet-stream") + "; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Security-Policy", "default-src 'self'; connect-src 'self'; object-src 'none'; frame-ancestors 'none'")
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(payload)
        self.close_connection = True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--device-id", default="demo-aircraft")
    parser.add_argument("--data-dir", type=Path)
    parser.add_argument("--client", type=Path)
    parser.add_argument("--ready-file", type=Path, help="write the public loopback URL, without credentials")
    parser.add_argument("--open-browser", action="store_true")
    args = parser.parse_args()
    if not 0 <= args.port <= 65535 or not args.device_id or len(args.device_id) > 128 or any(
            ord(character) < 33 or ord(character) > 126 for character in args.device_id):
        parser.error("invalid port or device ID")
    temporary = tempfile.TemporaryDirectory(prefix="vaporview-gateway-demo-") if args.data_dir is None else None
    client = args.client or Path(__file__).resolve().parents[2] / "build" / "Release" / "gateway_service_test.exe"
    state, server = None, None
    try:
        state = DemoState(args.data_dir or temporary.name, args.device_id, client=client)
        server = DemoServer(args.port, state)
        print("VaporView Gateway demo: " + state.url, flush=True)
        if args.ready_file:
            args.ready_file.parent.mkdir(parents=True, exist_ok=True)
            args.ready_file.write_text(state.url, encoding="utf-8")
        if args.open_browser:
            webbrowser.open(state.url)
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    except (OSError, sqlite3.Error, ValueError) as error:
        parser.exit(2, "Gateway demo startup failed: " + str(error) + "\n")
    finally:
        if server:
            server.server_close()
        if state:
            state.close()
        if temporary:
            temporary.cleanup()


if __name__ == "__main__":
    main()
