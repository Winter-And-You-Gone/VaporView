# Sky 设备覆盖矩阵

本文档记录当前正式 Ground/Local 设备在 Sky 端的产品链路覆盖。范围只包含 Device Config 页面正式暴露的设备/数据源；UM982、HiPNUC 等未进入正式设备配置流程的实现不纳入本矩阵。

## 正式设备集合

| Device | Local | Sky Config | Sky Real | Sky Telemetry | Ground Remote | Runtime Control | Recorder | Simulation |
|---|---|---|---|---|---|---|---|---|
| EPSILON | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| PTB210 pressure source | PASS | PASS | PASS | PASS | PASS | N/A | PASS raw + summary | PASS |
| BMP390 pressure source | PASS | PASS | PASS via pressure slot | PASS physical pressure | PASS | N/A | PASS pressure raw + summary | PASS |
| HMP3 humidity source | PASS | PASS | PASS | PASS | PASS | N/A | PASS raw + summary | PASS |
| SHT45 humidity source | PASS | PASS | PASS via humidity slot | PASS physical temperature/humidity | PASS | N/A | PASS humidity raw + summary | PASS |
| TFA1005-L | PASS | PASS | PASS | PASS | PASS | PASS | PASS raw + summary | PASS |
| RD105 temperature controller | PASS | PASS | PASS | PASS dedicated status | PASS | PASS current RD105 command set | PASS structured CSV | PASS data + commands |
| AI-8288 / AI-8 | PASS | PASS | PASS | PASS dedicated 8-channel status | PASS | N/A for current remote runtime commands | PASS structured CSV | PASS 8-channel data |
| Wave TCP | PASS | PASS | PASS | PASS waveform frames/features | PASS | PASS stream/range commands | PASS raw + features | PASS |

## SkyConfig

- Pressure and humidity keep the existing logical sections: `ptb` means the pressure slot and `hmp` means the temperature/humidity slot.
- `ptb.source` accepts `ptb210` or `bmp390`; missing legacy values default to `ptb210`.
- `hmp.source` accepts `hmp3` or `sht45`; missing legacy values default to `hmp3`.
- `ai8_temperature_controller` is a first-class SkyConfig section with `enabled`, `port`, `baud`, `frequency_hz`, and `slave_address`; missing legacy section defaults disabled.
- Source changes participate in `SkyConfigDiff`, so PTB210 to BMP390 or HMP3 to SHT45 forces the affected collector to disconnect, rebuild with the selected protocol, and reconnect.

## Telemetry And Recording

- `TelemetryBasic` remains for neutral physical quantities: pressure, temperature, humidity, lidar, navigation, and health/status fields.
- RD105 continues to use dedicated `TemperatureControllerStatus` telemetry and `sensors/temperature_controller.csv`.
- AI-8288 uses dedicated `Ai8TemperatureControllerStatus` telemetry and `sensors/ai8_temperature_controller.csv`, preserving eight measured channels, control states, main status, and alarm registers.
- RD105 and AI-8288 raw Modbus frame recording is intentionally not part of the current product recorder; structured controller state is the durable session artifact.

## Simulation Contract

In `--sky-simulate-data`, enabled formal Sky devices must not open real serial ports. A connected simulated device must have data semantics consistent with `Connected`:

- EPSILON, pressure, humidity, lidar, and Wave TCP continue to generate deterministic simulated physical data.
- BMP390/SHT45 use the same simulated physical quantities as PTB210/HMP3 while preserving their selected `source` through Get/Set/Save/Load.
- RD105 simulation maintains target, measured, output, PID, mode, address, baud, overtemperature, slope, startup delay, and sensor-config state for the current remote command set.
- AI-8288 simulation produces valid eight-channel live data and status fields for telemetry, Ground decode, TUI snapshot, and session CSV recording.

## Local IPC / TUI

- `SkyLocalIpcServer` forwards TelemetryStatus, SkyConfig, RD105 status, AI-8288 status, waveform, log, and basic telemetry frames to TUI clients.
- `SkyLocalIpcClient` keeps dashboard snapshots for RD105 and AI-8288 data in addition to navigation, environment, lidar, waveform, and device status.
- SkyTui `/devices` and `/device overview` display all formal Sky devices through `allStatuses`; command aliases include `rd105` and `ai8`.

## Validation Boundary

Software acceptance is based on automated tests and localhost Ground-to-Sky simulation. Physical hardware validation remains separate because it depends on actual serial adapters, RS485 wiring, station addresses, baud/parity/stop-bit behavior, firmware behavior, and field disconnect recovery.
