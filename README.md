# VaporView

基于 Qt 的导航系统可视化交互界面，用于实时监控 GNSS/IMU 导航数据、PTB210 气压计和 HMP3 温湿度传感器数据。

## 功能特性

- **多传感器数据采集**: 支持 GNSS (PVTSLN)、IMU (HiPNUC)、PTB210 气压计、HMP3 温湿度传感器
- **实时数据显示**: 横向并排显示所有传感器数据
- **串口配置管理**: 下拉选择串口，自动扫描可用设备
- **全屏模式**: 支持 F11 全屏显示
- **数据导出**: 支持 CSV、JSON 格式导出
- **日志记录**: 操作日志记录

## 架构

```
VaporView/
├── CMakeLists.txt          # CMake 构建配置
├── include/                # C++ 头文件
│   ├── MainWindow.h        # 主窗口
│   ├── serial_port.h       # 串口通信类
│   ├── data_types.h        # 数据类型定义
│   └── data_collector.h    # 数据采集器
├── src/                    # C++ 源文件
│   ├── main.cpp
│   ├── MainWindow.cpp
│   ├── serial_port.cpp
│   └── data_collector.cpp
├── python/                 # Python 辅助模块
│   ├── __init__.py
│   ├── config_manager.py   # 配置管理
│   ├── file_logger.py      # 日志记录
│   └── data_exporter.py    # 数据导出
└── README.md
```

## 技术架构

- **C++ 核心模块**: 串口通信、数据解析、实时数据更新（低延迟）
- **Python 辅助模块**: 配置管理、日志记录、数据导出
- **Qt 框架**: 跨平台 GUI

## 依赖

- CMake 3.16+
- Qt5 或 Qt6 (Core, Widgets)
- C++17 编译器
- Python 3.8+ (可选，用于 Python 模块)

## 构建

```bash
cd /home/nvidia/NAV/VaporView
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

### 使用 Qt6

```bash
cmake -DUSE_QT6=ON ..
make -j$(nproc)
```

## 运行

```bash
./build/VaporView
```

## 默认串口配置

| 设备 | 默认串口 | 波特率 | 参数 |
|------|----------|--------|------|
| GNSS | /dev/ttyCOM3 | 115200 | N81 |
| IMU | /dev/ttyIMU | 115200 | N81 |
| PTB210 | /dev/ttyBARO | 9600 | E71 |
| HMP3 | /dev/ttyHMP | 19200 | N82 |

## 支持的协议

### GNSS (UM982 RTK 接收机)
- PVTSLN 协议：完整导航解算数据

### IMU (HiPNUC)
- HI81: INS 数据包
- HI83: 扩展 IMU 数据包
- HI91: 浮点 IMU 数据包

### PTB210 气压计
- RS232C 串口通信
- `.P` 命令读取气压值

### HMP3 温湿度传感器
- Modbus RTU 协议
- 从站地址: 240 (0xF0)

## 许可证

Apache 2.0
