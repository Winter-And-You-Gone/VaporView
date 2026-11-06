# 命令日志关联（当前覆盖范围）

Ground 的遥测命令在发起业务操作时生成 correlation_id，并在重试和等待 ACK 期间保存在待处理命令中。Ground 的 `ground_command_send`、`ground_command_retry`、`ground_command_timeout`、`ground_command_ack` 与 SkyCore 的 `sky_command_received`、`sky_command_completed` 可按该 ID 查询。设备操作还包含 `sky_device_operation_started` 和 `sky_device_operation_finished`；EPSILON 和温控异步 worker 在执行前后记录 `sky_device_worker_started` 和 `sky_device_worker_finished`；进度使用 `sky_command_progress`。状态、配置查询和单次波形请求不生成 ID，避免周期查询带来额外日志。完成日志表示 SkyRuntime 返回命令结果，不代表每条设备驱动内部日志都已关联。

远端定时录制开始和停止日志复用实际发送命令的 correlation_id；在收到天空端确认之前，不推断远端 session_id。

运行时停止时，如果异步设备操作仍有尚未交付的结果，`sky_device_result_discarded` 保留该操作的 correlation_id 和开始时的 session_id。合并到同一设备操作的等待者按不同 correlation_id 分别记录，同一 ID 的重复请求只记录一次。该事件不说明设备操作是否实际执行成功或已取消，需检查设备状态。

配置命令的 ACK 已收到但配置结果仍未到达时，断链会记录 `ground_command_disconnected`，其 `stage` 为 `awaiting_config_result`，并保留该命令的 correlation_id 和 ACK 已确认的 session_id。
配置结果上下文在 30 秒到期或达到 256 条缓存容量被淘汰时，分别记录 `ground_config_result_context_expired` 和 `ground_config_result_context_evicted`。这表示关联上下文不再保留，并不代表配置操作失败；旧格式结果没有关联字段，可能无法确认其对应的请求。

新版 SkyCore 对请求关联配置结果的命令，在 ACK 可选元数据中声明是否会发送该结果。无效配置不产生结果，配置应用失败则可能仍有结果，不能仅凭错误码区分。旧 ACK 或元数据因长度限制未发送时状态为未知，客户端继续沿用旧等待行为。

新版业务请求可协商扩展 ACK 上下文：超过旧版 1024 字节元数据限制时，SkyCore 在有界的 256 KiB 扩展中直接携带 correlation_id 和 session_id。Ground 和 SkyTui 仅在该请求实际发送了能力标记、命令和 correlation_id 均匹配时接受扩展；无效的超长扩展会被拒绝。没有协商能力的旧请求沿用原有编码。

新版配置请求还可协商 ACK 省略超长 session_id，作为扩展 ACK 不可用或超限时的兼容回退。结果上下文先到时，客户端核对命令和 correlation_id 后恢复结果中的 Session；ACK 先到时 Session 保持未知，直到匹配的结果上下文到达。结果上下文独立协商 v2：Session 超过 4096 UTF-8 字节时，配置结果使用 JSON v2，typed 设备结果使用带 32 位长度的 `VRC2`；较短结果仍使用 v1。接收端只接受实际请求了 v2 能力的对应结果，旧客户端继续使用 v1 和其 4096 字节上限。扩展结果还受 Session、JSON 和帧的大小上限约束，超限时仍会回退旧结果格式；不能从缺失的上下文推断 Session。

ID 以可选 `VLC1` 尾随块放在遥测 Command 和 ACK 载荷之后，不改变设备子载荷。旧版读取器按已声明长度提取子载荷并忽略尾随数据；新版读取旧消息时关联字段为空。带 ID 的 ACK 必须匹配待处理操作才会被接受；Ground 对不含 ID 的旧 ACK 仍使用原有方式关联。旧版 SkyCore 不会输出关联日志，因此跨版本时不能假定 SkyCore 端可追踪。诊断字段不参与业务去重比较。

SkyTui 在收到配置 ACK 后等待关联结果期间，如 IPC 断开、等待上下文过期或因容量限制被移除，会记录原 correlation_id 和 ACK 中确认的 session_id；这些日志表示结果未收到或无法继续关联，不代表设备操作失败。结果已到达而仅等待 ACK 的缓存不会误报为未收到结果。

SkyCore 以本机录制器状态确定 Session，忽略请求中可能过期的 session_id。若录制或暂停中的 Session 存在，则以当前 Session 目录作为日志 session_id；命令结束时保留捕获值。录制启动成功后使用新建的 Session 目录。Ground 从对应 ACK 中获取 Session，不从可能延迟的状态快照推断；旧 ACK 无此信息，ACK 之前的 Ground 发送、重试、超时或断链日志也无法可靠获知远端 Session。

FPGA 命令将上下文传入工作线程及操作结果；延迟的设备连接确认按设备源暂存其上下文。设备管理器直接发起的异步 FPGA 连接以 `fpga_async_connection_result` 记录实际回调结果及原始关联上下文，超时不代表硬件状态已知。EPSILON 和温控的同步事务回调使用任务显式携带的上下文；内部环境串口连接的工作线程、结果及取消事件也携带相同 ID。后台采集不继承操作上下文。SkyTui 本地 IPC 的业务命令、ACK、超时和断链记录使用相同 ID。Ground 本地录制日志按真实会话状态关联，PPK 后处理和 RTCM 本地转发各自使用独立的操作 ID。

Ground 录制警告在产生时快照会话目录，排队投递时不会改用新会话；会话日志接收器在文件锁内检查非空 session_id，跳过不属于当前会话的记录。未指定 session_id 的后台日志保持原来的写入行为。Ground 遥测命令的发送、重试和响应处理在发布日志后校验连接代次，避免同步日志回调关闭或切换连接后继续操作已失效的待处理项。

Wave TCP 连接失败日志保留连接目标 `host`、`port`，收到 socket 错误时额外记录 `socket_error` 和 `system_error`；主动取消及成功连接不会伪造 socket 错误。

SkyCore 会话日志同样在文件锁内检查非空 session_id，避免旧会话的排队记录写入新会话；未绑定会话的后台记录保留原有行为。独立关闭运行时的录制收尾日志使用实际会话目录及独立操作 ID，外层已有操作 ID 时沿用该 ID。

新版 Ground 在设备命令的 `VLC1` 扩展中请求 typed 结果上下文；新版 SkyCore 通过独立的 `DeviceOperationResponseContext` 进程间消息返回原设备结果及 correlation_id、session_id。Ground 同时匹配请求标识、设备、操作、correlation_id 和已知 Session，拒绝不匹配的新版结果。结果先于 ACK 到达时，Ground 使用结果携带的 Session 立即交付；终态结果释放设备请求，并短期保留命令上下文以匹配迟到 ACK，不等待或重发物理操作。封装或 ACK 元数据超出限制时回退旧格式，不改变设备子载荷。旧客户端不请求该能力，仍收到旧格式；旧 SkyCore 忽略可选字段，新 Ground 保持旧格式的原有处理行为。

旧版 typed 设备响应仅有 request_id、设备和操作类型，不含 correlation_id；Ground 通过待处理请求及短期缓存恢复上下文。相同 request_id 在操作仍待处理或等待 typed 结果时会被拒绝，避免并发串扰。首次 typed 结果完成后该标识即可复用：如果旧响应的副本在复用后到达，可能被误归属给新操作，这种歧义无需等待缓存超时即可发生。为了保持旧端原有时序，在能力确认前收到的旧格式结果也仍按旧规则处理。缓存超时或被淘汰的结果可能没有关联。设备内部异步事件、链路异常以及实机行为尚未全部验证，不应据此宣称任意设备操作均已完整追踪。独立双进程取消测试使用可控模拟交换执行实际维护算法，验证工作线程返回取消状态并发送恢复命令；它不证明实体设备已停止。
