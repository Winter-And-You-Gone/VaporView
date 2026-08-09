#!/usr/bin/env python3
"""Audit legacy logging syntax by exact source locations.

The allowlists intentionally use a stable relative path plus the nearest
class/function symbol. A count-only baseline can miss a deletion followed by
an equally-sized regression; exact sets cannot.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import sys
from typing import Iterable, Mapping, Sequence


SOURCE_ROOTS = ("src", "include")
SOURCE_SUFFIXES = {".cpp", ".cc", ".cxx", ".h", ".hpp"}
SKIP_DIRS = {".git", "build", "third_party", "records", "resources", "packaging"}

# Keep these sets empty unless a real public API/lifecycle dependency has been
# reviewed and documented. Keys are (relative path, class/function symbol).
ALLOWED_LOG_LIKE_QSTRING_SIGNAL_DECLARATIONS: set[tuple[str, str, str]] = set()
ALLOWED_LOG_LIKE_QSTRING_SIGNAL_EMITS: set[tuple[str, str, str]] = set()
ALLOWED_MAINWINDOW_LEGACY_LOG_CALLS: set[tuple[str, str]] = set()
# First-party production code must not mark runtime events as legacy.
ALLOWED_LEGACY_UNCLASSIFIED: set[tuple[str, str, str]] = set()

LEGACY_KINDS = (
    "log_like_qstring_signal",
    "emit_log_like_qstring",
    "mainwindow_log_calls",
    "message_keyword_level",
    "message_keyword_ui_visibility",
    "legacy_unclassified",
)

EMIT_LOG_LIKE_QSTRING_RE = re.compile(
    r"\bemit\s+(?P<name>[A-Za-z_]\w*)\s*\("
)
STRING_LOG_LIKE_SIGNAL_RE = re.compile(
    r"\bvoid\s+(?P<name>[A-Za-z_]\w*)\s*\([^;]*"
    r"(?:const\s+)?QString\s*(?:&\s*)?(?:[A-Za-z_]\w*)?"
)
MAINWINDOW_LOG_CALL_RE = re.compile(r"(?<![\w:])(?:this\s*->\s*)?log\s*\(")
MESSAGE_KEYWORD_RE = re.compile(
    r"\b(?:message|messageLower|messageText)\s*\.\s*contains\s*\([^\n)]*"
    r"(?:failed|failure|error|warning|critical|debug|info|失败|错误|警告)"
)
MESSAGE_UI_VISIBILITY_RE = re.compile(
    r"\b(?:message|messageLower|messageText)\s*\.\s*contains\s*\([^\n)]*"
    r"(?:warning|error|critical|debug|info|failed|failure|失败|错误|警告)",
    re.IGNORECASE,
)
UI_VISIBILITY_CONTEXT_RE = re.compile(
    r"\b(?:ui_visibility|ui_visible|attention|details|hidden)\b"
)
LEGACY_UNCLASSIFIED_RE = re.compile(
    r"(?:QStringLiteral\(\"legacy_unclassified\"\)|\"legacy_unclassified\")"
)
EVENT_FIELD_RE = re.compile(
    r"(?:QStringLiteral\(\"event\"\)|\"event\")\s*,\s*"
    r"(?:QStringLiteral\(\"(?P<qvalue>[^\"]+)\"\)|\"(?P<rvalue>[^\"]+)\")"
)
FUNCTION_RE = re.compile(
    r"(?P<name>(?:[A-Za-z_]\w*::)+[A-Za-z_]\w*)\s*\("
)
UNQUALIFIED_FUNCTION_RE = re.compile(
    r"\b(?:void|bool|int|QString|QByteArray|LogRecord|[A-Za-z_]\w*)\s+"
    r"(?P<name>[A-Za-z_]\w*)\s*\("
)
FUNCTION_DEFINITION_PREFIX_RE = re.compile(
    r"^\s*(?:(?:static|inline|virtual|constexpr|consteval|friend|explicit)\s+)*"
    r"(?:(?:[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*|[<>{}*&:,~]+)\s+)+$"
)
CLASS_RE = re.compile(r"\bclass\s+(?:final\s+)?(?P<name>[A-Za-z_]\w*)")
SIGNALS_SECTION_RE = re.compile(r"^\s*signals\s*:\s*$")
ACCESS_SECTION_RE = re.compile(
    r"^\s*(?:public|private|protected)\s*(?:slots\s*)?:\s*$|^\s*(?:private|public|protected)\s+signals\s*:\s*$"
)


@dataclass(frozen=True)
class Location:
    kind: str
    relpath: str
    symbol: str
    line_no: int
    line: str
    identifier: str = ""

    @property
    def key(self) -> tuple[str, str]:
        return (self.relpath, self.symbol)

    @property
    def semantic_key(self) -> tuple[str, str, str]:
        return (self.relpath, self.symbol, self.identifier)

    def render(self) -> str:
        suffix = f", {self.identifier}" if self.identifier else ""
        return f"{self.relpath}:{self.line_no} ({self.symbol}{suffix}): {self.line}"


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


def strip_cpp_comments_keep_lines(text: str) -> str:
    """Remove comments without changing line count or string contents."""
    result: list[str] = []
    i = 0
    in_string = False
    in_char = False
    in_block = False
    escaped = False
    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if in_block:
            if ch == "*" and nxt == "/":
                in_block = False
                result.extend("  ")
                i += 2
                continue
            result.append("\n" if ch == "\n" else " ")
            i += 1
            continue
        if not in_string and not in_char and ch == "/" and nxt == "*":
            in_block = True
            result.extend("  ")
            i += 2
            continue
        if not in_string and not in_char and ch == "/" and nxt == "/":
            while i < len(text) and text[i] != "\n":
                result.append(" ")
                i += 1
            continue
        result.append(ch)
        if ch == "\\" and (in_string or in_char) and not escaped:
            escaped = True
        else:
            if ch == '"' and not in_char and not escaped:
                in_string = not in_string
            elif ch == "'" and not in_string and not escaped:
                in_char = not in_char
            escaped = False
        i += 1
    return "".join(result)


def _function_match(line: str) -> re.Match[str] | None:
    return FUNCTION_RE.search(line) or UNQUALIFIED_FUNCTION_RE.search(line)


def _is_function_definition(line: str, match: re.Match[str]) -> bool:
    if match.re is UNQUALIFIED_FUNCTION_RE:
        return True
    return FUNCTION_DEFINITION_PREFIX_RE.match(line[:match.start()]) is not None


def _nearest_event(lines: list[str], index: int) -> str:
    """Find the literal event paired with a legacy_unclassified field."""
    candidates: list[tuple[int, str]] = []
    start = max(0, index - 12)
    end = min(len(lines), index + 13)
    for candidate_index in range(start, end):
        for match in EVENT_FIELD_RE.finditer(lines[candidate_index]):
            value = match.group("qvalue") or match.group("rvalue")
            candidates.append((abs(candidate_index - index), value))
    return min(candidates, default=(0, "<missing_event>"))[1]


def _is_log_like_qstring_name(name: str) -> bool:
    lower = name.lower()
    return (
        ("log" in lower and "message" in lower) or
        "diagnostic" in lower or
        "errormessage" in lower or
        "statusmessage" in lower or
        "messagerequested" in lower
    )


def audit_text(relpath: str, text: str) -> list[Location]:
    scan = strip_cpp_comments_keep_lines(text)
    lines = scan.splitlines()
    locations: list[Location] = []
    current_function = "<global>"
    current_class = "<global>"
    in_signals_section = False

    for index, line in enumerate(lines):
        stripped = line.strip()
        class_match = CLASS_RE.search(line)
        if class_match:
            current_class = class_match.group("name")
            in_signals_section = False

        if SIGNALS_SECTION_RE.match(line):
            in_signals_section = True
        elif ACCESS_SECTION_RE.match(line):
            in_signals_section = False

        function_match = _function_match(line)
        if function_match and _is_function_definition(line, function_match):
            current_function = function_match.group("name")

        symbol = current_function
        emit_match = EMIT_LOG_LIKE_QSTRING_RE.search(line)
        if emit_match and _is_log_like_qstring_name(emit_match.group("name")):
            locations.append(Location("emit_log_like_qstring", relpath, symbol, index + 1, stripped,
                                      emit_match.group("name")))
        signal_match = STRING_LOG_LIKE_SIGNAL_RE.search(line)
        if in_signals_section and signal_match and _is_log_like_qstring_name(signal_match.group("name")):
            locations.append(Location("log_like_qstring_signal", relpath, current_class, index + 1, stripped,
                                      signal_match.group("name")))
        is_log_declaration = (
            re.search(r"\bvoid\s+log\s*\(", line) is not None
            or re.search(r"\bMainWindow::log\s*\(", line) is not None
        )
        if (relpath.startswith("src/ground/main/") and
                MAINWINDOW_LOG_CALL_RE.search(line) and not is_log_declaration):
            locations.append(Location("mainwindow_log_calls", relpath, symbol, index + 1, stripped))
        if MESSAGE_KEYWORD_RE.search(line):
            locations.append(Location("message_keyword_level", relpath, symbol, index + 1, stripped))
        if (MESSAGE_UI_VISIBILITY_RE.search(line) and
                UI_VISIBILITY_CONTEXT_RE.search(line)):
            locations.append(Location("message_keyword_ui_visibility", relpath, symbol, index + 1, stripped))
        if LEGACY_UNCLASSIFIED_RE.search(line):
            locations.append(Location("legacy_unclassified", relpath, symbol, index + 1, stripped,
                                      _nearest_event(lines, index)))
    return locations


def collect_locations(root: Path) -> list[Location]:
    locations: list[Location] = []
    for path in iter_source_files(root):
        relpath = path.relative_to(root).as_posix()
        locations.extend(audit_text(relpath, path.read_text(encoding="utf-8", errors="replace")))
    return locations


def allowlists() -> Mapping[str, set[tuple[str, ...]]]:
    return {
        "log_like_qstring_signal": ALLOWED_LOG_LIKE_QSTRING_SIGNAL_DECLARATIONS,
        "emit_log_like_qstring": ALLOWED_LOG_LIKE_QSTRING_SIGNAL_EMITS,
        "mainwindow_log_calls": ALLOWED_MAINWINDOW_LEGACY_LOG_CALLS,
        "message_keyword_level": set(),
        "message_keyword_ui_visibility": set(),
        "legacy_unclassified": ALLOWED_LEGACY_UNCLASSIFIED,
    }


def check_allowlists(
    locations: Sequence[Location],
    allowed: Mapping[str, set[tuple[str, ...]]],
) -> list[str]:
    failures: list[str] = []
    for kind in LEGACY_KINDS:
        use_semantic_key = kind in {
            "log_like_qstring_signal",
            "emit_log_like_qstring",
            "legacy_unclassified",
        }
        actual = {
            location.semantic_key if use_semantic_key else location.key
            for location in locations
            if location.kind == kind
        }
        expected = set(allowed.get(kind, set()))
        for location in locations:
            location_key = location.semantic_key if use_semantic_key else location.key
            if location.kind == kind and location_key not in expected:
                failures.append(f"Unexpected legacy {kind}: {location.render()}")
        for expected_key in sorted(expected - actual):
            failures.append(f"Missing allowlisted legacy {kind}: {expected_key}")
    return failures


def _assert_no_failures(label: str, locations: Sequence[Location], allowed: Mapping[str, set[tuple[str, ...]]]) -> None:
    failures = check_allowlists(locations, allowed)
    if failures:
        raise AssertionError(f"{label} unexpectedly failed:\n" + "\n".join(failures))


def _assert_fail(label: str, locations: Sequence[Location], allowed: Mapping[str, set[tuple[str, ...]]]) -> None:
    failures = check_allowlists(locations, allowed)
    if not failures:
        raise AssertionError(f"{label} unexpectedly passed")


def run_self_test() -> None:
    fixture = """
class Foo : public QObject {
signals:
    void logMessage(const QString& message);
    void logMessageRequested(const QString& message);
    void diagnosticMessage(const QString& message);
};
void Foo::first() { emit logMessage(QStringLiteral(\"one\")); }
void Foo::second() { emit logMessageRequested(QStringLiteral(\"two\")); }
void Foo::third() { emit diagnosticMessage(QStringLiteral(\"three\")); }
"""
    locations = audit_text("src/fixture/Foo.cpp", fixture)
    locations.extend(audit_text(
        "src/fixture/Foo.cpp",
        "void Foo::ui() { fields.insert(QStringLiteral(\"event\"), QStringLiteral(\"fixture_ui\")); "
        "fields.insert(QStringLiteral(\"legacy_unclassified\"), true); }",
    ))
    emit_keys = {location.semantic_key for location in locations if location.kind == "emit_log_like_qstring"}
    signal_keys = {location.semantic_key for location in locations if location.kind == "log_like_qstring_signal"}
    exact = {
        "emit_log_like_qstring": {
            ("src/fixture/Foo.cpp", "Foo::first", "logMessage"),
            ("src/fixture/Foo.cpp", "Foo::second", "logMessageRequested"),
            ("src/fixture/Foo.cpp", "Foo::third", "diagnosticMessage"),
        },
        "log_like_qstring_signal": {
            ("src/fixture/Foo.cpp", "Foo", "logMessage"),
            ("src/fixture/Foo.cpp", "Foo", "logMessageRequested"),
            ("src/fixture/Foo.cpp", "Foo", "diagnosticMessage"),
        },
        "mainwindow_log_calls": set(),
        "message_keyword_level": set(),
        "message_keyword_ui_visibility": set(),
        "legacy_unclassified": {("src/fixture/Foo.cpp", "Foo::ui", "fixture_ui")},
    }
    if emit_keys != exact["emit_log_like_qstring"] or signal_keys != exact["log_like_qstring_signal"]:
        raise AssertionError(f"location extraction failed: emits={emit_keys}, signals={signal_keys}")
    _assert_no_failures("allowlisted locations", locations, exact)

    empty: Mapping[str, set[tuple[str, ...]]] = {kind: set() for kind in LEGACY_KINDS}
    _assert_no_failures(
        "ordinary UI QString signal",
        audit_text("src/fixture/Ui.cpp", "class Ui : public QObject { signals: void titleChanged(const QString& title); };"),
        empty,
    )
    _assert_fail(
        "logMessageRequested signal",
        audit_text("src/fixture/Bar.h", "class Bar : public QObject { signals: void logMessageRequested(const QString& message); };"),
        exact,
    )
    _assert_fail(
        "diagnosticMessage signal",
        audit_text("src/fixture/Bar.h", "class Bar : public QObject { signals: void diagnosticMessage(const QString& message); };"),
        exact,
    )
    _assert_fail(
        "renamed log message signal",
        audit_text("src/fixture/Bar.h", "class Bar : public QObject { signals: void connectionLogMessageRequested(const QString& message); };"),
        exact,
    )
    _assert_fail(
        "new file emit",
        audit_text("src/fixture/Bar.cpp", "void Bar::newEvent() { emit logMessage(message); }"),
        exact,
    )
    _assert_fail(
        "same file different function",
        audit_text("src/fixture/Foo.cpp", "void Foo::fourth() { emit logMessage(message); }"),
        exact,
    )
    _assert_fail(
        "allowlist replacement with same count",
        audit_text("src/fixture/Foo.cpp", "void Foo::replacement() { emit logMessage(message); }"),
        {**exact, "emit_log_like_qstring": {("src/fixture/Foo.cpp", "Foo::first", "logMessage")}},
    )
    _assert_fail(
        "MainWindow legacy call",
        audit_text("src/ground/main/Foo.cpp", "void MainWindow::run() { log(QStringLiteral(\"old\")); }"),
        exact,
    )
    _assert_fail(
        "message keyword severity inference",
        audit_text(
            "src/sky/Foo.cpp",
            "void Foo::run() { const bool failed = message.contains(QStringLiteral(\"failed\")); }",
        ),
        exact,
    )
    _assert_fail(
        "message keyword UI visibility inference",
        audit_text(
            "src/sky/Foo.cpp",
            "void Foo::run() { const auto visibility = message.contains(QStringLiteral(\"warning\")) ? QStringLiteral(\"attention\") : QStringLiteral(\"details\"); }",
        ),
        exact,
    )
    _assert_fail(
        "new legacy_unclassified event",
        audit_text(
            "src/sky/Foo.cpp",
            "void Foo::run() { fields.insert(QStringLiteral(\"event\"), QStringLiteral(\"new_ui_event\")); fields.insert(QStringLiteral(\"legacy_unclassified\"), true); }",
        ),
        exact,
    )


def main(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--self-test", action="store_true", help="run built-in exact-location fixtures")
    args = parser.parse_args(argv)

    if args.self_test:
        run_self_test()

    root = args.root.resolve()
    locations = collect_locations(root)
    failures = check_allowlists(locations, allowlists())
    if failures:
        print("legacy logging audit failed; exact allowlist mismatch")
        for failure in failures:
            print(f"- {failure}")
        return 1

    counts = {kind: sum(location.kind == kind for location in locations) for kind in LEGACY_KINDS}
    for kind in LEGACY_KINDS:
        print(f"{kind}: current={counts[kind]}")
    print("legacy logging audit passed (exact position allowlist)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
