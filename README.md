# VaporView

`VaporView` 是一个基于 Qt Widgets 的导航与环境监控桌面程序，用于统一接入多类串口设备并实时展示、导出数据。

当前版本支持以下设备类型：

- GNSS / RTK：UM982，解析 `PVTSLN`
- IMU：HiPNUC，解析 `HI81 / HI83 / HI91`
- PTB210：气压计
- HMP3：温湿度传感器
- TF03：激光测距模块

注意：仓库目录名为 `VaporView`，工程目标名、命名空间和可执行文件名当前仍为 `VaproView`。

## 功能特性

- 多设备串口接入与端口刷新
- 多路实时数据显示与频率统计
- 设备独立采样率设置与统一频率联动
- 中英文界面切换
- 当前数据快照导出为 CSV 或 JSON
- RTK NTRIP 配置对话框，可通过外部 `str2str` 启动 RTCM 转发
- 实时日志面板与连接状态提示

## 项目结构

```text
VaporView/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── MainWindow.h
│   ├── RtkConfigDialog.h
│   ├── data_collector.h
│   ├── data_types.h
│   └── serial_port.h
├── src/
│   ├── MainWindow.cpp
│   ├── RtkConfigDialog.cpp
│   ├── data_collector.cpp
│   ├── main.cpp
│   └── serial_port.cpp
├── resources/
│   ├── app.ico
│   ├── app.rc
│   ├── combo_arrow_down.xpm
│   ├── combo_arrow_up.xpm
│   └── modern_style.qss
└── python/
    ├── __init__.py
    ├── config_manager.py
    ├── data_exporter.py
    └── file_logger.py
```

## 外部依赖源码

仓库当前依赖上一级目录中的协议驱动源码，不是单仓自包含构建：

```text
../um982_driver/
├── pvtsln_data.hpp
└── pvtsln_parser.cpp

../products-master/drivers/
├── hipnuc_dec.h
└── hipnuc_dec.c
```

缺少以上目录时，CMake 配置无法完成。

## 依赖要求

- CMake 3.16+
- 支持 C++17 的编译器
- Qt 5 或 Qt 6
  - `Core`
  - `Widgets`
  - `SerialPort`
- RTK 功能所需的外部命令
  - `str2str`
  - `curl`

## 构建

### 通用 CMake

```bash
cmake -S . -B build
cmake --build build
```

### Windows

项目已在 Windows + MSVC 2019 环境下验证可构建。

```powershell
cmake -S . -B build-win
cmake --build build-win --config Release
```

生成的可执行文件为：

```text
build-win/Release/VaproView.exe
```

### Linux

项目保留了 GCC / Clang 路径，串口层使用 `termios`。如需在 Linux 上构建，需确保 Qt、外部驱动源码及 `str2str` / `curl` 可用。

## 运行架构

### 主窗口

`src/MainWindow.cpp` 负责：

- 菜单栏、工具栏、状态栏
- 串口与频率配置
- 多设备数据显示面板
- 日志显示、导出、语言切换与全屏

### 串口层

`src/serial_port.cpp` 提供仓库自带的跨平台串口封装：

- Windows：Win32 `CreateFileA / ReadFile / WriteFile`
- Linux：`termios`

Qt SerialPort 当前主要用于枚举可用串口，实际数据读写由仓库内串口封装完成。

### 采集层

`src/data_collector.cpp` 实现了各设备对应的采集线程：

- `GnssCollector`
- `ImuCollector`
- `PtbCollector`
- `HmpCollector`
- `LidarCollector`

基类 `DataCollector` 负责串口启停、线程管理、频率统计与数据回调。

### RTK 对话框

`src/RtkConfigDialog.cpp` 通过 `QProcess` 调用外部工具实现 RTK 辅助功能：

- `str2str`：NTRIP 到串口转发
- `curl`：连通性测试与挂载点列表获取

## 设备支持

### GNSS / RTK

- 协议：`PVTSLN`
- 解析器：`../um982_driver/pvtsln_parser.cpp`
- 连接检测：等待串口流中出现 `$` 或 `#`
- 设备采样率命令：`PVTSLNA COM3 <interval>`

说明：

- UI 允许输入 `1-500 Hz`
- 设备侧实际限制为 `1-20 Hz`

### IMU

- 协议：`HI81 / HI83 / HI91`
- 解析器：`../products-master/drivers/hipnuc_dec.c`
- 连接检测：调用 `hipnuc_input()` 直到解出有效包
- 设备采样率命令：`LOG HI91 ONTIME <period>`

当前支持的设备采样率：

- `1, 2, 5, 10, 20, 50, 100, 200, 500 Hz`

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

- 距离以米为单位显示和导出
- 当前实现限制为 `1-100 Hz`

## 导出能力

GUI 当前导出的是内存中的最新一帧快照，而非持续录制数据流。

支持格式：

- CSV
- JSON

当前不支持：

- 连续轨迹导出
- 自动滚动保存
- GUI 中直接导出 KML

## Python 目录说明

`python/` 目录提供辅助脚本与工具模块：

- `config_manager.py`：配置读写
- `file_logger.py`：日志与 CSV 落盘
- `data_exporter.py`：CSV / JSON / KML 导出

当前 GUI 主流程未直接调用这些 Python 模块。

## Python 绑定状态

CMake 中保留了 `BUILD_PYTHON_BINDINGS` 选项，但仓库内目前不存在 `python/bindings.cpp`，因此该选项尚未处于可用状态。

## 默认串口参数

### Windows 默认值

- GNSS：`COM3 @ 115200 N81`
- IMU：`COM4 @ 115200 N81`
- PTB210：`COM5 @ 9600 E71`
- HMP3：`COM6 @ 19200 N82`
- TF03：`COM7 @ 115200 N81`

### 非 Windows 默认值

- GNSS：`/dev/ttyCOM3`
- IMU：`/dev/ttyIMU`
- PTB210：`/dev/ttyBARO`
- HMP3：`/dev/ttyHMP`
- TF03：`/dev/ttyTF03`

## 已知限制

- 工程目标名与仓库名不一致：`VaproView` vs `VaporView`
- 依赖上一级目录源码，仓库不是单仓自包含构建
- `BUILD_PYTHON_BINDINGS` 当前不完整
- GUI 导出仅为单次快照，不是持续记录
- Python 辅助模块尚未接入 GUI 主流程
- RTK 相关功能依赖外部 `str2str` 与 `curl`

## 许可证

当前仓库中未包含单独的 `LICENSE` 文件。对外分发前，建议补充正式许可证文件，并确认外部依赖的许可证边界。
