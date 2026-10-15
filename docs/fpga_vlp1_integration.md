# FPGA VLP1 软件适配与对接计划

直连/FPGA 共用 UI 的路径、参数读回及当前协议缺失见 [UI 能力与修复计划](fpga_ui_capabilities.md)。寄存器中已有配置字段不代表设备配置页已具备对应的硬件提交事务；AI8 完整参数页、EPSILON 下行和 RTCM 另受当前 VLP1 能力限制。

本文说明天空端 FPGA 控制、记录和离线查看边界。用户最终确认：VaporView 所在工控机位于天空端，USB/VLP1 连接 FPGA，其他设备全部连接 FPGA；数传另一路直连工控机，将天空数据发送地面并接收地面控制。SkyCore 是唯一 USB 所有者，同机 GUI 和地面 GUI 均通过 SkyCore 控制设备。协议对象为 XCKU11P + FX3 GP01 正式双 AD4630 系统；页面独立于既有串口 Device Configuration。本文所说“已实现”仅表示代码提供接口与处理路径，不代表接板、SDK 分支或硬件验收通过。第 11 节是历史 Ground 本机适配结果，不能作为本轮天空拓扑验证；本轮最终结果由第 12 节另行记录。配置和启动步骤见 [天空拓扑部署](fpga_sky_topology.md)。

首页传感器频率在 FPGA 模式下按各 source 的成功解析测量帧统计：使用有效的 100 MHz FPGA tick，在至少一秒的设备时间窗口内计算帧数增量/时间差；EPSILON 总频率包含各类已解码数据包，BMP 校准帧不计为测量。统计发生在 50 ms 界面合并刷新之前，USB 批量交付或快速回放不会改变设备数据频率。首个样本、无有效时间戳、断连、禁用或停更过期时频率不可用；新连接及时间回退重新建立窗口。该值是测量数据帧频率，不是串口波特率、界面刷新频率或传感器内部转换速率。

本地 USB 直接显示此统计；SkyCore 将各 source 的结果作为可选 `sensor_rates_hz` 对象放入已有 `FpgaStatus`（键为 source 的十进制字符串，值为 Hz）。远程首页固定读取 PTB210 环境气压频率；BMP390/SHT45 为独立内部设备，读取各自帧频率，不再用统一 TelemetryBasic 发送频率代替。旧 SkyCore 未提供该对象时显示频率不可用，需要更新 SkyCore；FPGA 固件无需新增字段。

中转模式首页波形监控标题栏提供原始信号与二次谐波两个采集入口，均复用 FPGA 页面的本地 USB、远程 SkyCore 和界面模拟控制。原始信号启动时，如 ADC/DLIA 尚未全部启用，先启动公共采集并等待最终读回确认，再开启 RAW 上传；采集已运行时只打开 RAW，不重启现有采集。停止原始信号只关闭 RAW 上传。二次谐波按钮对应 FPGA 页面的开始/停止采集，停止公共采集也会停止原始信号；两者不是独立的硬件采集引擎。按钮依据寄存器读回更新，忙状态、断连或远程状态过期时禁用；失败、拒绝及超时不会自动继续开启 RAW 或重试。

## 1. 事实来源与适用版本

以下文件位于同级 `Vapor_Radar_App_V2` 仓库：

- `Docs/protocol/HOST_HANDOFF_20261001.md`：当前联调入口、双 ADC、六传感器和操作顺序。
- `Docs/protocol/FPGA_HOST_PROTOCOL_V1.0.md`：VLP1 字节布局、寄存器、动作和 payload 语义。
- `Docs/protocol/source_reference/WMS_DAC_REGISTER_MAP.md`：WMS/DAC shadow、映射及范围约束。
- `Docs/protocol/source_reference/ADC_DILA_REGISTER_MAP.md`：DLIA 0101/四阶滤波说明；其中 ADC3660/12.5 MS/s ADC1 表明确为历史参考。
- `Docs/protocol/examples/bmp_compensation.py`：21 字节 BMP390 校准和 24 位原码的主机补偿，输出 Pa、℃。

当前 ADC1 按 HOST_HANDOFF 的双 AD4630 修订解释：CON16、24 位、与 ADC0 共享采样时序，默认 1 MS/s。不能套用历史 ADC1=16 位/12.5 MS/s，不能写 `0x4110/0x4130/0x4134`，不能访问未映射 `0x4140`。本文不使用独立 VFLP 设计稿替换 VLP1。历史 BIT 下载、CRC 或示波器报告也不构成本轮 VaporView 实机验收。

连接后预期核验：

| 地址 | 当前期望 | 含义 |
|---|---|---|
| `0x4000 / 0x4100` | `0x00200101 / 0x00210101` | 双 AD4630 模块 |
| `0x4018 / 0x4118` | `0x00011800` | 24 位有符号原码 |
| `0x5000 / 0x5100` | `0x00300101 / 0x00310101` | DLIA 0101 |
| `0x5024 / 0x5124` | `0x00040101` | 1 MHz、四阶 1 kHz 低通 profile |
| `0x6600` | `0x00460200` | AI8 schema 2 |

不匹配时保留诊断并拒绝套用当前配置；不能用软件默认值覆盖硬件读回，或声称旧版本同样兼容。

## 2. 需求、模块与完成边界

| 用户需求 | 软件责任 | 本轮实现与验收边界 |
|---|---|---|
| 独立 FPGA 控制页 | `FpgaControlPage`、`RemoteSkyController` | Remote 模式经本机 IPC 或数传发送 FPGA 控制，GUI 不直接持有 USB；等待最终异步事务及读回，不把受理 ACK 显示为完成 |
| 唯一后台 IN 接收 | `FpgaDeviceSession`、`FpgaUsbTransport` | 增量拆包、CRC、命令串行匹配、掉线清理；吞吐与拔插仍待实板 |
| 保守配置及启停 | `FpgaDeviceController`、`FpgaControlConfig` | 期望配置与硬件读回分离，shadow/commit/读回确认；命令计划与状态边界按下文逐项验证 |
| 默认 PTB210 | 传感器适配和压力来源选择 | 环境气压固定 `0x0040` PTB210；BMP390 仅内部气压，不覆盖环境或 session PTB 字段 |
| 六传感器物理量与质量 | `FpgaSensorDecoder`、`FpgaSensorAdapter` | TLV、FDILink、BMP 补偿、AI8 状态；通信成功不等于测量有效 |
| 传感器和 DLIA 连续记录 | `SkySessionRecorder`、`FpgaSessionArchive` | 完整 USB/命令/帧/快照在天空端后台落盘；地面只保存收到的标准遥测，新增预览用于界面观察 |
| RAW 按需上传 | 控制器和记录配置 | 默认关闭；显式启用后持续上传，无时长设置或自动关闭；手动停止确认必须检查命令及掩码读回 |
| 原码与校准边界 | 前端配置、波形展示 | 未确认独立通道校准时只显示原码/数字幅值；不输出电压或 ppm |
| 离线回放与导出 | Archive、原始解析页、FPGA 页 | 复用 live codec；CSV/JSON/BIN；离线状态不得伪装在线设备 |
| 历史 Session 兼容 | UnifiedRawDat、SessionManifest | 保持旧版本、source ID、旧文件及别名；新增独立可选 FPGA 文件 |

软件验收计划覆盖协议、传感器、组包、归档、控制器事务和页面。已经提供的软件 fixtures、replay transport 或 SDK 缺失失败路径，只能证明相应软件行为；本文件不预填“全部测试通过”。

## 3. USB、驱动及 VLP1

应用 `VID:PID=04B4:00F1`，配置/接口/alternate=`1/0/0`，Bulk OUT=`0x02`，IN=`0x86`。读取 EP0 vendor IN B1/B0，各 32 字节，value/index=0；核验 B1 的 GP01 与 `0x10AC` marker，并保存 B0/B1。BootLoader 或仅相同 VID/PID 均不能当作应用在线。Control Center 和 VaporView 不得同时占用端点。

USB connected 与 ready 分开保存：前者表示传输已打开，后者要求PING、能力及当前模块版本核验通过。USB已打开但探测失败/版本不匹配时允许刷新诊断或断开，不能启动采集、应用当前配置或显示已就绪；不要因ready=false丢掉仍打开的USB连接。离线回放也不是USB connected。

Windows 默认构建提供 WinUSB/SetupAPI 路径，实际设备必须绑定兼容驱动。Cypress 原生路径使用可选 CyAPI 后端与 CyUSB 驱动，不能把 WinUSB 能编译解释为 Cypress 驱动已适用。当前机器无 Cypress SDK、无 GP01 实板：CyAPI 条件分支的源码存在，但其 SDK 编译、链接、枚举、B1/B0、Bulk 吞吐和取消均未验证。未构建 SDK 分支时，显式选择 Cypress 应报告不可用，不假装在线。`auto` 的具体后端选择以构建和运行诊断为准。

VLP1 为小端：40 字节头依次为 magic、major/minor/type/header_words、total_len、sequence、source、msg、flags、tick、cycle、payload_len。magic=`VLP1`，版本 1.0，header_words=10，`total_len=44+payload_len`；CRC-32/ISO-HDLC 覆盖头和 payload，随后补零至 4 字节边界。USB read 可返回半帧或多帧，不能按 read 边界分包。正常最大 payload=8192、基础帧=8236 字节。

CMD/RESP=`0x01/0x02`，DATA=`0x10`，EVENT/STATUS=`0x11/0x12`，PROTOCOL_ERROR=`0x7F`。命令按会话、sequence、source、msg 联合匹配，并保持唯一持续 IN 接收。EVENT/STATUS、协议错误、CRC 错误不能计为测量点。sequence 是整个输出流的诊断字段，不以某条 ADC/DLIA 的相邻 sequence 判定丢片。

主机时间为 epoch 微秒；FPGA tick 为 100 MHz 本地 uint64 计数，1 tick=10 ns，不能直接当 UTC。画图先做 uint64 tick 差，再换算秒。重连/复位重新建立时间原点，不能把前后两个会话的 tick 拼接为连续测量。

## 4. 操作顺序与确认条件

### 4.1 连接、探测和配置

1. 天空工控机的 SkyCore 选择 USB 后端并连接；GUI 通过 Remote 模式请求操作。核对端点及 GP01 B1/B0。先开始后台 IN，再 PING、GET_CAPABILITIES 和合法寄存器读回。
2. 核验协议、时钟、payload 上限及上表模块版本。保存版本、配置、B0/B1、错误/drop 初值。探测前后的“连接成功”和“可应用当前配置”分别表达。
3. 首次不自动启用 HMP `0x0041`、电机或 RAW。六传感器配置保持 PTB210 为默认压力源。
4. 配置采样时序前，停止双 ADC、等待 PHY 空闲并排空旧队列，停止/排空双 DLIA。保持接收循环运行；不要无条件停止两条 WMS/DAC。
5. 按对应模块写 shadow，逐项读回，执行 commit，等待 pending 清零并检查 STATUS/ERROR 与 actual-rate。普通 WRITE ACK 只确认寄存器接受，不能当作外设执行完成。
6. WMS 配置使用对应 WMS COMMIT_CONFIG 协调 DAC，并恢复原 enable；不能用 DAC CONTROL.bit2 替代整个 WMS/DAC 联合提交。CONTROL=`4` 为禁用+提交，`5` 为保留 enable+提交，应按模块语义选择。
7. 共同 ADC 采样率只写 ADC0 `0x4010`，再核对 `0x4014/0x4114`。提交两路 DLIA 后读 `+0x4C/+0x50` 核对实际输出率和抽取因子。

状态读取还覆盖 `0x002C/0x0030` 主上传掩码、`0x0034/0x0050` RAW 掩码、TIME/HMP/电机的控制诊断，以及 STREAM/USB 的 enable、FIFO、drop 和 stall 计数。只读取文档定义的合法区间，避开 `0x0044` 喂狗写入口及未映射空洞，不清除错误或启动未验证模块。启用采集或传感器前，显式核验主上传掩码为全 1、STREAM/USB CONTROL=1，保留管理状态和时间事件；不能写入传感器 START 掩码 `0x7D`。这一步不改变已有 RAW 掩码，也不属于独立 WMS/DAC 操作。观察到 USB 上传通路被禁用时拒绝写操作，保留诊断，要求先恢复下位机通路后重新连接，避免恢复命令自身无法确认。

参数单位：WMS 扫描/正弦及 DLIA 参考为 mHz，100 Hz=100000；幅度/偏置为 Q1.31，DAC gain 为 UQ2.30，相位为 U32 整圈。请求更新率与硬件实际更新率分开显示。DAC SPI 无 ACK/器件 ID 读回能力；write_count 不证明模拟输出正确。

### 4.2 传感器

BMP390 在禁用状态设置 `0x6310=0x77` 并读回，取得当前连接的 21 字节校准后再补偿。正常地址配置不需要每次软复位；确有地址错误重试时先保存诊断，按文档对 BMP 局部恢复，不用全局复位替代。

Cypress 接收端保留跨空闲轮询的异步 IN 请求。对零字节且 NT=`0xC0000120`、USBD=`0xC0010000` 的取消完成，最多连续重新挂接 3 次读取，收到非空数据才重置计数；不重发写命令，不重置设备，部分传输或其他错误仍报告失败。诊断字段 `cancelled_read_retries` 记录本次连接恢复次数，Win32 错误取完成阶段值。寄存器 READ_REG 应答超过 10 秒未到时仅重发一次原查询，再等待 10 秒后报告超时；配置、传感器动作等写命令超时不自动重发。

USB 会话对非空读取连续收包，每批最多 64 次读取，每次完成后检查 4 ms 时间预算；达到任一上限便让出事件循环，并用零间隔定时器继续接收，避免每包额外等待 Windows 定时器。读空后恢复 1 ms 轮询。时间预算不打断底层正在等待的单次读取，不能视为严格的 4 ms 延迟保证；协议校验及 partial/overflow 标志不变。

`fpga_device_session_test --hardware-stream-probe` 使用上述自动定时器做只读实板接收检查：须预先启动双路 DLIA 并断开其他 USB 读取者，预热 3 秒后统计约 20 秒周期，并比较两路丢点计数。无数据、寄存器读取失败、新增丢点、overflow 或分片连续性异常均判失败；partial 单独报告，不将零新增丢点等同于完整周期有效。探针不启停采集、不清计数、不改输出参数。

开始采集会先停止并排空双 ADC/DLIA，然后软复位 ADC0（等待共享 AD4630 PHY 初始化）、ADC1 和双 DLIA，再提交设备保留的 ADC/DLIA 参数并检查状态，最后按 WMS、DLIA、ADC 顺序启用。原因是当前 RTL 在停止 DLIA 时会锁存 `cycle_partial`，直接重新 enable 无法清除；软复位同时清除相关模块的累计诊断计数，开始前的历史诊断需要提前保存。此流程不写入主机默认采样/解调参数，不重置 WMS/DAC，不启停传感器，也不放宽周期质量判断。对照依据为下位机仓库 `0f12988` 的 `host/qt_monitor/src/worker.cpp::startAcquisition` 及 `fpga/rtl/dila/dila_core.v`。

传感器“启用”只开启 FPGA 轮询，不为外部传感器供电。未上电或未接线的 PTB210 不会提供有效气压值；传感器超时与整条 USB 链路异常需分别诊断。

七传感器启动按 HOST_HANDOFF：SHT45、BMP390、PTB210、HMP3、AI8、EPSILON、TFA1500-L，逐步检查 source、CRC、freshness、ERROR/drop。AI8 启用只开始轮询，不自动改设温或加热。HMP41 已接入软件，RH/T、地址/轮询和压力补偿仍需现场验收。PTB210 超时或 AI8 报警不得以虚构值掩盖。

AI8 地址、通道、轮询间隔、`0x6660 timeout_ms`、`0x6664 retry_limit` 只能在 AI8 禁用时修改并核对；软件默认 timeout=200 ms、retry=1，协议允许 timeout=1..65535、retry=0..3。状态码、设备报警和传输异常是不同空间。

### 4.3 连续采集、独立 WMS/DAC 与 RAW

开始 DLIA 周期采集时保持对应 WMS 提供扫描标签，再启用对应 DLIA/ADC；MODE2 的独立参考不解除对 WMS 周期标签的依赖。停止采集先停止 ADC、等待并排空，再停止/排空 DLIA。独立 WMS 或 DAC 按钮只操作对应页 CONTROL，不调用会联动完整通道的通用 START/STOP，也不改变另一条 WMS/DAC。

RAW 默认关闭，传感器和 DLIA 记录默认开启。显式启用 RAW 后持续上传，直到手动停止 RAW 或停止采集，上位机不再设置诊断时长或定时关闭；掩码的 ADC0/1 位与 `0x403C/0x413C` 应一致。raw_stream_mask 与主 stream_mask 分别处理。六传感器 START 掩码不能替代 stream_mask；管理状态和时间事件不得被错误屏蔽。旧配置及旧客户端的 `rawWindowSeconds` 字段兼容读取但忽略，当前配置不再保存或发送该字段；Ground 与 SkyCore 应使用当前软件以保持一致的 RAW 策略。

RAW 传感器电压比例取决于真实 VREF、增益、极性、接线与数字校准。文档的 CON15 正号/CON16 负号、10/8388608 只是特定板上条件示例。每路独立确认校准且保存快照后才允许电压显示；DLIA H1/H2 为数字幅值，不套 RAW 电压系数，更不输出未经标气/光路模型确认的 ppm。

### 4.4 AI8 设温

串行保存本次 `0x6648 ack_count` 基线，目标须是 0.1℃ 整数步长、符合允许范围。通过协议支持的动作或 shadow+enable/commit 发起；不以普通 WRITE ACK 显示设温成功。

最终确认同时要求：pending=0；`0x664C COMMAND_STATUS` 的相关位值为 `0x10000`（result_valid=1，result/transport_error/exception=0）；`0x6648 == baseline+1` 按 32 位回绕；`0x6654` 低 16 位 last_requested、高 16 位 last_readback 均按 I16 解读并等于此次目标原码。显示“设备设定已确认”仍不等于 PV 已达到目标温度。超时、禁用中止、返回错误不能证明物理仪表未执行，先读回，不盲目重发。

## 5. 六传感器解码与质量

| 来源 / msg | 处理与单位 | 有效性边界 |
|---|---|---|
| PTB210 `0040/1100` | TLV tag0012 I32 mPa，Pa=raw/1000 | 过期、timeout、设备错误不能当新鲜压力；默认压力来源 |
| EPSILON `0042/1200` | 完整 FDILink，复用 IMU/姿态/导航/状态字段 | CRC/内部ID有效不等于导航或UTC有效；状态新鲜度和模式分别检查 |
| BMP390 `0043/1100` | tag0100 校准21B；0101/0102 24bit压力/温度原码；补偿输出Pa/℃ | 当前连接缺校准、原码越界或设备错误时不提供虚构补偿值；断开清缓存 |
| SHT45 `0044/1100` | tag0010 ℃/1000，0011 %RH/1000 | 原始/质量字段保留；通信、数据和freshness分开 |
| TFA1500-L `0045/1100` | tag0013距离已为mm | 不再统一乘10；寄存器历史距离不当本次样本 |
| AI8 `0046/1100` | schema2，PV/SP/SV原码×0.1℃，OP/报警/host/结果保留 | 在线、仪表报警、测量有效、设温确认分别表达 |

TLV 按 tag/type/length 解码，不依赖固定顺序。VLP flags bit0时间有效、bit1周期有效、bit2外部同步、bit3溢出、bit4设备错误、bit5部分数据、bit6配置变化、bit7原厂payload，均保留。过期样本显示历史/无效状态；EPSILON 缺状态、模式改变、UTC未初始化时不能沿用上一个连接的 valid 标志。

## 6. RAW/DLIA schema 与不完整周期

RAW `0020/0021, msg1000` 支持 schema1 与 schema2。schema1 头20B，N<=2043，格式0有符号I32、1无符号U32；schema2头32B，包含 total、fragment_index/count、first_sample、fragment_samples，每片最多2040点。ADC 位数按 payload 的 adc_bits，不能固定按通道猜。

DLIA `0030/0031, msg1001` 的当前协议仅 schema2，头32B。format0为I32 H1/H2、8B/点；format1为I1/Q1/I2/Q2、16B/点；format2再加H1/H2、24B/点。MODE不是运行时输出格式切换开关。未知schema/格式只保留原始记录，不能解释成有效数值。

组包按连接段/source/msg/cycle/tick 分组，检查 schema、格式、实际rate、总点数、片数、first index、点数、重复、重叠、首尾和 flags；乱序到达按 first index 排列。PARTIAL/OVERFLOW 周期即使片齐也不完整；缺片不能用下一周期补齐。默认每source最多4个待完成周期，超限淘汰最早到达周期，`takeExpired()` 发出 `complete=false, partial=true, continuityError=true` 证据；结束/重连用 `flush()` 保存残留周期，再清缓存。实现同时限制payload、总点数、片数及累计字节，不按未经校验的total做巨大数组分配。

实时连接的管理事件按 FPGA 发生 tick 过滤历史积压，不以主机接收时间判断新旧。首次匹配成功的 PING 回复提供本连接的 FPGA 时间锚点，减去从打开连接到收到回复的主机单调时钟耗时（100 MHz）作为保守起点；握手期间管理事件暂存，起点之前的事件不进入诊断卡片或可见日志。该估计保留握手期间的新故障，边界存在往返延迟误差。每次重连重新建立时间边界；首个当前状态的累计丢包值只建立基线，后续增加才告警。原始 USB/帧记录保留，离线回放不应用实时连接过滤。

传感器启用读回确认后另有 3 秒启动告警宽限期（重连恢复及单独启用均适用）。期间新错误保留在管理状态和记录中，暂不生成可见警告；到期检查最新测量有效性与 AI8 当前报警状态，仍异常才告警。TFA 无效返回不会生成距离数据，因此要求最近 1 秒内有有效样本；其它传感器使用 5 秒新鲜度。比暂存错误更新的健康样本可消除短暂启动告警，但仍锁存的配置/内部错误不因此忽略。没有错误事件却持续无效或无数据也会提示。禁用、断开或操作失败清理相关等待状态；宽限期后的新错误、连接/命令失败及非传感器故障照常提示。此策略不清除硬件错误位，不修改设备采样与控制参数。

不完整输出保留实际存在的片段位置，不能自行补0后画成无缺口真实采样。页面清除异常/缺片周期的连线并显示质量提示，原始片段仍保留供诊断。RAW signed/unsigned、DLIA IQ/H1/H2分开保存；H1/H2是数字幅值，滤波不补额外×2。

每路DLIA上下显示H1/H2，不再提供分量下拉框：format0和format2显示两个谐波，只有I/Q数据的format1显示I1/Q1和I2/Q2。每路ADC、H1、H2图高均为100逻辑像素，随字体缩放。绘图根据实际payload格式自动适配，不写MODE或更改硬件输出schema；format2的I/Q仍保留在原始记录中。完整format0周期可以展示“本周期2f最大值（数字幅值）”，不是ppm。GUI展示的抽稀只影响绘图，归档原始字节不抽稀。

## 7. Session、离线回放与导出

本轮在既有天空端 Session 中保存完整 FPGA 归档；历史 Ground 本机 FPGA Session 仍可读取，文件格式保持一致：

| 路径 | 内容 |
|---|---|
| `raw/fpga_vlp1.dat` | 既有VVRAWDAT外层格式，独立source=8；host time与归档sequence；payload保留精确字节 |
| `sensors/fpga_frames.jsonl` | 完整帧/OUT命令元数据、typed payload摘要及配置/校准/连接段快照 |
| `session.json` 的可选FPGA字段 | raw_files.fpga_vlp1和FPGA路径、记录计数；旧session无此项 |

record_type=1为IN完整帧，2为OUT命令，3为JSON快照，4为原始USB IN块。“记录传感器/记录解调数据/记录RAW”选项只筛选type1完整帧与对应解析摘要，不筛选精确type4 USB流、type2 OUT或type3快照；活动天空 Session保留接收到的完整USB流，包含未勾选的source。要降低高吞吐USB数据量必须关闭硬件RAW上传，不能靠解析记录筛选。原始USB包括半帧、多帧、填充、CRC损坏和不能解析的字节；完整帧与USB重复记录用于诊断，但回放/流BIN选择USB优先，不能重复计数。外层host时间、内层tick/source/msg/sequence/cycle/flags以及raw payload都可追溯；tick/host time等uint64字段的JSON使用十进制字符串避免精度损失。

天空端 `SkySessionRecorder::rawFpgaRecordCount()` 与 manifest `raw_files.fpga_vlp1.records` 对应；历史 Ground 本机记录仍保留 `GroundRecordingStatus::rawFpgaRecords`。这些值统计后台成功写入的四类记录总数，不是测量点数、USB 字节数或有效 VLP 帧数。刚入队尚未计入，pause/stop 排空后得到最终值；不用 PTB/raw waveform 旧计数冒充 FPGA 数量。地面数传 Session 保存既有标准遥测测量；新 FPGA 状态和有限波形预览仅用于界面，不作为完整原始归档保存。

记录经有界后台队列写入，GUI不做高频同步落盘。pause先排空、resume沿用session并写段标记；关闭排空后flush，写入/队列/flush失败标记incomplete，不宣称完整记录。配置、校准、连接和重连事件保存快照。FPGA记录不写入PTB/HMP旧raw文件，也不修改已有session。

`FpgaSessionArchive::scan/visit/replay/exportTo` 提供离线API：索引恢复中断尾部；每次读一个payload；replay复用VLP codec、USB优先、CRC失败不生成测量；connected/disconnected/reconnect/pause/resume快照重置半帧并通知适配器清状态。frame visitor可取消，额外cancel callback可中断扫描及逐块读取。离线回放不会向硬件发送归档OUT，也不会把页面标成在线采集。

CSV/JSON导出包括各record kind、元数据、原始payload和配置快照；BIN导出精确USB IN流，老FPGA记录仅有完整帧时回退IN帧流。BIN不会混入OUT命令/JSON，也不会自动剔除坏CRC。单条/批量原始记录导出仍可在已有原始解析页使用。导出不得覆盖源归档；文件使用原子提交。软件可以保存、扫描与回放，但不能借此证明板上无丢点或校准准确。

## 8. 结构化诊断与可追溯状态

FPGA操作诊断通过统一 `LogRecord/LogService` 发布：天空后端将共享控制器日志标记为 source=`SkyCore`、category=`device.fpga`，执行角色与会话 origin 一致，fields包含event=`fpga_operation_diagnostic`、operation、ready、busy；错误等级附error_code=`FPGA_OPERATION_FAILED`。页面诊断框展示message，统一日志保留结构化字段，不能以页面临时文本代替可审计错误记录。周期不完整、命令超时、版本不匹配及读回失败均应有诊断，并保留原始帧/USB证据。

config snapshot同时保存期望configuration、hardware_registers、ready、usb_connected和USB诊断字段。期望配置并不自动变成硬件实际值，USB opened也不自动变成ready；操作失败快照记录event=operation_failed和detail。现场需要结合原始OUT/RESP、B0/B1及状态初末值判断错误，不能从日志中的一个“ACK”推导采集、设温或模拟输出通过。

## 9. 本轮实施计划及验证记录

| 阶段 | 交付内容 | 验证方法与当前边界 |
|---|---|---|
| 1 协议/传输 | VLP1 codec、唯一接收、命令匹配、WinUSB及可选CyAPI | 半帧/多帧/CRC/超时/replay fixtures；实际驱动及CySDK分支未验证 |
| 2 数据 | 六传感器适配、BMP补偿、EPSILON/AI8有效性、RAW/DLIA组包 | 协议固定向量、状态失效、无校准、组包边界；软件结果由最终测试记录补充 |
| 3 控制 | 寄存器计划、版本门控、shadow/commit/读回、独立启停、AI8确认、限时RAW | Fake/replay响应事务测试；硬件效果未验证 |
| 4 页面 | 独立页面、参数持久化、在线/历史/离线、绘图和诊断 | 页面测试及真实GUI截图；不以无头测试代替可见结果 |
| 5 Session | 后台记录、分段快照、raw解析页、CSV/JSON/BIN与历史兼容 | 归档fixture、CRC损坏、截断尾、错误注入、取消、旧session回归 |
| 6 实机 | 驱动/GP01、六传感器、WMS/DAC、双ADC/DLIA、长采集及重连 | 按 `fpga_hardware_validation.md` 执行；当前无实机，全部待执行 |

本轮新增及扩充代码包含上述软件模块和直接相关测试。提交前须用最终 `build/Release` 构建、运行相关测试，并按记录链路改动面执行完整回归；日志/截图/现场凭据不得提交。未完成的软件检查不能写成通过。现场性能、CyAPI SDK分支、板卡电压/浓度校准和实际拔插均在本轮软件验证之外。

## 10. 使用入口与可选 SDK 构建

1. 按 [天空拓扑部署](fpga_sky_topology.md) 配置并启动 `VaporViewSkyCore`。`fpga.enabled=true` 允许 SkyCore 连接和探测 USB，但不代表已启用传感器、ADC、DLIA 或 DAC 输出；本机 GUI 与地面 GUI 均进入 Remote 模式。
2. 本机当前构建没有 CyAPI SDK，提供 WinUSB 后端及明确的 Cypress 不可用提示；WinUSB 要求设备绑定兼容驱动。若使用下位机文档中的 Cypress 驱动，须先安装匹配的官方 SDK，再在 MSVC Developer 环境、UTF-8 代码页下配置并构建：

   ```text
   cmake -S . -B build/Release -DVAPORVIEW_CYAPI_ROOT="<Cypress SDK 根目录>"
   cmake --build build/Release --config Release -- -j4
   ```

   CMake 必须同时找到 `CyAPI.h` 和 x64 `CyAPI` 库才会启用该分支；仅填写目录不代表编译或驱动验收通过。应用不自动更换驱动或下载固件。
3. 同机 GUI 经 TCP `127.0.0.1:39001` 接入本机 IPC；地面 GUI 经天空数传的串口或 TCP 接入原遥测链路，两者可并存。独立 FPGA 页通过 `DeviceOperation::FpgaControl` 请求允许的操作，SkyCore 执行并异步返回最终读回；页面不再直接打开 USB。核验 GP01、模块版本和上传通路，编辑配置仍需明确提交。
4. 传感器页逐路启用并检查有效性，压力默认 PTB210，可显式选择 BMP390。采集按钮按 WMS 标签、DLIA、ADC 顺序启用数字采集；DAC 输出需要独立操作，软件计数不能证明模拟波形正确。
5. 完整 FPGA Session 在天空工控机录制。默认记录传感器和 DLIA，RAW 上传需显式开启并手动停止；关闭解析记录筛选不会删除精确 USB 归档。数传默认仅承载标准化数据和有界波形预览，不发送全量 RAW/USB。
6. 停采后由 SkyCore 继续排空并保存最后状态。同机或地面 GUI 掉线不应自动停止 SkyCore 持续采集和录制。离线回放/导出读取天空 Session 的本地副本，不向设备发送归档 OUT；CSV/JSON/BIN 支持取消和原子提交。

VLP1 当前没有实现的 RTCM 下行和 EPSILON 高级参数操作返回 `Unsupported`，不得为完成旧串口操作绕过 FPGA 另开 EPSILON 串口。只有已明确映射和允许的 FPGA 动作可下发；请求受理不是最终硬件成功。

## 11. 历史第一阶段 Ground 本机软件验证记录（2026-10-10，7b68482）

以下结果只覆盖 `7b68482` 的 Ground 本机 USB 适配与离线流程，发生在天空端部署拓扑澄清之前。不能据此声明 SkyCore USB 所有权、本机 IPC 与数传并存、远程 FPGA 控制或天空归档已通过；本轮最终结果由第 12 节追加。

最终代码在本机 MSVC Developer 环境、UTF-8 代码页下完成 `build/Release` 全目标构建，osgEarth 保持开启，构建退出码为 0：

```text
cmake --build build/Release --config Release -- -j4
```

直接相关复验共 11 项，全部通过，用时 50.76 秒：10 项 `fpga_*` 测试覆盖 VLP1、组包、传感器解码/适配、USB、会话、控制器、归档取消、页面及主窗口接线；另有 `ui_test_mode_window_test` 验证既有数据源切换和记录状态。独立 FPGA 页面完成中英文、明暗主题及主窗口中的实际 Qt 渲染检查，临时截图及截图代码均已移除。

因涉及协议和记录链路，还在同一最终构建运行完整回归：

```text
ctest --test-dir build/Release --build-config Release --output-on-failure --timeout 180
```

完整回归用时 460.95 秒，125 项中 119 项通过、5 项失败、1 项跳过，CTest 返回非零。10 项 FPGA 测试在完整回归中也全部通过。未解决失败按实际断言记录如下，不能称为全量通过；也不把尚未逐项证明的失败一概归为环境问题。

| 测试 | 本轮最终回归失败断言 |
|---|---|
| `session_viewer_components_test` | `overview page creates its command controls` |
| `main_window_layout_test` | `title bar application submenu opens from a root row hover` |
| `title_application_menu_test` | `Up moves to the previous root menu row` |
| `session_viewer_theme_test` | `export button opens menu` |
| `main_window_map3d_open_test` | `map title switches immediately` |

`update_relauncher_elevated_parent_test` 在当前非提权环境跳过。菜单/布局和旧数据查看器失败保留为后续排查项；本轮没有为取得通过结果关闭这些测试或削弱其断言。`session_viewer_theme_test` 曾在此前直接相关回归中通过，最终完整回归仍按失败记录。

软件验收结论仅适用于当前构建的 WinUSB 路径、协议 fixtures、模拟传输和离线流程。没有接板实测；本机缺少 Cypress SDK，启用 CyAPI 的编译/链接及 Cypress 驱动连接尚未验证。吞吐、模拟输出、物理校准、拔插和持续采集验收全部保留在 `fpga_hardware_validation.md`，未预填完成。临时构建/测试日志及现场生成物不纳入版本库。

## 12. 天空端拓扑最终软件验证记录（2026-10-10）

本节覆盖用户最终确认的天空工控机拓扑：SkyCore 唯一持有 FPGA USB，同机 GUI 经本机 IPC 控制，数传另接工控机并服务地面 GUI。下位机事实来源为本地 `Vapor_Radar_App_V2` 的 `4914e1d` 及第 1 节交接文档；本轮未修改下位机仓库。

最终可执行输入在 MSVC Developer 环境、UTF-8 代码页下完成全目标 Release 构建 `[189/189]`，退出码 0，osgEarth 保持 ON：

```text
cmake --build build/Release --config Release -- -j4
ctest --test-dir build/Release --build-config Release -L fpga --output-on-failure --timeout 90
```

16 项 FPGA 专项测试全部通过，用时 25.05 秒。除第 11 节已有协议/控制/归档/页面测试外，新增 `fpga_telemetry_test`、`fpga_serial_budget_test`、`fpga_remote_controller_test`、`sky_fpga_backend_test`、`sky_fpga_runtime_test`、`sky_fpga_recording_test`。覆盖以下软件行为：

- 只有一个 USB controller/持续 IN；关闭后台时等待生产者退出，停止后创建的新后台也能关闭。
- 标准测量与 FPGA 诊断传输兼容；波形预览保留原始点数/速率及组合 stride，界面只绘制实际收到的点。
- 实际本机 TCP 模拟控制、受理 ACK 与最终响应区分、120 秒最终超时不自动重发；IPC 客户端/数传连接代际隔离、重复请求不重复物理写、同 ID 不同 payload 被拒绝。
- 4 秒远程状态门控和逐源历史标记；状态更新不会把旧波形刷新为新样本，Local/Remote 期望配置分别持久化。
- 精确 USB/OUT/帧/快照在天空后台队列归档，pause/stop 排空，12 轮并发开始/暂停/继续/停止，以及短写、溢出、flush 失败保守标记。
- 物理串口的诊断令牌预算、降点、队列上限与 IPC 预览保留；低速无线实际可用吞吐仍需实测。

页面完成主窗口内的中英文、明暗主题 Qt 实际渲染检查，修正英文连接按钮裁切和暗色波形对比度。临时截图逻辑与图片均已删除，再构建上述最终输入并运行专项测试。GUI 测试使用隔离配置；字体 100%、紧凑侧栏 62 逻辑像素，测试窗口和后台线程均已退出。

共享协议/记录链路及 CMake 改动另运行完整回归：

```text
ctest --test-dir build/Release --build-config Release --output-on-failure --timeout 180
```

完整运行用时 484.23 秒，131 项中 122 项通过、8 项失败、1 项跳过，CTest 返回非零。16 项 FPGA 测试在完整运行中也全部通过。其中 `logging_event_catalog_audit_test` 发现本次新增的 `fpga_recording_storage_failed` 与 `fpga_storage_failure_usb_closed` 缺目录登记；已补齐 `docs/logging_events.md`，最终单项复验通过，用时 1.21 秒。该修复只修改文档，复用已成功的可执行构建和 FPGA 测试结果。

最终仍有以下 7 项 UI/菜单断言未解决，不能声明完整回归通过。部分同名测试在第 11 节已失败，但断言并非都相同；其余失败未逐项判定原因，不能一概称为已证明的环境或基线故障。本轮未修改其测试或削弱断言：

| 测试 | 本轮完整回归失败断言 |
|---|---|
| `visual_text_label_test` | `repeated drag keeps keyboard focus on the selected title` |
| `session_viewer_components_test` | `overview page creates its command controls` |
| `main_window_layout_test` | `log search icon opens a popup search field` |
| `spin_arrow_disabled_test` | `enabled spin arrow hover does not duplicate the primary icon in the text area` |
| `title_application_menu_test` | `View submenu opens for trigger regression` |
| `session_viewer_theme_test` | `export button opens menu` |
| `main_window_map3d_open_test` | `map title switches immediately` |

`update_relauncher_elevated_parent_test` 在当前非提权环境跳过。实机 USB、Cypress SDK 编译/驱动分支、数传无线吞吐、模拟输出、物理校准和拔插均未验证；RTCM/EPSILON 高级参数等无 VLP1 下行定义的功能明确返回 Unsupported。部署步骤、带宽及地面/天空保存边界见 `fpga_sky_topology.md`，接板验收见 `fpga_hardware_validation.md`。构建日志、截图、现场 Session 和含凭据生成物不纳入 Git。

## 13. FPGA 页面界面测试覆盖（2026-10-11）

主窗口“界面测试模式”中的 FPGA 页面现支持可交互离线模拟。打开界面测试模式后进入 FPGA 页：模拟设备默认已连接、正在采集，六类传感器和两路 H1/H2 更新；RAW 默认关闭，点击“开始 RAW”后持续生成两路 ADC 波形，手动停止时模拟残缺尾周期并保留历史图。无需连接板卡或启动独立服务器。

连接/断开、ADC/DLIA 启停、传感器启停、WMS/DAC 启停、分区参数提交、AI8 设温和模拟记录开关会改变内存状态。提交参数后采集停止，重新点开始采集；WMS 幅度、偏置、频率与采样/输出率参与合成波形及读回。RAW 模拟参考固件每周期 16384 点上限，超限仍标记异常。记录按钮只模拟记录状态，不写现场文件；离线回放/导出在界面测试模式中仍被拦截。

WMS/DAC 两路合用一张卡片和一列字段名，各路输入、读回及启停按钮独立。模拟模式补齐实际更新率、相位模式、DAC 清除/限幅码、实际 SPI 频率，以及具名的错误寄存器和诊断计数；正常错误/丢弃计数显示 0，DAC 写入计数随扫描和 DAC 启停独立变化。这些是合成读回，不代表实机验证结果。

波形页提供“模拟单周期丢片”和“模拟 DLIA 异常锁存”：前者丢弃首分片并让下一周期独立恢复；后者持续附带 partial 标志，重启采集或重连后恢复。全局“部分设备异常”场景模拟 PTB210/SHT45 离线、TFA 距离无效；“数据停更”保留历史图并将传感器标为无效。

模拟波形由此前现场观察的量级和故障现象合成，不是现场记录逐点回放，不含日志、凭据或真实测量数据。schema-2 分片使用生产 Assembler 拼接，覆盖界面/状态和分片故障，但不模拟 USB 驱动时序、吞吐或真实模拟电路。退出模式恢复原配置和 USB 设备列表，清除模拟数据；不会写入真实硬件。回归入口：`ctest --test-dir build/Release -C Release -R "^(fpga_control_page_test|fpga_main_window_test|ui_test_mode_window_test)$" --output-on-failure`。

## WMS / DLIA 参数分区

FPGA 参数页面将 WMS / DAC 与 DLIA 分页，各有独立应用按钮；共享 ADC 采样率和 AI8 参数使用单独的应用按钮。应用会停止并排空 ADC/DLIA 采集，完成后需重新开始采集；仅提交所选分区的参数，不覆盖另一分区的未提交编辑。DLIA 输出率按设备当前 ADC 采样率校验；降低 ADC 采样率前须确保现有 DLIA 输出率不超过其八分之一。

WMS 扫描幅度、调制幅度和直流偏置以 V 显示及输入，设备读回使用相同单位。幅度为峰值（峰峰值为两倍），按 DAC ±10 V 标称量程、增益 1、偏移 0 换算：`V = Q1.31 × 10 / 2^31`。这不是实测端口电压；非默认 DAC 增益/偏移、板卡跳线及负载会影响实际输出。配置文件和协议仍保存原始整数码。

远程 `configure` 支持可选 `section`：`wms`、`dlia`、`acquisition`；省略或使用 `all` 保持原有完整配置行为。分区请求仍包含完整配置结构，寄存器读回是判断实际生效值的依据。

## PTB210 历史错误恢复

当前 PTB210 固件在 ERROR 寄存器非零时给后续气压数据持续附加 DEVICE_ERROR，即使新的串口解析已经成功。上位机继续尊重数据有效位，不直接把异常帧提升为有效。用户点击 PTB210 启用时，若读回存在历史错误，先保存配置/寄存器快照并记录错误值，再向 `0x600c` 写入已观察的错误位执行 W1C，随后启用并读回。手动刷新和禁用均不清错；后续重新发生的通信错误仍会使数据无效。该操作不清零累计解析错误或丢包计数，也不复位板卡。

## 连接时自动恢复

通过协议和模块版本核验后，每次 USB 连接自动执行一次恢复：保存设备寄存器快照，恢复上传通路，停止排空双 ADC/DLIA 并软复位、提交设备保留的参数；对原本启用的 PTB210、EPSILON、BMP390、SHT45、TFA1500-L、AI8 执行禁用、读回确认（BMP/SHT 额外等待忙状态退出，TFA 等待串口停止/静默切换完成，PTB 禁用时忙位仍为 1）、清除已记录的历史错误、重新启用。原本禁用的传感器仅清历史错误，不自动启用。BMP 地址及各传感器配置保持设备当前值。最后恢复两路 ADC/DLIA 各自的启用状态及 RAW 使能并核验。WMS/DAC 参数和启用状态不改，HMP、温控设定值、运动控制不参与恢复。

恢复期间页面保持忙状态，不发布过渡波形；完成后才恢复周期读回与测量显示。恢复失败显示未就绪并停止后续动作，需排查后重新连接。连接现在包含硬件写操作，`fpga_controller_test --hardware-probe` 也会执行这套恢复流程，不能再作为只读探针。常规状态刷新、自动轮询和界面切页不会重复复位。真实新故障仍保留无效标记；自动恢复不能修复线路故障。

## 首页设备启停

FPGA 中转模式下，首页设备概览、设备配置和 AI8 标题栏的连接/断开入口仍执行设备操作，不跳转 FPGA 页。EPSILON、气压（当前选择的 PTB210/BMP390）、温湿度、激光测距及 AI8 分别发送对应 source 的 START/STOP；波形入口控制 ADC/DLIA 采集，RAW 上传仍由独立按钮控制。RD105 没有 VLP1 通道，保持不可用。

本地首次连接单个设备时先打开所选 USB 后端，完成握手后再启用该设备；停止单个设备保留 USB 和其它模块。远程复用 SkyCore 的 FPGA 控制命令，必须收到新鲜的就绪状态；ACK 仅表示受理，最终寄存器读回才更新连接状态。测量无效与模块禁用分开处理，不能因测量无效把已启用设备的断开按钮变成连接按钮。界面测试复用同样的设备映射并只修改模拟状态。

命令执行期间仅锁定设备操作按钮，设备在线状态继续依据就绪状态与寄存器读回判断。启停 PTB 或其它模块不应使 EPSILON 导航状态暂时不可用，也不应生成虚假的 GNSS 丢失/恢复日志；真实断线、状态过期或 EPSILON 禁用仍会使导航不可用。

## RAW 手动停止与历史波形

手动停止 RAW 时，先关闭两路 RAW 生产使能，读回确认后关闭 RAW 掩码让下位机封装尾周期，随后等待 RAW 帧缓存排空；读回确认后仅排空两路 RAW 的主机分片缓存，不影响 DLIA 的在途周期。残缺周期仍保留诊断证据及警告，不会作为完整数据使用。页面保留上一完整 RAW 周期，并明确标注“历史值，非当前测量”；没有完整周期时仍显示空图和异常说明。实际寄存器确认 RAW 已停止后，波形标题显示“RAW 上传已停止”。停止 RAW 不停止 ADC/DLIA 采集。
