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
