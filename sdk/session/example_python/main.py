"""Run with SDK/python on PYTHONPATH. Use --library with a custom DLL location."""

import argparse
import json
import math
import sys

from vaporview_session import Session, SessionError, UINT64_MAX


def emit_json(value):
    print(json.dumps(value))


def show_warning(message):
    if message:
        print(f"Warning: {message}", file=sys.stderr)


def read_result(result):
    show_warning(result.warning)
    return result.value


def record_json(record):
    return {"ordinal": str(record.ordinal), "timestamp_us": str(record.timestamp_us),
            "sequence": None if record.sequence is None else str(record.sequence), "fields": record.fields}


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", help="override the native SDK library path")
    parser.add_argument("session", help="directory containing session.json")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("list")
    visit = commands.add_parser("query")
    visit.add_argument("--limit", type=int)
    export = commands.add_parser("export")
    for command in (visit, export):
        command.add_argument("category")
        command.add_argument("--first-us", type=int, default=0)
        command.add_argument("--last-us", type=int, default=UINT64_MAX)
    export.add_argument("directory")
    frame = commands.add_parser("frame")
    frame.add_argument("ordinal", type=int)
    args = parser.parse_args()
    if args.command == "query" and args.limit is not None and args.limit < 0:
        parser.error("--limit must be nonnegative")
    return args


def main():
    args = parse_args()
    try:
        with Session(args.session, library=args.library) as session:
            show_warning(session.open_warning)
            if args.command == "list":
                emit_json({"sdk_version": session.version, "metadata": read_result(session.metadata()),
                           "categories": read_result(session.categories())})
            elif args.command == "frame":
                waveform = read_result(session.read_waveform(args.ordinal))
                preview = {}
                for name in ("h1", "h2", "raw", "harmonic"):
                    values = getattr(waveform, name)
                    first = [value if math.isfinite(value) else None for value in values[:5]]
                    preview[name] = {"samples": len(values), "first": first}
                emit_json({"record": record_json(waveform.record), **preview})
            elif args.command == "export":
                count = read_result(session.export(args.category, args.directory, args.first_us, args.last_us))
                print(f"{count} records exported")
            else:
                shown = 0

                def visit(record):
                    nonlocal shown
                    if args.limit is not None and shown >= args.limit:
                        return False
                    emit_json(record_json(record))
                    shown += 1
                    return True

                result = session.visit(args.category, visit, args.first_us, args.last_us)
                if result.cancelled:
                    print(f"Stopped after {result.records} records", file=sys.stderr)
                else:
                    show_warning(result.message)
    except (SessionError, OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
