# VaporView Unified Raw DAT Format

本文档说明 `session_*/raw/*.dat` 的统一原始数据记录格式。常规设备source保存原始帧或响应字节，解析摘要在 `sensors/sensor_summary.csv` 中。可选FPGA source=8还保存原始USB IN块、OUT命令和JSON配置/连接快照；其帧摘要位于 `sensors/fpga_frames.jsonl`，不能把快照误作传感器测量。

地面端和天空端共用 `src/shared/session/UnifiedRawDat.*` 中的 raw DAT 常量、header 编解码和记录读写逻辑；`sensors/sensor_summary.csv` 的表头、字段顺序、转义和行格式化共用 `src/shared/session/SessionSensorCsv.*`。文件名描述数据类型，设备型号保存在 session metadata 中。生产代码不再分别维护 raw magic、record marker、format version、source ID 或 CSV schema。

## 文件位置

每个记录会话会创建 `raw/` 目录，并写入以下文件：

| 文件 | source_id | payload 内容 |
| --- | ---: | --- |
| `raw/navigation.dat` | 1 | 组合导航、GNSS、INS、姿态和位置原始帧 |
| `raw/pressure.dat` | 2 | 气压传感器有效压力响应原始行字节，包含行结束符 |
| `raw/temperature_humidity.dat` | 3 | 温湿度传感器完整 Modbus 数据响应帧 |
| `raw/distance.dat` | 4 | 已识别协议且校验通过的完整测距帧 |
| `raw/waveform.dat` | 5 | TCP 原始信号 payload 和二次谐波 payload |
| `raw/laser_temperature_controller.dat` | 6 | 激光温控完整、已校验 Modbus RTU response frame |
| `raw/system_temperature_controller.dat` | 7 | 系统温控完整、已校验 Modbus RTU response frame |
| `raw/fpga_vlp1.dat`（按需创建） | 8 | FPGA完整IN帧、OUT命令、JSON快照、精确USB IN块 |

常规新记录会话写上述source=1..7统一raw文件；地面FPGA会话在收到FPGA记录时按需创建source=8文件。不再额外生成旧型号文件名或 `waveform/*.dat`。数据查看器仍保留对旧会话 `raw/epsilon.dat`、`raw/tcp_wave.dat`、`raw/rd105.dat`、`raw/ai8.dat`、`raw/ai8288.dat` 和 `waveform/*.dat` 的读取兼容。缺少统一 raw magic 的历史波形文件仍按旧格式回退读取；带有合法统一 raw magic 但版本不受支持的文件会被明确拒绝，不会回退为旧格式。

## 字节序

除 `magic` 和 payload 外，所有多字节整数字段均为 little-endian。payload 保持设备或 TCP 流的原始字节顺序。

共享写入器逐字段生成 little-endian 字节，不依赖 C++ 结构体的内存布局或 padding。

## 文件头

每个 `.dat` 文件以固定文件头开头：

| 偏移 | 类型 | 字段 | 说明 |
| ---: | --- | --- | --- |
| 0 | char[8] | magic | 固定为 `VVRAWDAT` |
| 8 | uint32 | version | 当前为 `2` |
| 12 | uint32 | header_size | 当前文件头大小，当前为 `20` |
| 16 | uint16 | source_id | 数据源编号 |
| 18 | uint16 | reserved | 保留，当前为 `0` |

当前支持读取的 format version 为 `1` 和 `2`，新写入文件使用 `2`。读取器会在校验 `magic` 后、扫描任何记录前显式校验 `version`；不支持的版本会返回包含实际版本和支持版本列表的错误，例如 `Unsupported raw DAT format version 3; supported versions: 1, 2`。文件头截断、非法 `header_size`、未知 `source_id` 或非零 `reserved` 都是格式错误。

## 记录头

文件头后是连续记录，每条记录由记录头和 payload 组成：

| 偏移 | 类型 | 字段 | 说明 |
| ---: | --- | --- | --- |
| 0 | uint32 | marker | 固定为 `0x44525756` |
| 4 | uint32 | header_size | 当前记录头大小，当前为 `36` |
| 8 | uint64 | host_timestamp_us | 主机 UTC 时间戳，单位微秒 |
| 16 | uint32 | payload_size | payload 字节数 |
| 20 | uint16 | source_id | 数据源编号，和文件头一致 |
| 22 | uint16 | record_type | 源内记录类型 |
| 24 | uint32 | flags | 附加标志 |
| 28 | uint64 | sequence | 当前文件内从 0 开始递增的记录序号 |

完整记录定义为：记录头可完整读取、`marker = 0x44525756`、`header_size` 在允许范围内、`source_id` 与文件头一致、`record_type` 对该 source 有效、`payload_size` 不超过共享上限且 payload 字节全部存在。当前 payload 上限为 256 MiB，用于避免按损坏或恶意长度进行无界内存分配。序号不连续会作为警告报告，但不会丢弃本身有效的记录。

## record_type 与 flags

- EPSILON：`record_type` 为 FDILink packet id，例如 `0x40 / 0x41 / 0x42 / 0x50`；`flags` 低 8 位保存 FDILink serial number。
- PTB：`record_type = 1`，`flags = 0`。
- HMP：`record_type = 0x03`，对应 Modbus function code。
- LIDAR：`record_type` 使用程序内 `LidarProtocol` 枚举值：`1` 为历史保留值，当前不再生成；`2=TFA1500DistanceFrame`，`3=TFA1500LowFrequencyFrame`，`4=TFA1500HighFrequency`，`5=ObservedAaB7Frame`。
- TCP 波形：`record_type = 1`，`flags` bit0 表示 payload 内含两个 length-prefixed 子 payload；`flags` bits8-9 记录 payload 浮点编码，`0=legacy/unknown`、`1=little-endian float32`、`2=big-endian float32`、`3=word-swapped float32`。
- 激光温控：`record_type` 使用已校验 Modbus RTU response 的起始寄存器地址，`flags = 0`。
- 系统温控：`record_type = 1` 表示 8 路测量值响应，`2` 表示报警状态响应，`3` 表示主状态响应，`4` 表示 8 路控制状态响应；`flags = 0`。
- FPGA source=8：`record_type=1`完整IN帧，`2`主机OUT命令，`3`JSON快照，`4`精确USB IN块；外层`flags=0`，VLP flags保留在内层原始帧。

## FPGA source=8 payload与计数

| record_type | payload | 解释边界 |
| ---: | --- | --- |
| 1 | 一条完整VLP1 IN帧的头、payload、CRC及接收时保留的padding | shared codec的Frame.bytes可不含padding；归档不擅改原始字节 |
| 2 | 主机发送的VLP1 OUT命令字节 | 保留发送证据；离线回放绝不发送到硬件 |
| 3 | UTF-8 JSON配置、校准、连接、操作及pause/resume快照 | 是事件/上下文，不是测量点 |
| 4 | 一次USB IN read返回的全部字节 | 可含半帧、多帧、padding、坏CRC及未知内容，完整保留 |

FPGA的VLP1头、tick/source/msg/flags/sequence/cycle位于内层payload，其定义见 [`fpga_vlp1_integration.md`](fpga_vlp1_integration.md)。外层`host_timestamp_us`是主机UTC微秒，外层`sequence`是归档文件内序号，两者不能替代FPGA tick和VLP sequence。FPGA tick为100MHz本地uint64计数，不是UTC。

完整原始USB归档不受“记录传感器/记录DLIA/记录RAW”筛选影响：这些选项只决定type1完整帧及对应解析摘要是否记录。只要Ground Session处于活动记录，接收到的USB块type4、发送OUT type2、快照type3都保留；关闭RAW上传才会降低真实USB流量。不得把筛选关闭误解释为精确USB流中已不存在该source。

`GroundRecordingStatus::rawFpgaRecords`及manifest `raw_files.fpga_vlp1.records`统计后台成功写入的所有四类归档记录，包含USB块、帧、OUT和快照，**不是采样点数或仅有效VLP帧数**。暂停或停止会排空队列；UI计数可能落后于刚入队的记录。source=1..7旧计数、旧文件名和旧CSV不变。

离线codec回放和流BIN优先选择type4；没有USB块的历史FPGA记录才回退type1，避免USB与完整帧重复。坏VLP CRC不会生成测量，但其type4字节可从BIN恢复。外层VVRAWDAT合法与内层VLP CRC通过是两种不同完整性结论。CSV/JSON保留record kind、内层元数据、payload及snapshot；uint64时间/tick以十进制字符串保存，避免JSON精度损失。

## TCP 波形 payload

`raw/waveform.dat` 的每条 payload 由下面几段顺序拼接：

| 偏移 | 类型 | 字段 |
| ---: | --- | --- |
| 0 | uint32 | 原始信号 payload 字节数 |
| 4 | uint32 | 二次谐波 payload 字节数 |
| 8 | byte[] | 原始信号 payload |
| 8 + raw_size | byte[] | 二次谐波 payload |

这两个子 payload 是 TCP 帧长度头之后的原始负载，不包含 4 字节长度头；浮点字节序保持接收时的原始编码。`version=2` 的新记录通过记录头 `flags` bits8-9 保存该编码；旧 `version=1` 文件没有编码元数据，Session Viewer 会按 payload 内容自动探测后再回放。

## 读取与恢复策略

读取器只对物理文件末尾的未完成记录做非破坏性恢复：

- 文件头已经完整且合法；
- 文件末尾剩余字节不足一条完整 record header；
- 或 record header 完整，但文件在该记录声明的 payload 写完前结束。

恢复时，读取器忽略最后一条不完整记录，返回此前所有完整记录，并通过恢复状态、警告文本和 `lastValidOffset` 向调用方报告结果。恢复不会修改、截断或重写原始 `.dat` 文件，也不会把不完整 payload 暴露给上层。

以下情况仍会失败，不会被静默恢复：文件头不完整、magic 错误、不支持版本、非法 header size、非法 marker、source ID 不匹配、record type 无效、payload size 超过上限、TCP 子 payload 长度不一致、文件中部损坏或任何真实 I/O 错误。这样可以区分“最后一条记录写到一半”和“文件中部格式已经损坏”。

## 与 CSV 对齐

CSV 中的 `record_timestamp_us` 和 raw DAT 记录头中的 `host_timestamp_us` 使用同一主机 UTC 微秒时间基准，可用于离线对齐。CSV 保留解析后的摘要字段，raw DAT 保留原始帧字节。CSV schema、缺失字段表示、时间戳/浮点格式和 CSV 转义规则来自 `SessionSensorCsv`，因此地面端和天空端生成相同的列名、列顺序、单位和公共格式。

历史路径兼容映射为：`raw/epsilon.dat` -> `raw/navigation.dat`、
`raw/ptb.dat` -> `raw/pressure.dat`、`raw/hmp.dat` ->
`raw/temperature_humidity.dat`、`raw/lidar.dat` -> `raw/distance.dat`、
`raw/tcp_wave.dat` -> `raw/waveform.dat`、`raw/rd105.dat` ->
`raw/laser_temperature_controller.dat`、`raw/ai8.dat` /
`raw/ai8288.dat` -> `raw/system_temperature_controller.dat`。Reader 优先使用 manifest
声明的路径，其次使用新默认路径，最后回退到旧路径；该回退不会重命名或修改历史文件。
