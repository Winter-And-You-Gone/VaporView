# VaporView session package format

New Ground and Sky recordings use the same session package layout, file set,
CSV schemas, RAW DAT format, and `session.json` schema. The only structural
difference between the two origins is the explicit manifest field:

- `"recording_origin": "ground"`
- `"recording_origin": "sky"`

Production code represents this value with `VaporView::Session::RecordingOrigin`
and serializes it through the shared session helpers. New writers do not write
the legacy `mode` field.

## Standard directory tree

```text
session_yyyy-MM-dd_HH-mm-ss/
├── session.json
├── raw_dat_format.md
├── sensors/
│   ├── devices.csv
│   ├── rd105_temperature_controller.csv
│   └── waveform_features.csv
├── raw/
│   ├── epsilon.dat
│   ├── ptb.dat
│   ├── hmp.dat
│   ├── lidar.dat
│   ├── tcp_wave.dat
│   └── tcp_wave_peaks.csv
├── logs/
│   ├── event_log.csv
│   └── error_log.txt
└── config/
    └── device_config.json
```

All paths are relative to the session root and are defined by
`SessionPackageLayout`. Ground and Sky both create every standard file even
when the recorder has no data for that stream.

## Empty file rules

- `sensors/devices.csv` uses the shared `SessionSensorCsv` header.
- `sensors/rd105_temperature_controller.csv`,
  `sensors/waveform_features.csv`, `raw/tcp_wave_peaks.csv`, and
  `logs/event_log.csv` are created with their standard headers.
- `raw/epsilon.dat`, `raw/ptb.dat`, `raw/hmp.dat`, `raw/lidar.dat`, and
  `raw/tcp_wave.dat` are created as valid zero-record unified RAW DAT files.
- `config/device_config.json` is always valid JSON.
- `logs/error_log.txt` is created and may be empty.
- `raw_dat_format.md` is generated from built-in shared text so session
  creation does not depend on a source-tree-relative documentation file.

## Manifest schema

New `session.json` files contain the same top-level key set for Ground and Sky:

- `session_format`, `session_format_version`
- `recording_origin`
- `session_name`, `state`
- `start_time_utc`, `start_time_us`, `end_time_utc`, `end_time_us`,
  `elapsed_ms`
- `software_version`, `timestamp_unit`, `raw_dat_format_version`,
  `epsilon_schema_version`
- `sensor_export_rate_hz`, `other_devices_export_rate_hz`
- `raw_export_mode`
- `waveform_export_rate_hz`, `waveform_export_mode`,
  `waveform_value_type`, `waveform_timestamp_type`,
  `waveform_points_per_frame`, `waveform_file_count`
- `capture`, `counts`, `paths`, `raw_files`

`capture` always contains:

- `telemetry_transport`
- `telemetry_endpoint`
- `telemetry_port`
- `telemetry_baud`

When capture values do not apply, the keys remain present and the values are
`null`.

`counts` always contains string counters:

- `sensor_rows`
- `temperature_controller_rows`
- `waveform_frames`
- `waveform_feature_rows`
- `event_rows`
- `error_rows`

`paths` always contains every standard relative path:

- `devices_csv`
- `temperature_controller_csv`
- `waveform_features_csv`
- `epsilon_raw`, `ptb_raw`, `hmp_raw`, `lidar_raw`, `tcp_wave_raw`
- `tcp_wave_peaks_csv`
- `event_log`, `error_log`
- `device_config`
- `raw_format_document`

`raw_files` always contains `epsilon`, `ptb`, `hmp`, `lidar`, and
`tcp_wave`. Each entry contains:

- `path`
- `source_id`
- `format_version`
- `records`

Large counters and timestamps that may exceed exact JSON number precision are
stored as strings. Fixed schema/version/source identifiers remain JSON numbers.
Zero counts are still written as `"0"`; fields are not omitted just because a
stream has no records.

## Shared implementation

- `RecordingOrigin` defines and serializes `ground` / `sky`.
- `SessionPackageLayout` is the single production source of standard relative
  paths and shared CSV headers.
- `SessionManifest` serializes, parses, validates, and atomically writes the
  unified manifest.
- `SessionPackageInitializer` creates the standard directory tree, all empty
  files, zero-record RAW DAT files, device config JSON, format document, and the
  initial recording manifest.
- `GroundRecordingService` and `SkySessionRecorder` call the shared initializer
  and shared manifest writer instead of assembling complete session layouts or
  `session.json` independently.

## Historical compatibility

Readers are strict for new `recording_origin` values and accept only
`ground` or `sky`. For old sessions:

- missing `recording_origin` and missing `mode` are treated as Ground;
- legacy `"mode": "sky"` is treated as Sky;
- legacy root counters such as `sensor_rows` and `waveform_frames` remain
  readable;
- legacy raw counters named `record_count` remain readable;
- legacy path keys such as `waveform_peak_index` remain readable;
- old sessions are not required to contain the new standard empty files.

The viewer displays the parsed recording origin in the session overview.
