# Logging

VaporView writes software diagnostics as UTF-8 JSON Lines (`.jsonl`). Each line
is one complete `LogRecord`; the schema remains version 1 and includes UTC time,
microsecond timestamp, level, source/category, process/thread IDs, sequence,
correlation/session IDs, message, and structured fields.

Within one process, `sequence` is the authoritative event order. The normal log
writer preserves FIFO order in the main `<Application>-YYYY-MM-DD.jsonl` file. Under
extreme Warning/Error pressure, or after the pending-Critical hard limit is
reached, records may instead be synchronously written to
`<Application>-emergency-YYYY-MM-DD.jsonl`. Cross-file line order is not guaranteed;
analysis tools should merge records by `process_id`, then `sequence`, using the
timestamp only for wall-clock correlation. File names and directory enumeration
order are not event-order guarantees.

The preferred directory is `<ApplicationDir>/logs`. If it cannot be opened, the
writer switches to the platform-local VaporView log directory. Runtime code can
query the actual location through `LogService::logDirectory()` and
`LogService::logFilePath()`.

Main and emergency files rotate at 10 MiB. Rotated generations are bounded, and
the normal cleanup pass retains at most 10 matching application log files with a
target total size of about 100 MiB. Emergency output is also sent to `stderr`
(and the Windows debugger) so a failed emergency file write is still observable.

## Record size limits

Every record is normalized before it enters the asynchronous queue or is emitted
through `recordPublished`. Limits are measured on UTF-8/compact JSON bytes:

- `message`: 64 KiB;
- one string value or map key: 64 KiB;
- one `QByteArray`: 64 KiB before JSON conversion;
- the compact `fields` object: 192 KiB;
- one complete JSONL line, including `\n`: 256 KiB;
- one map/list/string-list container: 256 elements;
- QVariant nesting: 8 levels, with a 4096-node per-record work budget.

String truncation retains the beginning, ends with `...<truncated>`, and never
cuts a UTF-8 sequence. Maps use stable key order; lists retain their first
elements. Unsupported QVariant values become an object containing
`_unsupported_type`. Aggregate field and whole-record limits remove complete
values only, so the result is always a complete JSON object rather than a
byte-truncated fragment. The final record keeps schema/timestamps, sequence,
level, source/category, message, process/thread IDs, and truncation state;
correlation and session IDs are retained whenever the hard record budget permits.

Top-level `fields` names beginning with `_log_` are reserved. When any limit is
applied, the writer adds one metadata set containing `_log_truncated`,
`_log_truncation_reasons`, `_log_original_message_utf8_bytes`,
`_log_original_fields_json_bytes`, and applicable dropped/truncated counters.
For structures that stop at a depth, node, element, or byte budget,
`_log_original_fields_size_is_lower_bound` states that the reported original
fields size is a lower bound rather than a full traversal result. Business fields
cannot replace these reserved values.

Normal and emergency files use the same final bounded serializer. Emergency is
a synchronous reliability path, so a slow disk can still block the Critical
producer that uses it, but the 256 KiB record cap bounds the work for one record.

`LogService::instance()` remains available for compatibility. Its raw pointer is
not lifetime-safe and must not be retained; new callbacks use
`LogService::withCurrentInstance()`.

## 日志语言与自动审计

本节集中记录日志文本语言、机器标识命名和自动审计入口。`docs/logging_localization_progress.md`
只记录迁移进度，`docs/logging_events.md` 只记录事件目录；新增或修改日志时，应以本节作为
主规范入口。

VaporView 第一方运行日志采用“中文可读、英文可检索”的约定。`message` 默认使用简体中文，面向开发者和现场人员直接阅读；`level`、`source`、`category`、`event`、`error_code`、`reason_code`、字段键、枚举值、协议消息类型和命令行参数保持英文。

日志级别必须由调用方、状态机、返回值、错误枚举或明确事件分支决定，不能通过 `message.contains(...)` 搜索中文或英文关键词判断。外部库、操作系统、设备驱动、串口/TCP 错误、子进程 stdout/stderr、协议 payload 和设备返回原文必须保留在结构化字段中，例如 `system_error`、`process_output`、`external_raw_text` 或 `payload_hex`，不能覆盖或翻译成第一方描述。直接 Qt 日志调用 `qDebug()`、`qInfo()`、`qWarning()` 和 `qCritical()` 同样纳入审计；第一方文本必须是自然中文，第三方原文只能通过精确 allowlist 或结构化原文字段保留。

旧的仅字符串 UI/控制台日志路径如暂时无法完全结构化，必须使用稳定中文包装 message，并在 `fields` 中标记 `legacy_unclassified=true`，原始 UI 文本放入 `ui_message`。旧路径默认只能进入桌面日志的“全部”视图，应设置 `ui_visibility=details`；高频进度或原始输出设置 `ui_visibility=hidden`。中文 message 保持简洁、稳定，不把大量变量拼进正文；变量、端点、设备名、错误原文和重试参数优先放入 `fields`。允许在中文句子中保留产品名和协议名，例如 SkyCore、SkyTui、IPC、TCP、UDP、JSON、CRC、EPSILON、PTB210、HMP3、TFA1500-L 和 RD105；但 `设备 connect failed and retry later`、`IPC 服务 start failed`、`配置 file loaded successfully` 这类完整英文语法片段视为中英混杂，必须改成自然中文。

### 桌面日志面板展示策略

日志文件和桌面日志面板是分层的：JSONL 文件继续保存 Debug、Info、Warning、Error 和 Critical；桌面日志面板只根据结构化字段和级别决定是否显示，不提高全局最低日志级别，也不丢弃普通 Info 文件记录。

地面端桌面日志面板提供三个正向视图：

- `关注`：默认视图，只显示 Warning、Error、Critical，以及显式 `ui_visibility=attention` 的 Info。
- `全部`：显示 Info、Warning、Error、Critical；Debug 仍隐藏。
- `调试`：显示 Debug、Info、Warning、Error、Critical，用于现场诊断和 Qt/内部细节。

生产者可在 `fields` 中设置稳定英文字段 `ui_visibility`：

- `attention`：进入默认关注视图，用于用户需要即时知道的重要状态变化。
- `details`：进入全部/调试视图，用于普通运行细节和 legacy UI 文本。
- `hidden`：不进入桌面日志面板，但仍写入日志文件，用于高频进度、原始输出、清空显示审计等。

缺少 `ui_visibility` 的旧记录按级别兼容：Critical/Error/Warning 进入 `attention`，Info 进入 `details`，Debug 只在调试视图出现。显示策略不得通过 `message` 文本关键词判断，也不得因为中文或英文文案不同而改变等级、分类或可见性。

UI 展示层会对短时间内相同 `source/category/level/event/ui_dedupe_key` 的重复记录做合并；缺少 `event` 时使用 `source/category/level/message`。合并只影响桌面 UI，不改变 JSONL、sequence、Critical completion、emergency、session 或 IPC 日志内容。

### 命名规范

- `source`：稳定组件名称，保持现有英文风格，例如 `App`、`Ground`、`SkyCore`、`SkyTui`、`LogService`、`RTK`。
- `category`：小写点分层级，例如 `device.connection`、`session.write`、`telemetry.link`、`ipc.protocol`。
- `event`：小写下划线形式，例如 `device_connection_failed`、`sky_runtime_started`、`child_process_output`。
- `error_code`：大写下划线形式，例如 `SERIAL_OPEN_FAILED`、`SKY_CONFIG_SAVE_FAILED`。
- `reason_code`：大写下划线形式，用于可恢复、预期或不一定表示操作失败的原因，例如 `RAW_FRAME_QUEUE_FULL`。
- 字段键：小写下划线形式，例如 `device_id`、`retry_delay_ms`、`endpoint`、`system_error`。

审计采用的命名正则如下：

- `category`：`^[a-z0-9]+(?:\.[a-z0-9_]+)*$`
- `event`：`^[a-z0-9]+(?:_[a-z0-9]+)*$`
- `error_code` / `reason_code`：`^[A-Z0-9]+(?:_[A-Z0-9]+)*$`
- `fields` key：`^[a-z0-9]+(?:_[a-z0-9]+)*$`，保留键包括 `event`、`error_code`、`reason_code`、`system_error`、`external_raw_text`、`process_output`、`legacy_unclassified`、`ui_visibility`、`ui_dedupe_key`、`ui_dedupe` 以及 `_log_*` 内部字段。

`event` 表示“发生了什么”，`reason_code` 表示“为什么发生”，`error_code` 表示具体失败类型。不要因为 shutdown、retry、timeout 等上下文差异拆出新 event；优先复用稳定 event 并补 `reason_code`，例如 `sky_recording_stop_requested` + `APPLICATION_SHUTDOWN`。

不要仅为了中文化而修改已经稳定使用的机器标识；也不要把新事件写成中文 `event` 或中文字段键。

### 事件示例

设备连接成功：

```json
{"schema_version":1,"level":"Info","source":"SkyCore","category":"device.connection","message":"设备已连接。","fields":{"event":"device_connected","device_id":"epsilon","endpoint":"COM3"}}
```

设备连接失败：

```json
{"schema_version":1,"level":"Error","source":"SkyCore","category":"device.connection","message":"设备连接失败，将自动重试。","fields":{"event":"device_connection_failed","error_code":"SERIAL_OPEN_FAILED","device_id":"epsilon","port":"COM3","system_error":"Access is denied."}}
```

IPC 断开：

```json
{"schema_version":1,"level":"Info","source":"SkyTui","category":"ipc.connection","message":"SkyCore IPC 连接已断开。","fields":{"event":"sky_ipc_disconnected","ui_visibility":"attention","ui_visible":true}}
```

会话记录开始：

```json
{"schema_version":1,"level":"Info","source":"SkyCore","category":"session.recording","message":"天空端会话记录已开始。","fields":{"event":"sky_recording_started","session_directory":"data/session-20260801","transport":"tcp","endpoint":"0.0.0.0:39100"}}
```

会话写入失败：

```json
{"schema_version":1,"level":"Error","source":"Ground","category":"session.write","message":"无法从会话日志接收器写入 event_log.csv。","fields":{"event":"session_event_log_append_failed","error_code":"SESSION_EVENT_LOG_APPEND_FAILED","session_sink_failure":true,"source":"SkyCore","category":"device.connection"}}
```

子进程错误输出：

```json
{"schema_version":1,"level":"Warning","source":"Updater","category":"process","message":"已收到子进程错误输出。","fields":{"event":"child_process_output","stream":"stderr","process_output":"original raw stderr line","raw_bytes":24}}
```

emergency 日志：

```json
{"schema_version":1,"level":"Critical","source":"LogService","category":"queue.critical_overload","message":"Critical 日志队列已达到上限，已切换到紧急写入通道。","fields":{"event":"critical_queue_overload","reason_code":"CRITICAL_QUEUE_LIMIT","pending_critical_limit":16}}
```

### 例外规则

以下内容允许保留英文，但不得冒充第一方中文描述：

- 命令行帮助、参数名和示例命令；
- 协议枚举、消息类型、设备/产品名和标准术语；
- Qt、操作系统、驱动、串口/TCP socket、文件系统和子进程输出的原始文本；
- 测试 sentinel、golden fixture 和刻意用于审计自测的英文样例；
- UI 语言切换所需的英文界面文本。

### 自动审计

本仓库提供两个轻量 Python 审计入口：

```bash
python scripts/audit_logging_language.py --root . --self-test
python scripts/audit_logging_events.py --root . --self-test
```

`audit_logging_language.py` 检查第一方日志语言、`qDebug/qInfo/qWarning/qCritical` 字面量、中英混杂句、`category/event/error_code/reason_code/fields key/source` 命名、Error/Critical 缺少具体错误码，以及 `SKY_RUNTIME_ERROR` 是否只作为 SkyRuntime Release 兜底。`audit_logging_events.py` 比对源码可静态识别的 `event` 字面量和 `docs/logging_events.md`，检查遗漏、过期、重复、category/level 冲突、目录命名和错误事件缺少错误码。

CTest 中注册了 `logging_language_audit_test` 和 `logging_event_catalog_audit_test`；支持 Python 的 CI 必须运行这两个测试。审计失败时优先修正源码 message、机器标识或事件目录。只有第三方原文、测试 sentinel、无法静态结构化的 legacy 文本等明确场景可以进入 allowlist；allowlist 必须精确到文本或调用场景，不能按目录宽泛跳过。

新增日志或修改日志语义后，应同步更新 `docs/logging_events.md`，并运行上述两个脚本或对应 CTest。
