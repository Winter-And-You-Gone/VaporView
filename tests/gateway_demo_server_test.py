"""Protocol and storage checks for the loopback Gateway demonstration server."""

import base64
import hashlib
import http.client
import importlib.util
import json
from pathlib import Path
import tempfile
import threading
import unittest
from unittest import mock

MODULE_PATH = Path(__file__).resolve().parents[1] / "tools" / "gateway_demo" / "server.py"
SPEC = importlib.util.spec_from_file_location("gateway_demo_server", MODULE_PATH)
demo = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(demo)


class DemoServerTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="vaporview-demo-test-")
        self.root = Path(self.temporary.name)
        self.web = self.root / "web"
        self.web.mkdir()
        (self.web / "index.html").write_text("<html>demo</html>", encoding="utf-8")
        self.state = demo.DemoState(self.root / "data")
        self.token = self.state.token
        self.server = demo.DemoServer(0, self.state, self.web)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
        self.state.close()
        self.temporary.cleanup()

    def request(self, method, path, body=None, headers=None, authenticated=True):
        request_headers = {"Content-Type": "application/json"}
        if authenticated:
            request_headers.update(Authorization="Bearer " + self.token, **{"X-VaporView-Device-Id": "demo-aircraft"})
        request_headers.update(headers or {})
        connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=5)
        connection.request(method, path, None if body is None else json.dumps(body), request_headers)
        response = connection.getresponse()
        payload = response.read()
        status, content_type = response.status, response.getheader("Content-Type", "")
        connection.close()
        return status, json.loads(payload) if "application/json" in content_type else payload

    def telemetry(self, message="telemetry-one"):
        return {"deviceId": "demo-aircraft", "items": [{"id": message, "kind": "telemetry",
                "receivedAt": "2026-11-14T00:00:00.000Z", "frames": [{"type": 1, "flags": 0,
                "sequence": 1, "timeUs": "1794614400000000", "payloadBase64": "YWJj"}]}]}

    def chunk(self, path="raw/wave.dat", data=b"abc", offset=0, total=3, session="session-one"):
        return {"deviceId": "demo-aircraft", "sessionId": session, "relativePath": path, "offset": str(offset),
                "totalBytes": str(total), "dataBase64": base64.b64encode(data).decode(),
                "chunkSha256": hashlib.sha256(data).hexdigest(), "complete": False}

    def test_local_host_origin_auth_and_no_implicit_token(self):
        self.assertEqual(self.server.server_address[0], "127.0.0.1")
        self.assertEqual(self.request("GET", "/")[0], 200)
        self.assertEqual(self.request("GET", "/server.py")[0], 404)
        self.assertEqual(self.request("GET", "/../server.py")[0], 404)
        self.assertEqual(self.request("POST", "/gateway/v1/telemetry", self.telemetry(), authenticated=False)[0], 401)
        self.assertEqual(self.request("POST", "/gateway/v1/telemetry", self.telemetry(),
                                      {"X-VaporView-Device-Id": "different"})[0], 403)
        self.assertEqual(self.request("POST", "/demo/v1/commands", {"commandId": 10},
                                      {"Origin": "https://external.example"})[0], 403)
        self.assertEqual(self.request("POST", "/demo/v1/commands", {"commandId": 10},
                                      {"Host": "external.example"})[0], 403)
        status, state = self.request("GET", "/demo/v1/state", authenticated=False)
        self.assertEqual(status, 200)
        self.assertNotIn("token", state["connection"])
        self.assertNotIn(self.token, json.dumps(state))
        self.assertEqual(self.request("GET", "/demo/v1/connection", authenticated=False)[1]["token"], self.token)

    def test_telemetry_durable_idempotency_and_reused_id(self):
        body = self.telemetry()
        self.assertEqual(self.request("POST", "/gateway/v1/telemetry", body), (200, {"acceptedIds": ["telemetry-one"]}))
        self.assertEqual(self.request("POST", "/gateway/v1/telemetry", body)[0], 200)
        self.assertEqual(self.state.stats["frames"], 1)
        self.assertEqual(self.state.stats["duplicates"], 1)
        body["items"][0]["frames"][0]["sequence"] = 2
        self.assertEqual(self.request("POST", "/gateway/v1/telemetry", body)[0], 409)
        self.assertEqual(self.state.db.execute("SELECT COUNT(*) FROM telemetry").fetchone()[0], 1)
        # The receipt survives inspection-history trimming and a new DB connection.
        with self.state.lock:
            self.state.db.execute("DELETE FROM telemetry")
            self.state.db.commit()
        body["items"][0]["frames"][0]["sequence"] = 1
        self.assertEqual(self.request("POST", "/gateway/v1/telemetry", body)[0], 200)
        self.assertEqual(self.state.stats["frames"], 1)

    def test_commands_results_and_permissions(self):
        for invalid in (11, 90, True, "10"):
            self.assertEqual(self.request("POST", "/demo/v1/commands", {"commandId": invalid})[0], 400)
        status, reply = self.request("POST", "/demo/v1/commands", {"commandId": 10, "ttlSeconds": 60})
        self.assertEqual(status, 200)
        command = reply["command"]
        self.assertEqual(command["status"], "queued")
        self.assertTrue(command["expiresAt"].endswith("Z"))
        self.assertEqual(self.request("GET", "/gateway/v1/commands?deviceId=wrong")[0], 403)
        self.assertEqual(self.request("GET", "/gateway/v1/commands?deviceId=demo-aircraft")[1]["commands"][0]["id"], command["id"])
        result = {"deviceId": "demo-aircraft", "items": [{"id": "result-" + hashlib.sha256(command["id"].encode()).hexdigest(),
                  "kind": "command-result", "commandId": command["id"], "skyCommandId": 10, "status": "acknowledged",
                  "completedAt": "2026-11-14T00:00:00.000Z", "errorCode": 0}]}
        self.assertEqual(self.request("POST", "/gateway/v1/command-results", result)[0], 200)
        self.assertEqual(self.request("POST", "/gateway/v1/command-results", result)[0], 200)
        self.assertEqual(self.state.stats["commandResults"], 1)
        self.assertEqual(self.request("GET", "/gateway/v1/commands?deviceId=demo-aircraft")[1], {"commands": []})

    def test_session_offset_hash_duplicates_and_completion(self):
        body = self.chunk(data=b"abc", total=6)
        self.assertEqual(self.request("POST", "/gateway/v1/sessions", body), (200, {"accepted": True, "nextOffset": "3"}))
        self.assertEqual(self.request("POST", "/gateway/v1/sessions", body)[0], 200)
        bad = dict(body, chunkSha256="0" * 64)
        self.assertEqual(self.request("POST", "/gateway/v1/sessions", bad)[0], 400)
        self.assertEqual(self.request("POST", "/gateway/v1/sessions", self.chunk(data=b"xyz", total=6))[0], 409)
        self.assertEqual(self.request("POST", "/gateway/v1/sessions", self.chunk(data=b"x", offset=4, total=6))[0], 409)
        complete = {"deviceId": "demo-aircraft", "sessionId": "session-one", "complete": True,
                    "files": [{"path": "raw/wave.dat", "size": "6", "modified": "1"}]}
        self.assertEqual(self.request("POST", "/gateway/v1/sessions", complete)[0], 409)
        self.assertEqual(self.request("POST", "/gateway/v1/sessions", self.chunk(data=b"def", offset=3, total=6))[0], 200)
        self.assertEqual(self.request("POST", "/gateway/v1/sessions", complete)[0], 200)
        self.assertEqual(self.request("POST", "/gateway/v1/sessions", complete)[0], 200)
        self.assertEqual(self.state.stats["sessionBytes"], 6)
        self.assertEqual(self.state.stats["sessionsComplete"], 1)
        state = self.request("GET", "/demo/v1/state")[1]
        self.assertTrue(state["sessions"][0]["complete"])

    def test_session_path_traversal_and_links(self):
        for path in ("../escape", "/absolute", "C:/escape", "raw/../../escape", "raw\\escape", "./escape"):
            with self.subTest(path=path):
                self.assertEqual(self.request("POST", "/gateway/v1/sessions", self.chunk(path))[0], 400)
        outside = self.root / "outside"
        outside.mkdir()
        session_root = self.state.sessions_dir / hashlib.sha256(b"linked").hexdigest()
        session_root.mkdir()
        try:
            (session_root / "raw").symlink_to(outside, target_is_directory=True)
        except OSError:
            return  # Windows may not grant symbolic-link creation privileges.
        self.assertEqual(self.request("POST", "/gateway/v1/sessions", self.chunk(session="linked"))[0], 400)
        self.assertFalse((outside / "wave.dat").exists())

    def test_offline_api_but_dashboard_remains_available(self):
        self.assertEqual(self.request("POST", "/demo/v1/network", {"online": False})[0], 200)
        self.assertEqual(self.request("POST", "/gateway/v1/telemetry", self.telemetry())[0], 503)
        self.assertEqual(self.request("GET", "/demo/v1/state")[0], 200)
        self.assertEqual(self.request("GET", "/")[0], 200)
        self.assertEqual(self.request("POST", "/demo/v1/network", {"online": True})[0], 200)
        self.assertEqual(self.request("POST", "/gateway/v1/telemetry", self.telemetry())[0], 200)

    def test_invalid_batch_rolls_back_all_messages(self):
        body = self.telemetry()
        body["items"].append({"id": "invalid", "kind": "telemetry", "frames": []})
        self.assertEqual(self.request("POST", "/gateway/v1/telemetry", body)[0], 400)
        self.assertEqual(self.state.stats["frames"], 0)
        self.assertEqual(self.state.db.execute("SELECT COUNT(*) FROM telemetry").fetchone()[0], 0)
        self.assertEqual(self.state.db.execute("SELECT COUNT(*) FROM receipts").fetchone()[0], 0)

    def test_recovery_requires_a_confirmed_frame_captured_during_outage(self):
        with self.state.lock:
            self.state.verify_failures = 1
            self.state.outage_started_us = 1000
            self.state.outage_ended_us = 2000
        self.assertEqual(self.request("GET", "/gateway/v1/commands?deviceId=demo-aircraft")[0], 200)
        self.assertFalse(self.state.verify_recovered)
        body = self.telemetry("outside-outage")
        self.assertEqual(self.request("POST", "/gateway/v1/telemetry", body)[0], 200)
        self.assertFalse(self.state.verify_recovered)
        body = self.telemetry("during-outage")
        body["items"][0]["frames"][0]["timeUs"] = "1500"
        self.assertEqual(self.request("POST", "/gateway/v1/telemetry", body)[0], 200)
        self.assertTrue(self.state.verify_recovered)
        self.assertEqual(self.state.replayed_frames, 1)

    def test_persistent_directory_is_bound_to_one_device(self):
        directory = self.root / "owned-data"
        first = demo.DemoState(directory, device_id="first-device")
        first.close()
        same = demo.DemoState(directory, device_id="first-device")
        same.close()
        with self.assertRaisesRegex(ValueError, "another device ID"):
            demo.DemoState(directory, device_id="second-device")

    def test_legacy_data_without_owner_is_not_automatically_assigned(self):
        directory = self.root / "legacy-data"
        legacy = demo.DemoState(directory)
        with legacy.lock:
            legacy.create_command(10)
            legacy.db.execute("DELETE FROM metadata")
            legacy.db.commit()
        legacy.close()
        with self.assertRaisesRegex(ValueError, "unowned legacy data"):
            demo.DemoState(directory)

    def test_default_temporary_directory_is_removed_on_exit(self):
        factory = tempfile.TemporaryDirectory
        directories = []

        def tracked_directory(**kwargs):
            directory = factory(**kwargs)
            directories.append(Path(directory.name))
            return directory

        class InterruptServer:
            def __init__(self, port, state):
                state.url = "http://127.0.0.1:0"

            def serve_forever(self):
                raise KeyboardInterrupt

            def server_close(self):
                pass

        with mock.patch("sys.argv", ["server.py", "--port", "0"]), mock.patch.object(
                demo, "DemoServer", InterruptServer), mock.patch.object(
                demo.tempfile, "TemporaryDirectory", side_effect=tracked_directory), mock.patch("builtins.print"):
            demo.main()
        self.assertEqual(len(directories), 1)
        self.assertFalse(directories[0].exists())


if __name__ == "__main__":
    unittest.main()
