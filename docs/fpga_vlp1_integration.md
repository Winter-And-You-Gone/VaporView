# FPGA VLP 1.0 USB 对接边界（FX3 GP01）

本文对应 `Vapor_Radar_App_V2/firmware/fx3/README.md`、`GPIF_MAPPING.md` 与
`Docs/protocol/HOST_HANDOFF_20261001.md` 的 GP01 版本。固件资料当前位于同级
`Vapor_Radar_App_V2` 仓库；若版本不一致，应以板上 B0/B1 诊断和 BIT/IMG 哈希为准。
本对接实现以该仓库当前的 `VLP1` 线协议为唯一事实接口；工作区中独立的
`VFLP` 设计稿不参与当前帧解析，也不能直接拿来替换 VLP1 头格式。

## 已冻结的 USB 参数

- 应用 VID:PID：`04B4:00F1`；配置/接口/alternate 为 `1/0/0`。
- Bulk OUT `0x02`（FX3 UIB producer socket 2 → PIB consumer socket 3，FIFO 地址 3）。
- Bulk IN `0x86`（PIB producer socket 0，FIFO 地址 0 → UIB consumer socket 6）。
- DQ 为 32 位小端，所有 Bulk 字节数必须是 4 的倍数；VLP 头中的 `payload_len` 才是实际长度，末尾零字节为传输填充。USB 返回边界不能作为 VLP 帧边界。
- 每方向 4 个 16 KiB AUTO_SIGNAL DMA buffer；SuperSpeed burst 16，HS/FS 每 burst 一个包。
- EP0 vendor IN `0xB1` 与 `0xB0` 均读 32 字节，`wValue=wIndex=0`。B1 前 8 字节应为 `47 50 30 31 AC 10 00 00`（`GP01`、`0x10AC`）；B0/B1 都是只读诊断，不清错、不复位 GPIF。仅匹配 VID:PID 不能证明是 GP01。

`B0/B1` 的 32 字节布局为：偏移 0 mapping/marker，4 active/BUS_CONFIG，8 PIB 或 waveform 状态，12 DMA 或 GPIF 状态，16 最近错误/软件阶段，20 OUT/最近 PIB 错误阶段，24 IN/SMStart 前错误数，28 watermark/SMStart 后错误数。阶段值为 0 初始、1 Load、2 Load 完成、3 SMStart、4 启动成功、5 停止中/已停止。计数增加不能单独证明 FPGA 已解析命令，必须结合完整 PING 应答和 VLP CRC。

## GPIF 与端点边界

GPIF 使用 50 MHz 外部 PCLK、`GPIF_BUS_CONFIG=0x10AC`、线程 3/0。GP01 的 `A1/A0` 是 CTL11/CTL12（GPIO28/29），地址必须在一次 FIFO 事务内保持不变。`PKTEND#` 必须和最后一个数据字、`SLWR#` 在同一写边沿有效；单独拉低而不写数据是零长度包请求，不是普通短包结束。

部分/满/空 flag 有硬件延迟：写 full 约 3 个周期后才能采样，读 empty 约 2 个周期后才能采样；进入 watermark 临界区后使用隔离的读写脉冲和足够空闲周期。不要等待 partial flag 重新置位来判断短 USB 包，也不要在 DMA buffer 切换期间依据陈旧 flag 读写。GPIF 本身没有 byte-enable，最后一帧只允许 VLP padding 表示真实长度。

## Qt 传输抽象

`include/FpgaUsbTransport.h` 与 `src/ground/devices/FpgaUsbTransport.cpp` 定义了 QtCore-only 的边界：

- `open/close/isOpen/read/write/diagnostic` 是唯一 I/O 面；`stateChanged`、`bytesReceived`、`errorOccurred` 用于状态机和日志。
- Windows 构建使用系统 WinUSB/SetupAPI 枚举 `04B4:00F1`，检查 Bulk `0x02/0x86`，读取 B1/B0 并核验完整 GP01 marker；非 Windows 构建仍使用安全失败路径。未检测到硬件、驱动或 GP01 marker 时，`open()` 明确失败，绝不伪造在线硬件或采样值。
- `FpgaUsbReplayTransport` 可注入离线 USB 字节流，保留写入字节并拒绝非 4 对齐 Bulk 写入；它用于 VLP 拆包、CRC、命令队列和重连状态机测试。Replay 也不代表硬件验收。
- 传输层不解析 VLP、不按 USB read 返回拆帧；上层必须实现唯一 IN 接收循环、缓存重组、CRC-32/ISO-HDLC 和 `session/sequence/source/msg` 联合匹配。

`FpgaDeviceSession` 已接入 `vaporview_ground_device_core`，负责唯一 IN 轮询、VLP1 增量解析、单 pending 命令队列、响应超时、能力读取、寄存器命令、传感器 payload 分发和 RAW/DLIA fragment 聚合。VLP1/传感器/波形离线测试已接入 CMake；这些测试不构成实机验收。

RAW/DLIA 聚合按 `source/msg/cycle_id/timestamp` 建组，检查 schema、采样格式、片号、首点、点数、重复和连续性。`PARTIAL_DATA`、`OVERFLOW_SINCE_LAST` 或缺片时仍可发出原始聚合结果，但 `CompletedStream::complete` 保持为 `false`。

当前 WinUSB 读写采用有界同步调用，仍需要在实际板卡上验证拔插、超时、取消、吞吐和线程归属。不要让 Control Center 与 VaporView 同时占用端点。

## 推荐实现顺序

1. 先接入 replay：覆盖半帧/多帧、4 字节 padding、CRC 错误、超时和重连，保存原始字节证据。
2. 增加设备枚举与 B1/B0 读取，严格核对 GP01 marker、`0x10AC`、接口和端点；BootLoader 枚举应报告为错误状态。
3. 选定受支持的 CyUSB 或 WinUSB 后端，单独验证 bulk 超时、取消、拔插和线程归属；禁止 Control Center 与应用同时占用端点。
4. 运行 PING 往返，再按 HOST_HANDOFF 顺序做寄存器读回、WMS/DAC、双 ADC、DLIA、传感器启停；每步保留命令、应答、CRC、drop/overflow 和最终 B0/B1。

## 实机验收清单

- [ ] 固件/BIT/IMG 的版本和 SHA256 已记录；确认不是 BootLoader。
- [ ] 枚举 `04B4:00F1`，配置 `1/0/0`，读取并保存 B0/B1 32 字节；GP01 marker 与 `0x10AC` 匹配。
- [ ] OUT `0x02` / IN `0x86` PING 往返成功；短包与 4 字节 padding 均能正确重组，CRC 无误。
- [ ] 连续采集至少覆盖一轮 154 帧 CRC 校验；记录 DMA/PIB 错误、drop、overflow、重连结果。
- [ ] 按 HOST_HANDOFF 顺序验证寄存器 shadow→commit→读回，区分 ACK、协议完整、连续采样和模拟波形四个结论。
- [ ] 停止时先停 ADC 并排空在途数据，再停 DLIA；重连清理半帧、分片缓存、pending 请求和时间原点。
- [ ] 示波器单独核对 DAC/WMS 模拟波形；计数正常不能代替引脚波形验收。

当前阻塞项：本机没有连接 GP01 实板，无法确认实际固件/驱动组合下的 B0/B1、短包、持续吞吐、拔插和长时间重连行为。WinUSB 后端已经可以编译并严格拒绝非 GP01 设备；硬件在线、长期满速、反复重连和 EEPROM 启动仍待实机后验收。

## 本轮保守保留的边界

- 没有把 FPGA 连接控件硬塞进现有串口 Device Configuration 页面；当前 VaporView 没有既定的 FPGA 设备配置字段和页面位置。`FpgaDeviceSession` 已作为 Ground 设备核心服务提供，接下来应在确认页面归属后再接入持久化配置和连接按钮。
- 没有改变既有 session 文件格式。VLP 原始帧、ADC/DLIA 高速数据和 fragment drop/overflow 需要单独的 session 文件语义，不能复用 PTB/HMP/TFA 的旧 raw 文件名。当前服务通过 `rawFrameReceived` 和 `waveformCompleted` 提供记录入口，避免产生不可逆的格式猜测。
- EPSILON FDILink 已完成外层长度/CRC/ID/序列/原始数据保留，但复杂 INS/GPS 字段仍由后续适配器按下位机手册逐字段映射；BMP390 已保留原始 ADC/校准 payload，补偿算法需要确认 VaporView 的单位和校准责任后再启用。
