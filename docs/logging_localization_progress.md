# Logging Localization Progress

本文件记录日志中文化与结构化迁移的可审查进度。完成标准仍以 goal objective、`docs/logging.md`、`docs/logging_events.md`、自动审计和实际测试结果为准。

## 已审计模块

- `src/shared/logging/`：日志序列化、队列、emergency、子进程输出和 Qt message handler。
- 应用入口：`src/app/ground_main.cpp`、`StartupSplash.cpp`、SkyCore/SkyTui 控制台运行状态输出。
- 天空端运行链路：`SkyRuntime`、`SkyDeviceManager`、`SkyLocalIpcServer`、`SkyLocalIpcClient`、`SkyTuiApp`。
- 地面端遥测/记录/RTK：`GroundTelemetryService`、`GroundMainWindowRecording`、`MainWindow` session sink、`RtkConfigDialog`。
- 自动审计覆盖：`src/` 和 `include/` 下 C++ 源码中的第一方日志调用、`notifyFailure` 内部诊断文本和 stdout/stderr `QTextStream` 字面量。
- 全量 CTest 覆盖结构化日志、IPC、遥测、session、RTK 和地图相关回归。

## 已迁移内容

- 第一方运行日志 message 改为简体中文。
- `source`、`category`、`event`、`error_code`、`reason_code` 和字段键保持英文。
- SkyRuntime / IPC / Ground telemetry / RTK / session sink 的 Warning/Error/Critical 补稳定 `event`，错误事件补 `error_code` 或 `reason_code`。
- 子进程、socket、串口、设备采集器和 UI legacy 文本保留在 `process_output`、`system_error`、`external_raw_text` 或 `ui_message` 字段中。
- 移除了通过 `message.contains(...)` 判断日志级别或分类的主要路径；旧 UI-only 字符串路径改为 `legacy_unclassified=true`。
- `log_service_test` 增加同一失败文案在 Warning/Error 下保持显式 level 的回归断言。
- `diagnosticFailure` 内部故障提示和 SkyCore/SkyTui 运行诊断输出改为中文；SkyTui shutdown 路径改为转发结构化 `LogRecord`，旧字符串信号仅用于终端显示。
- `log_service_test` 和 `sky_ipc_log_test` 覆盖中文 IPC 往返、系统原始错误字段和外部原文不丢失。

## 有意保留英文

- 稳定机器标识：`level`、`source`、`category`、`event`、`error_code`、`reason_code`、字段键、枚举值和协议类型。
- 产品、协议和设备名称：SkyCore、SkyTui、IPC、TCP、JSON、CRC、EPSILON、PTB、HMP、RD105、Wave TCP 等。
- 命令行帮助、参数名、示例命令和 UI 英文界面文本。
- 外部原始文本：Qt/OS/socket/serial 错误、子进程 stdout/stderr、设备/驱动输出、协议 payload。
- 测试 sentinel 和审计自测 fixture。

## 新增事件

详见 `docs/logging_events.md`。本轮新增或明确化事件覆盖日志系统、应用启动参数、SkyRuntime 生命周期、设备连接/队列、IPC、遥测协议、session sink、RTK 和 legacy UI 隔离。

## 自动审计状态

- 脚本：`scripts/audit_logging_language.py`。
- CTest：`logging_language_audit_test`（已重新配置并注册）。
- 最近直接运行命令：`python scripts\audit_logging_language.py --root . --self-test`。
- 最近直接运行结果：通过，输出 `logging language audit passed`。
- focused logging/IPC tests：全部通过。
- `build/Release` Release 构建：通过。
- 全量 CTest（当前工作区）：66/69 通过；2 个稳定 GUI 几何/交互断言失败，另有 1 个地图 GUI SegFault：
  `main_window_layout_test`（RTK GGA 控件垂直居中）、
  `session_viewer_trajectory_test`（轨迹卡片标题选择/复制）、
  `map3d_real_data_load_test`（全量运行时偶发 SegFault，单独复跑通过）。

## 剩余问题

- 全量 CTest 中的 3 个 GUI/地图问题需要独立基线/环境排查；本轮不修改无关布局、session-viewer 或地图行为。
- Linux ARM64：WSL 为 x86_64；虽有 `aarch64-linux-gnu-g++`，但没有 Linux ARM64 Qt 6 工具链，无法完成本地 ARM64 构建/运行验证。一次交叉配置可生成，但构建误用了 Windows MinGW `moc.exe`，不作为兼容性证据。
- 最终收尾已完成 staged 文件审计、最终 `build/Release` 构建、提交并推送；Linux ARM64 未做实机或完整 Qt 交叉运行验证，仅通过固定宽度整数字段修正消除 LP64 ABI 歧义。
