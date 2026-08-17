#!/usr/bin/env python3
"""Audit VaporView first-party logging language and machine identifiers.

The checker is intentionally lightweight. It does not try to parse all of C++;
instead it extracts known first-party logging calls, inspects their literal
arguments, and flags patterns that would make log semantics depend on human
text. It also checks internal diagnostic callbacks, literal runtime
QTextStream output, direct Qt message calls, and structured machine identifier
formats.
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

CATEGORY_RE = re.compile(r"^[a-z0-9]+(?:\.[a-z0-9_]+)*$")
EVENT_RE = re.compile(r"^[a-z0-9]+(?:_[a-z0-9]+)*$")
CODE_RE = re.compile(r"^[A-Z0-9]+(?:_[A-Z0-9]+)*$")
FIELD_KEY_FORMAT_RE = re.compile(r"^[a-z0-9]+(?:_[a-z0-9]+)*$")

LOG_CALLS = {
    "publish": {"source": 1, "category": 2, "message": 3, "fields": 4},
    "reportUserIssue": {"source": 1, "category": 2, "message": 3, "fields": 4},
    "publishRuntimeLog": {"source_literal": "SkyCore", "category": 1, "event": 2, "message": 3, "fields": 4},
    "publishDeviceLog": {"source_literal": "SkyCore", "category": 1, "event": 2, "message": 3, "fields": 4},
    "logStructured": {"source_literal": "SkyCore", "category": 1, "event": 2, "message": 3, "fields": 4},
    "publishIpcLog": {"source_literal": "SkyCore", "category_literal": "ipc", "event": 1, "message": 2, "fields": 3},
    "publishClientLog": {"source_literal": "SkyTui", "category": 1, "event": 2, "message": 3, "fields": 4},
    "publishTelemetryLog": {"source_literal": "Ground", "category": 1, "event": 2, "message": 3, "fields": 4},
    "reportProtocolDiagnostic": {"source_literal": "Ground", "category": 1, "event": 2, "message": 3, "fields": 4},
    "publishGroundLog": {"source_literal": "Ground", "category": 1, "event": 2, "message": 3, "fields": 4},
    "publishUiTestEvent": {
        "source_literal": "Ground",
        "category_literal": "ui.test",
        "event": 0,
        "message_literal": "界面测试日志已更新。",
        "fields": 2,
    },
    "publishTcpWaveLog": {"source_literal": "Ground", "category_literal": "telemetry.wave.tcp", "event": 1, "message": 2, "fields": 3},
    "emitTcpLog": {"source_literal": "TelemetryLink", "category_literal": "telemetry.link", "event": 1, "message": 2, "fields": 3},
    "postConnectionLog": {"source_literal": "Ground", "category_literal": "device.connection", "event": 1, "message": 2, "fields": 3},
    "postImuLog": {
        "source_literal": "Ground",
        "category_literal": "device.navigation.command",
        "level": 1,
        "event": 2,
        "message": 3,
        "fields": 4,
    },
    "postSerialPortDetectionLog": {
        "source_literal": "Ground",
        "category_literal": "device.connection",
        "level": 1,
        "event": 2,
        "message": 3,
        "fields": 4,
    },
    "publishTemperatureCommandLog": {"source_literal": "Ground", "category_literal": "device.temperature.command", "event": 1, "message": 2, "fields": 3},
    "emitLog": {"source_literal": "Ground", "category": 2, "event": 3, "message": 4, "fields": 5},
}

LEVEL_NAMES = ("LogLevel::Warning", "LogLevel::Error", "LogLevel::Critical")
ERROR_LEVEL_NAMES = ("LogLevel::Error", "LogLevel::Critical")

RESERVED_FIELD_KEYS = {
    "event",
    "error_code",
    "reason_code",
    "system_error",
    "external_raw_text",
    "process_output",
    "legacy_unclassified",
}

TECHNICAL_TERMS = {
    "qt",
    "skycore",
    "skytui",
    "vapourview",
    "vaporview",
    "ipc",
    "tcp",
    "udp",
    "json",
    "crc",
    "epsilon",
    "ptb",
    "ptb210",
    "hmp",
    "hmp3",
    "rd105",
    "wave",
    "telemetrybasic",
    "waveformdownsampled",
    "waveformfeature",
    "telemetrystatus",
    "commandack",
    "skyconfig",
    "skyconfigapplyresult",
    "logevent",
    "endpoint",
}

THIRD_PARTY_QT_MESSAGE_ALLOWLIST = (
    # Fixture sentinel and raw external diagnostics carried through direct Qt
    # handlers. Keep this list exact; do not add broad source-directory skips.
    "Raw third-party driver line",
)

COMMON_ENGLISH_PHRASE_RE = re.compile(
    r"\b(?:"
    r"failed\s+to|unable\s+to|connect\s+failed|start\s+failed|load\s+success|"
    r"loaded\s+successfully|retry\s+later|disconnected\s+unexpectedly|"
    r"has\s+started|has\s+stopped|open\s+failed|parse\s+failed|"
    r"connection\s+failed|command\s+failed|cannot\s+[a-z]+"
    r")\b",
    re.IGNORECASE,
)

ENGLISH_FUNCTION_WORDS = {
    "a",
    "an",
    "and",
    "are",
    "as",
    "be",
    "been",
    "for",
    "from",
    "has",
    "have",
    "in",
    "is",
    "later",
    "not",
    "of",
    "on",
    "or",
    "retry",
    "the",
    "to",
    "will",
    "with",
}

ENGLISH_ACTION_WORDS = {
    "connect",
    "connected",
    "disconnect",
    "disconnected",
    "failed",
    "failure",
    "load",
    "loaded",
    "open",
    "parse",
    "retry",
    "start",
    "started",
    "success",
    "successfully",
    "timeout",
    "unexpectedly",
    "unable",
}

QSTRING_RE = re.compile(r"QStringLiteral\(\"((?:\\.|[^\"])*)\"\)")
RAW_STRING_RE = re.compile(r"(?<![A-Za-z0-9_])\"((?:\\.|[^\"])*)\"")
FIELD_PAIR_RE = re.compile(
    r"\{\s*"
    r"(?:QStringLiteral\(\"(?P<qkey>(?:\\.|[^\"])*)\"\)|\"(?P<rkey>(?:\\.|[^\"])*)\")"
    r"\s*,\s*"
    r"(?:QStringLiteral\(\"(?P<qvalue>(?:\\.|[^\"])*)\"\)|\"(?P<rvalue>(?:\\.|[^\"])*)\"|[^}\n]+)"
)
FIELD_INSERT_RE = re.compile(
    r"\b(?:fields|recordFields)\.insert\(\s*"
    r"(?:QStringLiteral\(\"(?P<qkey>(?:\\.|[^\"])*)\"\)|\"(?P<rkey>(?:\\.|[^\"])*)\")"
    r"\s*,\s*"
    r"(?:QStringLiteral\(\"(?P<qvalue>(?:\\.|[^\"])*)\"\)|\"(?P<rvalue>(?:\\.|[^\"])*)\"|[^)\n]+)"
)
LOG_FIELD_CONTEXT_RE = re.compile(r"\b(?:fields|recordFields)\b\s*(?:=|\{)")
QT_MESSAGE_START_RE = re.compile(r"\b(qDebug|qInfo|qWarning|qCritical)\s*\(")
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
PATH_OR_FILE_RE = re.compile(
    r"(?:[A-Za-z]:[\\/][^\s，。；;]+|[./\\][^\s，。；;]+|[A-Za-z0-9_-]+\.[A-Za-z0-9]{1,8})"
)
CODE_TOKEN_RE = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)+\b|\b[A-Z][A-Za-z0-9]*[A-Z][A-Za-z0-9]*\b")


@dataclass
class Issue:
    code: str
    path: str
    line: int
    detail: str
    suggestion: str

    def render(self) -> str:
        return f"{self.path}:{self.line}: {self.code}: {self.detail} 建议: {self.suggestion}"


@dataclass
class Call:
    name: str
    text: str
    line: int
    args: List[str]
    start: int
    end: int


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


def strip_cpp_comments_keep_lines(text: str) -> str:
    out: List[str] = []
    i = 0
    in_string = False
    in_char = False
    escape = False
    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if in_string or in_char:
            out.append(ch)
            if escape:
                escape = False
            elif ch == "\\":
                escape = True
            elif in_string and ch == '"':
                in_string = False
            elif in_char and ch == "'":
                in_char = False
            i += 1
            continue
        if ch == '"':
            in_string = True
            out.append(ch)
            i += 1
            continue
        if ch == "'":
            in_char = True
            out.append(ch)
            i += 1
            continue
        if ch == "/" and nxt == "/":
            out.extend("  ")
            i += 2
            while i < len(text) and text[i] != "\n":
                out.append(" ")
                i += 1
            continue
        if ch == "/" and nxt == "*":
            out.extend("  ")
            i += 2
            while i < len(text):
                if text[i] == "\n":
                    out.append("\n")
                    i += 1
                elif text[i] == "*" and i + 1 < len(text) and text[i + 1] == "/":
                    out.extend("  ")
                    i += 2
                    break
                else:
                    out.append(" ")
                    i += 1
            continue
        out.append(ch)
        i += 1
    return "".join(out)


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


def find_balanced_parenthesis(text: str, open_pos: int) -> int:
    depth = 0
    in_string = False
    escape = False
    for i in range(open_pos, len(text)):
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
                return i
    return -1


def find_statement_end(text: str, start: int) -> int:
    in_string = False
    escape = False
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
        elif ch == ";":
            return i + 1
        elif ch == "\n" and text[start:i].count("<<") == 0:
            return i
    return len(text)


def extract_calls(text: str) -> List[Call]:
    calls: List[Call] = []
    for name in LOG_CALLS:
        search_pos = 0
        needle = f"{name}("
        while True:
            pos = text.find(needle, search_pos)
            if pos < 0:
                break
            prefix = text[max(0, pos - 32):pos]
            if re.search(r"\b(void|LogRecord|bool|int|QString)\s+$", prefix):
                search_pos = pos + len(needle)
                continue
            open_pos = pos + len(needle) - 1
            end = find_balanced_parenthesis(text, open_pos)
            if end >= 0:
                inner = text[open_pos + 1:end]
                calls.append(Call(name, text[pos:end + 1], line_for_offset(text, pos), split_top_level_args(inner), pos, end + 1))
                search_pos = end + 1
            else:
                search_pos = pos + len(needle)
    calls.sort(key=lambda call: call.line)
    return calls


def decode_qstring_literal(value: str) -> str:
    return (
        value.replace(r"\"", '"')
        .replace(r"\\", "\\")
        .replace(r"\n", "\n")
        .replace(r"\t", "\t")
    )


def literal_from_arg(arg: str) -> str | None:
    match = QSTRING_RE.search(arg)
    if match:
        return decode_qstring_literal(match.group(1))
    match = RAW_STRING_RE.fullmatch(arg.strip())
    if match:
        return decode_qstring_literal(match.group(1))
    return None


def literal_pairs(text: str):
    for regex in (FIELD_PAIR_RE, FIELD_INSERT_RE):
        for match in regex.finditer(text):
            key = decode_qstring_literal(match.group("qkey") or match.group("rkey") or "")
            raw_value = match.group("qvalue") or match.group("rvalue")
            value = decode_qstring_literal(raw_value) if raw_value is not None else None
            yield key, value, match.start()


def field_values(text: str, key: str) -> List[str]:
    return [value for field_key, value, _ in literal_pairs(text) if field_key == key and value is not None]


def looks_like_log_field_pair_context(text: str, offset: int) -> bool:
    prefix = text[max(0, offset - 180):offset]
    return ".fields" in prefix or bool(LOG_FIELD_CONTEXT_RE.search(prefix))


def extract_string_literals(text: str) -> List[str]:
    literals: List[str] = []
    qstring_spans = []
    for match in QSTRING_RE.finditer(text):
        qstring_spans.append(match.span())
        literals.append(decode_qstring_literal(match.group(1)))
    for match in RAW_STRING_RE.finditer(text):
        if any(start <= match.start() < end for start, end in qstring_spans):
            continue
        literals.append(decode_qstring_literal(match.group(1)))
    return literals


def without_allowed_technical_text(value: str) -> str:
    cleaned = PATH_OR_FILE_RE.sub(" ", value)
    cleaned = CODE_TOKEN_RE.sub(" ", cleaned)
    for term in sorted(TECHNICAL_TERMS, key=len, reverse=True):
        cleaned = re.sub(rf"\b{re.escape(term)}\b", " ", cleaned, flags=re.IGNORECASE)
    cleaned = re.sub(r"%\d+|0x[0-9A-Fa-f]+|--?[A-Za-z0-9_-]+", " ", cleaned)
    return cleaned


def english_tokens(value: str) -> List[str]:
    cleaned = without_allowed_technical_text(value)
    return [token.lower() for token in re.findall(r"[A-Za-z]+", cleaned)]


def has_english_phrase(value: str) -> bool:
    cleaned = without_allowed_technical_text(value)
    if COMMON_ENGLISH_PHRASE_RE.search(cleaned):
        return True
    tokens = english_tokens(value)
    if len(tokens) < 2:
        return False
    for i in range(len(tokens) - 1):
        pair = tokens[i:i + 2]
        if any(token in ENGLISH_ACTION_WORDS for token in pair):
            return True
        if any(token in ENGLISH_FUNCTION_WORDS for token in pair) and len(tokens) >= 3:
            return True
    return False


def message_language_issue(value: str) -> str | None:
    if not value or value in THIRD_PARTY_QT_MESSAGE_ALLOWLIST:
        return None
    if CHINESE_RE.search(value):
        if has_english_phrase(value):
            return "mixed"
        return None
    if has_english_phrase(value):
        return "english"
    return None


def event_literal_for_call(call: Call) -> str | None:
    spec = LOG_CALLS[call.name]
    event_index = spec.get("event")
    if isinstance(event_index, int) and len(call.args) > event_index:
        return literal_from_arg(call.args[event_index])
    values = field_values(call.text, "event")
    return values[0] if values else None


def category_literal_for_call(call: Call) -> str | None:
    spec = LOG_CALLS[call.name]
    if "category_literal" in spec:
        return str(spec["category_literal"])
    category_index = spec.get("category")
    if isinstance(category_index, int) and len(call.args) > category_index:
        return literal_from_arg(call.args[category_index])
    return None


def source_literal_for_call(call: Call) -> str | None:
    spec = LOG_CALLS[call.name]
    if "source_literal" in spec:
        return str(spec["source_literal"])
    source_index = spec.get("source")
    if isinstance(source_index, int) and len(call.args) > source_index:
        return literal_from_arg(call.args[source_index])
    return None


def message_literal_for_call(call: Call) -> str | None:
    literal = LOG_CALLS[call.name].get("message_literal")
    if isinstance(literal, str):
        return literal
    message_index = LOG_CALLS[call.name]["message"]
    if isinstance(message_index, int) and len(call.args) > message_index:
        return literal_from_arg(call.args[message_index])
    return None


def fields_arg_is_dynamic(call: Call) -> bool:
    fields_index = LOG_CALLS[call.name].get("fields")
    if not isinstance(fields_index, int) or len(call.args) <= fields_index:
        return False
    return call.args[fields_index].strip() in {"fields", "recordFields", "details"}


def level_text(call: Call) -> str:
    level_index = LOG_CALLS[call.name].get("level", 0)
    if isinstance(level_index, int) and len(call.args) > level_index:
        return call.args[level_index]
    return call.args[0] if call.args else ""


def has_reason_or_error_code(call: Call) -> bool:
    return bool(field_values(call.text, "error_code") or field_values(call.text, "reason_code"))


def is_allowed_runtime_fallback(text: str, offset: int) -> bool:
    context = text[max(0, offset - 220):offset + 220]
    return "SkyRuntime::publishRuntimeLog" in context and "SKY_RUNTIME_ERROR" in context


def issue_for_language(path_label: str, line: int, code: str, value: str) -> Issue:
    if code == "mixed-first-party-message":
        detail = f"第一方 message 中存在明显中英混杂句子: {value}"
    elif code == "english-first-party-message":
        detail = f"第一方 message 应使用简体中文，英文原文应放入字段: {value}"
    else:
        detail = f"直接 Qt 日志文本不符合中文规范: {value}"
    return Issue(code, path_label, line, detail, "改为自然中文；外部原文放入 system_error/process_output/external_raw_text 等字段。")


def audit_qt_messages(path_label: str, text: str) -> List[Issue]:
    issues: List[Issue] = []
    for match in QT_MESSAGE_START_RE.finditer(text):
        open_pos = text.find("(", match.start())
        close_pos = find_balanced_parenthesis(text, open_pos)
        if close_pos < 0:
            continue
        end = find_statement_end(text, close_pos + 1)
        statement = text[match.start():end]
        literals = extract_string_literals(statement)
        for literal in literals:
            language = message_language_issue(literal)
            if language:
                issues.append(issue_for_language(
                    path_label,
                    line_for_offset(text, match.start()),
                    f"qt-message-{language}",
                    literal,
                ))
    return issues


def audit_machine_identifier(path_label: str, line: int, key: str, value: str | None, full_text: str, offset: int) -> List[Issue]:
    issues: List[Issue] = []
    if CHINESE_RE.search(key):
        issues.append(Issue(
            "chinese-field-key",
            path_label,
            line,
            f"结构化字段键必须保持英文: {key}",
            "字段键改为小写 snake_case，中文描述放入 message。",
        ))
    if key not in RESERVED_FIELD_KEYS and key.startswith("_log_"):
        return issues
    if key not in RESERVED_FIELD_KEYS and not FIELD_KEY_FORMAT_RE.fullmatch(key):
        issues.append(Issue(
            "invalid-field-key",
            path_label,
            line,
            f"fields key 必须为小写 snake_case: {key}",
            "使用 ^[a-z0-9]+(?:_[a-z0-9]+)*$，保留键仅限文档列出的 allowlist。",
        ))
    if key == "event" and value is not None and not EVENT_RE.fullmatch(value):
        issues.append(Issue(
            "invalid-event",
            path_label,
            line,
            f"event 必须为小写 snake_case: {value}",
            "将 event 改为 ^[a-z0-9]+(?:_[a-z0-9]+)*$。",
        ))
    if key in {"error_code", "reason_code"} and value is not None:
        if value == "SKY_RUNTIME_ERROR" and is_allowed_runtime_fallback(full_text, offset):
            return issues
        if not CODE_RE.fullmatch(value):
            issues.append(Issue(
                f"invalid-{key.replace('_', '-')}",
                path_label,
                line,
                f"{key} 必须为大写下划线形式: {value}",
                "使用 ^[A-Z0-9]+(?:_[A-Z0-9]+)*$；运行时动态值无法静态验证时可跳过。",
            ))
        if value == "SKY_RUNTIME_ERROR":
            issues.append(Issue(
                "generic-sky-runtime-error",
                path_label,
                line,
                "SKY_RUNTIME_ERROR 只能作为 SkyRuntime 发布函数中的 Release 兜底。",
                "为具体业务失败补充稳定 error_code 或 reason_code。",
            ))
    return issues


def audit_text(path_label: str, text: str) -> List[Issue]:
    issues: List[Issue] = []
    scan_text = strip_cpp_comments_keep_lines(text)

    for match in TEXT_SEVERITY_RE.finditer(scan_text):
        issues.append(Issue(
            "message-keyword-level",
            path_label,
            line_for_offset(scan_text, match.start()),
            "日志级别或分类不能通过 message.contains(...) 文本关键词判断。",
            "使用调用方状态、返回码、错误枚举或明确事件分支决定 level/category。",
        ))

    issues.extend(audit_qt_messages(path_label, scan_text))

    for match in DIAGNOSTIC_FAILURE_RE.finditer(scan_text):
        literal = decode_qstring_literal(match.group(1))
        language = message_language_issue(literal)
        if language:
            issues.append(Issue(
                f"{language}-diagnostic-failure",
                path_label,
                line_for_offset(scan_text, match.start()),
                f"LogService writer failure 内部提示应使用简体中文: {literal}",
                "改为中文固定文案，系统原始错误放入结构化字段。",
            ))

    for match in CONSOLE_TEXT_RE.finditer(scan_text):
        literal = decode_qstring_literal(match.group(1))
        language = message_language_issue(literal)
        if language:
            issues.append(Issue(
                f"{language}-console-diagnostic",
                path_label,
                line_for_offset(scan_text, match.start()),
                f"运行诊断用的 QTextStream 输出应使用简体中文: {literal}",
                "改为中文运行诊断文本，保留协议/产品名英文。",
            ))

    for regex in (FIELD_PAIR_RE, FIELD_INSERT_RE):
        for match in regex.finditer(scan_text):
            if regex is FIELD_PAIR_RE and not looks_like_log_field_pair_context(scan_text, match.start()):
                continue
            key = decode_qstring_literal(match.group("qkey") or match.group("rkey") or "")
            raw_value = match.group("qvalue") or match.group("rvalue")
            value = decode_qstring_literal(raw_value) if raw_value is not None else None
            line = line_for_offset(scan_text, match.start())
            issues.extend(audit_machine_identifier(path_label, line, key, value, scan_text, match.start()))

    for call in extract_calls(scan_text):
        source = source_literal_for_call(call)
        if source and CHINESE_RE.search(source):
            issues.append(Issue(
                "chinese-source",
                path_label,
                call.line,
                f"source 必须使用稳定英文组件名: {source}",
                "使用 SkyCore、SkyTui、Ground、LogService 等稳定英文 source。",
            ))
        category = category_literal_for_call(call)
        if category and not CATEGORY_RE.fullmatch(category):
            issues.append(Issue(
                "invalid-category",
                path_label,
                call.line,
                f"category 必须为小写点分层级: {category}",
                "使用 ^[a-z0-9]+(?:\\.[a-z0-9_]+)*$。",
            ))
        event = event_literal_for_call(call)
        if event and not EVENT_RE.fullmatch(event):
            issues.append(Issue(
                "invalid-event",
                path_label,
                call.line,
                f"event 必须为小写 snake_case: {event}",
                "使用 ^[a-z0-9]+(?:_[a-z0-9]+)*$。",
            ))

        for key, value, rel_offset in literal_pairs(call.text):
            issues.extend(audit_machine_identifier(path_label, call.line, key, value, scan_text, call.start + rel_offset))

        literal = message_literal_for_call(call)
        if literal:
            language = message_language_issue(literal)
            if language:
                issues.append(issue_for_language(
                    path_label,
                    call.line,
                    f"{language}-first-party-message",
                    literal,
                ))

        level = level_text(call)
        dynamic_fields = fields_arg_is_dynamic(call)
        if any(name in level for name in LEVEL_NAMES) and not event and not dynamic_fields:
            issues.append(Issue(
                "missing-event",
                path_label,
                call.line,
                "Warning/Error/Critical 日志必须带稳定 event。",
                "在 fields 中补 event，或使用带 event 参数的 helper。",
            ))
        if any(name in level for name in ERROR_LEVEL_NAMES) and not has_reason_or_error_code(call) and not dynamic_fields:
            issues.append(Issue(
                "missing-error-code",
                path_label,
                call.line,
                "Error/Critical 日志必须带 error_code 或明确 reason_code。",
                "补充具体业务 error_code；预期/可恢复原因使用 reason_code。",
            ))
    return issues


def assert_issue_codes(label: str, fixture: str, expected_present: set[str], expected_absent: set[str] | None = None) -> None:
    issues = audit_text(label, fixture)
    codes = {issue.code for issue in issues}
    missing = expected_present - codes
    unexpected = (expected_absent or set()) & codes
    if missing or unexpected:
        rendered = "\n".join(issue.render() for issue in issues)
        raise AssertionError(
            f"{label} self-test failed; missing={sorted(missing)}, unexpected={sorted(unexpected)}\n{rendered}"
        )


def run_self_test() -> List[Issue]:
    bad_fixture = r'''
void bad(LogService& logService, const QString& message) {
    const bool warning = message.contains(QStringLiteral("failed"), Qt::CaseInsensitive);
    qDebug("Device connection failed and will retry.");
    qDebug() << QStringLiteral("设备 connect failed and retry later");
    logService.publish(LogLevel::Warning,
                       QStringLiteral("SkyCore"),
                       QStringLiteral("Device.Connection"),
                        QStringLiteral("设备 connect failed and retry later"),
                       {{QStringLiteral("event"), QStringLiteral("设备失败")},
                        {QStringLiteral("reason_code"), QStringLiteral("bad_reason")},
                        {QStringLiteral("Bad-Key"), QStringLiteral("value")}});
    logService.publish(LogLevel::Warning,
                       QStringLiteral("天空端"),
                       QStringLiteral("device.connection"),
                       QStringLiteral("设备连接异常。"),
                       {{QStringLiteral("event"), QStringLiteral("device_connection_warning")},
                        {QStringLiteral("reason_code"), QStringLiteral("DEVICE_CONNECTION_WARNING")}});
    logService.publish(LogLevel::Error,
                       QStringLiteral("SkyCore"),
                       QStringLiteral("process"),
                       QStringLiteral("子进程发生错误。"),
                       {{QStringLiteral("event"), QStringLiteral("child_process_error")}});
    logService.publish(LogLevel::Error,
                       QStringLiteral("SkyCore"),
                       QStringLiteral("process"),
                       QStringLiteral("子进程发生错误。"),
                       {{QStringLiteral("event"), QStringLiteral("child_process_error")},
                        {QStringLiteral("error_code"), QStringLiteral("child_process_error")}});
    notifyFailure(QStringLiteral("Cannot write application log file."));
    QTextStream(stdout) << "Runtime status has started.";
}
'''
    expected = {
        "message-keyword-level",
        "qt-message-english",
        "qt-message-mixed",
        "invalid-category",
        "mixed-first-party-message",
        "invalid-event",
        "invalid-field-key",
        "missing-error-code",
        "invalid-error-code",
        "invalid-reason-code",
        "chinese-source",
        "english-diagnostic-failure",
        "english-console-diagnostic",
    }
    assert_issue_codes("<self-test-bad>", bad_fixture, expected)

    good_fixture = r'''
void good(LogService& logService) {
    qDebug("设备连接失败，将自动重试。");
    qDebug() << QStringLiteral("TCP 服务已开始监听。");
    qDebug("Raw third-party driver line");
    logService.publish(LogLevel::Info,
                       QStringLiteral("SkyCore"),
                       QStringLiteral("device.connection"),
                       QStringLiteral("EPSILON 设备连接失败。"),
                       {{QStringLiteral("event"), QStringLiteral("device_connection_failed")},
                        {QStringLiteral("error_code"), QStringLiteral("SERIAL_OPEN_FAILED")},
                        {QStringLiteral("config_path"), QStringLiteral("config.json")}});
    logService.publish(LogLevel::Info,
                       QStringLiteral("SkyCore"),
                       QStringLiteral("config.load"),
                       QStringLiteral("无法加载 config.json。"),
                       {{QStringLiteral("event"), QStringLiteral("sky_config_load_failed")}});
    QVariantMap fields;
    fields.insert(QStringLiteral("event"), event);
    fields.insert(QStringLiteral("error_code"), errorCode);
    logService.publish(LogLevel::Error,
                       QStringLiteral("SkyCore"),
                       QStringLiteral("runtime.dynamic"),
                       QStringLiteral("运行时动态错误。"),
                       fields);
}
'''
    assert_issue_codes(
        "<self-test-good>",
        good_fixture,
        set(),
        {
            "qt-message-english",
            "qt-message-mixed",
            "english-first-party-message",
            "mixed-first-party-message",
            "invalid-category",
            "invalid-event",
            "invalid-field-key",
            "invalid-error-code",
        },
    )
    return audit_text("<self-test-bad>", bad_fixture)


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
