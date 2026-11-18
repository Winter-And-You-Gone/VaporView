# VaporView Session SDK 1.0

SDK 提供 C++17 只读接口，适用于已完成的机载或地面 Session。程序按 Session 实际保存的文件发现数据类别。机载完整采集通常同时保存设备汇总、原始设备 DAT、TCP 波形或 FPGA 成对波形及周期特征。地面 Session 保存的类别取决于当次连接、录制选项与接收的数据。未保存的流在 `categories()` 中缺席，预先创建的零记录流可能存在并返回零条。SDK 不补录缺失的测量结果。

## 安装与独立调用

从已完成的 Release 构建中安装 SDK 组件。

```powershell
cmake --install X:/Project/GPS/VaporView/build/Release --config Release --component VaporViewSessionSdk --prefix C:/SessionSdk
cmake -S C:/SessionSdk/share/VaporViewSessionSdk/example -B C:/SessionExample -DCMAKE_PREFIX_PATH="C:/SessionSdk;F:/Qt/6.10.1/msvc2022_64"
cmake --build C:/SessionExample --config Release
$env:Path = "C:/SessionSdk/bin;F:/Qt/6.10.1/msvc2022_64/bin;" + $env:Path
C:/SessionExample/Release/vaporview_session_example.exe D:/Recordings/Run001 list
```

示例路径中的 Qt 目录请按安装环境替换。Windows 使用和 SDK 一致的位数、编译器 ABI、运行库和配置。SDK 动态链接 Qt 6 Core，要求和构建 SDK 的 Qt 兼容。运行时需能加载 SDK 和 Qt6Core 的动态库及其运行库依赖。组件包含公共头、动态库、导入库（Windows）、CMake 包和示例源码，Qt 由使用方提供。单配置生成器通常把示例直接输出到构建目录。

```cmake
find_package(VaporViewSessionSdk 1.0 CONFIG REQUIRED)
target_link_libraries(my_program PRIVATE VaporView::SessionSdk)
```

```powershell
vaporview_session_example D:/Recordings/Run001 query sensors 1770000000000123 1770000000000999
vaporview_session_example D:/Recordings/Run001 frame 0
New-Item -ItemType Directory -Force D:/Review/Run001
vaporview_session_example D:/Recordings/Run001 export waveforms D:/Review/Run001
```

## 调用约定

- 包含 `<vaporview/SessionSdk.h>`。用 `Reader::open(directory)` 打开独立 Session 目录。无清单的历史 Session 用可识别的源文件和历史别名定位。清单采用允许列表返回录制名称、录制侧、状态、起止 UTC、格式版本、声明计数及记录模式和频率。
- `categories()` 列出文件存在的数据类别。用 `visit(category, range, visitor)` 遍历指定类别，时间范围两端均包含。顺序按该源存储顺序，同一时间戳的各条记录保留。当前统一波形包含多个通道时，按 DAT 的记录顺序遍历。历史 VLP1 波形按通道分组，区组内保留各通道记录顺序。
- `Record::ordinal` 是未筛选类别内从零开始的记录序号，`sequence` 是源实际保存的序号，部分旧文件没有此字段。`timestampUs` 使用源记录的主时间字段。其它设备时间以及单位和质量字段保留在 `fields`。
- `readWaveform(ordinal, frame)` 按序号读取一个波形，返回 FPGA 的 H1 和 H2、TCP 的 RAW 和 harmonic，或历史流实际保留的分量。当前统一 DAT 建立偏移索引后只对所选帧读取采样数据。旧 VLP1 录制使用现有回放解析器建立临时缓存，缓存位于临时目录，在 Reader 析构时删除。
- `exportTo(category, range, directory)` 在指定的现有目录写一个文件。输出路径必须位于源 Session 外，已存在的同名文件会被拒绝。每个文件原子提交。返回的 `records` 统计逻辑记录数，波形采样块不单独计数。
- `Result::status` 区分缺流、参数无效、损坏、I/O 错误和访客停止。可恢复的 DAT 截断尾部返回已验证前缀和 `message` 警告。该警告须随结果留存，以便对账时识别部分记录。请在采集结束后使用保持静止的 Session 副本。

## 字段、单位和上位机导出

| 类别 | Session 来源与保留字段 | 时间、单位及缺失值 | 导出文件及上位机入口 |
| --- | --- | --- | --- |
| Session 元信息 | `session.json` 的公开字段，计数为清单声明值 | 起止为 ISO 8601 UTC，缺失时为空或省略 | SDK `metadata()`、示例 `list`，上位机会话概览 |
| `sensors` | `sensors/sensor_summary.csv`，旧版 `sensors/devices.csv`，保留源列。导航经纬、高度、位置、速度、姿态及 GNSS 状态，温湿度、气压、测距和设备有效性 | 首选 `record_timestamp_us`。旧版用 EPSILON 接收或 RTK 时间。温度 °C、湿度 %RH、气压 hPa、测距 m、经纬度 °，历史列按其名称保留原单位，例如 `baro_pa`。CSV 空字段表示缺失 | `sensors.csv`。Session 查看器 → 导出 Session 数据 → 设备与导航汇总。可用现有地图轨迹 CSV 单独取得展示用轨迹 |
| `navigation_raw`、`pressure_raw`、`temperature_humidity_raw`、`distance_raw` | `raw/navigation.dat`、`pressure.dat`、`temperature_humidity.dat`、`distance.dat` 及别名。保留 DAT 源 ID、记录类型、标志、序号、时间戳、负载长度和原协议负载 | DAT 主机接收时间（Unix µs）。负载按各设备协议解释，SDK 以 Base64 表达原字节。该组支持统一 RAW DAT v1/v2 | 各自的 `.jsonl`。Session 查看器 → 导出 Session 数据 → 相应原始流 |
| `waveforms` | `raw/waveform.dat`、历史 `raw/tcp_wave.dat`、相容的历史波形索引/分段或 `raw/fpga_vlp1.dat`。TCP 保留 RAW 与谐波存储分量。当前 FPGA 保留通道、周期、段、FPGA tick、点数、采样率、质量标志和同一点的 H1/H2 | DAT 主机接收时间（Unix µs）。历史索引取主机时间或其 EPSILON 时间列。TCP 保持采集端原数值单位。当前 FPGA 保持原 int32 或 float64 幅值，无效周期保留头且采样为空。历史 VLP1 按已有查看器的 float32 路径恢复，序号等缺失元信息留空 | `waveforms.jsonl`，每个帧头后跟按分量组织的采样块。Session 查看器 → 导出 Session 数据 → 波形采样数据 |
| `cycle_features` | `sensors/waveform_features.csv`，保留源列。当前 FPGA 周期行含通道、周期、原始点数、峰值、峰索引、平均值、RMS、质量标志、算法版本、tick 与波形序号。历史文件可能是 TCP 波形特征，仅保留其实际记录的列 | `host_time_us` 为 Unix µs。FPGA 的 H2/H1 无量纲。历史 TCP 数值沿用原波形单位。当前 FPGA 周期的空峰值对应无有效采样。`quality_flags` 保留源值，当前 FPGA 位掩码 1 参考无效、2 无有效点、4 周期不完整、8 溢出、16 连续性错误 | `cycle_features.csv`。Session 查看器 → 导出 Session 数据 → 周期特征与质量标志 |
| `tcp_peaks` | `raw/waveform_peaks.csv`，旧版 `raw/tcp_wave_peaks.csv` | `host_time_us` 为 Unix µs，峰值为原波形数值单位，列保留索引及搜索区间 | `tcp_peaks.csv`。Session 查看器 → 导出 Session 数据 → TCP 峰值索引 |
| `concentrations` | `calibration_snapshot.json` 和 `calibration_concentrations.jsonl`。保存原始结果和参数快照、参数标识、通道、波长、峰值、有效点、单位、质量和状态 | `host_time_us` 为 Unix µs。单位按当次参数的 `unit`，无效浓度为 JSON null。初始浓度保留 `compensated=false` 和未温压补偿状态 | `concentrations.json`，沿用历史浓度包格式及摘要。Session 查看器 → 查看本 Session 定标参数与初始浓度 → 导出全部浓度和原始参数 |

CSV 字段保留源文本精度，重写 CSV 时统一用 UTF-8、逗号分隔和 LF 换行。JSONL 的时间、序号及长整型 tick/段编号使用十进制字符串，避免 JSON 使用方将其读成精度不足的双精度数。波形块包含 `ordinal`、`component`、从零开始的 `first_point` 和 `samples` 数组。有限值以可往返的 JSON 数字写出，非有限值以 null 表示。

## A.18 对账

固定样本测试 `session_sdk_test` 从独立手写的 CSV 和二进制记录组装输入。它核对历史别名、可选流缺失、相同时间戳、64 位序号、30 万点 H1/H2 帧、零采样坏周期、气压负载、逐项导出、尾部截断与坏波形头。现场对账应使用一次完成采集后复制的 Session，记录软件与 SDK 版本、Session 标识、所选类别、时间范围、源记录数与输出记录数。按源 CSV、DAT 记录头及代表采样点、历史浓度行核对时间、序号、字段、单位和质量标志，记录差异及 A.18 记录编号。
