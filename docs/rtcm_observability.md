# RTCM 可观测性

组合导航 → 状态页面沿用差分卡片，展示 RTCM health、最近数据年龄、接收速率、天地链路丢失和天空端丢弃。接收 chunks/bytes、本地丢弃 bytes 及链路帧计数放在提示中，避免挤占页面。主题使用现有 healthy / warning / danger / inactive 语义，支持中文和英文。

## 当前链路和计数语义

链路仍为 NTRIP provider → `RtkStreamService` → `RtkConfigDialog` 的本地 sink → `RemoteSkyController` → `GroundTelemetryService` → `MsgType::RtcmCorrectionData` → 天地数传 → `SkyRuntime` → `SkyDeviceManager` → `forward_port` → EPSILON RTCM 口。NTRIP 获取、队列和串口转发流程保持原样，没有新增 ACK、重传或可靠传输层。

`rtcm_correction_bytes_received/chunks_received/last_receive_time_us` 在 Sky 接收有效载荷时更新，包含随后因配置无效、队列满或串口转发失败而丢弃的数据。`rtcm_correction_dropped_bytes/chunks` 只记录 Sky 本地转发丢弃，不能用于判断天地链路丢失。

## 最小协议扩展

现有 frame header 的 16 位 `seq` 在 Ground 的命令和 RTCM 之间共享，不能据此统计 RTCM 缺帧。原 RTCM payload 为小端 `u32 length + length bytes`。新版可在末尾追加两个小端 `u64`：`stream_id` 和 `sequence`，共 16 字节，原始 RTCM 字节不变。序号只随 RTCM 发送递增，命令穿插不会产生缺口。

TelemetryStatus 在现有末尾追加七个小端 `u64`：

| 字段 | 含义 |
| --- | --- |
| rtcm_observability_version | 1 表示支持此扩展；缺省 0 |
| rtcm_report_time_us | Sky 状态采样时间 |
| rtcm_boot_id | 当前 Sky runtime 启动标识 |
| rtcm_forward_enabled | Sky RTCM 转发是否启用 |
| rtcm_link_stream_id | 当前观测到的 Ground RTCM 流标识；0 表示无独立序号 |
| rtcm_link_frames_received | 当前流中已接收的 RTCM telemetry frame 数；有独立序号时去重 |
| rtcm_link_frames_lost | 已观测帧之间的 RTCM 独立序号缺口数 |

Ground 先从 Sky 状态确认版本 1，再发送 RTCM 序号尾部；连接打开时清除能力缓存。旧 Sky 始终得到原始载荷，新 Sky 也能接收旧 Ground 的载荷。未知能力/没有独立序号/没有窗口内样本时，UI 显示 `—`，不伪造 0%。这是遥测 frame/chunk 统计，并非 RTCM3 内部消息完整性统计。

Sky 对同流的连续序号计算 `lost += current - previous - 1`。换流、重连和序号回退时建立新基线。重复序号不增加独立链路帧计数，转发处理保持原有行为。Ground 打开连接时生成新流标识；64 位序号将耗尽时提前换流，避免回绕产生巨大缺口。Ground 写入零字节/失败时不消耗序号；部分写入已经产生不可解码帧，保留序号用于后续缺口检测。

## Ground 窗口和 health

年龄先取 `Sky report time - Sky last receive time`，随后按 Ground 单调时钟推进，不要求两台电脑时钟同步。速率由相邻 Sky 字节计数增量计算，使用最近 5 秒的窗口并按边界区间占比裁剪；断流后回到 0 B/s。丢失率使用最近 10 秒计数增量，分母为 received + lost。

VaporView UI heuristic 集中在 `RtcmHealthPolicy`，不是 EPSILON 或 RTCM 官方标准：

| 数据年龄 | 状态 |
| --- | --- |
| < 1 秒 | 正常 |
| 1–3 秒 | 不稳定 |
| 3–5 秒 | 严重延迟 |
| ≥ 5 秒 | 中断 |

最近 10 秒存在本地丢弃，或该窗口至少 20 个帧机会且丢失率 ≥ 5%，正常状态升级为不稳定。遥测连接关闭或超过状态新鲜度期限时显示链路断开、速率为 0、链路丢失为 `—`；期限取 3 秒与配置的三个 Sky status 周期中较大值，避免低频 status 误报。历史年龄继续增加、本地累计丢弃保留。RTK 未启动或 Sky 转发未启用时显示未启用。从未收到 RTCM 时显示未收到。

Local 模式显示本地输出（RTK 停止时为未启用），Sky 年龄、速率、链路丢失和本地丢弃均为 `—`。Remote 模式显示 Sky 实际上报的状态。Local/Remote、RTK stop/start 和 Sky enable/disable 切换清除窗口；Sky boot ID/流 ID 变化、计数变小、计数回绕、状态时钟回退、长时间 status 空档及 telemetry reconnect 均重新建立基线，避免负速率或累计计数尖峰。

只在中断/恢复、累计新增至少 10 chunks 本地丢弃、窗口链路丢失显著升高时记录诊断。丢弃/丢失告警有 10 秒节流，不逐帧记录。

## 验证与边界

`rtcm_observability_test` 覆盖 health 时间边界、恢复、速率、断流、计数重置/回绕、Sky restart、RTK/config/mode 切换、独立序号缺口、其它 MsgType 穿插、兼容格式、截断载荷、Sky 本地拒绝和 Ground 实际 TCP 能力识别/重连。`navigation_status_panel_test` 覆盖中英文、明暗主题、Remote/Local 字段、语义颜色及 130% 字体窄布局。现有跨进程模拟测试验证新版 Ground → SkyCore → Ground status 全链路。

序号连续性只能测量已建立基线后、两个收到的 RTCM 帧之间的缺口。首个观测帧之前的丢失，以及尾部完全丢失且没有后续帧时的数量不可推导；后者由年龄/中断状态揭示，链路丢失率不声称覆盖它。缺口在后续帧到达时归入统计窗口，不能恢复每个丢失帧的准确发生时间。旧 Ground 不携带独立序号时也不能测量链路丢失。Sky 本地丢弃为累计诊断计数；接收正常不代表 EPSILON 已解析或应用改正数。硬件串口和实际无线链路需在设备上进一步验证。
