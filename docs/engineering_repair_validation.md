# 工程审查修复验证

本页记录本轮修复的行为边界和现场验收要求，不代表已完成实机或断电验证。

## 已实现的保护

| 链路 | 行为 | 回归入口 |
| --- | --- | --- |
| RTK 配置 | 在进入 RTKLIB 前检查 UTF-8 字节长度、内嵌 NUL 和串口子字段容量 | `rtk_serial_topology_test` |
| RTK GGA | 在 RTKLIB 锁内更新位置；动态位置有效期为 3 秒；无效位置停止生成 GGA | `rtk_serial_topology_test` 的本地 caster 实验 |
| Session 缓存 | 自动写回只允许会话内标准 waveform peak 文件；拒绝中间目录和目标文件的符号链接、junction | `session_core_test` |
| 地图下载 | 清单目标必须位于 `resources/maps/`；安装和删除时检查路径链接 | `map_resource_manifest_test`、`map_resource_download_test` |
| TCP 遥测 | 换连接时清空流解析状态；单连接待发送数据上限 4 MiB，超限断开 | `telemetry_tcp_link_test` |
| 本地 IPC | 单客户端待发送数据上限 4 MiB；延迟断开慢客户端，避免广播中的重入删除 | `sky_command_execution_test` 的 512 帧慢消费者突发及 Core 响应检查；`sky_local_ipc_client_test` |
| EPSILON 配置 | collector 操作移到后台线程；Core 线程发布最终结果；执行中的重复请求共享结果 | `sky_command_execution_test` |
| 命令重试 | DeviceOperation 结果缓存最多 128 项、保留 120 秒；EPSILON 重试间隔 15 秒；旧连接回调不向新连接发送 ACK | `sky_command_execution_test`；物理执行次数仍须实机确认 |
| 两端录制 | header 短写拒绝启动；RAW/测量 CSV/peak 写入和最终 flush 失败被锁存；停止时写入 `state=incomplete`，加载时显示不完整警告 | `ground_recording_service_test`、`sky_session_recorder_test`、`session_core_test` |
| 暂停恢复 | session 起始时间不变；有效录制时长使用单调时钟累加 | 两端 recorder 测试 |
| IFW | Linux 维护工具不再执行 Windows marker/PermissionTool 复制 | `node tests/ifw_maintenance_operations_test.js` |
| 菜单生命周期 | 忽略隐藏/禁用菜单项上迟到的指针事件，避免已关闭子菜单再次出现 | `title_application_menu_test` 的排队 Enter 事件回归 |

## 保证范围

- `QFile::flush()` 成功不等于介质已经完成断电安全持久化；本轮没有增加 `fsync`/`FlushFileBuffers` 协议。
- 失败后的最后一个 RAW record 或 CSV 行可能只有前缀；`incomplete` 用于阻止把该会话误认为完整采集，不会恢复不存在的数据。
- 命令去重缓存仅存在于当前 Core 进程。Core 在设备执行后、回复前崩溃时，重启后的物理状态仍需查询确认；不提供跨重启 exactly-once 保证。
- 路径检查防止导入包中的静态路径逃逸和链接。不宣称可以抵抗拥有本地目录写权限的攻击者并发替换路径的 TOCTOU 攻击。
- TCP/IPC 上限保护 Qt 用户态发送队列；超限采用断开策略，并非可靠消息持久化或无损传输。

## 本机验证中发现的测试契约偏差

- EPSILON 配置等待窗口已延长；`epsilon_device_session_test` 验证前 5 秒不会提前超时，并在真实重试期限后报告 Timeout，因此归入 integration，超时上限为 75 秒。
- 地图下载 fixture 改为 `resources/maps/` 下的独立测试目录，继续检查下载、校验失败恢复和删除。
- 真实 3D 窗口测试显式执行“启动渲染 → 重载最佳本地地图”，再检查 55 个建筑载荷及关闭。默认预热视图和轻量启动不等于已经加载重型地图。
- 主窗口标签复用测试的对象快照移到数据源切换前，避免使用此前语言切换已销毁的 QLabel 指针。原有宽度比例相关改动保留。
- 遥测摘要卡片从本地切换远程时曾减少 50 像素。实际原因是字体/主题改变后，`RecordingStatusView` 的列宽缓存没有刷新，直到状态文本变化才重新计算，令右侧面板突然变宽。样式更新现在显式刷新录制卡片尺寸；本地/远程几何和标签复用断言已通过。新增同一文本下字体放大、恢复及标签对象复用的回归检查。
- 9 月 13 日的主窗口断言曾要求环境卡片占总宽度的 17%–23%、EPSILON 至少为其 3.6 倍；当时修正为检查内容最小宽度和剩余空间的 4:1 分配，完整主窗口测试通过。之后独立提交 `c3562190` 改为固定首页导航卡宽度；旧分配规则及其验证结果不能直接代表当前布局，当前复测见下文。
- 3D 关闭测试在创建 QApplication 前，通过 `ImmDisableIME(GetCurrentThreadId())` 关闭当前测试线程的原生 IME，并检查返回值。该测试没有文字输入场景；此隔离只作用于测试进程，不改变系统输入法设置，也不改变 VaporView 正式程序的输入能力。[Windows API 契约](https://learn.microsoft.com/en-us/windows/win32/api/imm/nf-imm-immdisableime)。这不是对第三方输入法模块的修复，IME 共存的生产环境仍需单独验收。

## 现场验收

1. EPSILON 配置操作期间持续采集其它传感器，观察 Core 心跳；重复发送同一请求，测量串口实际配置事务次数为一次。执行中断开 Ground 后重连，确认旧回调没有把结果关联到新命令。
2. NTRIP caster 记录从位置 A 移动到 B 的 GGA，确认不需要重新启动 RTK；停止定位更新，确认 3 秒有效期后不再发送旧位置。
3. 在独立测试卷上制造空间耗尽，分别覆盖 RAW、CSV、日志、初始 header、最终 manifest；检查 UI 提示、计数和 `state`。自动测试当前覆盖 header/RAW/测量 CSV 短写及最终 flush，不能替代全部文件故障矩阵。
4. Ground TCP 客户端和 TUI IPC 客户端连接后停止读取，持续输入真实峰值速率；确认慢客户端被断开、内存有界、Sky 采集与本地记录继续。
5. 用隔离安装目录分别运行 Linux IFW 和 Windows IFW 升级，验证中断升级与恢复；JavaScript 操作列表测试不等同于实际安装器执行。
6. 独立长时间采集包含反复 pause/resume、重连和进程异常结束，比较源端计数、RAW 有效前缀、CSV 行数与 manifest。队列丢弃、日志写入失败、断电恢复仍需单独验收。

## 2026-09-12 本机执行结果

- 在 VS/MSVC Developer 环境、UTF-8 代码页下执行 `cmake -S . -B build/Release` 和 `cmake --build build/Release --parallel 4`，完整构建成功，osgEarth 保持开启。
- 最终 `ctest --test-dir build/Release -L fast --output-on-failure`：67/67 通过。
- `epsilon_device_session_test`：真实等待重试期限后通过，耗时约 60.5 秒；未缩短生产超时以迁就测试。
- `map3d_real_window_close_test`、`main_window_map3d_real_close_test`：修正显式操作流程后 2/2 通过。
- `session_core_test` 及两端 recorder、异步命令、地图下载、日志目录检查均有单独通过记录；IFW JavaScript 的 Linux/Windows、旧/新 Framework 四种操作组合通过。
- 曾执行完整 102 项测试，首轮 6 项失败、1 项跳过；随后逐项修复复测。`update_relauncher_standard_parent_test` 因当前父进程条件跳过。主窗口后续定位结果见上文及以下复测记录。
- RTK 对象的 Ninja 依赖记录包含 `rtklib.h`（180 项有效依赖），已检查头文件改动的依赖跟踪。
- 未连接真实设备，未执行 Linux 实际安装、拔电、实盘空间耗尽或长期现场压力验收。

## 2026-09-13 复测

- 修复录制状态字体尺寸缓存后，再次执行 `cmake --build build/Release --parallel 4`，完整构建成功，osgEarth 为 ON。
- `ctest --test-dir build/Release --output-on-failure`：102 项中 99 项通过、2 项失败、1 项跳过，耗时 427.48 秒。其中 fast 的 67 项全部通过。
- `main_window_layout_test` 已通过新增字体放大/恢复检查、标签复用及源模式切换几何检查；后续仍在环境卡片总宽度约 1/5 的断言失败，契约冲突见上文。
- `map3d_real_window_close_test` 在全量运行中发生一次访问冲突。Windows Application Error 事件报告故障模块为 `SogouPY.ime`、异常 `0xc0000005`；单独复跑通过（10.12 秒）。这提供输入法模块相关线索，但没有调用栈，不能据此断言 VaporView 的所有关闭路径都安全。
- `update_relauncher_standard_parent_test` 因父进程条件跳过，elevated 版本通过。
- 对 3D 关闭测试再执行 `--repeat until-fail:3`，连续三次通过（10.35 / 9.07 / 10.28 秒）；连同首次单独复跑共四次通过，但不抹去全量运行中的崩溃记录。
- `node tests/ifw_maintenance_operations_test.js` 再次通过四种操作组合。

## 收尾验证

- 修正剩余宽度分配断言后，`main_window_layout_test` 完整通过（71.50 秒），`map3d_real_window_close_test` 通过（9.81 秒）。
- 增加测试线程的 IME 隔离后，3D 关闭测试 `--repeat until-fail:5` 五次均通过（9.70 / 8.21 / 9.19 / 9.03 / 8.71 秒）。该结果仅覆盖不使用原生输入法的渲染/关闭路径，不替代正式应用与第三方 IME 共存验证。
- 再次使用 `cmake -S . -B build/Release` 配置，并执行 `cmake --build build/Release --parallel 4`，完整构建成功。
- 后续全量回归发现一次标题菜单关闭失败。新增确定性用例：菜单仍显示时排队两个 `QEnterEvent`，关闭菜单后再分发事件，要求所有子菜单保持关闭。修复前此用例失败（4.88 秒）；菜单事件过滤器现检查目标的可见/可用状态。测试 hover helper 同时改为使用 `QEnterEvent`，不再以普通 `QEvent` 冒充进入事件。
- 修复该菜单问题后，`main_window_layout_test` 和 `title_application_menu_test` 2/2 通过（73.27 / 66.67 秒）；再次完整构建成功。

## 2026-09-15 当前工作区复核

- 基于当前 HEAD `8178a15f` 及本轮未提交修复复核。中断期间已有独立 UI 提交，本轮保留这些提交，不回滚其布局规则。
- 当前版本执行 `cmake --build build/Release --parallel 4` 成功。
- 对本轮改动运行 11 项 focused tests：10 项通过，包括 TCP 遥测、两端 recorder、RTK、异步命令、Session、地图清单/下载、菜单专项及 3D 关闭；`main_window_layout_test` 在菜单重开选择状态断言失败。
- 仅增加临时诊断后复测综合布局：菜单检查通过，随后在 `dragging the home sensor separator changes both card widths` 断言失败（44.04 秒）。当前存在独立的固定首页导航卡宽度改动 `c3562190`，本轮不扩大范围修改该布局契约。临时诊断已移除。
- 当前综合 UI 测试仍未通过；不将之前的通过记录或已被覆盖的全量日志表述为当前全量成功。最初的工程修复及其直接回归与这项综合 UI 失败分别记录。
