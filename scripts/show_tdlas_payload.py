#!/usr/bin/env python
"""Pretty-print TDLAS payload information from a snapshot JSON."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Iterable, List


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Show payload bytes and u16 words from a TDLAS snapshot JSON."
    )
    parser.add_argument("snapshot", type=Path, help="Path to tdlas_snapshot_*.json")
    parser.add_argument(
        "--byte-rows",
        type=int,
        default=4,
        help="How many 16-byte rows of payload preview to print (default: 4)",
    )
    parser.add_argument(
        "--word-limit",
        type=int,
        default=16,
        help="How many word-stat rows to print (default: 16)",
    )
    return parser.parse_args()


def load_snapshot(path: Path) -> dict:
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


def format_word_rows(snapshot: dict, payload_bytes: List[int], word_limit: int) -> str:
    word_stats = snapshot.get("word_stats") or []
    if word_stats:
        lines = []
        for stat in word_stats[:word_limit]:
            lines.append(
                "  w{word:02d} @ {offset:02d}: latest={latest:<4d} "
                "range=[{minv},{maxv}] unique={uniq} {state} raw={raw}".format(
                    word=int(stat.get("word_index", 0)),
                    offset=int(stat.get("offset", 0)),
                    latest=int(stat.get("latest_value", 0)),
                    minv=int(stat.get("min_value", 0)),
                    maxv=int(stat.get("max_value", 0)),
                    uniq=int(stat.get("unique_count", 0)),
                    state="stable" if stat.get("stable") else "dynamic",
                    raw=stat.get("raw_hex", "--"),
                )
            )
        if len(word_stats) > word_limit:
            lines.append(f"  ... {len(word_stats) - word_limit} more words in snapshot")
        return "\n".join(lines)

    if len(payload_bytes) < 2:
        return "(no word statistics stored in snapshot)"

    derived = []
    for word_index, row in enumerate(chunked(payload_bytes[: word_limit * 2], 2)):
        if len(row) < 2:
            break
        value = row[0] | (row[1] << 8)
        derived.append(
            f"  w{word_index:02d} @ {word_index * 2:02d}: latest={value:<4d} raw={row[0]:02X} {row[1]:02X}"
        )
    return "\n".join(derived)


def format_metric_rows(snapshot: dict) -> str:
    metrics = snapshot.get("current_metrics") or []
    if not metrics:
        return "(no candidate metrics in snapshot)"

    lines = []
    for metric in metrics:
        lines.append(
            "  {key} @ {offset}: {wire} raw={raw} value={value}".format(
                key=metric.get("key", "--"),
                offset=metric.get("offset", "--"),
                wire=metric.get("wire_type", "--"),
                raw=metric.get("raw_hex", "--"),
                value=metric.get("value", "--"),
            )
        )
    return "\n".join(lines)


def main() -> int:
    args = parse_args()
    snapshot = load_snapshot(args.snapshot)
    payload_bytes = parse_payload_preview(snapshot.get("payload_hex", ""))
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
    print(f"payload_signature: {snapshot.get('payload_signature', '--')}")
    print(f"payload_variation_summary: {snapshot.get('payload_variation_summary', '--')}")
    print()
    print("payload_preview:")
    print(format_byte_rows(payload_bytes, max_rows=args.byte_rows))
    print()
    print("word_stats:")
    print(format_word_rows(snapshot, payload_bytes, word_limit=args.word_limit))
    print()
    print("candidate_metrics:")
    print(format_metric_rows(snapshot))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
