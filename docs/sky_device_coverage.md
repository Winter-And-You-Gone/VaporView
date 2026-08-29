# Sky 设备覆盖矩阵

本文档记录当前正式 Ground/Local 设备在 Sky 端的产品链路覆盖。范围只包含 Device Config 页面正式暴露的设备/数据源；UM982、HiPNUC 等未进入正式设备配置流程的实现明确为 OUT OF SCOPE。

## Local / Remote 能力矩阵

`PASS` 表示当前产品链路已实现并有测试或既有生产路径，`N/A` 表示当前设备没有该类正常用户能力，不能当作缺陷。`Detailed Config` 只表示同一正式设备页面里的设备内部/高级业务配置，不把单纯连接配置混入 PASS。

| Device | Connection Config | Telemetry | Detailed Config | Runtime Control | Simulation | Software E2E | Decision / Notes |
|---|---|---|---|---|---|---|---|
| EPSILON | PASS | PASS | PASS | PASS | PASS | PASS | 同一个 `EpsilonConfigPanel` / RTK UI，经 `EpsilonDeviceSession` 路由 Local 或 Remote backend。 |
| PTB210 | PASS | PASS | N/A | N/A | PASS | PASS | 压力槽串口/频率配置，设备内部参数页 N/A。 |
| BMP390 | PASS | PASS | N/A | N/A | PASS | PASS | 复用压力槽，`source=bmp390`；设备内部参数读写 N/A。 |
| HMP3 | PASS | PASS | N/A | N/A | PASS | PASS | 温湿度槽串口/频率配置，设备内部参数页 N/A。 |
| SHT45 | PASS | PASS | N/A | N/A | PASS | PASS | 复用温湿度槽，`source=sht45`；设备内部参数读写 N/A。 |
| TFA1500-L | PASS | PASS | N/A | N/A | PASS | PASS | 当前 Local 没有真实用户可修改的内部参数。 |
| RD105 | PASS | PASS | PASS | PASS | PASS | PASS | 同一个 `TemperatureControllerPanel`，经 `Rd105DeviceSession` 路由 Local 或 Remote backend。 |
| AI-8288 / AI-8 | PASS | PASS | PASS | PASS | PASS | PASS | 同一个 `Ai8TemperatureControllerPanel`，写入后 Sky 端回读确认。 |
| AI-8288 / AI-8 factory reset | N/A | N/A | N/A（正式 UI） | N/A（真实 collector） | PASS | PASS | 当前正式 AI-8 页面没有恢复出厂按钮；仿真保留受控 factory-reset 验收。 |
| Wave TCP | PASS | PASS | N/A | PASS | PASS | PASS | 网络数据源，不套用串口参数页。 |

## EPSILON 详细配置结果

- `EpsilonDeviceSession` 统一承接 packet profile、output reconfigure、主天线杆臂、EPSILON COMM2~COMM5 RTCM input 配置，Local backend 调用 `EpsilonConfigurationService`，Remote backend 通过 `DeviceOperation` 到 SkyCore。
- Packet profile 在 Local 与 Remote 使用独立设置域；Remote profile 不覆盖本地 EPSILON profile。
- Remote output reconfigure、lever arm、RTCM input 在 SkyCore 的 `SkyDeviceManager` 执行；仿真模式保存 packet rates、lever arm、RTCM input / forwarding state。
- Remote RTCM correction 不是 request/ACK per chunk；Ground 通过 `MsgType::RtcmCorrectionData` 发送 bounded payload，Sky 端用 bounded queue / counters 接收，仿真模式只更新 receive stats，不打开真实串口。

| Capability | Local | Remote | Simulation / E2E |
|---|---|---|---|
| Packet Profile | PASS | PASS | PASS |
| Output Reconfigure | PASS | PASS | PASS |
| Main Antenna Lever Arm | PASS | PASS | PASS |
| RTCM COMM Config | PASS | PASS | PASS |
| RTCM Forward Endpoint | PASS | PASS | PASS |
| NTRIP / GGA UI reuse | PASS | PASS | PASS |
| RTCM Correction Forwarding | PASS | PASS | PASS |
| Status / Error / Timeout | PASS | PASS | PASS |

## AI-8 试点结果

- Local 继续调用 `LocalDeviceConnectionController` → `Ai8TemperatureControllerCollector` → `Ai8TemperatureControllerProtocol`。
- Remote 进入同一个参数页，调用 `RemoteSkyController` 的受控业务 API；Ground 不接受寄存器地址、功能码或任意 raw Modbus bytes。
- Channel、InputGroup、OutputGroup、Global 四页均以结构化 `PageData` 批量传递。Sky 仿真按 8 个通道、4 个输入组、4 个输出组和全局状态分别保存，写入后立即从同一状态回读。
- Ground 通过 `Ai8DeviceSession` 选择 Local/Remote backend；页面只接收统一的 `PageData`、Loading、完成/失败/超时/断链结果。
- Local 请求在会话专用 worker 中调用现有 collector，Remote 请求在 UI 线程异步发送；两条路径均在完成、失败、超时或断链后恢复按钮状态。请求同时绑定 session request id、command sequence 和 link generation；迟到响应不会写入新链路页面。

## 旧 Sky 安全降级

Device Operation 使用 capability learning：新链路首次状态为 Unknown，收到有效响应后变为 Supported；旧 Sky 对新增 command 返回 `UnknownCommand` 后变为 Unsupported，AI-8 / EPSILON 详细操作保持禁用并显示版本不支持，不会反复发送未知命令。重新连接会清除该状态并重新探测。其它既有 telemetry 和命令 numeric ID 不变。

## 本轮架构

```text
Existing Device UI
       |
Device Session
   /        \
Local       RemoteSkyController
collector   |
            GroundTelemetryService
            |
         SkyCore TCP
            |
         SkyRuntime
            |
      SkyDeviceManager
            |
 simulation state / collector
```

`Ai8DeviceSession`、`EpsilonDeviceSession`、`Rd105DeviceSession` 保持类型安全的业务 API。页面只处理结构化数据、状态和错误文本；传输位置由 backend 负责。当前仓库中其它设备没有额外的真实内部参数能力，因此保持明确的 N/A，不为矩阵虚构通用参数页。

## Remote Device Operation 协议

- 新增 `MsgType::DeviceOperationResponse = 0x09`。
- 新增 `MsgType::RtcmCorrectionData = 0x0A`，用于 Ground → Sky RTCM correction 数据帧。
- 新增 `CommandId::DeviceOperation = 60`；已有 `MsgType`、`CommandId`、`SkyDeviceId` 数值未改动。
- 请求：`request_id`、`device_id`、`operation`、受控结构化 payload。
- 响应：`request_id`、`device_id`、`operation`、`CommandErrorCode`、错误文本、结构化 payload。
- AI-8 operation：`ReadParameters=1`、`WriteParameters=2`、`FactoryReset=3`。
- EPSILON operation：`ConfigureEpsilonPacketRates=10`、`ConfigureEpsilonMainAntennaLeverArm=11`、`ConfigureEpsilonRtcmInput=12`。
- payload 使用 JSON 字段白名单表达完整 `PageData`，Sky 端继续做页面、选择、类型、范围、设备连接和 collector 状态校验。

旧 Ground 不会发送新 command；新 Ground 连接旧 Sky 时按上面的 Unsupported 状态降级，已有遥测继续工作。

## SkyConfig

- `epsilon` stores only `enabled`, `port`, and `baud`; legacy `frequency_hz` is accepted when reading older files but is no longer serialized. EPSILON packet rates are configured through the packet-rate profile workflow instead of a single SkyConfig frequency.
- Pressure and humidity keep the existing logical sections: `ptb` means the pressure slot and `hmp` means the temperature/humidity slot.
- `ptb.source` accepts `ptb210` or `bmp390`; missing legacy values default to `ptb210`.
- `hmp.source` accepts `hmp3` or `sht45`; missing legacy values default to `hmp3`.
- `ai8_temperature_controller` is a first-class SkyConfig section with `enabled`, `port`, `baud`, `frequency_hz`, and `slave_address`; missing legacy section defaults disabled.
- `epsilon.rtcm` / legacy `epsilon_rtcm` stores Sky-side RTCM forwarding state: `enabled`, EPSILON `device_port_index`, Sky `forward_port`, and `baud`; missing legacy values default disabled and do not open a serial port.
- Source changes participate in `SkyConfigDiff`, so PTB210 to BMP390 or HMP3 to SHT45 forces the affected collector to disconnect, rebuild with the selected protocol, and reconnect.

## Telemetry And Recording

- `TelemetryBasic` remains for neutral physical quantities: pressure, temperature, humidity, lidar, navigation, and health/status fields.
- `TelemetryStatus` includes RTCM correction receive/drop counters so Ground can distinguish configured/streaming/error states without logging every RTCM chunk.
- RD105 continues to use dedicated `TemperatureControllerStatus` telemetry and `sensors/laser_temperature_controller.csv`.
- AI-8288 uses dedicated `Ai8TemperatureControllerStatus` telemetry and `sensors/system_temperature_controller.csv`, preserving eight measured channels, control states, main status, and alarm registers.
- RD105 and AI-8288 raw Modbus response frames are recorded to `raw/laser_temperature_controller.dat` and `raw/system_temperature_controller.dat`; structured controller state remains in the semantic CSV artifacts.

## Simulation Contract

In `--sky-simulate-data`, enabled formal Sky devices must not open real serial ports. A connected simulated device must have data semantics consistent with `Connected`:

- EPSILON, pressure, humidity, lidar, and Wave TCP continue to generate deterministic simulated physical data.
- EPSILON detailed-operation simulation supports packet rates, main antenna lever arm, RTCM input config, RTCM forwarding config, and RTCM correction receive counters.
- BMP390/SHT45 use the same simulated physical quantities as PTB210/HMP3 while preserving their selected `source` through Get/Set/Save/Load.
- RD105 simulation maintains target, measured, output, PID, mode, address, baud, overtemperature, slope, startup delay, and sensor-config state for the current remote command set.
- AI-8288 simulation produces valid eight-channel live data and status fields for telemetry, Ground decode, TUI snapshot, and session CSV recording.
- AI-8 Device Operation simulation supports all four page reads, representative writes, read-back, invalid range rejection, selection isolation, and factory reset to deterministic defaults.

## Local IPC / TUI

- `SkyLocalIpcServer` forwards TelemetryStatus, SkyConfig, RD105 status, AI-8288 status, waveform, log, and basic telemetry frames to TUI clients.
- `SkyLocalIpcClient` keeps dashboard snapshots for RD105 and AI-8288 data in addition to navigation, environment, lidar, waveform, and device status.
- SkyTui `/devices` and `/device overview` display all formal Sky devices through `allStatuses`; command aliases include `rd105` and `ai8`.

## Validation Boundary

Software acceptance is based on automated tests and localhost Ground-to-Sky simulation. The focused E2E starts a real `VaporViewSkyCore --sky-simulate-data` process, drives EPSILON packet profile / lever arm / RTCM config / RTCM correction uplink, RD105 commands, and AI-8 DeviceOperation through `RemoteSkyController`, then starts a real `VaporView.exe --source remote` Ground UI process against the same SkyCore instance via `--remote-device-e2e-output` to repeat remote EPSILON detailed operations, RTCM uplink, representative RD105 target/PID commands, and AI-8 read verification. Physical hardware validation remains separate because it depends on actual serial adapters, RS485 wiring, station addresses, baud/parity/stop-bit behavior, firmware behavior, and field disconnect recovery. Real AI-8 factory reset remains intentionally unsupported until the collector has a documented safe device operation.

Focused validation targets are `ai8_device_session_test`, `epsilon_device_session_test`, `rd105_device_session_test`, `sky_device_manager_simulation_test`, `telemetry_codec_test`, and `sky_ground_simulation_e2e_test`. `ai8_temperature_controller_panel_test`、`temperature_controller_panel_test` 和 `ui_test_mode_window_test` 覆盖共享页面呈现与 UI test surface。
