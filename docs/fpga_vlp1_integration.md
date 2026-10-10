# FPGA VLP1 软件适配与对接计划

本文说明天空端 FPGA 控制、记录和离线查看边界。用户最终确认：VaporView 所在工控机位于天空端，USB/VLP1 连接 FPGA，其他设备全部连接 FPGA；数传另一路直连工控机，将天空数据发送地面并接收地面控制。SkyCore 是唯一 USB 所有者，同机 GUI 和地面 GUI 均通过 SkyCore 控制设备。协议对象为 XCKU11P + FX3 GP01 正式双 AD4630 系统；页面独立于既有串口 Device Configuration。本文所说“已实现”仅表示代码提供接口与处理路径，不代表接板、SDK 分支或硬件验收通过。第 11 节是历史 Ground 本机适配结果，不能作为本轮天空拓扑验证；本轮最终结果由第 12 节另行记录。配置和启动步骤见 [天空拓扑部署](fpga_sky_topology.md)。

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
| 默认 PTB210 | 传感器适配和压力来源选择 | `0x0040` 为默认来源，BMP390 可显式选择；不能无提示回退或混用两者校准 |
| 六传感器物理量与质量 | `FpgaSensorDecoder`、`FpgaSensorAdapter` | TLV、FDILink、BMP 补偿、AI8 状态；通信成功不等于测量有效 |
| 传感器和 DLIA 连续记录 | `SkySessionRecorder`、`FpgaSessionArchive` | 完整 USB/命令/帧/快照在天空端后台落盘；地面只保存收到的标准遥测，新增预览用于界面观察 |
| RAW 按需诊断 | 控制器、RAW 定时器和记录配置 | 默认关闭；默认诊断窗口 10 秒，到期请求关闭上传；结束确认必须检查命令及掩码读回 |
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

六传感器启动按 HOST_HANDOFF：SHT45、BMP390、PTB210、AI8、EPSILON、TFA1500-L，逐步检查 source、CRC、freshness、ERROR/drop。AI8 启用只开始轮询，不自动改设温或加热。HMP41 未验证，保持禁用。PTB210 超时或 AI8 报警不得以虚构值掩盖。

AI8 地址、通道、轮询间隔、`0x6660 timeout_ms`、`0x6664 retry_limit` 只能在 AI8 禁用时修改并核对；软件默认 timeout=200 ms、retry=1，协议允许 timeout=1..65535、retry=0..3。状态码、设备报警和传输异常是不同空间。

### 4.3 连续采集、独立 WMS/DAC 与 RAW

开始 DLIA 周期采集时保持对应 WMS 提供扫描标签，再启用对应 DLIA/ADC；MODE2 的独立参考不解除对 WMS 周期标签的依赖。停止采集先停止 ADC、等待并排空，再停止/排空 DLIA。独立 WMS 或 DAC 按钮只操作对应页 CONTROL，不调用会联动完整通道的通用 START/STOP，也不改变另一条 WMS/DAC。

RAW 默认关闭，传感器和 DLIA 记录默认开启。诊断窗口默认 10 秒，显式启用 RAW 后到期关闭 RAW 上传；掩码的 ADC0/1 位与 `0x403C/0x413C` 应一致。raw_stream_mask 与主 stream_mask 分别处理。六传感器 START 掩码不能替代 stream_mask；管理状态和时间事件不得被错误屏蔽。

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

不完整输出保留实际存在的片段位置，不能自行补0后画成无缺口真实采样。页面清除异常/缺片周期的连线并显示质量提示，原始片段仍保留供诊断。RAW signed/unsigned、DLIA IQ/H1/H2分开保存；H1/H2是数字幅值，滤波不补额外×2。

每路DLIA有分量选择器：format0可选H1/H2，format1可选I1/Q1/I2/Q2，format2可选I1/Q1/I2/Q2/H1/H2。选项来自实际payload格式，选择只改变当前绘图分量，不写MODE或更改硬件输出schema；格式变化时只保留仍存在的选项。完整format0周期可以展示“本周期2f最大值（数字幅值）”，不是ppm。GUI展示的抽稀只影响绘图，归档原始字节不抽稀。

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
5. 完整 FPGA Session 在天空工控机录制。默认记录传感器和 DLIA，RAW 诊断需显式开启，默认 10 秒；关闭解析记录筛选不会删除精确 USB 归档。数传默认仅承载标准化数据和有界波形预览，不发送全量 RAW/USB。
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
