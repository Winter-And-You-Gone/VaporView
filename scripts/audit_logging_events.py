#!/usr/bin/env python3
"""Compare VaporView logging events in source code with docs/logging_events.md."""

from __future__ import annotations

import argparse
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
import re
import sys
import tempfile
from typing import Iterable, Sequence

import audit_logging_language as lang


CATALOG_PATH = Path("docs/logging_events.md")
ACTIVE_STATUS = ""
NON_BLOCKING_STATUSES = {"planned", "deprecated", "legacy"}
LOG_ENTRY_INIT_RE = re.compile(
    r"log\s*\(\s*\{\s*VaporView::LogLevel::(?P<level>[A-Za-z]+)\s*,\s*"
    r"QStringLiteral\(\"(?P<event>[a-z0-9]+(?:_[a-z0-9]+)*)\"\)",
    re.DOTALL,
)
CONDITIONAL_EVENT_LITERAL_RE = re.compile(
    r"\?\s*QStringLiteral\(\"(?P<true>[a-z0-9]+(?:_[a-z0-9]+)*)\"\)\s*"
    r":\s*QStringLiteral\(\"(?P<false>[a-z0-9]+(?:_[a-z0-9]+)*)\"\)",
    re.DOTALL,
)


@dataclass(frozen=True)
class EventUse:
    event: str
    category: str | None
    level: str | None
    source: str | None
    path: str
    line: int


@dataclass
class CatalogEntry:
    event: str
    source: str
    category: str
    level: str
    message: str
    required_fields: str
    optional_fields: str
    code_cell: str
    status: str
    line: int


@dataclass
class Issue:
    code: str
    location: str
    detail: str
    suggestion: str

    def render(self) -> str:
        return f"{self.location}: {self.code}: {self.detail} 建议: {self.suggestion}"


def source_for_call(call: lang.Call) -> str | None:
    return lang.source_literal_for_call(call)


def category_for_call(call: lang.Call) -> str | None:
    return lang.category_literal_for_call(call)


def level_for_call(call: lang.Call) -> str | None:
    level = lang.level_text(call)
    match = re.search(r"LogLevel::([A-Za-z]+)", level)
    return match.group(1) if match else None


def event_for_call(call: lang.Call) -> str | None:
    return lang.event_literal_for_call(call)


def offset_inside_call(offset: int, calls: Sequence[lang.Call]) -> bool:
    return any(call.start <= offset < call.end for call in calls)


def extract_event_uses_from_text(path_label: str, text: str) -> list[EventUse]:
    scan_text = lang.strip_cpp_comments_keep_lines(text)
    calls = lang.extract_calls(scan_text)
    uses: list[EventUse] = []
    seen = set()
    for call in calls:
        spec = lang.LOG_CALLS[call.name]
        event_index = spec.get("event")
        event_literals: list[str] = []
        if isinstance(event_index, int) and len(call.args) > event_index:
            event_arg = call.args[event_index]
            for match in CONDITIONAL_EVENT_LITERAL_RE.finditer(event_arg):
                event_literals.extend([match.group("true"), match.group("false")])
        if not event_literals:
            event = event_for_call(call)
            if event:
                event_literals.append(event)
        for event_literal in event_literals:
            use = EventUse(
                event=event_literal,
                category=category_for_call(call),
                level=level_for_call(call),
                source=source_for_call(call),
                path=path_label,
                line=call.line,
            )
            key = (use.event, use.category, use.level, use.source, use.path, use.line)
            if key not in seen:
                seen.add(key)
                uses.append(use)

    for regex in (lang.FIELD_PAIR_RE, lang.FIELD_INSERT_RE):
        for match in regex.finditer(scan_text):
            if regex is lang.FIELD_PAIR_RE and not lang.looks_like_log_field_pair_context(scan_text, match.start()):
                continue
            if offset_inside_call(match.start(), calls):
                continue
            key = lang.decode_qstring_literal(match.group("qkey") or match.group("rkey") or "")
            value = lang.decode_qstring_literal(match.group("qvalue") or match.group("rvalue") or "")
            if key != "event" or not value:
                continue
            use = EventUse(
                event=value,
                category=None,
                level=None,
                source=None,
                path=path_label,
                line=lang.line_for_offset(scan_text, match.start()),
            )
            dedupe = (use.event, use.path, use.line)
            if dedupe not in seen:
                seen.add(dedupe)
                uses.append(use)

    for match in LOG_ENTRY_INIT_RE.finditer(scan_text):
        event = match.group("event")
        use = EventUse(
            event=event,
            category=None,
            level=match.group("level"),
            source=None,
            path=path_label,
            line=lang.line_for_offset(scan_text, match.start()),
        )
        key = (use.event, use.category, use.level, use.source, use.path, use.line)
        if key not in seen:
            seen.add(key)
            uses.append(use)

    return uses


def extract_event_uses(root: Path) -> list[EventUse]:
    uses: list[EventUse] = []
    for path in lang.iter_source_files(root):
        text = path.read_text(encoding="utf-8", errors="replace")
        rel = path.relative_to(root).as_posix()
        uses.extend(extract_event_uses_from_text(rel, text))
    return uses


def split_markdown_row(line: str) -> list[str]:
    return [cell.strip() for cell in line.strip().strip("|").split("|")]


def status_from_cells(cells: Sequence[str]) -> str:
    joined = " ".join(cells).lower()
    match = re.search(r"\bstatus\s*=\s*(planned|deprecated|legacy)\b", joined)
    if match:
        return match.group(1)
    for status in NON_BLOCKING_STATUSES:
        if re.search(rf"\b{status}\b", cells[3].lower() if len(cells) > 3 else ""):
            return status
    return ACTIVE_STATUS


def parse_catalog(path: Path) -> list[CatalogEntry]:
    entries: list[CatalogEntry] = []
    if not path.exists():
        raise FileNotFoundError(path)
    for line_no, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        if not line.startswith("|"):
            continue
        if re.match(r"\|\s*-+", line):
            continue
        cells = split_markdown_row(line)
        if len(cells) < 8 or cells[2].lower() == "event":
            continue
        entries.append(CatalogEntry(
            source=cells[0],
            category=cells[1],
            event=cells[2],
            level=cells[3],
            message=cells[4],
            required_fields=cells[5],
            optional_fields=cells[6],
            code_cell=cells[7],
            status=status_from_cells(cells),
            line=line_no,
        ))
    return entries


def has_error_level(level: str) -> bool:
    return any(part.strip().lower() in {"error", "critical"} for part in re.split(r"[/,]", level))


def catalog_code_values(entry: CatalogEntry) -> list[str]:
    if not entry.code_cell:
        return []
    return [part.strip() for part in re.split(r"[/,]", entry.code_cell) if part.strip()]


def audit_catalog(root: Path) -> list[Issue]:
    uses = extract_event_uses(root)
    catalog_file = root / CATALOG_PATH
    entries = parse_catalog(catalog_file)
    issues: list[Issue] = []

    uses_by_event: dict[str, list[EventUse]] = defaultdict(list)
    for use in uses:
        uses_by_event[use.event].append(use)
    entries_by_event: dict[str, list[CatalogEntry]] = defaultdict(list)
    for entry in entries:
        entries_by_event[entry.event].append(entry)

    for use_event, event_uses in sorted(uses_by_event.items()):
        if use_event not in entries_by_event:
            first = event_uses[0]
            issues.append(Issue(
                "missing-catalog-event",
                f"{first.path}:{first.line}",
                f"源码事件未登记到 docs/logging_events.md: {use_event}",
                "为该 event 添加目录行；若已合并语义，修改源码使用稳定 event + reason_code。",
            ))

    for event, event_entries in sorted(entries_by_event.items()):
        active_entries = [entry for entry in event_entries if entry.status not in NON_BLOCKING_STATUSES]
        if len(event_entries) > 1:
            lines = ", ".join(str(entry.line) for entry in event_entries)
            issues.append(Issue(
                "duplicate-catalog-event",
                f"{CATALOG_PATH.as_posix()}:{event_entries[0].line}",
                f"event 重复登记: {event} (lines {lines})",
                "合并为单一目录行；不同原因使用 reason_code，不拆分重复 event。",
            ))
        if event not in uses_by_event and active_entries:
            first = active_entries[0]
            issues.append(Issue(
                "stale-catalog-event",
                f"{CATALOG_PATH.as_posix()}:{first.line}",
                f"目录事件当前源码未使用: {event}",
                "确认是否删除、改为 status=planned/deprecated/legacy，或补回源码事件。",
            ))

    for entry in entries:
        loc = f"{CATALOG_PATH.as_posix()}:{entry.line}"
        if not lang.EVENT_RE.fullmatch(entry.event):
            issues.append(Issue(
                "invalid-catalog-event",
                loc,
                f"目录 event 命名不规范: {entry.event}",
                "使用小写 snake_case：^[a-z0-9]+(?:_[a-z0-9]+)*$。",
            ))
        if entry.category and not lang.CATEGORY_RE.fullmatch(entry.category):
            issues.append(Issue(
                "invalid-catalog-category",
                loc,
                f"目录 category 命名不规范: {entry.category}",
                "使用小写点分层级：^[a-z0-9]+(?:\\.[a-z0-9_]+)*$。",
            ))
        for code in catalog_code_values(entry):
            if code in {"status=planned", "status=deprecated", "status=legacy"}:
                continue
            if code == "SKY_RUNTIME_ERROR":
                issues.append(Issue(
                    "catalog-generic-sky-runtime-error",
                    loc,
                    "事件目录不得推荐 SKY_RUNTIME_ERROR。",
                    "改为具体业务 error_code；保留 SkyRuntime 内部 Release 兜底即可。",
                ))
            elif not lang.CODE_RE.fullmatch(code):
                issues.append(Issue(
                    "invalid-catalog-code",
                    loc,
                    f"目录 error_code/reason_code 命名不规范: {code}",
                    "使用大写下划线：^[A-Z0-9]+(?:_[A-Z0-9]+)*$。",
                ))
        if has_error_level(entry.level) and not catalog_code_values(entry) and entry.status not in NON_BLOCKING_STATUSES:
            issues.append(Issue(
                "catalog-error-missing-code",
                loc,
                f"Error/Critical 事件缺少 error_code / reason_code: {entry.event}",
                "为重要错误事件补推荐 error_code；确实无错误语义时显式记录 reason_code 或 status。",
            ))

    for event, event_uses in sorted(uses_by_event.items()):
        if event not in entries_by_event:
            continue
        entry = entries_by_event[event][0]
        source_categories = {use.category for use in event_uses if use.category}
        if source_categories and entry.category not in source_categories:
            first = event_uses[0]
            issues.append(Issue(
                "catalog-category-conflict",
                f"{first.path}:{first.line}",
                f"{event} 源码 category={sorted(source_categories)}，目录 category={entry.category}",
                "让源码与目录 category 保持一致；若语义不同，应拆分或用 reason_code。",
            ))
        source_levels = {use.level for use in event_uses if use.level}
        if len(source_levels) == 1 and entry.level and "/" not in entry.level and entry.level not in source_levels:
            first = event_uses[0]
            issues.append(Issue(
                "catalog-level-conflict",
                f"{first.path}:{first.line}",
                f"{event} 源码 level={sorted(source_levels)}，目录 recommended level={entry.level}",
                "同步目录推荐 level；多级别事件可写成 Info/Error 形式。",
            ))

    return issues


def write_catalog(root: Path, rows: list[str]) -> None:
    docs = root / "docs"
    docs.mkdir(exist_ok=True)
    (docs / "logging_events.md").write_text("\n".join(rows) + "\n", encoding="utf-8")


def run_self_test() -> None:
    header = [
        "# Logging Event Catalog",
        "",
        "| source | category | event | recommended level | 中文 message | required fields | optional fields | error_code / reason_code |",
        "| --- | --- | --- | --- | --- | --- | --- | --- |",
    ]
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / "src").mkdir()
        source = r'''
void good(LogService& logService) {
    logService.publish(LogLevel::Error,
                       QStringLiteral("SkyCore"),
                       QStringLiteral("device.connection"),
                       QStringLiteral("设备连接失败。"),
                       {{QStringLiteral("event"), QStringLiteral("device_connection_failed")},
                        {QStringLiteral("error_code"), QStringLiteral("SERIAL_OPEN_FAILED")}});
}
'''
        (root / "src" / "fixture.cpp").write_text(source, encoding="utf-8")

        write_catalog(root, header + [
            "| SkyCore | device.connection | device_connection_failed | Error | 设备连接失败。 |  |  | SERIAL_OPEN_FAILED |",
        ])
        assert not audit_catalog(root), "consistent catalog should pass"

        write_catalog(root, header + [
            "| SkyCore | device.connection | device_connection_failed | Error | 设备连接失败。 |  |  | SERIAL_OPEN_FAILED |",
            "| SkyCore | device.connection | stale_device_event | Error | 已失效事件。 |  |  | STALE_DEVICE_EVENT |",
        ])
        assert any(issue.code == "stale-catalog-event" for issue in audit_catalog(root)), "stale active catalog event should fail"

        write_catalog(root, header)
        assert any(issue.code == "missing-catalog-event" for issue in audit_catalog(root)), "missing catalog event should fail"

        write_catalog(root, header + [
            "| SkyCore | device.connection | device_connection_failed | Error | 设备连接失败。 |  |  | SERIAL_OPEN_FAILED |",
            "| SkyCore | device.connection | device_connection_failed | Error | 重复。 |  |  | SERIAL_OPEN_FAILED |",
        ])
        assert any(issue.code == "duplicate-catalog-event" for issue in audit_catalog(root)), "duplicate catalog event should fail"

        write_catalog(root, header + [
            "| SkyCore | device.session | device_connection_failed | Error | 设备连接失败。 |  |  | SERIAL_OPEN_FAILED |",
        ])
        assert any(issue.code == "catalog-category-conflict" for issue in audit_catalog(root)), "category conflict should fail"

        write_catalog(root, header + [
            "| SkyCore | device.connection | device_connection_failed | Info | 设备连接失败。 |  |  | SERIAL_OPEN_FAILED |",
        ])
        assert any(issue.code == "catalog-level-conflict" for issue in audit_catalog(root)), "level conflict should fail"

        write_catalog(root, header + [
            "| SkyCore | device.connection | device_connection_failed | Error | 设备连接失败。 |  |  | SERIAL_OPEN_FAILED |",
            "| SkyCore | future | future_event | planned | 计划事件。 |  | status=planned |  |",
            "| SkyCore | old | old_event | deprecated | 已废弃事件。 |  | status=deprecated |  |",
        ])
        assert not audit_catalog(root), "planned/deprecated catalog entries should not fail as stale"


def main(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--self-test", action="store_true", help="run built-in fixture tests before repository audit")
    args = parser.parse_args(argv)

    if args.self_test:
        run_self_test()

    root = args.root.resolve()
    issues = audit_catalog(root)
    if issues:
        for issue in issues:
            print(issue.render())
        print(f"\nlogging event catalog audit failed: {len(issues)} issue(s)")
        return 1
    print("logging event catalog audit passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
