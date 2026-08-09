# Logging Localization Progress

本文件记录日志中文化与结构化迁移的可审查进度。完成标准仍以 goal objective、`docs/logging.md`、`docs/logging_events.md`、自动审计和实际测试结果为准。

## 已审计模块

- `src/shared/logging/`：日志序列化、队列、emergency、子进程输出和 Qt message handler。
- 应用入口：`src/app/ground_main.cpp`、`StartupSplash.cpp`、SkyCore/SkyTui 控制台运行状态输出。
- 天空端运行链路：`SkyRuntime`、`SkyDeviceManager`、`SkyLocalIpcServer`、`SkyLocalIpcClient`、`SkyTuiApp`。
- 地面端遥测/记录/RTK：`GroundTelemetryService`、`GroundMainWindowRecording`、`MainWindow` session sink、`RtkConfigDialog`。
- 自动审计覆盖：`src/` 和 `include/` 下 C++ 源码中的第一方日志调用、`qDebug/qInfo/qWarning/qCritical` 字面量、明显中英混杂句、`notifyFailure` 内部诊断文本、stdout/stderr `QTextStream` 字面量、机器标识命名和事件目录漂移。
- 全量 CTest 覆盖结构化日志、IPC、遥测、session、RTK 和地图相关回归。

## 已迁移内容

- 第一方运行日志 message 改为简体中文。
- `source`、`category`、`event`、`error_code`、`reason_code` 和字段键保持英文。
- SkyRuntime / IPC / Ground telemetry / RTK / session sink 的 Warning/Error/Critical 补稳定 `event`，错误事件补 `error_code` 或 `reason_code`。
- 子进程、socket、串口、设备采集器和 UI legacy 文本保留在 `process_output`、`system_error`、`external_raw_text` 或 `ui_message` 字段中。
- 移除了通过 `message.contains(...)` 判断日志级别或分类的主要路径；旧 UI-only 字符串日志桥接已迁移为结构化事件，第一方生产代码不再使用 `legacy_unclassified=true`。
- `log_service_test` 增加同一失败文案在 Warning/Error 下保持显式 level 的回归断言。
- LogService writer failure 内部提示和 SkyCore/SkyTui 运行诊断输出改为中文；SkyTui shutdown 路径改为转发结构化 `LogRecord`，普通终端显示不再作为跨组件日志协议。
- `log_service_test` 和 `sky_ipc_log_test` 覆盖中文 IPC 往返、系统原始错误字段和外部原文不丢失。

## 有意保留英文

- 稳定机器标识：`level`、`source`、`category`、`event`、`error_code`、`reason_code`、字段键、枚举值和协议类型。
- 产品、协议和设备名称：SkyCore、SkyTui、IPC、TCP、JSON、CRC、EPSILON、PTB、HMP、RD105、Wave TCP 等。
- 命令行帮助、参数名、示例命令和 UI 英文界面文本。
- 外部原始文本：Qt/OS/socket/serial 错误、子进程 stdout/stderr、设备/驱动输出、协议 payload。
- 测试 sentinel 和审计自测 fixture。

## 新增事件

详见 `docs/logging_events.md`。本轮补齐启动参数兼容模式、启动页资源失败、Qt message handler、用户可见问题 fallback、Ground/SkyTui 遥测解析、IPC 配置与日志帧解析、SkyTui UI legacy、SkyCore legacy 设备日志等已在源码使用但未登记的事件。

## 本轮收尾

- `qDebug()` 已纳入直接 Qt 日志审计，与 `qInfo()`、`qWarning()`、`qCritical()` 使用同一语言规则。
- 第一方 message 增加明显中英混杂检测，允许产品名、协议名、路径、文件名和第三方原文保留英文。
- `category`、`event`、`error_code`、`reason_code`、字面量 fields key 和中文 source 误用已自动验证。
- 新增 `scripts/audit_logging_events.py`，自动比较源码事件和 `docs/logging_events.md`，检查遗漏、过期、重复、category/level 冲突和错误事件缺少错误码。
- shutdown 场景不再拆分 `sky_recording_stop_requested_for_shutdown` / `sky_recording_metadata_save_failed_on_shutdown`，改为复用稳定事件并写入 `reason_code=APPLICATION_SHUTDOWN`。

## 自动审计状态

- 脚本：`scripts/audit_logging_language.py`、`scripts/audit_logging_events.py`。
- CTest：`logging_language_audit_test`、`logging_event_catalog_audit_test`（均已注册）。
- 最近直接运行命令：`python scripts\audit_logging_language.py --root . --self-test`。
- 最近直接运行结果：通过，输出 `logging language audit passed`。
- 最近直接运行命令：`python scripts\audit_logging_events.py --root . --self-test`。
- 最近直接运行结果：通过，输出 `logging event catalog audit passed`。
- focused logging/IPC tests：`ctest --test-dir build\Release -C Release -R "^(log_service_test|sky_ipc_log_test)$" --output-on-failure` 通过。
- 审计 CTest：`ctest --test-dir build\Release -C Release -R "^(logging_language_audit_test|logging_event_catalog_audit_test)$" --output-on-failure` 通过。
- `build/Release` Release 构建：`cmake --build build\Release --config Release -- -j1` 通过。
- 快速 CTest：`ctest --test-dir build\Release -C Release -L fast --output-on-failure` 为 52/53 通过；当时 `vaporview_startup_test` 未通过，原因是 `build/Release/VaporView.exe` 仍嵌入 `requireAdministrator` 清单，非提权 QProcess 启动被 Windows 拒绝。
- 全量 CTest：`ctest --test-dir build\Release -C Release --output-on-failure` 为 67/70 通过；`main_window_layout_test`、`session_viewer_trajectory_test` 和 `vaporview_startup_test` 未通过，均不在本轮日志治理改动面。

## 剩余问题

- `main_window_layout_test` 仍有 RTK GGA 控件垂直居中断言失败，需要独立 UI 布局排查；本轮未修改相关页面。
- `session_viewer_trajectory_test` 仍有轨迹卡片标题选择/复制断言失败，需要独立 session viewer UI 排查；本轮未修改相关页面。
- `vaporview_startup_test` 曾与当时 Windows 主程序 `requireAdministrator` 清单存在测试环境冲突；后续权限模型改为主程序显式 `asInvoker` 后，应重新以当前构建结果验证该 smoke test。
