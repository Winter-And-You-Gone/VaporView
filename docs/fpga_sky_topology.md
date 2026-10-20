# 天空端 FPGA 与数传部署

最终接线以用户确认的天空端工控机为中心：

```mermaid
flowchart LR
  Devices[其他全部设备] <--> FPGA[FPGA]
  FPGA <-- USB / VLP1 --> Core[天空工控机 SkyCore]
  GUI[同机 VaporView GUI] <-- 本机 TCP IPC 127.0.0.1:39001 --> Core
  Core <--> Radio[直连工控机的天空数传]
  Radio <-- 标准化数据上行 / 控制下行 --> Ground[地面数传和 VaporView GUI]
  Core --> Session[天空完整原始 Session]
```

SkyCore 唯一打开 USB 并持续接收 VLP1。同机 GUI 复用 `RemoteSkyController`，连接本机 IPC；地面 GUI 连接数传串口或 TCP 遥测。两个客户端可同时观察和控制同一 SkyCore，均不另开 USB 或设备串口。`fpga.enabled` 默认 false；显式启用后连接/探测不等于打开物理输出。

无实体板卡时，可使用 [FPGA 协议模拟器](fpga_simulator.md)：Local 在 FPGA 页选择“模拟器”后端，由 Ground 直接生成并解析 VLP1；Remote 运行 `VaporViewFpgaSimulator.exe`，复用 SkyCore/Ground 链路。两种模式独立保存模拟记录，不注册系统 USB 设备，也不代表实板验收。

## 数据源与传感器路径是两个独立维度

Ground 的 Local/Remote 只表示“当前由谁持有上位机链路”：Local 是
VaporView 本机，Remote 是 SkyCore。它不表示传感器是否经过 FPGA。设备配置页另有
“传感器路径”选择：

| 数据源主机 | 传感器直连 | FPGA 中转 |
| --- | --- | --- |
| VaporView Local | 传感器串口 → VaporView | 传感器 → FPGA → VaporView 的 FPGA USB/VLP1 |
| SkyCore Remote | 传感器串口 → SkyCore → 遥测 → VaporView | 传感器 → FPGA → SkyCore → 遥测/IPC → VaporView |

本地配置保存为 `MainWindow/sensor/transport_path`，天空端 `SkyConfig` 使用
`sensor_transport_path`，取值为 `direct_devices` 或 `fpga_relay`。默认值是
`direct_devices`，以兼容已有设备现场配置。选择 `fpga_relay` 后，采集数据改由
FPGA 的 VLP1 通道上送，Ground/SkyCore 不再为这些设备启动第二套直连串口采集。
设备配置页的六行串口在 FPGA 路径下固定显示 `FPGA`，不可选择；直连串口值仍保存在
原配置中，切回直连后恢复。波特率、频率和数据源仍保留设备参数编辑。RD105 同样
显示 `FPGA`，但当前 VLP1 没有对应通道，实际连接不可用，不回退到直连串口。
波形行随路径切换，首页仍名为“波形监控”：FPGA 下可选择双通道 ADC/DLIA，固定
显示原始信号、归一化二次谐波和归一化二次谐波峰值趋势，不显示一次谐波。
format0/2 按同周期、同采样点计算 `H2/H1`；仅 IQ 的 format1 计算
`hypot(I2, Q2)/hypot(I1, Q1)`，结果为无量纲比值，不是气体浓度。
H1 非正或输入无效时保留采样位置并留断点；该点不参与峰值，整周期无有效参考时峰值留断点。
峰值在绘图抽稀前从有效归一化值取最大值，缺片或异常周期留断点，切换通道保留各自趋势。
新版天空端预览携带完整周期峰值；旧发送端仅有抽稀样本时显示“预览峰值”。趋势时间为本机
接收时间，不把 FPGA tick 当 UTC。复用 FPGA 页的校准、完整性与历史值标记。采集
启停仍使用 FPGA 页，直连路径恢复 TCP 主机/端口和原有波形显示设置。
FPGA 诊断页仍显示 H1/H2/IQ；新 Session 保存完整 H1/H2，回放默认按帧计算
`H2/H1`，峰值趋势优先读取已记录的完整周期特征，仍可选择 H1/H2 分量。直连 TCP 数据不重复归一化。
其中 PC 的 `COMx` 名称不能透传到 FPGA，FPGA 端需要使用板上固定通道/协议支持的
地址、波特率和轮询寄存器。VLP1 尚未提供的字段必须返回明确的 `Unsupported`，不能
静默改走直连串口。

界面测试模式会临时把上述路径切换为 `fpga_relay`，并在 FPGA 页面显示“模拟 FPGA”的软件链路；它用于验证布局、状态和路由语义，不创建真实
Windows USB 设备，也不代表已经完成 GP01 实板或 VLP1 原始字节实测。

## 配置与启动

将以下 JSON 保存为工控机上的 `sky-fpga.json`。未提供的 FPGA 配置字段按 `FpgaControlConfig` 默认值处理；旧串口/TCP 采集项显式关闭，避免该部署开启第二套设备链路。空 `locator` 交由后端枚举，必须确认唯一目标设备；多设备时可在 GUI 的 FPGA 页选择目标后连接，或在启动配置中填写所选设备的路径。

```json
{
  "sensor_transport_path": "fpga_relay",
  "fpga": {
    "enabled": true,
    "locator": "",
    "configuration": {
      "backend": "auto",
      "pressureSource": 64,
      "recordSensors": true,
      "recordDlia": true,
      "recordRaw": false
    }
  },
  "epsilon": {"enabled": false, "rtcm": {"enabled": false}},
  "ptb": {"enabled": false},
  "hmp": {"enabled": false},
  "lidar": {"enabled": false},
  "temperature_controller": {"enabled": false},
  "ai8_temperature_controller": {"enabled": false},
  "wave_tcp": {"enabled": false},
  "telemetry": {
    "basic_rate_hz": 10,
    "feature_rate_hz": 10,
    "waveform_rate_hz": 1,
    "heartbeat_rate_hz": 1,
    "status_rate_hz": 1
  }
}
```

在 `build/Release` 可执行文件所在目录启动；下面 `COM12` 是示例天空数传端口，替换为实际端口和匹配波特率：

```powershell
.\VaporViewSkyCore.exe --sky-config .\sky-fpga.json --telemetry-transport serial --telemetry-port COM12 --telemetry-baud 921600 --ipc-host 127.0.0.1 --ipc-port 39001
```

若数传提供 TCP 链路，SkyCore 作为监听端，使用对应工控机网卡地址和端口；下面以默认遥测端口演示：

```powershell
.\VaporViewSkyCore.exe --sky-config .\sky-fpga.json --telemetry-transport tcp --telemetry-host 0.0.0.0 --telemetry-tcp-port 39100 --ipc-host 127.0.0.1 --ipc-port 39001
```

上述串口和 TCP 是两种遥测启动方式，选其一；本机 IPC 与所选遥测方式独立并存。不要加 `--no-ipc`，也不要运行第二个 SkyCore/VaporViewSky 实例占用同一 USB 或数传端口。`VaporViewSky` 是另一个包含 Core 的入口，不能与已启动的独立 Core 同时持有设备。

启动同机 `VaporView.exe`，首页选择 Remote，链路选择 TCP，主机 `127.0.0.1`、端口 `39001`。地面 GUI 同样选择 Remote，但连接地面数传实际串口，或 SkyCore 遥测 TCP `39100` 可达地址。`39001` 是同机 IPC，`39100` 是 TCP 遥测，两者用途不同。观察 FPGA 页的 USB connected、ready、模块版本及硬件实际读回；无设备、驱动缺失或探测失败必须显示失败，不能按配置默认值显示就绪。

## USB 自动发现与选择

打开 FPGA 页时自动枚举 GP01 FX3（`04B4:00F1`），下拉列表显示设备名称、实例标识、USB 位置和驱动；只有一台时自动选中，多台时须手动选择。插拔设备后点击“刷新设备”，刷新按设备路径保留已有选择。无需手工查找或输入 Windows USB 路径；连接按钮使用所选项内部保存的路径。

Local 枚举 VaporView 所在电脑；Remote 通过 `FpgaControl` 的只读 `discover` 动作枚举 SkyCore 所在电脑，需先连接 IPC/数传并启用 `fpga.enabled`。切换主机或关闭远程链路时清空原列表，避免误用另一台主机的路径。远程返回的 `usb_discovery` 含 `devices` 和 `error`；每项设备含 `locator`、`label`、`driver`、`backend`、`available`。列表只随请求返回，不加入周期遥测。远程 GUI 与 SkyCore 都需要更新到支持该动作的版本。

枚举只读取 Windows SetupAPI/PnP 信息，不打开 USB 端点、不发送 VLP1 命令；发现设备不等于固件就绪。连接仍核验 GP01 B1 标记、B0、Bulk IN `0x86` / OUT `0x02` 和协议版本。`auto` 根据所选设备绑定的驱动选择后端，不在多设备之间悄悄择一。

绑定 `CYUSB3` / `CyUSB` 的设备使用 Cypress 后端，构建需提供 CyAPI 头文件及匹配架构、运行库的静态库（`VAPORVIEW_CYAPI_ROOT`，或显式设置 `VAPORVIEW_CYAPI_INCLUDE_DIR` 与 `VAPORVIEW_CYAPI_LIBRARY`）。绑定 WinUSB 的设备使用 WinUSB 后端，枚举优先使用驱动注册的接口 GUID。缺少相应后端或驱动时列表仍展示设备并标为不可用，不会假装连接成功。

本机使用顺序：选择“本地” → FPGA 页选择枚举出的设备 → 保持 `Auto` → 点击“连接” → 等待协议与模块版本核验完成。核验通过后可“刷新读回”；需要修改参数时再“提交参数并核验”，确认后按需启动采集。扫描和 DAC 为独立输出控制，不随连接自动启动。Cypress 空闲读取超时会保留挂起的 IN 请求并继续接收，不应导致断线；真正的读取失败保留 Win32、NT 和 USBD 错误信息。

## 控制与记录边界

传感器配置的目标是“设备配置意图 → 传输适配器”：直连路径交给现有串口采集器，
中转路径交给 VLP1 `WRITE_REG`/`SENSOR_ACTION`，并按下位机要求执行提交、状态等待
和读回确认。当前界面已经保持两条路径共用设备配置控件；尚未接入实板的字段必须
在日志和命令结果中标明待映射或不支持，不能把“控件可编辑”当成已经完成硬件下发。

独立 FPGA 页发送 `DeviceOperation::FpgaControl`，SkyCore 对动作执行 allowlist 校验并串行处理，最终结果来自异步事务及读回；请求受理或普通寄存器 ACK 不等于完成。当前 allowlist 包含只读设备枚举、连接/断开、刷新、配置、采集、WMS、DAC、传感器、限时 RAW 和 AI8 设温，详细确认条件仍见 [协议适配](fpga_vlp1_integration.md)。

VLP1 尚未提供的 RTCM 下行、EPSILON 高级参数操作应返回 `Unsupported`，不能绕到设备串口或构造未知 VLP1 命令。超时不盲目重发，先读取最终设备状态；本机 IPC 与数传请求均须仅执行一次。

SkyCore 在天空端保存完整 Session，与直连共用 `raw/waveform.dat`（source=5，FPGA harmonic-pair record type=2）。完整 H1/H2 和每周期归一化峰值/位置/质量分别写入波形文件和 `sensors/waveform_features.csv`；归一化波形在查看时计算。取消 USB/VLP1 包、命令、ADC RAW 和独立 FPGA JSONL 档案写入，配置/连接/分段信息复用配置文件与事件日志。队列有界，写入/溢出/flush 失败标记 incomplete；pause/stop 排空，stop 结束写线程。旧 FPGA 档案保留读取兼容，详细对比见 [Session 接入说明](fpga_session_recording.md)。

数传默认发送标准化设备数据、状态、命令结果和有界波形预览；RAW 默认关闭，不默认搬运全量 USB/RAW。地面 Session 保存既有标准遥测测量，新 FPGA 状态与波形预览用于界面显示，当前不存入地面 FPGA 原始归档。需要完整离线回放时，取得天空 Session 副本。GUI/数传掉线不应误停 SkyCore 采集或记录，重连后重新建立观察状态，不能把历史预览当新鲜测量。

## 串口数传的软件带宽边界

串口固定使用 8N1，无流控。理论有效载荷上限为 `baud / 10` 字节/秒；115200 对应 11520 B/s。这是串口线路理论值，不能作为无线数传吞吐实测结果，实际可用速率可能更低。

FPGA 诊断 Sensor 和 Preview 共用 `baud / 40` B/s 的令牌预算，即理论有效载荷的 25%；桶容量为一秒预算，允许一次有界突发，每个诊断帧连同 21 字节遥测封装不能超过半秒预算。115200 对应共享 2880 B/s、单帧不超过 1440 字节。每个 Sensor 源最多发送一次/秒；预算不足跳过整帧，不积压历史数据。Preview 按实际编码长度选择最多 64/32/16/8 点，连 8 点也不适合预算时跳过整帧；新增抽稀因子与已有 stride 相乘，原始周期点数和实际采样率保持不变。发送队列已有超过 200 ms 理论载荷时，也跳过诊断帧。

物理串口上的 FPGA Status 合并为最多 1 Hz 的最新值。控制 ACK、最终 DeviceOperationResponse 和标准 Basic/AI8 不参与诊断采样或降点；这并不保证任意配置的标准遥测速率都适合无线链路。串口待发送队列硬上限为 `max(16 KiB, baud / 10 × 2)`，超过上限明确报错并关闭数传链路，控制投递须视为未确认，不能静默丢弃 ACK 或盲目重发写命令。115200 的队列上限为 23040 字节。

这些限制只作用于物理串口输出；本机 IPC 仍接收原有最多 64 点预览及原频率，状态即时/周期通知保持原行为，USB 接收和天空完整 Session 归档继续独立运行。窄带地面预览可能降点、缺帧或暂时不显示，不能据此判断天空未采集，也不能用地面预览代替完整天空归档。TCP 遥测不使用此串口诊断预算。

## 验证状态

本文配置键、启动选项和架构对应当前源码接口。天空拓扑软件构建/测试历史结果见 [协议适配验证记录](fpga_vlp1_integration.md#12-天空端拓扑最终软件验证记录2026-10-10)；历史 `7b68482` 仅验证 Ground 本机适配。上述历史记录未进行 GP01/Cypress 实测。

2026-10-11 的 USB 枚举验证使用 Infineon 官方 CyAPI 1.3.2.0 源码构建 x64 库，并在一台绑定 `CYUSB3` 的 `04B4:00F1` 实板上验证自动枚举、驱动接口路径选择、`auto` 选中 Cypress、B1/B0 身份读取及关闭释放；主窗口浅色/深色设备下拉框已截图检查。此项验证没有发送 VLP1 控制命令，也不覆盖采集、DAC 或吞吐。单 USB 所有者、IPC/数传并存、断线连续采集、最终读回、吞吐和归档完整性仍需按 [实机清单](fpga_hardware_validation.md) 验收。

随后针对连接后读取失败的实板回归已覆盖 PING、GET_CAPABILITIES、READ_REG、完整控制器协议/模块核验、空闲持续读取、周期读回以及两轮断开重连。使用 `fpga_device_session_test --hardware-probe` 和 `fpga_controller_test --hardware-probe` 显式运行，普通 CTest 不访问实板；未启动采集、扫描或 DAC，也未提交配置。
