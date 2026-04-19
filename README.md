# VaporView

`VaporView` 是一个基于 Qt Widgets 的导航与环境监控桌面程序，用于统一接入多类串口设备并实时展示、记录数据。

当前版本支持以下设备类型：

- EPSILON：组合导航一体机，解析 `FDILink`
- PTB210：气压计
- HMP3：温湿度传感器
- TF03 / TFA1500-L：激光测距模块

## 功能特性

- 多设备串口接入与端口刷新
- 多路实时数据显示与频率统计
- 设备独立采样率设置与统一频率联动
- 本地 TCP 波形监视，支持按 LabVIEW 参考协议读取 `127.0.0.1:8888` 的波形图1/波形图4数据
- 中英文界面切换
- 连接会话自动记录，按可配置频率聚合写入 CSV
- `session_*/sensors/epsilon_raw.dat` 已校验 EPSILON `FDILink` 原始帧归档
- RTK NTRIP 配置对话框，内置 RTKLIB 流服务实现 RTCM 转发
- 实时日志面板与连接状态提示

## 记录格式说明

EPSILON 原始记录格式说明见：

- [epsilon_raw_dat_format.md](D:/Project/GPS/VaporView/docs/epsilon_raw_dat_format.md)

## 项目结构

```text
VaporView/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── MainWindow.h
│   ├── RtkConfigDialog.h
│   ├── RtkStreamService.h
│   ├── data_collector.h
│   ├── data_types.h
│   └── serial_port.h
├── src/
│   ├── MainWindow.cpp
│   ├── RtkConfigDialog.cpp
│   ├── RtkStreamService.cpp
│   ├── data_collector.cpp
│   ├── main.cpp
│   └── serial_port.cpp
├── resources/
│   ├── app.ico
│   ├── app.rc
│   ├── combo_arrow_down.xpm
│   ├── combo_arrow_up.xpm
│   └── modern_style.qss
├── third_party/
│   ├── rtklib/
│   │   ├── LICENSE.txt
│   │   └── src/
│   ├── um982_driver/           # legacy reference
│   └── hipnuc_driver/          # legacy reference
└── python/
    ├── __init__.py
    ├── config_manager.py
    ├── data_exporter.py
    └── file_logger.py
```

## 第三方源码

仓库已包含构建所需的第三方源码：

- `third_party/rtklib`：RTKLIB `strsvr` 相关流服务实现
- `third_party/um982_driver`：旧 UM982 代码，当前仅保留作历史参考
- `third_party/hipnuc_driver`：旧 HiPNUC 代码，当前仅保留作历史参考

## 依赖要求

- CMake 3.16+
- 支持 C++17 的编译器
- Qt 6
  - `Core`
  - `Widgets`
  - `SerialPort`
  - `Network`

## 构建

### 通用 CMake

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/<toolchain>
cmake --build build
```

### Windows

项目当前按 Qt 6 构建，已在 Windows + MSVC 2022 + Qt 6.8.3 环境下验证可构建。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/software/QT6/6.8.3/msvc2022_64
cmake --build build --config Release
```

生成的可执行文件为：

```text
build/Release/VaporView.exe
```

### Linux

项目保留了 GCC / Clang 路径，串口层使用 `termios`。如需在 Linux 上构建，需确保 Qt 开发环境可用。

## 运行架构

### 主窗口

`src/MainWindow.cpp` 负责：

- 菜单栏、工具栏、状态栏
- 串口与频率配置
- 多设备数据显示面板
- 日志显示、自动记录、语言切换与全屏

### 串口层

`src/serial_port.cpp` 提供仓库自带的跨平台串口封装：

- Windows：Win32 `CreateFileA / ReadFile / WriteFile`
- Linux：`termios`

Qt SerialPort 当前主要用于枚举可用串口，实际数据读写由仓库内串口封装完成。

### 采集层

`src/data_collector.cpp` 实现了各设备对应的采集线程：

- `EpsilonCollector`
- `PtbCollector`
- `HmpCollector`
- `LidarCollector`

基类 `DataCollector` 负责串口启停、线程管理、频率统计与数据回调。

### TCP 波形监视

项目当前额外提供了一条独立于串口采集链的 TCP 波形监视能力：

- GUI 中新增了 `TCP Wave Monitor / TCP波形监视` 面板
- 默认目标地址为 `127.0.0.1:8888`
- 按参考 LabVIEW 逻辑读取两段波形数据：
  - `4字节 little-endian 长度 + 波形图1 float32 数组`
  - `4字节 little-endian 长度 + 波形图4 float32 数组`

### RTK 对话框

`src/RtkConfigDialog.cpp` 通过仓库内 RTKLIB 封装与 Qt 网络模块实现 RTK 辅助功能：

- `src/RtkStreamService.cpp`：基于 vendored RTKLIB `strsvr` API 实现 NTRIP 到串口转发
- `QNetworkAccessManager`：连通性测试与挂载点列表获取

## 设备支持

### EPSILON

- 协议：`FDILink`
- 默认串口参数：`921600 N81`
- 连接检测：扫描有效 `FDILink` 帧并校验 `CRC8 / CRC16`
- 设备配置命令：`#fconfig`、`#fmsg`、`#fsave`、`#freboot`

当前运行时会自动维护以下输出报文：

- 高频：`0x40 IMU`、`0x41 AHRS`、`0x42 INS/GPS`、`0x50 SYS_STATE`
- 低频：`0x59 RAW_GNSS`、`0x5A SATELLITE`、`0x5C GEODETIC_POS`、`0x5D ECEF_POS`

当前支持的 UI 采样率：

- `20, 50, 100, 200 Hz`

说明：

- 高频报文按 UI 频率下发
- 低频报文固定为 `min(UI频率, 20 Hz)`
- 程序仅在检测到 `#fmsg` 配置不匹配时才写入并保存设备配置

### PTB210

- 串口参数：`E71`
- 检测命令：`.P\r`
- 连续输出命令：`.BP\r`
- 设备采样率命令：`.MPM.<value>\r`

说明：

- 代码中设备采样率支持范围为 `1-70 Hz`
- UI 高频输入高于 `70 Hz` 时不会在设备侧生效

### HMP3

- 串口参数：`N82`
- 协议：Modbus RTU
- 从站地址：`240`
- 读取寄存器：`0x0000` 起连续 4 个寄存器

说明：

- 当前未实现设备侧采样率配置
- UI 中的频率调整本质上是主机轮询频率调整

### TF03

- 串口参数：`115200 N81`
- 协议：UART 连续输出
- 帧头：`0x59 0x59`
- 数据帧长度：9 字节
- 距离字段：第 3-4 字节，小端，单位厘米
- 信号强度字段：第 5-6 字节，小端
- 校验：前 8 字节求和低 8 位
- 设备采样率命令：`5A 06 03 LL HH SU`

说明：

- 距离以米为单位显示和记录
- 当前实现限制为 `1-100 Hz`

## 会话记录

GUI 现在使用手动控制的会话记录策略：

- 点击“连接”后，程序只建立设备连接，不会自动开始录制
- 工具栏提供 `开始记录`、`暂停记录`、`结束记录` 三个按钮
- “数据 -> 记录频率” 菜单可分别配置 `波形`、`EPSILON原始帧`、`其余设备` 三类记录策略
- 只要至少有一个串口设备在线，或者 TCP 波形链路已经连接成功，就可以点击 `开始记录`
- `开始记录` 会创建新的 `session_*` 目录；如果当前 session 只是暂停，则会继续写入同一个 session
- `暂停记录` 会同时暂停 `devices.csv` 和波形 `.dat` 的写入，但不会结束当前 session
- `结束记录` 会统一结束 CSV 和波形写盘，并关闭本次 session
- “数据 -> 数据查看器...” 可以打开额外的数据查看窗口，用来读取 `devices.csv` 和 `waveform/*.dat`
- 数据查看器提供“清空页面”按钮，可随时清掉当前加载的内容而不影响原始文件
- 点击“断开”或连接流程失败时，如果当前还有未结束的 session，程序会自动结束它

保存结构如下：

```text
data/
└─ session_2026-04-01_18-00-00/
   ├─ session.json
   ├─ waveform/
   │  ├─ waveform_2026-04-01_18-00-00.dat
   │  ├─ waveform_2026-04-01_18-01-00.dat
   │  └─ ...
   ├─ sensors/
   │  └─ devices.csv
   ├─ logs/
   │  ├─ event_log.csv
   │  └─ error_log.txt
   └─ config/
      └─ device_config.json
```

目录与文件说明：

- `session.json`：本次 session 的索引文件，记录开始时间、波形参数、时间戳单位、分文件策略和各输出路径
- `waveform/`：保存 TCP 面板“归一化二次谐波”的二进制波形文件
- `sensors/devices.csv`：保存 EPSILON、温湿度、气压、TF03 的低速状态快照
- `logs/event_log.csv`：保存开始采集、停止采集、重连、告警等事件
- `logs/error_log.txt`：保存写盘失败、设备异常、断连等错误信息
- `config/device_config.json`：保存本次采集使用的串口、TCP、采样率和分段配置快照

记录写入策略：

- 默认保存根目录为项目根目录下的 `data/`
- 可通过 GUI 菜单手动选择其他记录根目录，程序会记住上次选择
- `devices.csv` 由独立记录线程按“其余设备”记录频率写入，每行记录当前时刻的最新有效设备快照
- `epsilon_raw.dat` 固定保存完整且已校验的 `FDILink` 原始帧
- TCP 面板中的“归一化二次谐波”默认按“收到一帧存一帧”导出到 `.dat`，也可以通过“波形”记录频率菜单限频保存
- 每帧 `.dat` 数据格式固定为 `uint64 时间戳 + 50000 个 float32`
- 波形写盘采用“采集回调 + 缓冲队列 + 独立写盘线程”结构
- 波形分文件时长可在 GUI 中配置为 `1` 到 `5` 分钟，文件名采用本地时间戳，便于回查
- 数据查看器会按需读取 `.dat` 波形帧，并自动高亮时间最接近的 `devices.csv` 行
- 主窗口和数据查看器在空间不足时会显示滚动条，而不是直接遮挡控件
- 文本日志和设备 CSV 每次写入后都会及时 `flush`，优先保证异常退出时数据尽量落盘

## Python 目录说明

`python/` 目录提供辅助脚本与工具模块：

- `config_manager.py`：配置读写
- `file_logger.py`：日志与 CSV 落盘
- `data_exporter.py`：CSV / JSON / KML 导出

这些模块当前更接近“辅助工具代码”，不是桌面程序运行时正在使用的核心组件。

更具体地说：

- `VaporView.exe` 的主界面、串口连接、设备采集、实时显示、日志刷新和导出流程，当前都由 C++ / Qt 代码实现
- `python/` 目录下的模块目前没有被主窗口或采集线程直接导入、调用或嵌入执行
- 因此，修改这些 Python 文件不会直接改变当前 GUI 的运行行为，除非后续专门把它们接入主程序

对使用者的直接影响是：

- 只运行桌面程序时，可以先忽略 `python/` 目录
- 如果后续需要做脚本化处理、批量导出、离线分析或工具脚本，这个目录可以作为现成基础继续扩展
- 当前 README 中提到的导出、日志等主功能，不是由这些 Python 模块驱动的，而是由 C++ 代码独立完成的

## Python 绑定状态

CMake 中仍然保留了 `BUILD_PYTHON_BINDINGS` 选项，目的是未来通过 `pybind11` 把部分 C++ 能力导出给 Python 调用。

当前仓库没有 `python/bindings.cpp` 这个真实入口文件，因此这条构建链现在被显式标记为“不可用”：

- 默认构建桌面程序时，这一项不会影响正常使用
- 如果手动打开 `BUILD_PYTHON_BINDINGS=ON`，CMake 会在配置阶段直接失败，并明确提示缺少 `python/bindings.cpp`
- 这部分不影响当前 GUI 功能，只表示“Python 调用 C++ 核心能力”这条扩展路线尚未实现

## 默认串口参数

### Windows 默认值

- EPSILON：`COM3 @ 921600 N81`
- PTB210：`COM5 @ 9600 E71`
- HMP3：`COM6 @ 19200 N82`
- TF03：`COM7 @ 115200 N81`

### 非 Windows 默认值

- EPSILON：`/dev/ttyEPSILON`
- PTB210：`/dev/ttyBARO`
- HMP3：`/dev/ttyHMP`
- TF03：`/dev/ttyTF03`

## 已知限制

- `BUILD_PYTHON_BINDINGS` 当前显式禁用；若开启会在配置阶段直接报错提示缺少 `python/bindings.cpp`
- Python 辅助模块尚未接入 GUI 主流程
- RTK 对话框依赖 Qt Network 模块

## 许可证

仓库当前未包含顶层项目许可证文件。`third_party/rtklib` 已随代码附带上游 `LICENSE.txt`。

对外分发前，建议为 `VaporView` 主项目补充单独的许可证文件，并确认 `third_party/um982_driver`、`third_party/hipnuc_driver` 与 `third_party/rtklib` 的许可证边界。

