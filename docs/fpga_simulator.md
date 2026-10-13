# FPGA 协议模拟程序

`VaporViewFpgaSimulator.exe` 是可独立启动的测试服务，无需板卡、Cypress SDK 或 USB 驱动。它在 FPGA 传输接口处生成完整 VLP1 字节流，经正常的拆包、CRC、传感器解码、波形重组、SkyCore 遥测与记录链路送入 VaporView；没有开启“界面测试模式”。

## 启动与连接

1. 双击 `build/Release/StartFpgaSimulator.cmd`，保持控制台运行。
2. 打开 `build/Release/VaporView.exe`，在标题栏选择“远程”。
3. 在“设备配置”的天空端连接设置中选择 TCP，地址 `127.0.0.1`、端口 `39100`，点击连接。
4. 配置同步完成后，传感器路径为“FPGA”。打开 FPGA 页面即可看到传感器和双路 DLIA；首页、温控页和导航状态会收到对应数据。
5. RAW 默认关闭。启动 RAW 短窗后可观察两路 ADC；默认十秒后停止上传，采集和 DLIA 仍继续。

命令行也可直接运行：

```powershell
& .\build\Release\VaporViewFpgaSimulator.exe
```

服务默认仅监听本机：遥测 TCP `39100`，SkyCore 本机 IPC `39001`。如果已有 SkyCore 占用了这些端口，可指定另一组端口，并让 VaporView 连接对应遥测端口：

```powershell
& .\build\Release\VaporViewFpgaSimulator.exe --telemetry-tcp-port 39200 --ipc-port 39201
```

正常退出：在控制台输入 `quit` 回车，或按 Ctrl+C。启动脚本会保留失败退出的提示，便于发现端口占用。

## 模拟内容

| 内容 | 行为 |
|---|---|
| PTB210 | 随时间变化的气压 |
| BMP390 | 原始 ADC 与标定数据，实际经过主机补偿 |
| SHT45 | 温度、湿度、加热器状态；用于 FPGA 温湿度来源 |
| EPSILON | 带有效 FDILink CRC 的 IMU、姿态、定位、速度、UTC、卫星和状态报文 |
| TFA1500 | 变化的距离 |
| AI8 | PV/SP/SV、状态和计数；设温后确认计数与读回更新，PV 逐步接近 SP |
| 双 ADC | 按需 RAW，带周期、采样率和分片元数据 |
| 双 DLIA | 连续 H1/H2、IQ 数据；模式和输出率可配置 |
| 双 WMS/DAC | 独立启停、参数写入、配置提交、实际频率和 DAC 计数回读 |

服务初始启用六路传感器及两路采集、WMS/DAC，便于立即观察。禁用某个传感器会停止该源的新报文；停止采集会停止新的 ADC/DLIA 周期。断开再连接会重置为模拟板的初始状态，并重新执行握手和恢复流程。

模拟器的板时钟为 100 MHz，生成周期为 100 ms；RAW/DLIA 单周期点数上限 16384，读取故意拆成最多 503 字节的块，以覆盖半帧、粘包和分片重组。它用于功能测试，输出量不代表实际 1 MS/s USB 吞吐。

## 配置与记录

默认配置位于 Qt 的本机应用数据目录下 `VaporView/VaporViewFpgaSimulator/fpga_simulator/sky_config.json`。启动时会打印完整路径；不会读取正式 `build/Release/sky_config.json`。

也可指定独立配置文件：

```powershell
& .\build\Release\VaporViewFpgaSimulator.exe --sky-config 'D:/VaporViewSimulation/sky_config.json'
```

在 VaporView 中启动天空端记录后，会话保存到该配置文件同目录的 `simulated_sessions`，包含原始 `raw/fpga_vlp1.dat`、传感器数据与配置快照。可使用现有 Session 查看、FPGA 回放和导出功能。模拟服务诊断标明 `mode=simulation`、`SIMULATED-GP01`；这些数据不是实测数据。

## 验证范围

模拟器不在 Windows 设备管理器中注册虚拟 USB；连接是在现有 `FpgaUsbTransport` 接口处替换物理后端。真实 USB 枚举、驱动、拔插、吞吐和模拟输出校准仍需实板验证。

FPGA 当前接入的是 PTB210、EPSILON、BMP390、SHT45、TFA1500 和 AI8 六路源。HMP3/RD105 独立串口路径、EPSILON 串口高级配置和 PPK 原始观测不由这个 FPGA 模拟服务生成。

协议依据 `Vapor_Radar_App_V2/Docs/protocol/FPGA_HOST_PROTOCOL_V1.0.md`、双 AD4630 修订及主机交接文档；未使用旧 VFLP 设计稿。

## 开发验证

在 MSVC Developer 环境中：

```powershell
cmake -S . -B build/Release
cmake --build build/Release --config Release --target VaporViewFpgaSimulator fpga_simulator_test -- -j4
ctest --test-dir build/Release -C Release -R '^fpga_simulator(_e2e)?_test$' --output-on-failure
```

协议/控制测试覆盖真实解码、无效寄存器、设温确认、配置回读、两路 RAW、独立启停、停止排空和重连。跨进程测试启动真实模拟 EXE，通过 Ground 的 `RemoteSkyController` 收发，检查遥测、波形预览、记录落盘和重连，并正常退出子进程。测试使用临时配置和记录目录。
