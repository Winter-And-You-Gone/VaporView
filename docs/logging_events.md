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
| Ground | device.collector | epsilon_configuration_collector_output | Info | EPSILON 配置过程输出了采集器诊断信息。 | device, process_output, external_raw_text | ui_visibility |  |
| SkyCore | device.navigation.command | rtk_command_write_failed | Error | RTK 命令发送失败。 | device_id, command, requested_rate_hz, error_code |  | SERIAL_WRITE_FAILED |
| SkyCore | device.navigation.command | rtk_command_response_timeout | Warning | RTK 命令未收到响应。 | device_id, command, requested_rate_hz, error_code |  | COMMAND_TIMEOUT |
| SkyCore | device.navigation.command | epsilon_output_rate_rejected_unsupported | Warning | EPSILON 输出频率不受支持。 | device_id, device, requested_rate_hz, reason_code |  | COMMAND_NOT_SUPPORTED |
| SkyCore | device.navigation.command | imu_output_message_rejected_unsupported | Warning | IMU 输出消息类型不受支持。 | device_id, device, message_type, reason_code |  | COMMAND_NOT_SUPPORTED |
| SkyCore | device.navigation.command | imu_command_rejected_serial_closed | Warning | IMU 串口未打开，无法发送命令。 | device_id, device, reason_code |  | DEVICE_NOT_CONNECTED |
| SkyCore | device.navigation.command | imu_command_write_failed | Error | IMU 命令发送失败。 | device_id, device, command, error_code |  | SERIAL_WRITE_FAILED |
| SkyCore | device.navigation.command | imu_sample_rate_rejected_unsupported | Warning | IMU 采样频率不受支持。 | device_id, device, requested_rate_hz, reason_code |  | COMMAND_NOT_SUPPORTED |
| SkyCore | device.navigation.command | imu_sample_rate_command_failed | Error | IMU 采样频率命令发送失败。 | device_id, device, message_type, requested_rate_hz, error_code |  | SERIAL_WRITE_FAILED |
| SkyCore | device.pressure.command | ptb_sample_rate_rejected_unsupported | Warning | PTB210 采样频率不受支持。 | device_id, device, requested_rate_hz, reason_code |  | COMMAND_NOT_SUPPORTED |
| SkyCore | device.pressure.command | ptb_average_command_write_failed | Error | PTB210 AVRG 命令发送失败。 | device_id, device, command, error_code |  | SERIAL_WRITE_FAILED |
| SkyCore | device.pressure.command | ptb_mpm_command_write_failed | Error | PTB210 MPM 命令发送失败。 | device_id, device, command, mpm, error_code |  | SERIAL_WRITE_FAILED |
| SkyCore | device.pressure.command | ptb_reset_command_write_failed | Error | PTB210 RESET 命令发送失败。 | device_id, device, command, error_code |  | SERIAL_WRITE_FAILED |
| SkyCore | device.pressure.command | ptb_continuous_restore_failed | Error | PTB210 恢复连续输出失败。 | device_id, device, command, error_code |  | SERIAL_WRITE_FAILED |
| SkyCore | device.pressure.command | ptb_pressure_probe_write_failed | Error | PTB210 压力探测命令发送失败。 | device_id, device, command, attempt, error_code |  | SERIAL_WRITE_FAILED |
| SkyCore | device.pressure.command | ptb_continuous_start_failed | Error | PTB210 启动连续输出失败。 | device_id, device, command, error_code |  | SERIAL_WRITE_FAILED |
| SkyCore | device.temperature.state | temperature_controller_model_read_failed | Error | RD105 温控器型号读取失败。 | device_id, device, command, error_code |  | COMMAND_VERIFY_FAILED |
| SkyCore | device.temperature.state | temperature_controller_firmware_read_failed | Error | RD105 温控器版本号读取失败。 | device_id, device, command, error_code |  | COMMAND_VERIFY_FAILED |
| SkyCore | device.temperature.state | temperature_controller_parameters_read_failed | Error | RD105 温控器参数读取失败。 | device_id, device, command, error_code |  | COMMAND_VERIFY_FAILED |
| SkyCore | device.temperature.state | temperature_controller_parameters_incomplete | Error | RD105 温控器参数读取不完整。 | device_id, device, command, error_code |  | COMMAND_VERIFY_FAILED |
| SkyCore | device.lidar.command | lidar_high_frequency_start_failed | Error | TFA1005-L 高频测距启动命令发送失败。 | device_id, device, command, error_code |  | SERIAL_WRITE_FAILED |
| SkyCore | device.lidar.command | lidar_standby_command_failed | Error | TFA1005-L 待机命令发送失败。 | device_id, device, command, error_code |  | SERIAL_WRITE_FAILED |
| SkyCore | device.lidar.command | lidar_distance_output_command_failed | Error | TFA1005-L 距离输出命令发送失败。 | device_id, device, command, error_code |  | SERIAL_WRITE_FAILED |
| SkyCore | device.lidar.command | lidar_low_frequency_continuous_command_failed | Error | TFA1005-L 低频连续测距命令发送失败。 | device_id, device, command, error_code |  | SERIAL_WRITE_FAILED |
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
| Ground | session.write | raw_format_document_copy_failed | Warning | 未能将统一 raw DAT 格式说明复制到当前会话目录。 | error_code | ui_dedupe_key | RAW_FORMAT_DOCUMENT_COPY_FAILED |
| Ground | session.write | device_config_snapshot_failed | Warning | 保存设备配置快照失败。 | error_code | ui_dedupe_key | DEVICE_CONFIG_SNAPSHOT_FAILED |
| Ground | session.write | session_metadata_update_failed | Warning | 更新会话元数据失败。 | error_code | ui_dedupe_key | SESSION_METADATA_UPDATE_FAILED |
| Ground | session.write | tcp_raw_recording_queue_backlog | Warning | TCP 原始记录队列出现积压，磁盘写入可能慢于数据流。 | backlog_mib, reason_code | ui_dedupe_key | BACKPRESSURE |
| Ground | session.write | tcp_raw_recording_queue_full | Warning | TCP 原始记录队列已满，正在丢弃新到原始帧以保持链路响应。 | queue_mib, error_code | ui_dedupe_key | TCP_RAW_QUEUE_FULL |
| Ground | session.write | tcp_raw_frames_dropped | Warning | TCP 原始记录队列已满，已丢弃部分帧。 | dropped_frames, error_code | ui_dedupe_key | TCP_RAW_FRAMES_DROPPED |
| Ground | session.write | device_raw_recording_queue_backlog | Warning | 设备原始记录队列出现积压，磁盘写入可能慢于串口数据流。 | backlog_mib, reason_code | ui_dedupe_key | BACKPRESSURE |
| Ground | session.write | device_raw_recording_queue_full | Warning | 设备原始记录队列已满，正在丢弃新到原始帧以保持采集线程响应。 | queue_mib, error_code | ui_dedupe_key | DEVICE_RAW_QUEUE_FULL |
| Ground | session.write | device_raw_frames_dropped | Warning | 设备原始记录队列已满，已丢弃部分帧。 | dropped_frames, error_code | ui_dedupe_key | DEVICE_RAW_FRAMES_DROPPED |
| Ground | session.recording | session_recording_started | Info | 已开始记录会话。 | session_directory, resumed | ui_visibility |  |
| Ground | session.recording | session_recording_resumed | Info | 已继续记录会话。 | session_directory, resumed | ui_visibility |  |
| Ground | session.recording | session_recording_paused | Info | 已暂停记录会话。 | session_directory | ui_visibility |  |
| Ground | session.recording | session_recording_stopped | Info | 记录会话已结束。 | session_directory, sensor_rows, waveform_frames | ui_visibility |  |
| Ground | session.recording | session_recording_start_failed | Error | 启动记录会话失败。 | error_code | system_error | SESSION_LAYOUT_CREATE_FAILED / SESSION_FILES_OPEN_FAILED / SESSION_METADATA_WRITE_FAILED / SESSION_RECORDING_START_FAILED |
| Ground | session.recording | session_recording_rejected_dependency_unavailable | Warning | 开始记录前请先连接天空端数传。 | reason_code, dependency, mode | ui_dedupe_key | DEPENDENCY_UNAVAILABLE |
| Ground | session.recording | session_recording_rejected_no_source | Warning | 开始记录前，至少需要一个串口设备在线或 TCP 波形链路已连接。 | reason_code, mode, serial_connected, tcp_wave_connected | ui_dedupe_key | NO_RECORDING_SOURCE_CONNECTED |
| Ground | session.recording | scheduled_recording_configured | Info | 定时记录已配置。 | summary, mode, duration_seconds, interval_seconds, fixed_count_enabled, total_runs, next_start_time | ui_visibility |  |
| Ground | session.recording | scheduled_recording_canceled | Info | 定时记录已取消。 |  | ui_visibility |  |
| Ground | session.recording | scheduled_recording_start_failed | Warning | 定时记录未能启动。 | reason_code, failure_reason, mode, next_start_time | ui_dedupe_key | DEPENDENCY_UNAVAILABLE |
| Ground | session.recording | scheduled_recording_stop_failed | Warning | 定时记录未能停止，当前记录链路不可用。 | reason_code | ui_dedupe_key | DEPENDENCY_UNAVAILABLE |
| Ground | session.recording | scheduled_recording_completed | Info | 定时记录已完成。 | summary, completed_runs, total_runs | ui_visibility |  |
| Ground | session.recording | scheduled_recording_next_start_scheduled | Info | 已安排下一次定时记录。 | next_start_time, completed_runs | ui_visibility |  |
| Ground | session.recording | scheduled_recording_start_command_sent | Info | 定时记录开始命令已发送。 | execution_path, command, command_seq | ui_visibility |  |
| Ground | session.recording | scheduled_recording_stop_command_sent | Info | 定时记录停止命令已发送。 | execution_path, command, command_seq | ui_visibility |  |
| Ground | device.connection | ground_device_connection_status | Info | 设备连接状态已更新。 | ui_visibility, ui_message, legacy_unclassified, ui_visible |  |  |
| Ground | device.connection | serial_ports_refreshed | Info | 串口列表已刷新。 | serial_port_count | ui_visibility |  |
| Ground | device.connection | serial_port_detection_started | Info | 开始自动识别串口。 |  | ui_visibility |  |
| Ground | device.connection | serial_port_detection_cancel_requested | Info | 已请求取消，正在停止自动识别串口。 | reason_code | ui_visibility | USER_CANCELLED |
| Ground | device.connection | serial_port_detection_progress_updated | Debug/Info | 串口自动识别状态已更新。 | ui_message, inline | ui_visibility |  |
| Ground | device.connection | device_connection_started | Info | 正在连接本地设备。 |  | ui_visibility |  |
| Ground | device.connection | device_connection_progress_updated | Debug | 设备连接进度已更新。 | ui_message, inline | ui_visibility |  |
| Ground | device.connection | device_connection_rejected_busy | Warning | 已有设备连接流程正在进行。 | reason_code | ui_dedupe_key | INVALID_STATE |
| Ground | device.connection | device_connection_cancel_requested | Info | 已请求取消，正在停止连接流程。 | reason_code | ui_visibility | USER_CANCELLED |
| Ground | device.connection | device_disconnection_started | Info | 正在断开本地设备。 |  | ui_visibility |  |
| Ground | device.connection | local_device_disconnected | Info | 本地设备已断开。 |  | ui_visibility |  |
| Ground | device.connection | temperature_controller_connection_rejected_missing_port | Warning | 请先选择本地 RD105 串口。 | device, device_id, reason_code | ui_dedupe_key | MISSING_ENDPOINT |
| Ground | device.connection | temperature_controller_connection_rejected_invalid_baud | Warning | RD105 波特率无效。 | device, device_id, reason_code, baud_text | ui_dedupe_key | CONFIG_INVALID |
| Ground | device.connection | temperature_controller_connection_started | Info | 正在连接本地 RD105 温控器。 | device, device_id, port, baud, sample_rate_hz | ui_visibility |  |
| Ground | device.connection | temperature_controller_connected | Info | 本地 RD105 温控器已连接。 | device, device_id | details, ui_visibility |  |
| Ground | device.connection | temperature_controller_connection_failed | Error | 本地 RD105 温控器连接失败。 | device, device_id, error_code | system_error, ui_dedupe_key | SERIAL_OPEN_FAILED |
| Ground | device.connection | temperature_controller_connection_rejected_busy | Warning | 另一个本地连接操作正在进行中。 | device, device_id, reason_code | ui_dedupe_key | INVALID_STATE |
| Ground | device.connection | temperature_controller_disconnected | Info | 本地 RD105 温控器已断开。 | device, device_id | ui_visibility |  |
| Ground | telemetry.connection | remote_sky_connection_rejected_missing_host | Warning | 请先输入天空端数传 IP。 | reason_code, transport | ui_dedupe_key | MISSING_ENDPOINT |
| Ground | telemetry.connection | remote_sky_connection_rejected_missing_port | Warning | 请先选择天空端数传串口。 | reason_code, transport | ui_dedupe_key | MISSING_ENDPOINT |
| Ground | telemetry.connection | remote_sky_connection_started | Info | 正在连接天空端数传。 | transport, endpoint | ui_visibility |  |
| Ground | telemetry.connection | remote_sky_connection_opened | Info | 数传链路已打开，正在等待天空端握手。 | endpoint, transport | ui_visibility |  |
| Ground | telemetry.connection | remote_sky_connection_open_failed | Error | 打开天空端数传链路失败。 | error_code, endpoint, transport | ui_dedupe_key | TELEMETRY_LINK_OPEN_FAILED |
| Ground | telemetry.connection | remote_sky_handshake_confirmed | Info | 天空端握手成功。 |  | ui_visibility |  |
| Ground | telemetry.connection | remote_sky_disconnection_started | Info | 正在断开天空端数传。 |  | ui_visibility |  |
| Ground | telemetry.connection | remote_sky_disconnected | Info | 天空端数传已断开。 |  | ui_visibility |  |
| Ground | telemetry.link | remote_sky_legacy_status | Info | 远程数传状态已更新。 | ui_message, legacy_unclassified | ui_visibility |  |
| Ground | telemetry.command | remote_command_ack_received | Debug | 远程命令 ACK 已收到。 | command, command_id, command_seq, ack_result, command_error_code | ui_visibility |  |
| Ground | telemetry.command | remote_command_failed | Error | 远程命令执行失败。 | command, command_id, command_seq, ack_result, command_error_code, error_code | ui_dedupe_key | REMOTE_COMMAND_FAILED / INVALID_PAYLOAD / INVALID_DEVICE_ID / CONFIG_INVALID / CONFIG_APPLY_FAILED / INTERNAL_ERROR |
| Ground | telemetry.command | peak_search_range_sent | Info | 峰值搜索区间已下发到天空端。 | range_start_index, range_end_index, command_seq | ui_visibility |  |
| Ground | telemetry.command | peak_search_range_applied | Info | 峰值搜索区间已生效，旧远程峰值趋势已清空。 | command, command_id, command_seq, range_start_index, range_end_index | ui_visibility |  |
| Ground | telemetry.command | peak_search_range_rejected_dependency_unavailable | Warning | 天空端数传链路未连接，无法下发峰值搜索区间。 | reason_code, dependency, range_start_index, range_end_index | ui_dedupe_key | DEPENDENCY_UNAVAILABLE |
| Ground | device.command | remote_device_command_rejected_dependency_unavailable | Warning | 天空端数传链路未连接，无法下发设备命令。 | reason_code, dependency, device_id, command | ui_dedupe_key | DEPENDENCY_UNAVAILABLE |
| Ground | ui.action | sky_device_config_rejected_not_connected | Warning | 打开天空端设备配置前，请先连接天空端数传。 | reason_code, dependency | ui_dedupe_key | DEPENDENCY_UNAVAILABLE |
| Ground | ui.action | language_switched | Info | 界面语言已切换。 | language | ui_visibility |  |
| Ground | ui.action | theme_switched | Info | 界面主题已切换。 | theme | ui_visibility |  |
| Ground | ui.action | font_scale_updated | Info | 界面字号已更新。 | font_scale_percent | ui_visibility |  |
| Ground | ui.test | ui_test_log_updated | Info | 界面测试日志已更新。 | ui_message | ui_visibility |  |
| Ground | device.navigation.command | epsilon_rtcm_config_rejected_recording_active | Warning | 请先结束记录，再配置 EPSILON RTCM 串口。 | device, reason_code | ui_dedupe_key | INVALID_STATE |
| Ground | device.navigation.command | epsilon_rtcm_config_rejected_missing_main_port | Warning | 请先选择 EPSILON 主串口。 | device, reason_code | ui_dedupe_key | MISSING_ENDPOINT |
| Ground | device.navigation.command | epsilon_rtcm_config_rejected_invalid_main_baud | Warning | EPSILON 波特率无效。 | device, reason_code, baud_text | ui_dedupe_key | CONFIG_INVALID |
| Ground | device.navigation.command | epsilon_rtcm_config_rejected_missing_forward_port | Warning | 请选择连接到 EPSILON 第二串口的本机串口。 | device, reason_code, main_port | ui_dedupe_key | MISSING_ENDPOINT |
| Ground | device.navigation.command | epsilon_rtcm_config_rejected_port_conflict | Warning | RTCM 转发串口不能与 EPSILON 主串口相同。 | device, reason_code, main_port, forward_port, details | ui_dedupe_key | CONFIG_INVALID |
| Ground | device.navigation.command | epsilon_rtcm_config_rejected_invalid_forward_baud | Warning | RTCM 转发波特率无效。 | device, reason_code, baud_text | ui_dedupe_key | CONFIG_INVALID |
| Ground | device.navigation.command | epsilon_rtcm_config_started | Info | 正在把 EPSILON 第二通信串口配置为 RTCM。 | device, main_port, main_baud, forward_port, forward_baud | ui_visibility |  |
| Ground | device.navigation.command | epsilon_rtcm_port_open_failed | Error | 打开 EPSILON 串口进行 RTCM 配置失败。 | device, operation, port, baud, system_error, error_code | ui_dedupe_key | SERIAL_OPEN_FAILED |
| Ground | device.navigation.command | epsilon_rtcm_port_config_failed | Error | EPSILON RTCM 串口配置失败。 | device, operation, port, baud, forward_port, forward_baud, error_code | ui_dedupe_key | CONFIG_APPLY_FAILED |
| Ground | device.navigation.command | epsilon_rtcm_port_config_completed | Info | EPSILON RTCM 串口配置已完成，RTK 转发配置已预填。 | device, operation, port, forward_port, forward_baud | ui_visibility |  |
| Ground | device.navigation.command | epsilon_live_stream_pause_for_configuration | Info | 为配置 EPSILON 临时停止当前数据流。 | device, operation | ui_visibility |  |
| Ground | device.navigation.command | epsilon_configuration_completed_live_stream_restored | Info | EPSILON 配置已完成，实时导航流已恢复。 | device, operation | ui_visibility |  |
| Ground | device.navigation.command | epsilon_configuration_failed_live_stream_restored | Error | EPSILON 配置失败，但原实时导航流已恢复。 | device, operation, error_code | ui_visibility | CONFIG_APPLY_FAILED |
| Ground | device.navigation.command | epsilon_live_stream_restore_failed | Error | EPSILON 实时导航流未能恢复，请手动重新连接 EPSILON。 | device, operation, error_code, recovery_error | ui_dedupe_key | STREAM_RESTORE_FAILED |
| Ground | device.navigation.command | epsilon_main_antenna_lever_arm_config_started | Info | 正在通过临时连接下发 EPSILON 主天线杆臂配置。 | device, operation, port, baud, values | ui_visibility |  |
| Ground | device.navigation.command | epsilon_main_antenna_lever_arm_open_failed | Error | 打开 EPSILON 串口进行主天线杆臂配置失败。 | device, operation, port, baud, system_error, error_code | ui_dedupe_key | SERIAL_OPEN_FAILED |
| Ground | device.navigation.command | epsilon_main_antenna_lever_arm_config_failed | Error | EPSILON 主天线杆臂配置失败。 | device, operation, port, baud, x_m, y_m, z_m, error_code | ui_dedupe_key | CONFIG_APPLY_FAILED |
| Ground | device.navigation.command | epsilon_packet_profile_rejected_bandwidth | Warning | EPSILON 包频率超过当前波特率安全带宽。 | device, baud_text, required_kbps, limit_kbps, reason_code | details, ui_dedupe_key | CONFIG_INVALID |
| Ground | device.navigation.command | epsilon_output_reconfigure_rejected_recording_active | Warning | 请先结束记录，再重新配置 EPSILON 输出。 | device, reason_code | ui_dedupe_key | INVALID_STATE |
| Ground | device.navigation.command | epsilon_output_reconfigure_rejected_missing_port | Warning | 请先选择 EPSILON 串口。 | device, reason_code | ui_dedupe_key | MISSING_ENDPOINT |
| Ground | device.navigation.command | epsilon_output_reconfigure_rejected_invalid_baud | Warning | EPSILON 波特率无效。 | device, reason_code, baud_text | ui_dedupe_key | CONFIG_INVALID |
| Ground | device.navigation.command | epsilon_output_reconfigure_skipped_rate_unspecified | Info | EPSILON 频率为“不设定”，已跳过输出频率下发。 | device, reason_code | ui_visibility | COMMAND_NOT_SUPPORTED |
| Ground | device.navigation.command | epsilon_output_reconfigure_started | Info | 开始手动重配 EPSILON 输出。 | device, port, baud, packet_rate_profile, packet_rate_summary | ui_visibility |  |
| Ground | device.navigation.command | epsilon_output_reconfigure_open_failed | Error | 打开 EPSILON 串口进行手动重配失败。 | device, operation, port, baud, system_error, error_code | ui_dedupe_key | SERIAL_OPEN_FAILED |
| Ground | device.navigation.command | epsilon_output_reconfigure_failed | Error | EPSILON 输出手动重配失败。 | device, operation, port, baud, output_rate_hz, callback_rate_hz, packet_rate_signature, error_code | ui_dedupe_key | CONFIG_APPLY_FAILED |
| Ground | device.navigation.command | epsilon_output_reconfigure_completed | Info | EPSILON 输出手动重配已完成。 | device, operation, port, output_rate_hz, callback_rate_hz, packet_rate_signature | ui_visibility |  |
| Ground | device.navigation.command | epsilon_output_rate_command_disabled | Info | 已禁用 EPSILON 输出频率下发，使用设备当前输出。 | device, apply_device_rate | ui_visibility |  |
| Ground | device.navigation.command | epsilon_output_rate_command_failed | Error | EPSILON 输出频率下发失败。 | device, requested_rate_hz, error_code | ui_dedupe_key | COMMAND_VERIFY_FAILED |
| Ground | device.navigation.command | epsilon_output_rate_saved_deferred | Info | EPSILON 输出频率已保存，将在下次连接时应用。 | device, requested_rate_hz | ui_visibility |  |
| Ground | device.navigation.command | epsilon_output_rate_updated_custom_profile_retained | Info | EPSILON 分组频率已更新，但仍使用已保存的自定义包频率配置。 | device, requested_rate_hz, epsilon_packet_profile | ui_visibility |  |
| Ground | device.navigation.command | epsilon_output_rate_updated | Info | EPSILON 输出频率已更新。 | device, requested_rate_hz | ui_visibility |  |
| Ground | device.navigation.command | imu_profile_apply_rejected_missing_port | Warning | 请先选择 IMU 串口。 | device, reason_code | ui_dedupe_key | MISSING_ENDPOINT |
| Ground | device.navigation.command | imu_profile_apply_rejected_unsupported | Warning | IMU 输出格式或频率不受支持。 | device, reason_code, output_format, rate_hz | ui_dedupe_key | COMMAND_NOT_SUPPORTED |
| Ground | configuration.apply | epsilon_packet_profile_custom_enabled | Info | 检测到包频率已偏离分组模式，已自动启用自定义包频率配置。 | device, packet_rate_summary | ui_visibility |  |
| Ground | configuration.apply | epsilon_packet_profile_saved | Info | 已保存 EPSILON 包频率配置。 | device, packet_rate_profile, packet_rate_summary | ui_visibility |  |
| Ground | configuration.apply | epsilon_packet_profile_disabled | Info | 已关闭 EPSILON 自定义包频率，后续将使用分组配置。 | device, grouped_rate_hz | ui_visibility |  |
| Ground | configuration.apply | epsilon_packet_profile_apply_requested | Info | 正在应用刚保存的 EPSILON 包频率配置。 | device, port, packet_rate_summary | ui_visibility |  |
| Ground | configuration.apply | epsilon_packet_profile_saved_deferred | Info | EPSILON 包频率配置已保存，将在下次连接或重配时生效。 | device, packet_rate_summary | ui_visibility |  |
| Ground | configuration.apply | recording_directory_updated | Info | 记录目录已更新。 | recording_directory | ui_visibility |  |
| Ground | configuration.apply | recording_csv_rate_updated | Info | 其余设备记录频率已更新。 | recording_rate_hz | ui_visibility |  |
| Ground | configuration.apply | epsilon_raw_recording_full_frames_enabled | Info | EPSILON 原始记录固定保存完整已校验 FDILink 帧。 | device, recording_mode | ui_visibility |  |
| Ground | configuration.apply | tcp_wave_raw_recording_full_frames_enabled | Info | TCP 波形原始记录固定保存每组完整 TCP 帧。 | device, recording_mode | ui_visibility |  |
| Ground | device.pressure.command | ptb_sample_rate_command_disabled | Info | 已禁用 PTB210 采样频率下发，使用设备当前输出。 | device, apply_device_rate | ui_visibility |  |
| Ground | device.pressure.command | ptb_sample_rate_command_failed | Error | PTB210 采样频率命令下发失败。 | device, requested_rate_hz, error_code | ui_dedupe_key | COMMAND_VERIFY_FAILED |
| Ground | device.lidar.command | lidar_output_rate_command_disabled | Info | 已禁用激光测距仪输出频率下发，使用设备默认或自适应输出。 | device, apply_device_rate | ui_visibility |  |
| Ground | device.rate | sample_rate_apply_partial_failure | Warning | 主机侧频率已更新，但一个或多个设备输出频率命令失败。 | requested_rate_hz, epsilon_command_failed, ptb_command_failed, reason_code | ui_dedupe_key | DEPENDENCY_UNAVAILABLE |
| Ground | device.rate | sample_rates_updated | Info | 所有频率已更新。 | requested_rate_hz, epsilon_rate_hz, ptb_rate_hz, hmp_rate_hz, lidar_rate_hz, temperature_rate_hz | ui_visibility |  |
| Ground | device.rate | sample_rates_updated_custom_epsilon_profile | Info | 所有频率已更新，EPSILON 仍保留自定义包频率配置。 | requested_rate_hz, epsilon_rate_hz, ptb_rate_hz, hmp_rate_hz, lidar_rate_hz, temperature_rate_hz, epsilon_packet_profile | ui_visibility |  |
| Ground | device.rate | sample_rate_device_commands_skipped_unspecified | Info | 已选择“不设定”的设备保持不下发输出频率命令。 | epsilon_skipped, ptb_skipped, hmp_skipped, lidar_skipped, temperature_skipped | ui_visibility |  |
| Ground | device.rate | ptb_sample_rate_capped | Info | PTB210 采样频率已按设备上限限制。 | device, requested_rate_hz, effective_rate_hz | ui_visibility |  |
| Ground | device.rate | ptb_sample_rate_updated_capped | Info | PTB210 采样频率已更新，并按设备上限限制。 | device, requested_rate_hz, effective_rate_hz | ui_visibility |  |
| Ground | device.rate | ptb_sample_rate_updated | Info | PTB210 采样频率已更新。 | device, requested_rate_hz | ui_visibility |  |
| Ground | device.rate | hmp_polling_rate_defaulted | Info | HMP3 轮询频率保持不设定，使用默认主机轮询频率。 | device, effective_rate_hz | ui_visibility |  |
| Ground | device.rate | hmp_sample_rate_updated | Info | HMP3 采样频率已更新。 | device, requested_rate_hz | ui_visibility |  |
| Ground | device.rate | lidar_sample_rate_updated | Info | 激光测距仪采样频率已更新。 | device, requested_rate_hz | ui_visibility |  |
| Ground | device.rate | temperature_polling_rate_defaulted | Info | RD105 轮询频率保持不设定，使用默认主机轮询频率。 | device, effective_rate_hz | ui_visibility |  |
| Ground | device.rate | temperature_polling_rate_updated | Info | RD105 轮询频率已更新。 | device, requested_rate_hz | ui_visibility |  |
| Ground | device.rate | temperature_polling_rate_capped | Info | RD105 轮询频率已按设备上限限制。 | device, requested_rate_hz, effective_rate_hz | ui_visibility |  |
| Ground | device.temperature.command | ai8_page_read_completed | Info | AI-8288 参数页读取完成。 | device, device_id, page, channel, input_group, output_group, details | ui_visibility |  |
| Ground | device.temperature.command | ai8_page_read_failed | Error | AI-8288 参数页读取失败。 | device, device_id, page, channel, input_group, output_group, details, error_code | ui_dedupe_key | AI8_PAGE_READ_FAILED |
| Ground | device.temperature.command | ai8_page_write_completed | Info | AI-8288 参数页写入完成。 | device, device_id, page, channel, input_group, output_group, details | ui_visibility |  |
| Ground | device.temperature.command | ai8_page_write_failed | Error | AI-8288 参数页写入失败。 | device, device_id, page, channel, input_group, output_group, details, error_code | ui_dedupe_key | AI8_PAGE_WRITE_FAILED |
| Ground | device.temperature.command | temperature_command_sent | Info | RD105 温控命令已下发到天空端。 | device, command, command_id, execution_path, command_seq | channel, target, ui_visibility |  |
| Ground | device.temperature.command | temperature_command_completed | Info | RD105 温控命令执行成功。 | device, command, command_id, execution_path | channel, target, command_seq, ui_visibility |  |
| Ground | device.temperature.command | temperature_command_rejected_not_connected | Warning | 本地 RD105 温控器未连接，无法下发温控命令。 | device, command, command_id, execution_path, reason_code | channel, target, command_seq, command_error_code, ui_dedupe_key | DEVICE_NOT_CONNECTED |
| Ground | device.temperature.command | temperature_command_rejected_dependency_unavailable | Warning | 天空端数传链路未连接，无法下发 RD105 温控命令。 | device, command, command_id, execution_path, dependency, reason_code | channel, target, ui_dedupe_key | DEPENDENCY_UNAVAILABLE |
| Ground | device.temperature.command | temperature_command_ack_timeout | Warning | RD105 温控命令 ACK 等待超时。 | device, command, command_id, execution_path, command_seq, error_code | channel, target, ui_dedupe_key | COMMAND_TIMEOUT |
| Ground | device.temperature.command | temperature_command_failed | Error | RD105 温控命令执行失败。 | device, command, command_id, execution_path, error_code | channel, target, command_seq, command_error_code, ack_result, ui_dedupe_key | COMMAND_VERIFY_FAILED / INVALID_PAYLOAD / INVALID_DEVICE_ID / CONFIG_INVALID / CONFIG_APPLY_FAILED / INTERNAL_ERROR |
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
- Ground / device.connection / `ground_device_connection_status` / Info / `attention` / 可合并 / 默认键：用户点击设备连接后产生的连接进度、结果和 TCP 波形连接状态需要默认进入关注视图。
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
