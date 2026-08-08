#!/usr/bin/env python3
"""Audit remaining legacy string logging entry points.

This is a migration guard, not a completion claim. The baseline counts below
are the explicit temporary allowlist for legacy string paths that still need to
be retired. New occurrences must not increase these counts; when a call is
migrated, lower the matching baseline in this file.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import sys
from typing import Iterable, Sequence


SOURCE_ROOTS = ("src", "include")
SOURCE_SUFFIXES = {".cpp", ".cc", ".cxx", ".h", ".hpp"}
SKIP_DIRS = {".git", "build", "third_party", "records", "resources", "packaging"}

BASELINES = {
    # MainWindow::log(QString) is still present as a compatibility path, but
    # first-party ground code must not call it for business events.
    "mainwindow_log_calls": 0,
    # String-only logMessage signals still bridge legacy Sky/Ground components.
    "emit_logMessage_qstring": 6,
    "logMessage_qstring_signal": 6,
}

LOG_CALL_RE = re.compile(r"(?<![\w:>])log\s*\(")
EMIT_LOG_MESSAGE_RE = re.compile(r"\bemit\s+logMessage\s*\(")
STRING_LOG_MESSAGE_SIGNAL_RE = re.compile(
    r"\bvoid\s+logMessage\s*\(\s*const\s+QString\s*&\s*message\s*\)"
)


@dataclass(frozen=True)
class Location:
    kind: str
    relpath: str
    line_no: int
    line: str

    def render(self) -> str:
        return f"{self.relpath}:{self.line_no}: {self.line}"


def iter_source_files(root: Path) -> Iterable[Path]:
    for source_root in SOURCE_ROOTS:
        base = root / source_root
        if not base.exists():
            continue
        for path in base.rglob("*"):
            if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
                continue
            if any(part in SKIP_DIRS for part in path.parts):
                continue
            yield path


def audit_text(relpath: str, text: str) -> list[Location]:
    locations: list[Location] = []
    for line_no, line in enumerate(text.splitlines(), start=1):
        stripped = line.strip()
        if relpath.startswith("src/ground/main/") and LOG_CALL_RE.search(line):
            if ("void MainWindow::log(" not in line and
                    "void MainWindow::logConnectionInfo(" not in line and
                    not stripped.startswith("void log(")):
                locations.append(Location("mainwindow_log_calls", relpath, line_no, stripped))
        if EMIT_LOG_MESSAGE_RE.search(line):
            locations.append(Location("emit_logMessage_qstring", relpath, line_no, stripped))
        if STRING_LOG_MESSAGE_SIGNAL_RE.search(line):
            locations.append(Location("logMessage_qstring_signal", relpath, line_no, stripped))
    return locations


def run_self_test() -> None:
    fixture = """
void MainWindow::log(const QString& message) {}
void MainWindow::x() { log(QStringLiteral("legacy")); }
signals:
    void logMessage(const QString& message);
void y() { emit logMessage(message); }
void z() { std::log(2.0); logUiTest(QStringLiteral("ok")); }
"""
    locations = audit_text("src/ground/main/Foo.cpp", fixture)
    counts = {kind: 0 for kind in BASELINES}
    for location in locations:
        counts[location.kind] += 1
    expected = {
        "mainwindow_log_calls": 1,
        "emit_logMessage_qstring": 1,
        "logMessage_qstring_signal": 1,
    }
    if counts != expected:
        rendered = "\n".join(location.render() for location in locations)
        raise AssertionError(f"legacy logging self-test failed: {counts} != {expected}\n{rendered}")


def main(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--self-test", action="store_true", help="run built-in regex fixture before repository audit")
    args = parser.parse_args(argv)

    if args.self_test:
        run_self_test()

    root = args.root.resolve()
    locations: list[Location] = []
    for path in iter_source_files(root):
        relpath = path.relative_to(root).as_posix()
        text = path.read_text(encoding="utf-8", errors="replace")
        locations.extend(audit_text(relpath, text))

    counts = {kind: 0 for kind in BASELINES}
    by_kind = {kind: [] for kind in BASELINES}
    for location in locations:
        counts[location.kind] += 1
        by_kind[location.kind].append(location)

    failures: list[str] = []
    for kind, baseline in BASELINES.items():
        current = counts[kind]
        if current > baseline:
            failures.append(f"{kind}: current={current} baseline={baseline}")

    if failures:
        print("legacy logging audit failed; new string logging entry points were found")
        for failure in failures:
            print(f"- {failure}")
        for kind in BASELINES:
            if counts[kind] > BASELINES[kind]:
                print(f"\n{kind} locations:")
                for location in by_kind[kind]:
                    print(location.render())
        return 1

    for kind, baseline in BASELINES.items():
        print(f"{kind}: current={counts[kind]} baseline={baseline}")
    print("legacy logging audit passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
