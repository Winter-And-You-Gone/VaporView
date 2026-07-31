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
