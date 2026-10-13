# VaporView 测试流程

所有本地构建和测试统一使用 `build/Release`。Windows 下先进入 VS/MSVC Developer 环境；仓库脚本会自动完成这一步。日常只运行改动面对应的 focused 集合，跨模块改动或发布前再运行完整 CTest。

无板卡的 FPGA 功能验证可使用 [FPGA 协议模拟器](fpga_simulator.md)：本地模式在 FPGA 页选择“模拟器”后端直接连接；远程模式双击 `build/Release/StartFpgaSimulator.cmd` 后通过 TCP 连接。`fpga_simulator_test`、`fpga_simulator_e2e_test` 和 `fpga_local_simulator_test` 覆盖协议/控制、独立进程链路和真实本地主窗口的记录回放。

## 常用入口

Windows：

```powershell
# 只构建桌面程序，不编译全部测试目标
powershell -ExecutionPolicy Bypass -File .\scripts\build-windows-msvc2022.ps1 -Action BuildApp

# 只构建 fast 集合需要的测试目标并运行
powershell -ExecutionPolicy Bypass -File .\scripts\build-windows-msvc2022.ps1 -Action TestFast

# 构建测试及其依赖并运行完整 CTest
powershell -ExecutionPolicy Bypass -File .\scripts\build-windows-msvc2022.ps1 -Action Test
```

Linux ARM64：

```bash
./scripts/build-linux-arm64.sh app
./scripts/build-linux-arm64.sh test-fast
./scripts/build-linux-arm64.sh test
```

## Windows 后台测试

Windows 的每个 CTest 命令默认经过 `vaporview_test_desktop.exe`。它用 `CreateDesktopW` 创建独立原生桌面，通过 `STARTUPINFO.lpDesktop` 将测试及其子进程放到该桌面，从不调用 `SwitchDesktop`。窗口可以正常创建、显示、处理事件和执行 Qt grab，但不会显示在当前输入桌面；当前桌面上的输入和窗口焦点不受测试窗口操作影响。直接运行 `ctest -R`、`ctest -L`、完整 preset 和 `TestFast` 都使用这一入口。

测试以低于正常的进程优先级运行；Windows preset 默认最多并行两个测试，3D/GPU 测试仍串行。Job Object 在测试结束、崩溃或 CTest 超时时回收子进程；创建隔离桌面失败时明确报错，不退回当前桌面。测试的 QSettings sandbox 隔离注册表和配置，独立桌面本身不隔离文件系统、注册表或网络。

后台测试仍占用 CPU、内存和 GPU；需要进一步降低负载可使用 `ctest --preset windows-msvc2022-x64-release -j1`。直接双击测试 exe 会绕过隔离入口，如需单独调用使用：

```powershell
build/Release/vaporview_test_desktop.exe build/Release/main_window_layout_test.exe --input-arrows-only
```

Qt `offscreen` 会改变平台行为，未将它用作 Windows 默认方案。自动化测试可在独立桌面完成；真实 DWM 透明合成、屏幕截图、UAC 提权和实机设备验收仍需专门验证，不能由隐藏桌面的测试结果代替。提权父进程测试在普通权限运行时保留明确 Skipped 状态，不弹 UAC。

实现依据：[CreateDesktopW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-createdesktopw)、[STARTUPINFO.lpDesktop](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/ns-processthreadsapi-startupinfow)、[子进程桌面继承](https://learn.microsoft.com/en-us/windows/win32/winstation/thread-connection-to-a-desktop)、[Job Objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects)。

## 按改动面运行

小改动优先构建直接相关的目标，再用测试名或标签收敛 CTest。以下命令需要在已经初始化的 MSVC Developer 环境中执行。

主窗口布局、导航或设备配置：

```powershell
cmake --build build/Release --config Release --target main_window_layout_test
ctest --test-dir build/Release -C Release -R "^main_window_layout_test$" --output-on-failure
```

日志 UI 展示策略、主窗口交互、重复合并和 SkyTui 日志模型：

```powershell
cmake --build build/Release --config Release --target ui_log_model_test main_window_log_panel_test sky_tui_log_model_test
ctest --test-dir build/Release -C Release -R "^(ui_log_model_test|main_window_log_panel_test|sky_tui_log_model_test)$" --output-on-failure
```

Session 数据查看、轨迹查看或主题：

```powershell
cmake --build build/Release --config Release --target session_viewer_theme_test
ctest --test-dir build/Release -C Release -R "^session_viewer_theme_test$" --output-on-failure
```

协议、Telemetry 或 RD105：

```powershell
cmake --build build/Release --config Release --target temperature_controller_protocol_test telemetry_codec_test telemetry_tcp_link_test
ctest --test-dir build/Release -C Release -L protocol --output-on-failure
```

FPGA VLP1、传感器 payload、RAW/DLIA fragment、离线会话和天空端远程对接：

```powershell
cmake --build build/Release --config Release -- -j4
ctest --test-dir build/Release -C Release -L fpga --output-on-failure
```

`fpga` 标签同时包含 `fpga_*` 和 `sky_fpga_*`：协议、USB/replay、控制器、归档、页面，以及 SkyCore 唯一 USB 后台、真实本机 TCP 模拟控制、串口带宽预算、并发录制及失败注入。仅用 `-R "^fpga_"` 会漏掉天空端测试。这些测试使用 fixture 和模拟传输，不需要连接 GP01，也不能替代实机 USB、B0/B1、无线吞吐和模拟波形验收。配置部署见 [天空拓扑](fpga_sky_topology.md)，实机验证前应确认驱动、BIT/IMG 哈希，并避免 Cypress Control Center 同时占用端点。

Sky 设备覆盖、模拟模式、本地 IPC/TUI 和统一 Device Config：

```powershell
cmake --build build/Release --config Release --target telemetry_codec_test sky_device_manager_simulation_test sky_ground_simulation_e2e_test sky_session_recorder_test sky_local_ipc_client_test sky_tui_controller_device_test device_config_layout_test
ctest --test-dir build/Release -C Release -R "^(telemetry_codec_test|sky_device_manager_simulation_test|sky_ground_simulation_e2e_test|sky_session_recorder_test|sky_local_ipc_client_test|sky_tui_controller_device_test|device_config_layout_test)$" --output-on-failure
```

## 本轮流程审查与精简

审查范围包括全部 CTest 注册、114 个测试源文件的入口/分支、共享窗口用例、标签/超时/串行属性、构建脚本和 presets。当前配置为 osgEarth ON、Python 可用：CTest 入口从 136 个减少到 133 个，fast 集合从 86 个收敛为 81 个。没有发现两个完全相同的 CTest 命令；主要重复发生在总用例与专用入口，以及多次创建完整窗口来检查同类样式。

| 原入口/流程 | 处理及保留的覆盖 |
|---|---|
| `home_environment_layout_test`、`ai8_overview_dark_theme_test`、`home_theme_layout_test` | 合并为 `home_appearance_layout_test`；同一个窗口检查环境卡片排版、主题切换的几何稳定性和深色 AI-8 可读性 |
| `combo_arrow_hover_test`、`spin_arrow_disabled_test` | 合并为 `input_arrow_style_test`；同一个窗口检查两种主题的下拉箭头和启用/禁用数字输入箭头 |
| 总布局测试重复调用上述箭头检查、首页语言宽度 round trip | 移除重复调用；专用测试保留真实渲染/语言宽度检查 |
| 标题栏缩放测试逐级遍历整段比例 | 直接设置边界附近的状态，再触发真实菜单操作；保留增减步长、上下限钳制及重复边界操作 |
| `TestFast` 先编译所有测试 | 改用按最终 `fast` 标签计算的 `VaporViewFastTests`；完整集合使用 `VaporViewTests`，辅助程序随依赖构建 |
| `trajectory_3d_layer_benchmark` 默认参与构建 | 改为 `EXCLUDE_FROM_ALL`；需要性能测量时显式构建，普通验证不编译 benchmark |
| FPGA 连接/控制超时、应用启动、3D 窗口烟雾测试 | 纠正标签：真实等待和完整窗口初始化不放入 fast；协议/算法小用例继续保留 |
| 过时的 fixture/assertion | 纯图标按钮按 accessibleName 定位；FPGA 测试明确中转路径并等待启动连接就绪；底部渐隐覆盖限定于安装了共享渐隐的三个页面；箭头像素检查排除合法焦点边框 |

以下集合检查不同契约，保留而不因名称相似删除：

| 集合 | 保留原因 |
|---|---|
| 协议/传感器/队列/坐标/配置单元测试 | 各自验证独立格式和边界，执行成本小 |
| FPGA core、独立模拟器进程、本地主窗口及 SkyCore 集成 | 分别覆盖解析/控制、跨进程、录制回放和唯一设备后台 |
| Session viewer 的 IO、窗口状态、轨迹、主题、组件 | 同一 exe 的不同参数执行互不重叠的分支 |
| UI 测试模式、导航覆盖、深色 EPSILON 布局 | 不同分支检查模拟模式隔离、页面数据和主题几何 |
| 日志语言/事件目录/遗留输出审计 | 分别验证语言、目录完整性和日志路径；新增事件应补目录，不能删除审计来掩盖失败 |
| local-device / sky-link source audit | 防止已迁移的 typed model 回退到 QWidget 作为状态源 |
| 3D 算法、窗口生命周期、真实地图加载 | 数据/渲染/关闭契约不同；未安装真实数据时明确 skip |
| EPSILON ACK/legacy fallback 和操作超时 | 真实时间链路仍有独特覆盖，保留 slow 集合；不把等待缩短到失去协议语义 |
| 更新、权限、重启恢复 | 普通父进程和提权父进程属于不同权限场景；不自动请求提权 |

`VaporViewFastTests` / `VaporViewTests` 只控制编译依赖，不会执行测试；仍使用对应 CTest preset 运行。按改动面选 focused tests，不要求每次先跑 fast 再跑完整集合。

## 本轮验证结果（2026-10-13，Windows / osgEarth ON）

- `cmake --build build/Release --config Release -- -j4` 成功。
- 完整 `ctest --preset windows-msvc2022-x64-release`：133 项，131 Passed、1 Skipped、1 Failed，511.89 秒。fast 的 81 项均在该轮通过，复用结果，不再重复执行 fast 集合。
- 未提权的 `update_relauncher_elevated_parent_test` 按设计 skip；`main_window_map3d_open_test` 保留已有的 `map title switches immediately` 失败，未改动地图产品逻辑。
- 标题栏菜单测试由精简前独立桌面实测 77.66 秒降至本轮 40.06 秒；主窗口总布局测试通过（65.71 秒）。
- 原生窗口隔离探针确认测试桌面与输入桌面不同，参数转义和非零退出码传递正确，退出后回收遗留子进程；完整测试运行期间抽样未发现测试进程的窗口或前台焦点出现在输入桌面。
- 测试前后应用注册表摘要相同，无新增 sandbox 注册表残留；测试进程全部退出。本次临时探针、截图/日志/清单不纳入版本库。
- Ninja 依赖查询确认 fast 编译目标不包含主窗口大矩阵、标题栏菜单、长超时 FPGA 测试、3D 窗口测试及手动 benchmark；Windows 构建脚本语法检查通过。Linux 分支仅检查等价 CMake/脚本路径，未执行远程或 Linux 构建。

## 标签

| 标签 | 用途 |
|---|---|
| `fast` | PR 和常规回归使用的快速集合 |
| `unit` | 核心算法、编解码和小型组件测试 |
| `integration` | 跨组件或真实窗口集成测试 |
| `ui` | Qt Widgets 窗口、布局、主题和启动测试 |
| `slow` | 大型 GUI 或真实数据测试 |
| `protocol` | Telemetry、TCP 和温控协议测试 |
| `fpga` | VLP1、传感器、RAW/DLIA replay、页面及 SkyCore 远程控制/归档/带宽测试 |
| `3d` | osgEarth/OSG 三维地图测试 |
| `real-data` | 依赖仓库本地西湖三维数据的测试 |

直接运行标签：

```powershell
ctest --test-dir build/Release -C Release -L fast --output-on-failure
ctest --test-dir build/Release -C Release -L 3d --output-on-failure
ctest --test-dir build/Release -C Release -L real-data --output-on-failure
```

每个测试在 CMake 中设置了 30 至 180 秒的超时，避免 GUI 或渲染生命周期异常无限阻塞测试流程。

## osgEarth 验证顺序

三维地图相关修改按以下顺序验证：

1. 配置 `VAPORVIEW_ENABLE_OSGEARTH=OFF`，确认默认桌面程序可以构建。
2. 配置 `VAPORVIEW_ENABLE_OSGEARTH=ON`，构建相关 3D/UI 测试目标。
3. 先跑 `3d` 或更小的测试集合；大范围改动再跑完整 CTest。
4. 最终让 `build/Release` 保持 ON，便于直接启动程序检查三维效果。

真实西湖数据未安装时，三个 `real-data` 测试会以 CTest 的 Skipped 状态结束。`map_data_script_test` 找不到 Python 时也使用相同的 skip 语义，不会伪装成 Passed。

## 视觉验收

布局测试只能验证几何、属性、stylesheet 和交互契约。Popup 阴影、透明合成、黑边和真实字体渲染仍需在 Windows 桌面上启动 `VaporView.exe` 并用真实截图确认。
