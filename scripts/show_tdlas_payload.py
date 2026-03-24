#!/usr/bin/env python
"""Show raw TDLAS payload preview bytes from a snapshot JSON."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Iterable, List


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Show raw payload preview bytes from a TDLAS snapshot JSON."
    )
    parser.add_argument("snapshot", type=Path, help="Path to tdlas_snapshot_*.json")
    parser.add_argument(
        "--byte-rows",
        type=int,
        default=4,
        help="How many 16-byte rows of payload preview to print (default: 4)",
    )
    return parser.parse_args()


def load_snapshot(path: Path) -> dict:
    if not path.exists():
        raise FileNotFoundError(f"snapshot not found: {path}")
    if path.stat().st_size == 0:
        raise ValueError(f"snapshot is empty: {path}")
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def parse_payload_preview(payload_hex: str) -> List[int]:
    preview = payload_hex.replace("...", "").strip()
    if not preview:
        return []
    bytes_out: List[int] = []
    for token in preview.split():
        try:
            bytes_out.append(int(token, 16))
        except ValueError:
            continue
    return bytes_out


def chunked(values: Iterable[int], size: int) -> Iterable[List[int]]:
    row: List[int] = []
    for value in values:
        row.append(value)
        if len(row) == size:
            yield row
            row = []
    if row:
        yield row


def format_byte_rows(payload_bytes: List[int], max_rows: int) -> str:
    if not payload_bytes:
        return "(no payload preview stored in snapshot)"

    rows = []
    for row_index, row in enumerate(chunked(payload_bytes, 16)):
        if row_index >= max_rows:
            break
        offset = row_index * 16
        row_hex = " ".join(f"{value:02X}" for value in row)
        rows.append(f"  +{offset:03d}: {row_hex}")
    return "\n".join(rows)


def main() -> int:
    args = parse_args()
    snapshot = load_snapshot(args.snapshot)
    payload_hex = snapshot.get("payload_preview_hex", snapshot.get("payload_hex", ""))
    payload_bytes = parse_payload_preview(payload_hex)
    headers = snapshot.get("headers") or {}
    ipv4 = headers.get("ipv4") or {}
    udp = headers.get("udp") or {}
    counters = snapshot.get("counters") or {}
    rates = snapshot.get("rates") or {}

    print(f"snapshot: {args.snapshot}")
    print(f"exported_at_utc: {snapshot.get('exported_at_utc', '--')}")
    print(
        "endpoint: {src_ip}:{src_port} -> {dst_ip}:{dst_port}".format(
            src_ip=ipv4.get("source_ip", "--"),
            src_port=udp.get("source_port", "--"),
            dst_ip=ipv4.get("destination_ip", "--"),
            dst_port=udp.get("destination_port", "--"),
        )
    )
    print(
        "packet_length: {packet_len} | matched_rate_hz: {matched_rate} | matched_packets: {matched_packets}".format(
            packet_len=snapshot.get("packet_length", "--"),
            matched_rate=rates.get("matched_rate_hz", "--"),
            matched_packets=counters.get("matched_packets", "--"),
        )
    )
    print(f"payload_preview_bytes_in_snapshot: {len(payload_bytes)}")
    print(f"payload_preview_truncated: {snapshot.get('payload_preview_truncated', '...' in payload_hex)}")
    print()
    print("payload_preview_raw_bytes:")
    print(format_byte_rows(payload_bytes, max_rows=args.byte_rows))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
