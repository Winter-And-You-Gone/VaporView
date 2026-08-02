# Logging Event Catalog

本清单定义 VaporView 第一方日志的稳定机器事件。`message` 为推荐简体中文文案；`source`、`category`、`event`、`error_code`、`reason_code` 和字段键保持英文。

| source | category | event | recommended level | 中文 message | required fields | optional fields | error_code / reason_code |
| --- | --- | --- | --- | --- | --- | --- | --- |
| App | lifecycle | logging_started | Info | 应用日志系统已启动。 | log_directory | fallback_directory |  |
| App | lifecycle | logging_stopped | Info | 应用日志系统已停止。 |  |  |  |
| App | startup.arguments | startup_argument_invalid | Critical | 启动参数无效。 | argument, value | transport | INVALID_TELEMETRY_TRANSPORT / INVALID_TELEMETRY_TCP_PORT |
| App | startup.arguments | startup_argument_compatibility_mode | Info | 已按兼容规则选择串口遥测。 | argument, selected_transport |  |  |
| App | startup.arguments | startup_argument_missing | Critical | 启动参数缺失。 | argument, transport |  | MISSING_TELEMETRY_PORT |
| App | startup.lifecycle | sky_background_mode_started | Info | 天空端后台模式已启动。 | transport | split_core_executable, split_tui_executable |  |
| Ground | startup | startup_splash_logo_load_failed | Warning | 启动页标志加载失败。 | path |  | STARTUP_SPLASH_LOGO_LOAD_FAILED |
| LogService | queue | log_queue_overloaded | Warning | 日志写入队列已满，已丢弃部分日志记录。 | dropped_count |  | LOG_QUEUE_FULL |
| LogService | queue.critical_overload | critical_queue_overload | Critical | Critical 日志队列已达到上限，已切换到紧急写入通道。 | pending_critical_limit |  | CRITICAL_QUEUE_LIMIT |
| Qt | qt | qt_message | Debug/Info/Warning/Critical | Qt message handler 已转发日志。 |  | file, function, line | QT_CRITICAL_MESSAGE / QT_FATAL_MESSAGE |
| Any | ui.legacy | user_issue_reported | Info/Warning/Error | 用户可见问题已上报。 | ui_visible | legacy_unclassified | UNCLASSIFIED_USER_ISSUE |
| Any | process | child_process_output | Debug/Warning | 已收到子进程标准输出。 / 已收到子进程错误输出。 | stream, process_output, raw_bytes | partial |  |
| Any | process | child_process_finished | Info/Error | 子进程已结束。 | exit_code, exit_status |  | CHILD_PROCESS_ABNORMAL_EXIT |
| Any | process | child_process_error | Error | 子进程发生错误。 | process_error |  | CHILD_PROCESS_ERROR |
| SkyCore | runtime.lifecycle | sky_runtime_started | Info | SkyRuntime 已开始运行。 | endpoint, transport |  |  |
| SkyCore | runtime.lifecycle | sky_runtime_stopped | Info | SkyRuntime 已停止。 |  |  |  |
| SkyCore | config.load | sky_config_load_failed | Warning | 天空端配置加载失败，已使用默认配置。 | config_path, system_error |  | SKY_CONFIG_LOAD_FAILED |
| SkyCore | config.apply | sky_config_apply_rejected | Warning | 天空端配置无效，未应用。 | system_error |  | SKY_CONFIG_INVALID |
| SkyCore | config.save | sky_config_save_failed | Error | 无法保存天空端配置。 | config_path, system_error |  | SKY_CONFIG_SAVE_FAILED |
| SkyCore | telemetry.serial | telemetry_serial_port_missing | Error | 无法启动串口遥测：串口名称为空。 |  |  | TELEMETRY_SERIAL_PORT_MISSING |
| SkyCore | telemetry.serial | telemetry_serial_open_failed | Error | 无法打开天空端遥测串口。 | port, baud | system_error | TELEMETRY_SERIAL_OPEN_FAILED |
| SkyCore | telemetry.tcp | telemetry_tcp_listen_failed | Error | 无法监听天空端 TCP 遥测端点。 | host, port | system_error | TELEMETRY_TCP_LISTEN_FAILED |
| SkyCore | telemetry.link | telemetry_link_error | Warning | 天空端遥测链路异常。 | system_error |  | TELEMETRY_LINK_ERROR |
| SkyCore | telemetry.link | telemetry_link_status | Info | 天空端遥测链路状态已更新。 | external_raw_text |  |  |
| SkyCore | session.recording | sky_recording_started | Info | 天空端会话记录已开始。 | session_directory, transport, endpoint |  |  |
| SkyCore | session.recording | sky_recording_paused | Info | 天空端会话记录已暂停。 | session_directory |  |  |
| SkyCore | session.recording | sky_recording_stop_requested | Info | 天空端会话记录已请求停止。 | telemetry_rows, waveform_frames | reason_code | APPLICATION_SHUTDOWN |
| SkyCore | session.recording | sky_recording_stopped | Info | 天空端会话记录已停止。 | session_directory |  |  |
| SkyCore | session.recording | sky_recording_start_failed | Error | 无法启动天空端会话记录。 | system_error |  | SKY_RECORDING_START_FAILED |
| SkyCore | session.recording | sky_recording_stop_failed | Error | 无法停止天空端会话记录。 | system_error |  | SKY_RECORDING_STOP_FAILED |
| SkyCore | session.write | sky_recording_metadata_save_failed | Error | 无法保存天空端会话记录元数据。 | system_error | reason_code | SKY_RECORDING_METADATA_SAVE_FAILED / APPLICATION_SHUTDOWN |
| SkyCore | session.write | sky_session_log_append_failed | Error | 无法写入天空端会话日志记录。 | session_sink_failure, event_ok, error_ok, source, category |  | SKY_SESSION_LOG_APPEND_FAILED |
| SkyCore | device.raw_queue | raw_frame_queue_overloaded | Warning | 原始数据帧队列已满，已丢弃部分数据。 | dropped_count, total_dropped_count |  | RAW_FRAME_QUEUE_FULL |
| SkyCore | device.connection | device_disconnected | Info | 设备已断开，缓存数据已失效。 | device_id |  |  |
| SkyCore | device.collector | device_collector_output | Info | 设备采集器输出了原始诊断信息。 | device_id, process_output, external_raw_text |  |  |
| SkyCore | device.legacy | legacy_device_log | Info | 设备 legacy 日志已更新。 | legacy_unclassified |  |  |
| SkyCore | device.wave_tcp | wave_tcp_peak_search_range_updated | Info | Wave TCP 峰值搜索范围已更新。 | start_index, end_index |  |  |
| SkyCore | device.wave_tcp | wave_tcp_resync_discarded_bytes | Warning | Wave TCP 重新同步时已丢弃部分字节。 | dropped_bytes |  | WAVE_TCP_FRAME_HEADER_NOT_FOUND |
| SkyCore | device.wave_tcp | wave_tcp_resync_skipped_bytes | Warning | Wave TCP 重新同步时已跳过部分字节。 | skipped_bytes, header_order |  | WAVE_TCP_FRAME_OFFSET |
| SkyCore | device.wave_tcp | wave_tcp_payload_format_locked | Info | Wave TCP 载荷格式已锁定。 | header_order, float_encoding |  |  |
| SkyCore | ipc | sky_ipc_listening | Info | 本地 IPC 服务已开始监听。 | host, port |  |  |
| SkyCore | ipc | sky_ipc_listen_failed | Error | 本地 IPC 服务监听失败。 | host, port, system_error |  | SKY_IPC_LISTEN_FAILED |
| SkyCore | ipc | sky_ipc_client_connected | Info | 本地 IPC 客户端已连接。 | peer_host, peer_port |  |  |
| SkyCore | ipc | sky_ipc_client_disconnected | Info | 本地 IPC 客户端已断开。 |  |  |  |
| SkyCore | ipc | sky_ipc_client_error | Warning | 本地 IPC 客户端连接异常。 | socket_error, system_error |  | SKY_IPC_CLIENT_SOCKET_ERROR |
| SkyCore | ipc | sky_ipc_shutdown_requested | Info | 已收到本地 IPC 停止请求。 | command_id, command_seq |  |  |
| SkyTui | ipc.connection | sky_ipc_connected | Info | SkyCore IPC 已连接。 | host, port |  |  |
| SkyTui | ipc.connection | sky_ipc_disconnected | Info | SkyCore IPC 连接已断开。 |  |  |  |
| SkyTui | ipc.connection | sky_ipc_unavailable_retrying | Warning | SkyCore IPC 暂不可用，将自动重连。 | socket_error, system_error |  | SKY_IPC_UNAVAILABLE |
| SkyTui | ipc.connection | sky_ipc_socket_error | Error | SkyCore IPC 发生错误。 | socket_error, system_error |  | SKY_IPC_SOCKET_ERROR |
| SkyTui | ipc.connection | sky_ipc_reconnect_scheduled | Info | SkyCore IPC 自动重连已计划。 | retry_delay_ms |  |  |
| SkyTui | ipc.command | sky_ipc_command_send_skipped | Warning | 未连接 SkyCore，命令未发送。 | command_id, command_value |  | SKY_IPC_NOT_CONNECTED |
| SkyTui | ipc.protocol | sky_ipc_telemetry_basic_parse_failed | Warning | 无法解析 SkyCore TelemetryBasic 载荷。 | message_type, payload_bytes |  |  |
| SkyTui | ipc.protocol | sky_ipc_waveform_downsampled_parse_failed | Warning | 无法解析 SkyCore WaveformDownsampled 载荷。 | message_type, payload_bytes |  |  |
| SkyTui | ipc.protocol | sky_ipc_waveform_feature_parse_failed | Warning | 无法解析 SkyCore WaveformFeature 载荷。 | message_type, payload_bytes |  |  |
| SkyTui | ipc.protocol | sky_ipc_telemetry_status_parse_failed | Warning | 无法解析 SkyCore TelemetryStatus 载荷。 | message_type, payload_bytes |  |  |
| SkyTui | ipc.protocol | sky_ipc_command_ack_parse_failed | Warning | 无法解析 SkyCore CommandAck 载荷。 | message_type, payload_bytes |  |  |
| SkyTui | ipc.protocol | sky_ipc_config_parse_failed | Warning | 无法解析 SkyCore 返回的配置。 | payload_bytes | system_error | SKY_IPC_CONFIG_PARSE_FAILED |
| SkyTui | ipc.config | sky_ipc_config_apply_result_received | Info | 已收到 SkyCore 配置应用结果。 | config_apply_result |  |  |
| SkyTui | ipc.protocol | sky_ipc_log_event_parse_failed | Warning | 无法解析 SkyCore 日志帧。 | message_type, payload_bytes |  |  |
| SkyTui | ipc.protocol | sky_ipc_error_frame_received | Error | 已收到 SkyCore telemetry Error 帧。 | payload_hex, payload_bytes |  | SKY_IPC_ERROR_FRAME |
| SkyTui | ui | sky_tui_ui_log | Info | SkyTui 界面日志已更新。 | ui_visibility, ui_visible, legacy_unclassified |  |  |
| Ground | telemetry.serial | ground_telemetry_serial_open_failed | Error | 无法打开地面端遥测串口。 | port, baud | system_error | GROUND_TELEMETRY_SERIAL_OPEN_FAILED |
| Ground | telemetry.tcp | ground_telemetry_tcp_connect_failed | Error | 无法连接地面端 TCP 遥测端点。 | host, port | system_error | GROUND_TELEMETRY_TCP_CONNECT_FAILED |
| Ground | telemetry.link | ground_telemetry_link_error | Warning | 地面端遥测链路异常。 | system_error |  | GROUND_TELEMETRY_LINK_ERROR |
| Ground | telemetry.link | ground_telemetry_link_status | Info | 地面端遥测链路状态已更新。 | external_raw_text |  |  |
| Ground | telemetry.command | telemetry_command_ack_timeout | Warning | 天空端命令 ACK 等待超时。 | command_id, command_value, command_seq |  | TELEMETRY_COMMAND_ACK_TIMEOUT |
| Ground | protocol.crc | telemetry_crc_or_version_error | Warning | 遥测解码器拒绝了 CRC 或协议版本错误的数据帧。 | total_errors, delta |  |  |
| Ground | protocol.frame | telemetry_frame_dropped | Warning | 遥测解码器已丢弃过大或格式错误的数据帧。 | total_dropped, delta |  |  |
| Ground | protocol.parse | telemetry_basic_parse_failed | Warning | 无法解析 TelemetryBasic 遥测载荷。 | message_type, payload_bytes |  |  |
| Ground | protocol.parse | waveform_downsampled_parse_failed | Warning | 无法解析 WaveformDownsampled 遥测载荷。 | message_type, payload_bytes |  |  |
| Ground | protocol.parse | waveform_feature_parse_failed | Warning | 无法解析 WaveformFeature 遥测载荷。 | message_type, payload_bytes |  |  |
| Ground | protocol.parse | telemetry_status_parse_failed | Warning | 无法解析 TelemetryStatus 遥测载荷。 | message_type, payload_bytes |  |  |
| Ground | protocol.parse | command_ack_parse_failed | Warning | 无法解析 CommandAck 遥测载荷。 | message_type, payload_bytes |  |  |
| Ground | protocol.parse | sky_config_parse_failed | Warning | 无法解析 SkyConfig JSON 遥测载荷。 | message_type, payload_bytes |  |  |
| Ground | protocol.parse | sky_config_apply_result_parse_failed | Warning | 无法解析 SkyConfigApplyResult JSON 遥测载荷。 | message_type, payload_bytes |  |  |
| Ground | protocol.parse | temperature_controller_status_parse_failed | Warning | 无法解析 TemperatureControllerStatus 遥测载荷。 | message_type, payload_bytes |  |  |
| Ground | protocol.parse | log_event_parse_failed | Warning | 无法解析 LogEvent 遥测载荷。 | message_type, payload_bytes |  |  |
| Ground | protocol.error | telemetry_error_frame_received | Error | 已收到遥测 Error 帧。 | payload_hex, payload_bytes |  | TELEMETRY_ERROR_FRAME |
| Ground | protocol.unknown | unknown_telemetry_message_type | Warning | 已收到未知遥测消息类型。 | message_type, payload_bytes |  |  |
| Ground | session.write | session_event_log_append_failed | Error | 无法从会话日志接收器写入 event_log.csv。 | session_sink_failure, source, category |  | SESSION_EVENT_LOG_APPEND_FAILED |
| Ground | session.write | session_error_log_append_failed | Error | 无法从会话日志接收器写入 error_log.txt。 | session_sink_failure, source, category |  | SESSION_ERROR_LOG_APPEND_FAILED |
| Ground | session.write | recording_stop_summary_append_failed | Error | 无法写入记录停止摘要。 |  |  | RECORDING_STOP_SUMMARY_APPEND_FAILED |
| Ground | ui.legacy | ground_ui_legacy_log | Info | 地面端界面日志已更新。 | ui_visibility, ui_message, legacy_unclassified, ui_visible |  |  |
| Ground | ui.progress | ground_ui_progress_updated | Debug | 界面进度日志已更新。 | ui_visibility, ui_message, inline, legacy_unclassified, ui_visible |  |  |
| Ground | ui.log | ui_log_view_cleared | Info | 日志面板显示已清空。 | ui_visibility |  |  |
| Ground | ui.log | ui_log_pending_queue_dropped | Info | 桌面日志 UI 队列已满，已丢弃部分待显示记录。 | ui_visibility, dropped_count, pending_limit |  |  |
| RTK | gga | gga_input_buffer_overflow | Warning | GGA 输入缓冲区超过上限，已丢弃未结束的前缀。 | buffer_limit_bytes |  | GGA_INPUT_BUFFER_LIMIT |
| RTK | service | rtk_service_log | Info | RTK 服务状态已更新。 | ui_visibility, ui_message, legacy_unclassified, ui_visible |  |  |
| RTK | service.raw | rtk_service_raw_output | Debug | RTK 服务输出了原始诊断信息。 | ui_visibility, external_raw_text, ui_visible |  |  |

新增或调整日志事件时，保持本表作为开发入口；无法立即结构化的旧路径必须在 `event` 中体现 legacy 隔离，并在字段中说明保留原因。

## 桌面日志展示迁移清单

本清单记录本轮已经结构化分类的 UI 可见 Info/Debug 入口。`ui_visibility` 只影响桌面日志面板，不影响 JSONL 文件保存。

清单格式使用普通条目，避免被事件目录审计脚本当成第二张事件目录表解析：

- Any / ui.legacy / `user_issue_reported` / Info、Warning、Error / `attention` / 可合并 / 默认键：用户问题上报需要默认进入关注视图，Info 也可作为重要状态确认。
- Ground / ui.legacy / `ground_ui_legacy_log` / Info / `details` / 可合并 / 默认键：旧 `log(QString)` 只保留普通 UI 文本，默认不占用关注视图。
- Ground / ui.progress / `ground_ui_progress_updated` / Debug / `hidden` / 可合并 / 默认键：回车进度和高频状态只写文件，不进入桌面面板。
- Ground / ui.log / `ui_log_view_cleared` / Info / `hidden` / 可合并 / 默认键：“清空显示”动作可审计，但清空后不能立即生成可见行。
- Ground / ui.log / `ui_log_pending_queue_dropped` / Info / `hidden` / 可合并 / 默认键：pending 队列过载只写入 JSONL 统计，不进入桌面关注面板。
- Ground / telemetry helper / Info / `details` / 可合并 / 默认键：普通遥测状态保留在全部视图；Warning 和 Error 按 helper 回退进入关注。
- Ground / telemetry helper / Warning、Error / `attention` / 可合并 / 默认键：遥测异常需要默认可见，且不依赖 message 文本判断。
- SkyTui / ui / `sky_tui_ui_log` / Info / `details` / 可合并 / 默认键：TUI 本地操作提示为普通状态，不默认占用桌面关注视图。
- SkyTui / IPC helper / Info / `details` / 可合并 / 默认键：IPC 普通状态进入全部视图；Warning 和 Error 由 helper 标为关注。
- SkyTui / IPC helper / Warning、Error / `attention` / 可合并 / 默认键：IPC 异常需要默认可见，并保留 source、category 和 event。
- RTK / service / `rtk_service_log` / Info / `details` / 可合并 / 默认键：RTK 对话框仍本地显示服务状态，桌面主日志默认只在全部视图显示。
- RTK / service.raw / `rtk_service_raw_output` / Debug / `hidden` / 可合并 / 默认键：原始 RTK 输出只写文件和 RTK 对话框，不进入桌面主日志。

尚未逐项迁移的普通 Info 事件：全仓库仍有历史 helper/legacy 路径；缺少 `ui_visibility` 时由桌面模型按级别兼容回退，普通 Info 进入 `details`，Debug 只在调试视图显示。后续新增日志应直接在生产端显式填写 `ui_visibility`。
