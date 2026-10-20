# FPGA 协议模拟器（本地与远程）

`VaporViewFpgaSimulator.exe` 是可独立启动的测试服务，无需板卡、Cypress SDK 或 USB 驱动。它在 FPGA 传输接口处生成完整 VLP1 字节流，经正常的拆包、CRC、传感器解码、波形重组、SkyCore 遥测与记录链路送入 VaporView；没有开启“界面测试模式”。

同一模拟后端也已接入 VaporView 的本地 FPGA 控制器。本地模式直接使用它，不需要启动独立服务或连接 TCP；远程模式使用上述独立 EXE。

## 本地模式

1. 打开 `build/Release/VaporView.exe`，在标题栏选择“本地”和“FPGA”。
2. 打开 FPGA 页面，在后端下拉框（原 Auto/Cypress/WinUSB）中选择“模拟器”。
3. 等待设备列表显示 `FPGA Protocol Simulator · SIMULATED`，点击连接。
4. 握手仅连接板卡并恢复板卡原先启用的模块。新板卡默认各设备关闭，使用首页设备按钮或 FPGA 页启停按钮逐个启用；采集、DAC 和 RAW 分别控制。

本地连接、控制、记录与回放使用正常的 Ground 工作线程和会话服务，不依赖界面测试模式。切回 Auto/Cypress/WinUSB 后枚举真实 USB，模拟器不会混入物理设备的自动选择。远程模式中这个本地模拟选项禁用。

模拟板卡按固件复位语义启动：全局使能、传感器、ADC/DLIA 和 WMS/DAC 默认关闭；USB 和流上传通路默认可用。所有入口使用相同的板卡初始状态。首页单设备按钮只启用目标设备；本地标题栏连接先完成 FPGA 握手，再通过正常控制事务逐个启用七个受支持传感器和 ADC/DLIA，界面测试模式使用同一流程。FPGA 页连接仅恢复板卡原先启用的模块。RAW、WMS/DAC 单独控制。关闭再打开 USB 保留板卡参数、启停状态和时间基准；切换后端或重启模拟服务创建新板卡。

界面测试模式也在独立工作线程中使用 `FpgaDeviceController`、VLP1 命令队列、字节流解析、传感器解码和波形拼接。页面不再合成设备状态、寄存器或测量值；VLP1 尚不支持的 EPSILON 参数配置不会返回演示成功；模式开启后先处于未连接状态。部分故障、数据停更、缺片和 DLIA 锁存故障从模拟传输层注入，由正常协议链路决定显示结果。退出模式先丢弃排队的界面结果，再关闭工作线程并恢复原配置。首页数据摘要也读取实际解码状态；本地路径没有数传流量时，不再显示演示频率或“所有设备有数据”。

FPGA 页在界面测试模式中禁用记录按钮，不再伪造记录成功；实际记录、回放和导出请使用本地“模拟器”后端或独立服务验证。`RunningDemo` 仅是底层测试显式选择的已运行板卡夹具，应用连接入口不选择它。

本地模拟记录保存在当前记录目录的 `simulated_sessions` 子目录，配置仍保存到本地 FPGA 的期望配置；退出模拟连接后可重新选择真实驱动后端。

## 远程服务的启动与连接

1. 双击 `build/Release/StartFpgaSimulator.cmd`，保持控制台运行。
2. 打开 `build/Release/VaporView.exe`，在标题栏选择“远程”。
3. 在“设备配置”的天空端连接设置中选择 TCP，地址 `127.0.0.1`、端口 `39100`，点击连接。
4. 配置同步后打开 FPGA 页面；新服务中的设备默认关闭。按需启用传感器和采集后，首页、温控页和导航状态收到相应数据。
5. RAW 默认关闭。启动 RAW 后可持续观察两路 ADC，直到手动停止 RAW 或停止采集；停止 RAW 后采集和 DLIA 仍继续。

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

远程独立服务的默认配置位于 Qt 的本机应用数据目录下 `VaporView/VaporViewFpgaSimulator/fpga_simulator/sky_config.json`。启动时会打印完整路径；不会读取正式 `build/Release/sky_config.json`。

也可指定独立配置文件：

```powershell
& .\build\Release\VaporViewFpgaSimulator.exe --sky-config 'D:/VaporViewSimulation/sky_config.json'
```

在 VaporView 中启动天空端记录后，会话保存到该配置文件同目录的 `simulated_sessions`，包含共用 `raw/waveform.dat` 中的完整 H1/H2、`sensors/waveform_features.csv` 中的归一化峰值、传感器数据与标准配置/事件快照；不保存 USB/VLP1 转发包。可使用现有 Session 查看、FPGA 回放和导出功能。模拟服务诊断标明 `mode=simulation`、`SIMULATED-GP01`；这些数据不是实测数据。

## 验证范围

模拟器不在 Windows 设备管理器中注册虚拟 USB；连接是在现有 `FpgaUsbTransport` 接口处替换物理后端。真实 USB 枚举、驱动、拔插、吞吐和模拟输出校准仍需实板验证。

FPGA 当前接入的是 PTB210、EPSILON、BMP390、SHT45、TFA1500 和 AI8 六路源。HMP3/RD105 独立串口路径、EPSILON 串口高级配置和 PPK 原始观测不由这个 FPGA 模拟服务生成。

协议依据 `Vapor_Radar_App_V2/Docs/protocol/FPGA_HOST_PROTOCOL_V1.0.md`、双 AD4630 修订及主机交接文档；未使用旧 VFLP 设计稿。

## 开发验证

在 MSVC Developer 环境中：

```powershell
cmake -S . -B build/Release
cmake --build build/Release --config Release --target VaporView VaporViewFpgaSimulator fpga_simulator_test fpga_main_window_test -- -j4
ctest --test-dir build/Release -C Release -R '^fpga_(local_simulator|simulator(_e2e)?)_test$' --output-on-failure
```

协议/控制测试覆盖真实解码、无效寄存器、设温确认、配置回读、两路 RAW、独立启停、停止排空和重连。跨进程测试启动真实模拟 EXE，通过 Ground 的 `RemoteSkyController` 收发，检查遥测、波形预览、记录落盘和重连，并正常退出子进程。测试使用临时配置和记录目录。

`fpga_local_simulator_test` 通过真实 MainWindow 的本地连接按钮验证工厂后端、传感器、双路解调、RAW、设温、独立记录与回放，以及本地/远程切换后的选项状态。

`fpga_control_page_test` 验证界面测试模式的真实命令与读回、工作线程隔离、冷启动、参数与启停状态保留、缺片排空、故障恢复、数据过期和操作中退出。上述验证限于软件行为，合成信号不能替代真实板卡、模拟前端及 USB 吞吐的实机验证。

带轮询间隔的传感器按相应寄存器调度，不再一律按 10 Hz 产出；命令应答使用板卡单调时间，USB 重连不清零。生成循环最小间隔为 100 ms，波形也采用有界合成数据，因此模拟器不用于实际采样吞吐或线路时延测量。
