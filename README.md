# VaporView

`VaporView` 是一个基于 Qt Widgets 的桌面程序。当前主界面面向 EPSILON 组合导航、PTB210 气压计、HMP3 温湿度传感器、TF03 / TFA1500-L 激光测距模块和本地 TCP 波形流，提供串口接入、实时数据显示、RTK/NTRIP 转发、会话记录、离线查看和轨迹查看能力。

本文档只描述当前仓库中可以直接从代码、构建脚本和随仓库文档确认的内容。对应代码入口主要是：

- `CMakeLists.txt`
- `include/`
- `src/`
- `docs/`
- `tools/`
- `scripts/`

## 当前功能

- Qt Widgets 图形界面，应用名和窗口标题为 `VaporView`，应用版本为 `1.0.0`。
- 中英文界面切换、F11 全屏、70% / 80% / 90% / 100% / 115% / 130% 字号缩放。
- 串口刷新与自动识别，当前自动识别覆盖 EPSILON、PTB210、HMP3、TF03 和 TFA1500-L。
- EPSILON `FDILink` 组合导航数据解析、包频率配置、RTCM 串口配置、主天线杆臂配置。
- PTB210 气压、HMP3 温湿度、TF03 / TFA1500-L 距离数据显示与频率统计。
- TCP 波形监视，默认连接 `127.0.0.1:8888`，显示原始信号、归一化二次谐波和峰值趋势。
- RTK NTRIP 配置对话框，基于内置 RTKLIB `strsvr` 把 NTRIP 输入转发到串口或 TCP Client 输出。
- 会话记录：手动开始、暂停、结束，按配置写入 `session_*` 目录。
- 数据查看器：读取已记录的 `session.json`、`devices.csv` 和 `waveform/*.dat`，显示波形、峰值、温湿度、气压和关联 CSV 行。
- 轨迹查看器：从会话 CSV 中提取 RTK 轨迹点，支持 OpenStreetMap、天地图矢量和天地图卫星底图；天地图底图需要用户提供 Key。
- 诊断脚本：EPSILON 串口探测、EPSILON 主口恢复、TCP 波形模拟发送。

## 项目结构

```text
VaporView/
├── CMakeLists.txt
├── README.md
├── cmake/
│   └── CopyRuntimeDependencies.cmake.in
├── design/
│   ├── vaporview_icon_concept.svg
│   └── vaporview_icon_minimal.svg
├── docs/
│   ├── epsilon_raw_dat_format.md
│   └── imu_raw_dat_format.md
├── include/
│   ├── MainWindow.h
│   ├── RangeSelectionAxisWidget.h
│   ├── RtkConfigDialog.h
│   ├── RtkStreamService.h
│   ├── SessionViewerWindow.h
│   ├── TcpWavePanel.h
│   ├── TrajectoryViewerDialog.h
│   ├── compiler_compat.h
│   ├── data_collector.h
│   ├── data_types.h
│   ├── serial_port.h
│   └── serial_probe_utils.h
├── python/
│   ├── __init__.py
│   ├── config_manager.py
│   ├── data_exporter.py
│   └── file_logger.py
├── resources/
│   ├── app.ico
│   ├── app.rc
│   ├── combo_arrow_down.xpm
│   ├── combo_arrow_up.xpm
│   └── modern_style.qss
├── scripts/
│   ├── mock_tcp_waveform_sender.py
│   └── recover_epsilon_main.ps1
├── src/
│   ├── MainWindow.cpp
│   ├── RtkConfigDialog.cpp
│   ├── RtkStreamService.cpp
│   ├── SessionViewerWindow.cpp
│   ├── TcpWavePanel.cpp
│   ├── TrajectoryViewerDialog.cpp
│   ├── data_collector.cpp
│   ├── main.cpp
│   └── serial_port.cpp
├── third_party/
│   ├── hipnuc_driver/
│   ├── rtklib/
│   └── um982_driver/
└── tools/
    ├── epsilon_serial_probe.py
    └── epsilon_serial_probe.ps1
```

说明：

- `build/`、`data/`、`records/`、`AGENTS.md` 等本地文件在 `.gitignore` 中忽略，不属于交付源码。
- `third_party/rtklib` 参与当前 RTK 流服务构建，并带有 `third_party/rtklib/LICENSE.txt`。
- `third_party/um982_driver` 和 `third_party/hipnuc_driver` 的源码仍被 `CMakeLists.txt` 编入 `VaporView`，但当前主窗口配置面板没有把 UM982 或独立 HiPNUC IMU 作为可选择设备行展示。

## 依赖要求

- CMake `3.16+`
- 支持 C++17 的 C/C++ 编译器
- Qt 6，当前 CMake 必需组件：
  - `Core`
  - `Widgets`
  - `SerialPort`
  - `Network`
- Windows 下如使用 MinGW / UCRT64，CMake 会在满足条件时查找 `windeployqt6` / `windeployqt`，并使用 `cmake/CopyRuntimeDependencies.cmake.in` 生成运行时依赖复制脚本。

## 构建

仓库约定使用 `build/Release` 作为本地交付验证目录。

当前本机已有的 `build/Release/CMakeCache.txt` 显示：

- `CMAKE_GENERATOR:INTERNAL=Ninja`
- `CMAKE_PREFIX_PATH:UNINITIALIZED=F:/msys64/ucrt64`
- `Qt6_DIR:PATH=F:/msys64/ucrt64/lib/cmake/Qt6`

在相同环境下复用现有配置：

```powershell
cmake --build build/Release
```

重新生成 `build/Release` 时，按本机当前 Qt 路径可使用：

```powershell
cmake -S . -B build/Release -G Ninja -DCMAKE_PREFIX_PATH=F:/msys64/ucrt64
cmake --build build/Release
```

如 Qt 安装路径不同，替换 `CMAKE_PREFIX_PATH` 为实际 Qt 6 前缀路径。

当前 CMake 目标：

- `VaporView`：主桌面程序。
- `rtklib_strsvr`：静态库，封装 RTKLIB 流服务所需源码。
- `str2str`：当 `third_party/rtklib/app/consapp/str2str/str2str.c` 存在时构建。

构建后主程序位置：

```text
build/Release/VaporView.exe
```

## 主程序架构

### 入口与主窗口

- `src/main.cpp` 创建 `QApplication`，设置应用名 `VaporView`、版本 `1.0.0` 和组织名 `VaporView`。
- `src/MainWindow.cpp` 负责菜单栏、工具栏、状态栏、设备配置区、实时数据区、TCP 波形区、日志区、记录会话和全局设置。
- `resources/modern_style.qss`、`resources/app.ico`、`resources/combo_arrow_down.xpm`、`resources/combo_arrow_up.xpm` 会在构建后复制到构建目录下的 `resources/`。

### 串口层

`src/serial_port.cpp` 是仓库自带的跨平台串口封装：

- Windows：使用 Win32 `CreateFileA`、`ReadFile`、`WriteFile`、`DCB`、`COMMTIMEOUTS`。
- Linux / 非 Windows：使用 `termios`、`open`、`read`、`write`。

Qt `SerialPort` 模块当前用于枚举可用串口；实际采集读写使用仓库内 `VaporView::SerialPort`。

### 采集层

`include/data_collector.h` 和 `src/data_collector.cpp` 定义采集器：

- `DataCollector`：串口启停、线程管理、回调、日志、取消检测、采样率和实际接收频率统计。
- `EpsilonCollector`：EPSILON `FDILink` 采集、校验、解析、包频率配置、RTCM 串口配置、主天线杆臂配置。
- `PtbCollector`：PTB210 气压采集。
- `HmpCollector`：HMP3 Modbus RTU 温湿度采集。
- `LidarCollector`：TF03 / TFA1500-L 激光测距采集。
- `GnssCollector` 和 `ImuCollector` 仍在源码中保留，但当前主窗口没有独立 GNSS 或 IMU 设备配置行。

## 设备支持

### EPSILON

当前主界面默认串口参数：

- Windows：`COM3 @ 921600 N81`
- 非 Windows：`/dev/ttyEPSILON @ 921600 N81`

当前实现事实：

- 协议帧头为 `0xFC`，帧尾为 `0xFD`。
- 解析前校验 `CRC8`、`CRC16` 和完整帧长度。
- 串口探测优先读取有效 `FDILink` 帧；如果未检测到导航流，会尝试 `#fconfig`、`#fmsg`、`#fdeconfig` 命令模式握手并恢复导航流。
- UI 分组采样率只提供 `20`、`50`、`100`、`200 Hz`。
- 设备命令使用 `#fconfig`、`#fmsg`、`#fsave`、`#freboot`、`#fdeconfig`、`#fparam`、`#fantearm`。
- 记录侧只把校验通过的完整 `FDILink` 原始帧写入 `sensors/epsilon_raw.dat`。

当前解析和频率统计覆盖的 EPSILON 报文：

| Packet ID | 名称 |
| --- | --- |
| `0x40` | `MSG_IMU` / IMU 原始数据 |
| `0x41` | `MSG_AHRS` / AHRS 姿态解 |
| `0x42` | `MSG_INSGPS` / INS/GPS 融合解 |
| `0x50` | `MSG_SYS_STATE` / 系统状态 |
| `0x51` | Unix Time |
| `0x52` | Formatted Time |
| `0x59` | `MSG_RAW_GNSS` / 原始 GNSS |
| `0x5A` | `MSG_SATELLITE` / 卫星汇总 |
| `0x5C` | `MSG_GEODETIC_POS` / 大地坐标 |
| `0x5D` | `MSG_ECEF_POS` / ECEF 坐标 |
| `0xF0` | Main MAVLink Tunnel |

EPSILON 包频率配置：

- 分组模式下，`0x40`、`0x41`、`0x42`、`0x50` 按 UI 频率下发。
- 分组模式下，`0x59`、`0x5A`、`0x5C`、`0x5D` 使用 `min(UI频率, 20 Hz)`。
- `设备 -> 设置EPSILON包频率...` 可保存自定义包频率。
- 自定义包频率支持 `0 Hz` 关闭输出；`0x40` 最高列表到 `1000 Hz`，其他当前配置项最高列表到 `500 Hz`。
- “推荐默认值”按钮保存的包频率为：`0x40=250 Hz`、`0x41=50 Hz`、`0x42=100 Hz`、`0x50=100 Hz`、`0x59=10 Hz`、`0x5A=1 Hz`、`0x5C=10 Hz`、`0x5D=10 Hz`。

EPSILON RTCM 与杆臂配置：

- `EpsilonCollector::configureRtcmPort` 允许配置 EPSILON 通信端口 `2` 到 `5`；端口 `1` 被明确拒绝，因为它必须保持 Main port。
- RTCM 串口配置会写入 `COMM_STREAM_TYP<n>=3` 和 `COMM_BAUD<n>`，保存后重启并等待导航流恢复。
- 主天线杆臂通过 `#fantearm X Y Z` 写入，单位为米，保存后退出配置模式并等待导航流恢复。

### PTB210

当前主界面默认串口参数：

- Windows：`COM5 @ 9600 E71`
- 非 Windows：`/dev/ttyBARO @ 9600 E71`

当前实现事实：

- 探测命令：`.P\r`
- 连续输出命令：`.BP\r`
- 停止命令：`\r`
- 设置频率时会发送 `.AVRG.0\r`、`.MPM.<value>\r`、`.RESET\r`
- 设备侧采样率支持范围为 `1-70 Hz`
- `.MPM` 写入值为 `hz * 60`，并限制在 `6` 到 `4200`
- 运行时把设备返回的数值解析为 `pressure_hpa`

### HMP3

当前主界面默认串口参数：

- Windows：`COM6 @ 19200 N82`
- 非 Windows：`/dev/ttyHMP @ 19200 N82`

当前实现事实：

- 协议：Modbus RTU
- 从站地址：`240`
- 功能码：`0x03`
- 读取起始寄存器：`0x0000`
- 读取寄存器数量：`4`
- 湿度寄存器起点：`0x0000`
- 温度寄存器起点：`0x0002`
- 当前没有设备侧采样率命令；UI 频率改变的是主机轮询间隔。

### TF03 / TFA1500-L

当前主界面默认串口参数：

- Windows：`COM7 @ 500000 N81`
- 非 Windows：`/dev/ttyLidar @ 500000 N81`

自动识别顺序中：

- TFA1500-L：`500000 N81`
- TF03：`115200 N81`

TF03 当前实现事实：

- 帧头：`0x59 0x59`
- 帧长：`9` 字节
- 距离字段：第 `3-4` 字节，小端，单位厘米，显示和记录时转换为米
- 信号强度字段：第 `5-6` 字节，小端
- 校验：前 `8` 字节求和后取低 `8` 位
- 有效性：信号强度不低于 `40` 且距离大于 `0`
- 设备侧帧率命令：`5A 06 03 LL HH SUM`
- 设备侧帧率支持范围：`1-100 Hz`

TFA1500-L 当前实现事实：

- 支持高频模式、距离输出模式和低频连续测距模式的启动命令。
- 高频启动命令：`55 AA CB CC CC CC CC FB`
- 高频停止命令：`55 AA CC CC CC CC CC FC`
- 待机命令：`55 00 02 00 00 57`
- 距离输出命令：`5A 0A 02 02 00 F1`
- 低频连续测距命令：`55 02 02 20 00 75`
- TFA1500-L 距离帧头为 `0x5C`，当前距离帧长度为 `5` 字节。
- 高频模式和 `>= 500000` 波特率下使用设备自适应输出，主机采样率限制最高保存到 `1000 Hz`。

## TCP 波形监视

`src/TcpWavePanel.cpp` 实现 TCP 客户端波形面板：

- 默认主机：`127.0.0.1`
- 默认端口：`8888`
- 输入协议为连续两段长度前缀 payload：
  - `4` 字节长度头 + 原始信号 float32 payload
  - `4` 字节长度头 + 归一化二次谐波 float32 payload
- 长度头支持自动识别小端 / 大端。
- float32 payload 支持自动识别小端、大端、16 位字交换小端。
- 首选 payload 大小为 `200000` 字节，即 `50000` 个 float32 点。
- 单个 payload 最大允许 `16 MiB`。
- 异常波形幅值阈值为 `1.0e6`，超过后丢弃该帧并等待下一帧。
- 峰值趋势默认在样本区间 `[10000, 50000)` 内取归一化二次谐波最大值。
- TCP 连接状态可以单独作为记录启动条件；不要求串口设备同时在线。

仓库提供模拟发送脚本：

```powershell
python scripts/mock_tcp_waveform_sender.py --port 8888
```

该脚本发送两段 payload，默认每段 `50000` 个 float32 样本，默认 `10 Hz`。

## RTK NTRIP

RTK 功能由 `src/RtkConfigDialog.cpp` 和 `src/RtkStreamService.cpp` 实现。

当前实现事实：

- NTRIP 输入使用 RTKLIB `STR_NTRIPCLI`。
- 输出模式支持串口 `STR_SERIAL`，`RtkStreamConfig` 也定义了 TCP Client 输出模式。
- 串口输出路径格式为 `port:baud:8:n:1:off`。
- 默认输出波特率为 `115200`。
- 默认 NTRIP 端口为空时按 `2101` 处理。
- 默认超时为 `5000 ms`，默认重连间隔为 `1000 ms`。
- 当 EPSILON 主串口有可用经纬高时，RTK 服务按 `1 Hz` 从该位置生成 NMEA GGA 发给 NTRIP caster。
- 当没有可用 EPSILON 主串口位置时，RTK 服务保留输出口 GGA 回读兼容模式。
- 对话框支持挂载点获取、RTCM 诊断统计、无信号测试、GGA 监视、串口自动识别、配置保存和加载。

## 会话记录

当前记录策略由主窗口手动控制：

- 点击“连接”只连接设备，不自动开始记录。
- 工具栏提供“开始记录”“暂停记录”“结束记录”。
- 至少一个串口设备在线，或者 TCP 波形链路已经连接成功，才允许开始记录。
- “开始记录”创建新的 `session_*` 目录；暂停后再次开始会继续写入同一 session。
- “暂停记录”停止记录线程和波形写盘线程，但保留打开的 session。
- “结束记录”写入结束时间，刷新并关闭 CSV、EPSILON 原始帧、事件日志、错误日志和波形文件。
- 连接失败或断开时，如果还有未结束的 session，程序会自动结束它。

默认记录根目录：

- 程序从应用目录向上查找包含 `CMakeLists.txt` 和 `README.md` 的仓库根目录。
- 找到仓库根目录时，默认记录到 `<repo>/data`。
- 找不到仓库根目录时，默认记录到 `<applicationDir>/data`。
- 用户可通过“数据 -> 记录目录...”修改，路径保存到 `QSettings("VaporView", "MainWindow")`。

会话目录结构：

```text
data/
└── session_yyyy-MM-dd_HH-mm-ss/
    ├── session.json
    ├── epsilon_raw_dat_format.md
    ├── waveform/
    │   ├── waveform_yyyy-MM-dd_HH-mm-ss.dat
    │   └── ...
    ├── sensors/
    │   ├── devices.csv
    │   └── epsilon_raw.dat
    ├── logs/
    │   ├── event_log.csv
    │   └── error_log.txt
    └── config/
        └── device_config.json
```

记录频率菜单：

| 分类 | 当前行为 |
| --- | --- |
| 波形 | `每帧` 或 `1/2/5/10/20/50/100/200/500/1000 Hz` 限频 |
| EPSILON 原始帧 | 固定保存完整已校验 `FDILink` 帧 |
| 其余设备 | `1/2/5/10/20/50/100/200 Hz` |

`session.json` 当前字段包括：

- `session_name`
- `start_time_utc`
- `start_time_us`
- `end_time_utc`
- `software_version`
- `epsilon_schema_version`
- `waveform_points_per_frame`
- `sensor_export_rate_hz`
- `other_devices_export_rate_hz`
- `epsilon_raw_export_mode`
- `waveform_export_rate_hz`
- `waveform_export_mode`
- `waveform_value_type`
- `waveform_timestamp_type`
- `timestamp_unit`
- `waveform_split_minutes`
- `sensor_rows`
- `waveform_frames`
- `waveform_file_count`
- `paths`

`device_config.json` 当前记录：

- 当前记录目录和 session 目录
- 设备记录频率
- 波形主机、端口、记录模式、分文件分钟数、每帧点数和值类型
- EPSILON、PTB、HMP、Lidar 的串口、波特率和 UI 频率文本

## 数据文件格式

### `sensors/devices.csv`

`devices.csv` 由独立记录线程按“其余设备”记录频率写入。每行是记录时刻的最新设备快照。

当前表头字段包括：

- 记录时间：`record_timestamp_us`
- EPSILON 时间：`epsilon_host_timestamp_us`、`epsilon_device_timestamp_us`、`epsilon_utc_unix_s`、`epsilon_utc_microseconds`
- 导航位置：`nav_lat_deg`、`nav_lon_deg`、`nav_height_m`
- ECEF：`ecef_x_m`、`ecef_y_m`、`ecef_z_m`
- NED：`ned_n_m`、`ned_e_m`、`ned_d_m`
- 速度：`vel_n_mps`、`vel_e_mps`、`vel_d_mps`
- 机体系速度和加速度
- 姿态角、四元数、角速度、IMU 加速度、IMU 角速度、磁场
- GNSS 状态、卫星数、DOP、精度、差分龄期、heading 状态、EPSILON 状态位
- EPSILON 有效性和错误信息
- HMP 温度 / 湿度、PTB 气压、Lidar 距离 / 信号强度 / 有效性

### `sensors/epsilon_raw.dat`

`epsilon_raw.dat` 格式说明见 [docs/epsilon_raw_dat_format.md](docs/epsilon_raw_dat_format.md)。

当前写入逻辑：

- 文件头 magic 为 `VVEPSRAW`
- 格式版本为 `1`
- 每条记录 marker 为 `0x524D5549`
- 记录主机 Unix epoch 微秒时间
- 记录 `packet id`
- `reserved[0]` 保存 FDILink `serial number`
- payload 为完整 `FDILink` 帧字节，不是解码后的字段
- 只写入 CRC 和帧尾校验通过的完整帧

### `waveform/*.dat`

波形文件由独立写盘线程写入。

当前格式：

- 无文件头。
- 每帧固定 `8 + 50000 * 4` 字节。
- 前 `8` 字节为 `uint64` 时间戳，单位微秒。
- 后续为 `50000` 个 float32，内容为 TCP 面板收到的“归一化二次谐波”波形。
- 文件按 `waveform_split_minutes` 分段，当前 UI 范围为 `1` 到 `5` 分钟。
- 文件名使用本地时间 `waveform_yyyy-MM-dd_HH-mm-ss.dat`。

## 数据查看器与轨迹查看器

`src/SessionViewerWindow.cpp` 实现数据查看器：

- 可选择 session 目录或 `session.json`。
- 读取 `session.json` 中的相对路径。
- 读取 `sensors/devices.csv` 并显示表格。
- 扫描 `waveform/*.dat`，按 `waveform_points_per_frame` 计算总帧数。
- 支持帧滑块和数字框定位。
- 对每帧波形计算峰值，默认峰值搜索区间为 `[10000, 50000)`。
- 峰值过滤支持无过滤、IQR 离群过滤、保留范围、排除范围。
- 波形峰值图、温度图、湿度图和气压图支持散点 / 折线模式切换。
- 根据波形时间戳高亮最接近的 CSV 行。

`src/TrajectoryViewerDialog.cpp` 实现轨迹查看器：

- 数据来源为会话 CSV 中的 RTK / EPSILON 坐标字段。
- 可把最接近时间的波形峰值关联到轨迹点。
- 支持 OpenStreetMap、天地图矢量、天地图卫星。
- 天地图 Key 保存于本机 `QSettings`，界面提供删除保存 Key 的按钮。

## 工具脚本

### EPSILON 串口探测

Python 版本：

```powershell
python tools/epsilon_serial_probe.py --port COM3 --baud 921600 460800 115200 --duration 3 --show-frames
```

PowerShell 版本：

```powershell
powershell -ExecutionPolicy Bypass -File tools/epsilon_serial_probe.ps1 -Port COM3 -BaudRates 921600,460800,115200 -ShowFrames
```

这两个脚本都会统计 EPSILON `FDILink` 字节、帧、CRC 和 packet id。

### EPSILON 主口恢复

```powershell
powershell -ExecutionPolicy Bypass -File scripts/recover_epsilon_main.ps1 -Ports COM3 -Bauds 921600,115200,230400,460800
```

该脚本会扫描端口和波特率，读取 `FDILink` 概览，并在需要时发送 `#fdeconfig` 尝试退出配置模式。

可选参数包括：

- `-QueryConfig`
- `-RebootUnsaved`
- `-FactoryReset`
- `-ListPortsOnly`

### TCP 波形模拟

```powershell
python scripts/mock_tcp_waveform_sender.py --host 0.0.0.0 --port 8888 --samples 50000 --rate 10
```

## Python 目录状态

`python/` 目录提供辅助模块：

- `ConfigManager`、`AppConfig`、`SerialPortConfig`
- `FileLogger`、`DataLogger`
- `DataExporter`

当前 C++ / Qt 主程序没有直接导入或嵌入执行这些 Python 模块。

CMake 中保留了 `BUILD_PYTHON_BINDINGS` 选项，但默认关闭。当前仓库没有 `python/bindings.cpp`，因此手动设置 `BUILD_PYTHON_BINDINGS=ON` 会在 CMake 配置阶段直接失败，并提示该入口文件缺失。

## 当前限制

- 仓库没有顶层 `LICENSE` 文件。
- `BUILD_PYTHON_BINDINGS=ON` 当前不可用，因为缺少 `python/bindings.cpp`。
- `docs/imu_raw_dat_format.md` 描述的是 HiPNUC / HI91 原始记录格式；当前主窗口会话记录写入的是 `sensors/epsilon_raw.dat`，并会把 `docs/epsilon_raw_dat_format.md` 复制到 session 根目录。
- `data/` 为本地记录输出目录，已被 `.gitignore` 忽略。
- `scripts/` 目录已被 `.gitignore` 忽略规则覆盖，但仓库中已有跟踪的 `scripts/mock_tcp_waveform_sender.py` 和 `scripts/recover_epsilon_main.ps1`。

## 许可证

当前仓库没有顶层项目许可证文件。第三方 RTKLIB 源码随仓库保留 `third_party/rtklib/LICENSE.txt`。
