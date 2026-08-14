# Sky 设备覆盖矩阵

本文档记录当前正式 Ground/Local 设备在 Sky 端的产品链路覆盖。范围只包含 Device Config 页面正式暴露的设备/数据源；UM982、HiPNUC 等未进入正式设备配置流程的实现明确为 OUT OF SCOPE。

## Local / Remote 能力矩阵

`PASS` 表示当前产品链路已实现并有测试或既有生产路径，`N/A` 表示当前设备没有该类正常用户能力，不能当作缺陷；Remote 列描述同一设备页面通过 Sky backend 的结果。

| Device | Capability | Local | Remote | Simulation / E2E | Decision / Notes |
|---|---|---|---|---|---|
| EPSILON | 连接、配置、实时状态、包频率/输出控制、原始记录 | PASS | PASS | PASS | 保留现有 EPSILON 业务命令与 FDILink collector。 |
| PTB210 | 串口、波特率、采样频率、压力实时数据、记录 | PASS | PASS | PASS | 无额外设备内部参数页，参数读写 N/A。 |
| BMP390 | 串口、波特率、采样频率、压力实时数据、记录 | PASS | PASS | PASS | 复用压力槽，`source=bmp390`；设备内部参数读写 N/A。 |
| HMP3 | 串口、波特率、采样频率、温湿度实时数据、记录 | PASS | PASS | PASS | 无额外设备内部参数页，参数读写 N/A。 |
| SHT45 | 串口、波特率、采样频率、温湿度实时数据、记录 | PASS | PASS | PASS | 复用温湿度槽，`source=sht45`；设备内部参数读写 N/A。 |
| TFA1005-L | 连接配置、实时距离、状态、记录 | PASS | PASS | PASS | 当前 Local 没有真实用户可修改的内部参数，Remote 参数读写 N/A。 |
| RD105 | 参数页、目标值、输出、模式、PID、报警、传感器、恢复默认 | PASS | PASS | PASS | Remote 复用已有 RD105 业务命令；不新建 Remote 页面。 |
| AI-8288 / AI-8 | 四页参数读取/写入：通道、输入组、输出组、全局 | PASS | PASS | PASS | 同一个 `Ai8TemperatureControllerPanel`，backend 按连接位置切换；写入后 Sky 端回读确认。 |
| AI-8288 / AI-8 | 当前 UI 的恢复出厂入口 | N/A | N/A（真实 collector） | PASS（simulation） | 当前正式 AI-8 页面没有恢复出厂按钮；协议保留受控 factory-reset，仿真用于验收，真实 collector 明确拒绝。 |
| Wave TCP | 连接配置、波形/特征实时数据、流开关、峰值搜索范围、记录 | PASS | PASS | PASS | 网络数据源，不套用串口参数页。 |

## AI-8 试点结果

- Local 继续调用 `LocalDeviceConnectionController` → `Ai8TemperatureControllerCollector` → `Ai8TemperatureControllerProtocol`。
- Remote 进入同一个参数页，调用 `RemoteSkyController` 的受控业务 API；Ground 不接受寄存器地址、功能码或任意 raw Modbus bytes。
- Channel、InputGroup、OutputGroup、Global 四页均以结构化 `PageData` 批量传递。Sky 仿真按 8 个通道、4 个输入组、4 个输出组和全局状态分别保存，写入后立即从同一状态回读。
- Ground 通过 `Ai8DeviceSession` 选择 Local/Remote backend；页面只接收统一的 `PageData`、Loading、完成/失败/超时/断链结果。
- Local 请求在会话专用 worker 中调用现有 collector，Remote 请求在 UI 线程异步发送；两条路径均在完成、失败、超时或断链后恢复按钮状态。请求同时绑定 session request id、command sequence 和 link generation；迟到响应不会写入新链路页面。

## 旧 Sky 安全降级

Device Operation 使用 capability learning：新链路首次状态为 Unknown，收到有效响应后变为 Supported；旧 Sky 对新增 command 返回 `UnknownCommand` 后变为 Unsupported，AI-8 参数按钮保持禁用并显示版本不支持，不会反复发送未知命令。重新连接会清除该状态并重新探测。其它既有 telemetry 和命令 numeric ID 不变。

## 本轮架构

```text
Ai8TemperatureControllerPanel
            |
      AI-8 business API
       /             \
LocalDeviceConnectionController   RemoteSkyController
       |             |
AI-8 collector       GroundTelemetryService
       |             |
       +------ SkyCore TCP ------+
                     |
                 SkyRuntime
                     |
              SkyDeviceManager
                     |
       simulation state / AI-8 collector
```

页面只处理 `PageData`、状态和错误文本；传输位置由 backend 负责。当前仓库中其它设备没有额外的真实内部参数能力，因此保持明确的 N/A，不为矩阵虚构通用参数页。

## Remote Device Operation 协议

- 新增 `MsgType::DeviceOperationResponse = 0x09`。
- 新增 `CommandId::DeviceOperation = 60`；已有 `MsgType`、`CommandId`、`SkyDeviceId` 数值未改动。
- 请求：`request_id`、`device_id`、`operation`、受控结构化 payload。
- 响应：`request_id`、`device_id`、`operation`、`CommandErrorCode`、错误文本、结构化 payload。
- AI-8 operation：`ReadParameters=1`、`WriteParameters=2`、`FactoryReset=3`。
- payload 使用 JSON 字段白名单表达完整 `PageData`，Sky 端继续做页面、选择、类型、范围、设备连接和 collector 状态校验。

旧 Ground 不会发送新 command；新 Ground 连接旧 Sky 时按上面的 Unsupported 状态降级，已有遥测继续工作。

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
- AI-8 Device Operation simulation supports all four page reads, representative writes, read-back, invalid range rejection, selection isolation, and factory reset to deterministic defaults.

## Local IPC / TUI

- `SkyLocalIpcServer` forwards TelemetryStatus, SkyConfig, RD105 status, AI-8288 status, waveform, log, and basic telemetry frames to TUI clients.
- `SkyLocalIpcClient` keeps dashboard snapshots for RD105 and AI-8288 data in addition to navigation, environment, lidar, waveform, and device status.
- SkyTui `/devices` and `/device overview` display all formal Sky devices through `allStatuses`; command aliases include `rd105` and `ai8`.

## Validation Boundary

Software acceptance is based on automated tests and localhost Ground-to-Sky simulation. The focused E2E starts a real `VaporViewSkyCore --sky-simulate-data` process, drives its TCP business-operation endpoint through `RemoteSkyController`, and then starts a real `VaporView.exe --source remote` Ground UI process against the same SkyCore instance to read, write, and read back the AI-8 page. Physical hardware validation remains separate because it depends on actual serial adapters, RS485 wiring, station addresses, baud/parity/stop-bit behavior, firmware behavior, and field disconnect recovery. Real AI-8 factory reset remains intentionally unsupported until the collector has a documented safe device operation.

Focused validation targets are `ai8_device_session_test` (Local async/generation/error states), `sky_device_manager_simulation_test` (simulation state and validation), `telemetry_codec_test` (wire contract), and `sky_ground_simulation_e2e_test` (SkyCore TCP plus real Ground UI process). `ai8_temperature_controller_panel_test` and `ui_test_mode_window_test` cover the shared page presentation and UI test surface.
