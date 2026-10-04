# 导航与 RTCM 联合记录

新建 Ground 和 Sky session 都包含 `sensors/navigation_status.csv`。它将 RTCM
状态、GNSS 解状态、设备报告的定位精度和导航坐标放在同一行，供实飞后的时间序列分析。
文件在开始录制时写入首行，录制期间约每秒采样一次；离散状态变化在下一次状态刷新时
保留额外行。暂停和停止期间不写数据，恢复录制会立即写入当前状态。

Ground 记录与组合导航状态页面一致的可用性判断，即使该页面隐藏也持续采样。
Sky 从本机设备和 RTCM 接收计数独立采样，Ground 遥测连接断开时仍能记录。
两端观测位置不同，文件使用相同 schema，但数值和采样时刻不保证相同。

## 时间和字段

| 字段 | 单位和语义 |
| --- | --- |
| `record_timestamp_us` | 当前录制主机的 UTC Unix 微秒；用于关联同一个 session 的 sensor CSV / RAW 主机时间 |
| `epsilon_device_timestamp_us` | EPSILON 原始设备时间；仅在 EPSILON 数据新鲜时填写，不能直接当作 UTC |
| `sky_report_timestamp_us` | Sky 状态采样的 UTC Unix 微秒；与 Ground 时间属于不同主机时钟 |
| `source_mode` | Ground 为 `local` 或 `remote`，Sky 为 `sky` |
| `sky_boot_id`, `rtcm_stream_id` | Sky runtime / RTCM 流标识；分析累计计数前须按这两个标识分段 |
| `rtk_service_running` | Ground 的 RTK 服务状态；Sky 不掌握该服务状态，留空 |
| `rtcm_health` | `local`, `disabled`, `waiting`, `normal`, `warning`, `delayed`, `interrupted`, `link_disconnected`, `unavailable` |
| `rtcm_available`, `rtcm_age_ms` | Sky 是否支持 RTCM 状态扩展；最近有效载荷的接收年龄，毫秒 |
| `rtcm_receive_bytes_per_s` | 最近 5 秒接收字节速率，B/s |
| `rtcm_link_loss_available`, `rtcm_link_loss_percent` | 独立 RTCM 序号统计是否可用，以及最近 10 秒缺帧百分比 |
| `rtcm_link_frames_received`, `rtcm_link_frames_lost` | 当前 RTCM 流累计的接收帧数和已观测序号缺口数 |
| `rtcm_received_bytes`, `rtcm_received_chunks` | Sky 累计收到的 RTCM 字节数 / chunks；包含随后被本地丢弃的输入 |
| `rtcm_dropped_bytes`, `rtcm_dropped_chunks` | Sky 本地转发累计丢弃；不能等同于天地链路丢失 |
| `navigation_available`, `position_available` | 本次采样中的导航解、坐标是否可用；未知或过期的值留空 |
| `gnss_fix_code`, `rtk_fixed`, `gnss_satellites` | EPSILON 原始解代码、是否 FIX、卫星数；沿用页面映射，代码 6 / 9 为 FIX |
| `filter_status_bits`, `update_status_bits` | EPSILON 原始状态位，十进制整数；状态不可用时留空 |
| `hacc_m`, `vacc_m` | EPSILON 报告的水平 / 垂直精度，米；非有限值、负值或导航解不可用时留空 |
| `nav_lat_deg`, `nav_lon_deg`, `nav_height_m` | 导航坐标，度 / 度 / 米；坐标不可用或超出经纬度范围时留空 |

Local 模式不推断 Sky 年龄、接收速率、丢失率或丢弃计数，这些字段为空。
当前 Local EPSILON 的 GNSS/坐标只有聚合包时间，组合导航页不能确认字段独立新鲜度，
联合记录也保留其不可用状态。Sky 的新鲜度沿用现有遥测设备状态判断，不新增协议推断。
旧 Sky 不支持 RTCM 扩展时，RTCM 数值同样为空。

RTCM 年龄使用 Sky 报告时间与最近接收时间之差，并在 Ground 用单调时钟推进，
因此不依赖两端 UTC 同步。跨主机拼接两个 session 时应先校准时钟；设备时间也应单独对齐。
健康阈值和计数边界见 [RTCM 可观测性](rtcm_observability.md)。

## 日志和分析

Ground / SkyCore 在健康状态、解代码、可用性或来源变化时发布结构化事件
`navigation_correction_status_changed`，同时携带 RTCM 年龄、解代码和可用的精度 / 坐标。
`rtk_fix_transition` 为 `baseline`, `degraded`, `recovered`, `unavailable` 或 `unchanged`。
首次观测、模式变化、Sky 重启或换流建立新基线；数据失效不记作真实 FIX 退化。
普通精度波动和状态位数值波动只进入 CSV，避免每秒刷日志。
Ground 完全空闲、没有可用导航或 RTCM 数据的初始基线只进入 CSV，不额外写 UI 日志。
应用结构化日志保留机器字段，session 的 `logs/event_log.csv` 延续现有格式保存事件文案。

离线分析可以先在 CSV 中找到 `rtcm_health=interrupted` 的区间，再寻找同一时间轴上的
`rtk_fixed` 从 1 变 0、`hacc_m` / `vacc_m` 升高，并与 `sensor_summary.csv` 或
`raw/navigation.dat` 的轨迹按主机时间对齐。额外事件行可能使相邻样本不足一秒，
不要按固定行距代替时间戳。计数变化须排除换流和重启，不可用区间须与真实非 FIX 区分。
这提供时间相关证据，不能单凭相关性认定 RTCM 是轨迹异常的唯一原因。

`session.json` 新增 `paths.navigation_status_csv` 和字符串计数
`counts.navigation_status_rows`。这是兼容的附加文件：旧 session 无此文件仍可读取，
缺失计数按 0 处理；已有录制不补造历史状态。

## 验证

`navigation_correction_recording_test` 覆盖中断 → FIX 退化 → 恢复、未知数据、Local 空值、
重启基线、计时抖动、Ground / Sky 相同 schema、暂停恢复、会话边界、写入 / flush 失败、
旧 manifest 兼容，以及无 Ground 连接时的 Sky 生产采样和导航失效日志。
模拟验证不代表已完成真实无线链路、EPSILON 串口或实飞验证。
