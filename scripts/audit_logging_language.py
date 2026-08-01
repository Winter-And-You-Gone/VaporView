#!/usr/bin/env python3
"""Audit VaporView first-party logging language and machine identifiers.

The checker is intentionally lightweight. It does not try to parse all of C++;
instead it extracts known first-party logging calls, inspects their literal
arguments, and flags patterns that would make log semantics depend on human
text. It also checks internal diagnostic callbacks and literal runtime
QTextStream output. Third-party/raw output is allowed when it is carried in
structured fields.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import sys
from typing import Iterable, List, Sequence


TEXT_SEVERITY_TERMS = (
    "failed",
    "failure",
    "error",
    "disconnect",
    "失败",
    "错误",
    "断开",
)

SOURCE_ROOTS = ("src", "include")
SOURCE_SUFFIXES = {".cpp", ".cc", ".cxx", ".h", ".hpp"}
SKIP_DIRS = {
    ".git",
    "build",
    "third_party",
    "records",
    "resources",
    "packaging",
}

LOG_CALLS = {
    "publish": 3,
    "reportUserIssue": 3,
    "publishRuntimeLog": 3,
    "publishDeviceLog": 3,
    "publishIpcLog": 2,
    "publishClientLog": 3,
    "publishTelemetryLog": 3,
    "reportProtocolDiagnostic": 3,
}

LEVEL_NAMES = ("LogLevel::Warning", "LogLevel::Error", "LogLevel::Critical")
ERROR_LEVEL_NAMES = ("LogLevel::Error", "LogLevel::Critical")

FIELD_KEY_RE = re.compile(r"\{\s*QStringLiteral\(\"([^\"]+)\"\)\s*,")
QSTRING_RE = re.compile(r"QStringLiteral\(\"((?:\\.|[^\"])*)\"\)")
QT_MESSAGE_RE = re.compile(r"\bq(?:Info|Warning|Critical)\s*\(")
DIAGNOSTIC_FAILURE_RE = re.compile(
    r"\bnotifyFailure\s*\(\s*QStringLiteral\(\"((?:\\.|[^\"])*)\"\)"
)
CONSOLE_TEXT_RE = re.compile(
    r"\bQTextStream\(\s*(?:stdout|stderr)\s*\)\s*<<\s*\"((?:\\.|[^\"])*)\""
)
TEXT_SEVERITY_RE = re.compile(
    r"\.contains\s*\(\s*QStringLiteral\s*\(\s*\"(?:"
    + "|".join(re.escape(term) for term in TEXT_SEVERITY_TERMS)
    + r")\"\s*\)",
    re.IGNORECASE,
)
CHINESE_RE = re.compile(r"[\u4e00-\u9fff]")
ENGLISH_SENTENCE_RE = re.compile(r"[A-Za-z]+(?:[ '\-/]+[A-Za-z0-9]+){2,}")

ALLOWED_ENGLISH_MESSAGE_SUBSTRINGS = (
    # Product/protocol identifiers that are intentionally stable English.
    "Qt",
    "SkyCore",
    "SkyTui",
    "TCP",
    "IPC",
    "JSON",
    "CRC",
    "EPSILON",
    "PTB",
    "HMP",
    "RD105",
)


@dataclass
class Issue:
    code: str
    path: str
    line: int
    message: str

    def render(self) -> str:
        return f"{self.path}:{self.line}: {self.code}: {self.message}"


@dataclass
class Call:
    name: str
    text: str
    line: int
    args: List[str]


def iter_source_files(root: Path) -> Iterable[Path]:
    for top in SOURCE_ROOTS:
        base = root / top
        if not base.exists():
            continue
        for path in base.rglob("*"):
            if any(part in SKIP_DIRS for part in path.parts):
                continue
            if path.is_file() and path.suffix in SOURCE_SUFFIXES:
                yield path


def line_for_offset(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def split_top_level_args(inner: str) -> List[str]:
    args: List[str] = []
    start = 0
    depth = 0
    in_string = False
    escape = False
    for i, ch in enumerate(inner):
        if in_string:
            if escape:
                escape = False
            elif ch == "\\":
                escape = True
            elif ch == '"':
                in_string = False
            continue
        if ch == '"':
            in_string = True
        elif ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth = max(0, depth - 1)
        elif ch == "," and depth == 0:
            args.append(inner[start:i].strip())
            start = i + 1
    tail = inner[start:].strip()
    if tail:
        args.append(tail)
    return args


def extract_calls(text: str) -> List[Call]:
    calls: List[Call] = []
    for name in LOG_CALLS:
        search_pos = 0
        needle = f"{name}("
        while True:
            pos = text.find(needle, search_pos)
            if pos < 0:
                break
            # Avoid matching declarations such as "void publishTelemetryLog(".
            prefix = text[max(0, pos - 32):pos]
            if re.search(r"\b(void|LogRecord|bool|int|QString)\s+$", prefix):
                search_pos = pos + len(needle)
                continue
            start = pos + len(needle) - 1
            depth = 0
            in_string = False
            escape = False
            end = -1
            for i in range(start, len(text)):
                ch = text[i]
                if in_string:
                    if escape:
                        escape = False
                    elif ch == "\\":
                        escape = True
                    elif ch == '"':
                        in_string = False
                    continue
                if ch == '"':
                    in_string = True
                elif ch == "(":
                    depth += 1
                elif ch == ")":
                    depth -= 1
                    if depth == 0:
                        end = i
                        break
            if end >= 0:
                inner = text[start + 1:end]
                calls.append(Call(name, text[pos:end + 1], line_for_offset(text, pos), split_top_level_args(inner)))
                search_pos = end + 1
            else:
                search_pos = pos + len(needle)
    calls.sort(key=lambda call: call.line)
    return calls


def decode_qstring_literal(value: str) -> str:
    # Keep this conservative; audit strings only need readable comparisons.
    return value.replace(r"\"", '"').replace(r"\\", "\\").replace(r"\n", "\n")


def literal_from_arg(arg: str) -> str | None:
    match = QSTRING_RE.search(arg)
    if not match:
        return None
    return decode_qstring_literal(match.group(1))


def is_obvious_english_sentence(value: str) -> bool:
    if CHINESE_RE.search(value):
        return False
    if not ENGLISH_SENTENCE_RE.search(value):
        return False
    # A sentence made only from stable product/protocol words is not actionable.
    normalized = re.sub(r"[^A-Za-z0-9]+", " ", value).strip()
    if normalized in ALLOWED_ENGLISH_MESSAGE_SUBSTRINGS:
        return False
    return True


def has_event(call: Call) -> bool:
    if re.search(r"QStringLiteral\(\"event\"\)", call.text):
        return True
    # Helper calls carry event as a dedicated argument.
    if call.name in {
        "publishRuntimeLog",
        "publishDeviceLog",
        "publishIpcLog",
        "publishClientLog",
        "publishTelemetryLog",
        "reportProtocolDiagnostic",
    }:
        return len(call.args) > LOG_CALLS[call.name] - 1 and literal_from_arg(call.args[LOG_CALLS[call.name] - 1]) is not None
    return False


def has_reason_or_error_code(call: Call) -> bool:
    return bool(re.search(r"QStringLiteral\(\"(?:error_code|reason_code)\"\)", call.text))


def level_text(call: Call) -> str:
    if not call.args:
        return ""
    return call.args[0]


def audit_text(path_label: str, text: str) -> List[Issue]:
    issues: List[Issue] = []
    for match in TEXT_SEVERITY_RE.finditer(text):
        issues.append(Issue(
            "message-keyword-level",
            path_label,
            line_for_offset(text, match.start()),
            "日志级别或分类不能通过 message.contains(...) 文本关键词判断。",
        ))
    for match in QT_MESSAGE_RE.finditer(text):
        issues.append(Issue(
            "qt-message-direct",
            path_label,
            line_for_offset(text, match.start()),
            "第一方运行日志应通过 LogService 发布，避免 qInfo/qWarning/qCritical 写入英文诊断。",
        ))

    for match in DIAGNOSTIC_FAILURE_RE.finditer(text):
        literal = decode_qstring_literal(match.group(1))
        if is_obvious_english_sentence(literal):
            issues.append(Issue(
                "english-diagnostic-failure",
                path_label,
                line_for_offset(text, match.start()),
                f"diagnosticFailure 内部诊断应使用简体中文: {literal}",
            ))

    for match in CONSOLE_TEXT_RE.finditer(text):
        literal = decode_qstring_literal(match.group(1))
        if is_obvious_english_sentence(literal):
            issues.append(Issue(
                "english-console-diagnostic",
                path_label,
                line_for_offset(text, match.start()),
                f"运行诊断用的 QTextStream 输出应使用简体中文: {literal}",
            ))

    for call in extract_calls(text):
        for match in FIELD_KEY_RE.finditer(call.text):
            key = decode_qstring_literal(match.group(1))
            if CHINESE_RE.search(key):
                issues.append(Issue(
                    "chinese-field-key",
                    path_label,
                    call.line,
                    f"结构化字段键必须保持英文: {key}",
                ))
        message_index = LOG_CALLS[call.name]
        if len(call.args) > message_index:
            literal = literal_from_arg(call.args[message_index])
            if literal and is_obvious_english_sentence(literal):
                issues.append(Issue(
                    "english-first-party-message",
                    path_label,
                    call.line,
                    f"第一方 message 应使用简体中文，英文原文应放入字段: {literal}",
                ))
        level = level_text(call)
        variable_fields_are_prepared = (
            call.name == "publish" and
            len(call.args) > message_index + 1 and
            call.args[message_index + 1] in {"fields", "recordFields"}
        )
        if any(name in level for name in LEVEL_NAMES) and not has_event(call) and not variable_fields_are_prepared:
            issues.append(Issue(
                "missing-event",
                path_label,
                call.line,
                "Warning/Error/Critical 日志必须带稳定 event。",
            ))
        if any(name in level for name in ERROR_LEVEL_NAMES) and not has_reason_or_error_code(call) and not variable_fields_are_prepared:
            issues.append(Issue(
                "missing-error-code",
                path_label,
                call.line,
                "Error/Critical 日志必须带 error_code 或 reason_code。",
            ))
        for event_match in re.finditer(r"QStringLiteral\(\"event\"\)\s*,\s*QStringLiteral\(\"([^\"]+)\"\)", call.text):
            event_value = decode_qstring_literal(event_match.group(1))
            if CHINESE_RE.search(event_value):
                issues.append(Issue(
                    "chinese-event",
                    path_label,
                    call.line,
                    f"event 必须保持英文 snake_case: {event_value}",
                ))
    return issues


def run_self_test() -> List[Issue]:
    fixture = r'''
void bad(LogService& logService, const QString& message) {
    const bool warning = message.contains(QStringLiteral("failed"), Qt::CaseInsensitive);
    logService.publish(LogLevel::Warning,
                       QStringLiteral("SkyCore"),
                       QStringLiteral("device.connection"),
                       QStringLiteral("Device connection failed and will retry."),
                       {{QStringLiteral("event"), QStringLiteral("设备失败")}});
    logService.publish(LogLevel::Error,
                       QStringLiteral("SkyCore"),
                       QStringLiteral("process"),
                       QStringLiteral("子进程发生错误。"),
                       {{QStringLiteral("event"), QStringLiteral("child_process_error")}});
    logService.publish(LogLevel::Info,
                       QStringLiteral("SkyCore"),
                       QStringLiteral("process"),
                       QStringLiteral("已收到子进程错误输出。"),
                       {{QStringLiteral("event"), QStringLiteral("child_process_output")},
                        {QStringLiteral("process_output"), QStringLiteral("Raw English stderr line")}});
    notifyFailure(QStringLiteral("Cannot write application log file."));
    QTextStream(stdout) << "Runtime status has started.";
}
'''
    issues = audit_text("<self-test>", fixture)
    codes = {issue.code for issue in issues}
    expected = {
        "message-keyword-level",
        "english-first-party-message",
        "chinese-event",
        "missing-error-code",
        "english-diagnostic-failure",
        "english-console-diagnostic",
    }
    missing = expected - codes
    unexpected_raw_output_flags = [
        issue for issue in issues
        if "Raw English stderr line" in issue.message
    ]
    if missing or unexpected_raw_output_flags:
        rendered = "\n".join(issue.render() for issue in issues)
        raise AssertionError(
            f"self-test failed; missing={sorted(missing)}, raw_output_flags={len(unexpected_raw_output_flags)}\n{rendered}"
        )
    return issues


def main(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--self-test", action="store_true", help="run built-in detection fixture before repository audit")
    args = parser.parse_args(argv)

    if args.self_test:
        run_self_test()

    root = args.root.resolve()
    issues: List[Issue] = []
    for path in iter_source_files(root):
        text = path.read_text(encoding="utf-8", errors="replace")
        rel = path.relative_to(root).as_posix()
        issues.extend(audit_text(rel, text))

    if issues:
        for issue in issues:
            print(issue.render())
        print(f"\nlogging language audit failed: {len(issues)} issue(s)")
        return 1
    print("logging language audit passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
