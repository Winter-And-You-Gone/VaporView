# VaporView

一个基于 Qt Widgets 的导航监控桌面程序，用于同时接入并展示 5 类串口设备数据:

- GNSS / RTK: UM982，解析 `PVTSLN`
- IMU: HiPNUC，解析 `HI81 / HI83 / HI91`
- PTB210: 气压计，主动轮询压力值
- HMP3: 温湿度传感器，基于 Modbus RTU 轮询
- TF03: 激光测距模块，解析连续 UART 9 字节数据帧

注意:

- 仓库目录名是 `VaporView`
- 代码中的工程名、命名空间、可执行文件名实际写成了 `VaproView`

下文以“仓库名 / 目录名”为 `VaporView`、以“程序目标名”为 `VaproView` 说明。

## 我确认过的项目事实

- 主程序是 C++17 + Qt5/Qt6 的桌面 GUI，不是 Web 项目
- 采集逻辑在仓库内，GNSS/IMU 协议解析依赖上一级目录中的外部驱动源码
- GUI 当前只导出“当前一帧快照”到 CSV/JSON，不做持续录制
- 仓库里有 `python/` 辅助模块，但当前 GUI 代码没有直接调用它们
- `BUILD_PYTHON_BINDINGS` 选项目前不完整，因为仓库内没有 `python/bindings.cpp`

## 功能概览

- 多设备串口连接和自动扫描可用端口
- GNSS、IMU、环境与测距三栏实时显示
- 每类设备独立采样率设置，以及统一频率联动设置
- 中英文界面切换
- F11 全屏显示
- 当前数据导出为 CSV 或 JSON
- RTK NTRIP 配置对话框，可启动外部 `str2str`
- 日志面板实时显示连接过程和设备状态

## 实际模块结构

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
│   ├── main.cpp
│   ├── MainWindow.cpp
│   ├── RtkConfigDialog.cpp
│   ├── data_collector.cpp
│   └── serial_port.cpp
├── resources/
│   ├── app.ico
│   ├── app.rc
│   └── modern_style.qss
└── python/
    ├── __init__.py
    ├── config_manager.py
    ├── data_exporter.py
    └── file_logger.py
```

仓库外部的必需依赖源码:

```text
../um982_driver/
├── pvtsln_data.hpp
└── pvtsln_parser.cpp

../products-master/drivers/
├── hipnuc_dec.h
└── hipnuc_dec.c
```

也就是说，这个仓库当前不是“单仓可独立构建”的状态。

## 运行架构

### 1. 主窗口

`src/MainWindow.cpp` 负责:

- 菜单栏、工具栏、状态栏
- 串口参数和采样率配置面板
- GNSS / IMU / 环境数据显示面板
- 日志显示
- 连接、断开、导出、刷新端口、语言切换、全屏

界面布局是左侧主内容 + 右侧日志，不是 README 里原先画的“底部日志全宽布局”。

### 2. 串口层

`src/serial_port.cpp` 提供了一个仓库自带的跨平台串口封装:

- Windows: Win32 `CreateFileA / ReadFile / WriteFile`
- Linux: `termios`

这意味着项目虽然链接了 `Qt::SerialPort` 用来枚举串口，但实际读写并不是用 `QSerialPort` 完成的。

### 3. 采集层

`src/data_collector.cpp` 中每类设备各自运行在线程里:

- `GnssCollector`
- `ImuCollector`
- `PtbCollector`
- `HmpCollector`
- `LidarCollector`

通用基类 `DataCollector` 负责:

- 打开串口
- 启停线程
- 数据回调
- 频率统计
- 主机侧限流输出

### 4. RTK 对话框

`src/RtkConfigDialog.cpp` 不是直接实现 NTRIP 协议，而是通过 `QProcess` 调用外部工具:

- 启动: `str2str`
- 连通性测试: `curl`

这部分依赖系统环境里已经可用这些命令。

## 各设备数据链路

### GNSS / RTK

- 协议: `PVTSLN`
- 解析器: `../um982_driver/pvtsln_parser.cpp`
- 连接检测: 等待串口流中出现 `$` 或 `#`
- 设备采样率命令: `PVTSLNA COM3 <interval>`

说明:

- UI 可输入 `1-500 Hz`
- 但设备侧 `setDeviceSampleRate()` 实际会限制到 `1-20 Hz`
- 因此 GNSS 的“500 Hz”是 UI 允许，不代表设备命令层支持

### IMU

- 协议: `HI81 / HI83 / HI91`
- 解析器: `../products-master/drivers/hipnuc_dec.c`
- 连接检测: 调用 `hipnuc_input()` 直到解出有效包
- 设备采样率命令: `LOG HI91 ONTIME <period>`

当前实现支持的设备采样率:

- `1, 2, 5, 10, 20, 50, 100, 200, 500 Hz`

### PTB210

- 串口参数: `E71`
- 检测命令: `.P\r`
- 轮询命令: `.P\r`
- 设备采样率命令: `.MPM.<value>\r` 后再 `.RESET\r`

说明:

- 代码里对 PTB210 的设备采样率支持范围是 `1-70 Hz`
- UI 同样允许填到 `500`，但高于 `70` 时设备设置不会成功

### HMP3

- 串口参数: `N82`
- 协议: Modbus RTU
- 从站地址: `240`
- 读取寄存器: `0x0000` 起连续 4 个寄存器

说明:

- HMP3 当前没有单独实现设备侧采样率配置
- UI 改变频率时，本质是调整主机轮询频率

### TF03

- 串口参数: `115200 N81`
- 协议: UART 连续输出
- 默认输出: 上电后自动输出 9 字节测距帧
- 帧头: `0x59 0x59`
- 距离字段: 第 3-4 字节，小端，单位厘米
- 信号强度字段: 第 5-6 字节，小端
- 校验: 前 8 字节求和低 8 位
- 连接检测: 等待收到合法 TF03 数据帧
- 设备采样率命令: `5A 06 03 LL HH SU`

说明:

- 当前实现将距离换算为米显示和导出
- UI 与设备命令层都限制为 `1-100 Hz`
- 当信号强度低于手册给出的有效阈值时，界面会显示为无效或无回波

## 导出能力

当前 GUI 的导出入口在 `MainWindow::onExportClicked()`，导出的是“当前内存中的最后一帧数据”。

支持:

- CSV
- JSON

不支持:

- 连续轨迹导出
- 自动滚动保存
- GUI 中直接使用 KML

`python/data_exporter.py` 虽然实现了 KML 导出，但当前 GUI 没有接入这部分逻辑。

## Python 目录的真实作用

`python/` 目录目前更像独立辅助脚本集合，而不是主程序运行时的一部分:

- `config_manager.py`: 配置读写
- `file_logger.py`: 日志和 CSV 数据落盘
- `data_exporter.py`: CSV / JSON / KML 导出

现状限制:

- 主 GUI 没有 import 或调用这些模块
- CMake 里的 `BUILD_PYTHON_BINDINGS` 选项引用了不存在的 `python/bindings.cpp`

所以如果要启用 Python 绑定，还需要补齐绑定源文件或重构构建脚本。

## 依赖

- CMake 3.16+
- C++17 编译器
- Qt5 或 Qt6:
  - `Core`
  - `Widgets`
  - `SerialPort`
- 上一级目录中的外部协议源码:
  - `../um982_driver`
  - `../products-master/drivers`
- 如果要使用 RTK NTRIP 对话框:
  - `str2str`
  - `curl`

## 构建

### 通用 CMake

```bash
cmake -S . -B build
cmake --build build
```

### 我当前验证到的状态

在当前工作区中:

- `cmake -S . -B build-test` 可以成功生成工程
- 外部依赖目录存在，CMake 能找到它们
- `cmake --build build-test --config Release` 已在 Windows + MSVC 2019 环境实际编译通过

这次能通过编译的前提是 `VaporView` 仓库自身补上了 MSVC 兼容层，处理了:

- 外部 `hipnuc_dec.h` 在 MSVC 下对 `__attribute__((__packed__))` 的兼容问题

也就是:

- 在 MSVC 下通过强制包含 `include/compiler_compat.h` 将 `__attribute__(...)` 兼容为空实现
- GCC / Clang 仍然保持原有编译路径

因此当前这套代码在不改变 Linux/GNU 路径的前提下，已经可以在 Windows + MSVC 下构建。

## 默认串口参数

### Windows 默认值

- GNSS: `COM3 @ 115200 N81`
- IMU: `COM4 @ 115200 N81`
- PTB210: `COM5 @ 9600 E71`
- HMP3: `COM6 @ 19200 N82`
- TF03: `COM7 @ 115200 N81`

### 非 Windows 默认值

- GNSS: `/dev/ttyCOM3`
- IMU: `/dev/ttyIMU`
- PTB210: `/dev/ttyBARO`
- HMP3: `/dev/ttyHMP`
- TF03: `/dev/ttyTF03`

## 已知限制

- 工程名与仓库名不一致: `VaporView` vs `VaproView`
- 依赖上一级目录源码，仓库不能独立构建
- README 原先提到的 `third_party/` 目录实际上不存在
- `BUILD_PYTHON_BINDINGS` 选项当前不完整
- GUI 导出仅为单次快照，不是持续记录
- Python 辅助模块尚未接入 GUI 主流程

## 许可证

仓库 README 原先标注为 Apache 2.0，但本仓库内目前没有单独的 `LICENSE` 文件；如果要对外分发，建议补充正式许可证文件并确认外部依赖许可证边界。
