# VaporView session package format

For a file-by-file audit of the real `data\对比1` package against the v1.0
offline-session specification, see
[`session_format_actual_audit.md`](session_format_actual_audit.md). That audit
is based on the bytes and fields actually present in the sample package; it is
not inferred from production source code.

New Ground and Sky recordings use the same session package layout, file set,
CSV schemas, RAW DAT format, `session.json` schema, and `device_config.json`
schema. The origin value is explicit in both JSON metadata files:

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
│   ├── sensor_summary.csv
│   ├── laser_temperature_controller.csv
│   ├── system_temperature_controller.csv
│   └── waveform_features.csv
├── raw/
│   ├── navigation.dat
│   ├── pressure.dat
│   ├── temperature_humidity.dat
│   ├── distance.dat
│   ├── waveform.dat
│   ├── laser_temperature_controller.dat
│   ├── system_temperature_controller.dat
│   └── waveform_peaks.csv
├── logs/
│   ├── event_log.csv
│   └── error_log.txt
└── config/
    └── device_config.json
```

File names describe the data semantics, not the current device model or
transport. Device models, ports, and serial details remain in
`session.json`/`device_config.json`. All paths are relative to the session root
and are defined by `SessionPackageLayout`. Ground and Sky both create every
standard file even when the recorder has no data for that stream.

## Empty file rules

- `sensors/sensor_summary.csv` uses the shared `SessionSensorCsv` header.
- `sensors/laser_temperature_controller.csv`,
  `sensors/system_temperature_controller.csv`,
  `sensors/waveform_features.csv`, `raw/waveform_peaks.csv`, and
  `logs/event_log.csv` are created with their standard headers.
- `raw/navigation.dat`, `raw/pressure.dat`, `raw/temperature_humidity.dat`,
  `raw/distance.dat`, `raw/waveform.dat`,
  `raw/laser_temperature_controller.dat`, and
  `raw/system_temperature_controller.dat` are created as valid zero-record
  unified RAW DAT files.
- `config/device_config.json` uses the shared device configuration schema
  described below.
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
- `laser_temperature_controller_rows`
- `system_temperature_controller_rows`
- `waveform_frames`
- `waveform_feature_rows`
- `event_rows`
- `error_rows`

`paths` always contains every standard relative path:

- `sensor_summary_csv`
- `laser_temperature_controller_csv`
- `system_temperature_controller_csv`
- `waveform_features_csv`
- `navigation_raw`, `pressure_raw`, `temperature_humidity_raw`,
  `distance_raw`, `waveform_raw`
- `laser_temperature_controller_raw`
- `system_temperature_controller_raw`
- `waveform_peaks_csv`
- `event_log`, `error_log`
- `device_config`
- `raw_format_document`

`raw_files` always contains `navigation`, `pressure`, `temperature_humidity`,
`distance`, `waveform`, `laser_temperature_controller`, and
`system_temperature_controller`. Each entry contains:

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
- `SessionDeviceConfig` serializes and atomically writes the unified device
  configuration snapshot.
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
- legacy path keys and raw-file keys remain readable;
- readers resolve an explicit manifest path first, then the semantic default,
  then the historical device-named path. When no manifest path is declared and
  both files exist, the semantic file wins and a diagnostic warning is returned;
- old sessions are not required to contain the new standard empty files.

The historical path mapping is:

| Semantic standard path | Historical path |
| --- | --- |
| `raw/navigation.dat` | `raw/epsilon.dat` |
| `raw/pressure.dat` | `raw/ptb.dat` |
| `raw/temperature_humidity.dat` | `raw/hmp.dat` |
| `raw/distance.dat` | `raw/lidar.dat` |
| `raw/waveform.dat` | `raw/tcp_wave.dat` |
| `raw/laser_temperature_controller.dat` | `raw/rd105.dat` |
| `raw/system_temperature_controller.dat` | `raw/ai8.dat`, `raw/ai8288.dat` |
| `raw/waveform_peaks.csv` | `raw/tcp_wave_peaks.csv` |
| `sensors/sensor_summary.csv` | `sensors/devices.csv` |
| `sensors/laser_temperature_controller.csv` | `sensors/temperature_controller.csv`, `sensors/rd105_temperature_controller.csv` |
| `sensors/system_temperature_controller.csv` | `sensors/ai8_temperature_controller.csv`, `sensors/ai8288_temperature_controller.csv` |

New writers never create the historical names, and historical sessions are
read in place without renaming, copying, or modifying their files.

The viewer displays the parsed recording origin in the session overview.

## Device configuration schema

`config/device_config.json` is serialized by the shared `SessionDeviceConfig`
model. Ground and Sky therefore write the same top-level and nested key sets and
the same JSON types for populated values. It contains:

- `device_config_format` (`"vaporview.device_config"`) and
  `device_config_format_version` (`1`)
- `recording_origin`, `recording_directory`, and `session_directory`
- numeric schema, export-rate, and RAW DAT format versions
- `telemetry` with `transport`, `endpoint`, `port`, and `baud`
- `waveform` with endpoint, frame, point, and scalar encoding settings
- `raw_dat` with the shared RAW directory, format document, and write mode
- `sensors` with `epsilon`, `ptb`, `hmp`, `lidar`,
  `laser_temperature_controller`, and `system_temperature_controller`; each
  entry always contains `configured`, `enabled`, `port`, `baud`, `rate_hz`,
  and `slave_address`

Connections or device settings that do not apply to a recorder remain present
as JSON `null`. For example, Sky recordings preserve all five sensor entries
even when their per-device serial settings are unavailable. Common values such
as the session directories, origin, export rates, capture connection, and
format versions are filled by `SessionPackageInitializer`, so an endpoint
cannot omit or change those schema fields while creating a package.
