# VaporView Session SDK 1.1

外部开发者可以用普通 C、C++ 或 Python 读取已完成的机载和地面 Session。Windows x64 开发包自带 SDK 和 Qt Core 运行库。使用标准 C 接口或 Python 封装时，开发机无需安装 Qt。已使用 Qt 的 C++17 程序可继续调用原有的 `Reader`。操作人员可直接在上位机的 Session 查看器导出数据。

## 普通 C 或 C++ 工程

解压 Windows x64 开发包到任意目录，例如 `C:/SessionSdk`。用 x64 编译器打开开发终端，以下命令可同时编译包内的 C 和 C++ 示例。Windows 上需有匹配的 Microsoft Visual C++ x64 运行库。[微软运行库下载](https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist)

```powershell
cmake -S C:/SessionSdk/share/VaporViewSessionSdk/example_c -B C:/SessionExample -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/SessionSdk
cmake --build C:/SessionExample
$env:Path = "C:/SessionSdk/bin;" + $env:Path
C:/SessionExample/vaporview_session_c_example.exe D:/Recordings/Run001 list
C:/SessionExample/vaporview_session_cpp_example.exe D:/Recordings/Run001 list
```

在自己的项目中包含 `<vaporview/SessionSdkC.h>`。CMake 仅需以下两行。直接用 MSVC 时，将包的 `include` 加入包含路径，将 `lib/vaporview_session_sdk.lib` 加入链接输入。部署应用时，可以将包内两个 DLL 放在可执行文件旁边。

```cmake
find_package(VaporViewSessionSdk 1.1 CONFIG REQUIRED COMPONENTS C)
target_link_libraries(my_program PRIVATE VaporView::SessionSdkC)
```

```powershell
C:/SessionExample/vaporview_session_c_example.exe D:/Recordings/Run001 query sensors 1770000000000123 1770000000000999
C:/SessionExample/vaporview_session_c_example.exe D:/Recordings/Run001 frame 0
New-Item -ItemType Directory -Force D:/Review/Run001
C:/SessionExample/vaporview_session_c_example.exe D:/Recordings/Run001 export waveforms D:/Review/Run001 0 18446744073709551615
```

标准 C ABI 当前为版本 1。路径和 JSON 为以 NUL 结尾的 UTF-8，时间戳和序号使用 64 位无符号整数。同步遍历回调返回 0 可提前结束；记录数统计已返回非零的回调。借用的记录仅在当前回调期间有效。元数据、类别和消息字符串用 `vv_session_free_string` 释放，波形分量指针在 `vv_session_free_waveform` 前有效。相同 Session 句柄上的调用须串行，回调内操作其他独立句柄即可。即使状态为成功，也要检查返回的警告字符串。

## Python

使用与 DLL 同架构的 Python 3.8 或以上版本。将包内的 `python` 加入导入路径即可，无需 pip 安装依赖。模块会从开发包内定位动态库。

```powershell
$env:PYTHONPATH = "C:/SessionSdk/python"
python C:/SessionSdk/share/VaporViewSessionSdk/example_python/main.py D:/Recordings/Run001 list
python C:/SessionSdk/share/VaporViewSessionSdk/example_python/main.py D:/Recordings/Run001 query sensors --limit 1
```

```python
from vaporview_session import Session

with Session(r"D:\Recordings\Run001") as session:
    print(session.categories().value)
    result = session.read_waveform(0)
    if result.warning:
        print(result.warning)
    print(result.value.record.timestamp_us, len(result.value.raw), len(result.value.h1))
```

`visit` 在指定的闭区间内逐条调用 Python 函数。回调返回 `True` 继续，返回 `False` 停止。Python 记录及波形采样已复制，可在关闭 Session 后继续使用。`SessionError` 提供状态、消息及已处理记录数。`read_waveform`、`export` 等成功结果包含 `value` 与 `warning`；遍历的结果包含 `records`、`cancelled` 与 `message`。打开时的警告可从 `session.open_warning` 获取。

## 现有 Qt 工程及打包

原有 `find_package(VaporViewSessionSdk 1.0 CONFIG REQUIRED)` 和 `VaporView::SessionSdk` 接法可继续用于有匹配 Qt 6 Core 的 C++17 环境。新项目也可以显式请求 `COMPONENTS Qt`。Qt 示例位于 `share/VaporViewSessionSdk/example`。

维护者可用 `cmake --install build/Release --config Release --component VaporViewSessionSdk --prefix <空目录>` 从 Release 构建组包，将该目录的内容压缩为 ZIP。Windows 组件包含与本次构建匹配的 Qt Core 动态库和许可材料。组包机需要相同版本 Qt Core 的 HTML 文档，另行安装时可用 `VAPORVIEW_QTCORE_DOC_DIR` 指定含有 `qtcore-attribution-*.html` 的目录。随包的 `share/VaporViewSessionSdk/licenses/QtRuntimeNotice.md` 记录对应 Qt 源码的获取与分发方式。

## 读取及导出约定

SDK 按实际保存的文件发现类别。机载完整采集通常同时保存设备汇总、原始设备 DAT、TCP 波形或 FPGA 成对波形及周期特征。地面 Session 的类别取决于当次连接、录制选项与接收的数据。预先创建的空数据流可以列出并返回零条。

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
