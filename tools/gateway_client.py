"""Query a VaporView ground gateway from a separate computer (HTTP/WS)."""

import argparse
import json
import re
import sys
import time
import uuid
from datetime import datetime, timedelta, timezone
from urllib.error import HTTPError
from urllib.parse import urlsplit
from urllib.request import (
    HTTPRedirectHandler, ProxyHandler, Request, build_opener,
)


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def command_id(value):
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,64}", value):
        raise argparse.ArgumentTypeError("invalid gateway command ID")
    return value


def display(value):
    print(json.dumps(value, ensure_ascii=False), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("gateway", help="HTTP origin, for example http://ground.example:8443")
    actions = parser.add_subparsers(dest="action", required=True)
    actions.add_parser("status")
    actions.add_parser("latest")
    stream = actions.add_parser("stream")
    stream.add_argument("--count", type=int, default=5, help="number of messages to receive")
    control = actions.add_parser("command")
    commands = {"request-status": 10, "start-recording": 1, "stop-recording": 3}
    control.add_argument("name", choices=commands)
    result = actions.add_parser("result")
    result.add_argument("id", type=command_id, help="command ID returned by the gateway")
    args = parser.parse_args()
    origin = urlsplit(args.gateway)
    if (origin.scheme != "http" or not origin.hostname or origin.username is not None
            or origin.query or origin.fragment or origin.path not in ("", "/")):
        parser.error("provide the HTTP origin of the ground gateway")
    root = f"http://{origin.netloc}/api/v1"
    opener = build_opener(ProxyHandler({}), NoRedirect())

    def request(path, body=None):
        req = Request(root + path)
        if body is not None:
            req.data = json.dumps(body).encode("utf-8")
            req.add_header("Content-Type", "application/json")
        with opener.open(req, timeout=15) as response:
            return json.load(response)

    if args.action == "stream":
        if args.count <= 0:
            parser.error("--count must be positive")
        try:
            import websocket  # websocket-client
        except ImportError:
            parser.error("install websocket-client for the stream command")
        connection = websocket.create_connection(
            f"ws://{origin.netloc}/api/v1/stream", timeout=15,
            http_no_proxy=[origin.hostname], redirect_limit=0,
        )
        try:
            for _ in range(args.count):
                display(json.loads(connection.recv()))
        finally:
            connection.close()
    elif args.action == "command":
        identifier = str(uuid.uuid4())
        expires = datetime.now(timezone.utc) + timedelta(seconds=30)
        # Keep the ID visible even if a response is lost after submission.
        print(f"Command ID: {identifier}", file=sys.stderr, flush=True)
        reply = request("/commands", {"id": identifier, "commandId": commands[args.name],
                                     "expiresAt": expires.strftime("%Y-%m-%dT%H:%M:%SZ")})
        display(reply)
        deadline = time.monotonic() + 35
        while reply.get("status") == "accepted" and time.monotonic() < deadline:
            time.sleep(0.25)
            reply = request(f"/commands/{identifier}")
        display(reply)
        return 0 if reply.get("status") == "acknowledged" else 1
    elif args.action == "result":
        display(request(f"/commands/{args.id}"))
    else:
        display(request("/status" if args.action == "status" else "/measurements/latest"))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except HTTPError as error:
        sys.exit(f"HTTP {error.code}: {error.read(2048).decode('utf-8', 'replace')}")
    except (OSError, ValueError) as error:
        sys.exit(str(error))
