# RTCM 转发生命周期日志

RTK 配置页每次启动流时创建一个 correlation_id；启动请求、启动成功、失败、意外停止和正常停止事件通过 `rtcm.lifecycle` 类别保留该 ID。停止后释放该 ID，下次启动生成新的 ID。仅记录生命周期，不逐条记录 RTCM 数据。流状态日志在输出时关联实际打开的本地 Session（包括暂停状态）；远端 Session 未知或本地 Session 已结束时留空。界面测试的事件带有 `simulated=true`。

启动配置校验、接收端创建或流服务启动失败时，`rtcm_stream_start_failed` 的 `reason` 字段保留对应的错误原因，便于与同一操作的 correlation_id 一起检索。

启动过程中收到停止请求时，`rtcm_stream_start_cancelled` 使用原启动操作的 correlation_id 和停止请求时实际打开的 Session；已经创建的转发监听器和流服务会在返回前关闭。

此 ID 表示 Ground 本地 NTRIP 到输出端的流生命周期，并不声称与 SkyCore 接收的每个 RTCM 数据帧关联。EPSILON RTCM 输入配置属于独立设备命令，使用其命令 correlation_id。

FPGA 转发失败的异步回调保留提交时的流 ID；旧流迟到失败不会清除新流的队列。由于同一流可跨多个 Session，异步回复不能可靠推断提交时的 Session，故其 session_id 留空。

SkyCore 独立 RTCM 串口在首次打开失败或写入失败时记录 `rtcm_serial_output_failed`，包含端口、波特率、系统错误及写入字节数；连续失败不会逐帧重复记录，完整写入成功后记录 `rtcm_serial_output_recovered`。这些是后台链路状态事件，不继承已结束的配置命令 ID 或过期 Session。工作线程快照首次失败和最新状态，由管理器线程限频发布；快速抖动的中间状态会合并，最终状态在限频窗口结束后发布。重配置或停止会丢弃旧代次尚未投递的事件。
