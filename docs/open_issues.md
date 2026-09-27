# 问题状态索引

复核日期：2026-09-27。历史审计和规划按各自基线保存，本页区分当前待办、已关闭问题和实机验收边界；软件测试通过不等于硬件验收通过。

## 仍需闭环

| 事项 | 当前状态与下一步 | 依据 |
| --- | --- | --- |
| EPSILON ECEF、系统状态与航向 | 记录 2 的原始 ECEF 异常和 `0x0011` 状态尚未得到型号/固件解释；取得安装参数，与厂商工具及独立朝向参考对照。不能把航迹差直接作为补偿角。 | [导航审计](epsilon_session2_navigation_audit.md) |
| EPSILON 报文频率、杆臂配置 | 普通报文仍缺 200 Hz 而包含 250/500 Hz；IMU 另有 1000 Hz。需要实测支持范围及 `#fantearm` 的生效/保存/断电行为。 | [实机备忘录](epsilon_hardware_validation_memo.md) |
| AI-8288 未确认语义 | `Srun=1`、32 位 PV 字序/符号/缩放、扩展寄存器语义仍待确认；Addr/bAud/AFC 切换须先验证设备响应和重连时序。输入选型 21/22 仍需现场温度点及规格核验。 | [联调清单](ai8288_hardware_validation.md)、[交接记录](ai8288_hardware_handoff.md) |
| Session 正式规范补齐 | 业务数据/日志时间边界已写入正式规范；日志四列及转义、诊断文本接口和 waveform peaks 搜索范围 sentinel 仍需完整定义。 | [正式规范](session_format.md)、[审计 C.11/C.13](session_format_actual_audit.md) |
| Sky 配置快照 | 当前 recorder 未接收实际 SkyConfig，不能把默认/不可用串口字段当真实设备快照；是否补实际快照仍需产品决策。 | [审计 C.11](session_format_actual_audit.md) |
| 非波形 RAW 验收 | 六类协议非空 RAW 的实际设备或 protocol-to-session fixture 验收仍须补齐；零记录合法文件不能代替协议 payload 验证。 | [审计 C.8/C.13](session_format_actual_audit.md) |
| 现场可靠性验收 | 实盘耗尽、断电、长期采集、真实峰值速率慢客户端、串口物理事务去重、实际 Windows/Linux 安装器中断恢复尚未完整验收。 | [工程验证](engineering_repair_validation.md) |
| Windows 输入法共存 | 3D 测试隔离 IME 后的通过结果不覆盖正式程序与第三方输入法共存。 | [工程验证](engineering_repair_validation.md) |

## 已关闭或被后续结论取代

- 环境趋势时间刻度与短样本范围不一致、首页分隔条首次拖动被自动宽度上限阻挡已修复；相关 9 项 UI 回归通过。主窗口测试的 Windows 原生配置也已隔离。

- AI-8 输入类型 22/24 已进入参数页面，不能继续列作 UI 缺项。
- Session Start 前 waveform feature 混入新录制已修复；C.13 取代 C.11 的 A-1 和 C.12 的冻结阻塞结论。
- Stop 生命周期日志晚于 manifest end 属于允许的诊断语义，不是业务数据越界；正式规范已明确。
- Session 计数字符串类型和零记录 RAW 文件语义已在正式规范定义。
- 日志治理文档中旧的 session viewer 轨迹测试、启动测试失败已被本次整理前的 Release 全量通过结果覆盖；旧记录保留为历史。

## 文档阅读规则

- [RD105 规划](temperature_controller_plan.md) 是历史阶段设计；传感器配置、PID 自整定及 Local/Remote 链路已有实现，不能把整份 V2 当作待开发需求。
- [Sky 设备矩阵](sky_device_coverage.md) 的 PASS 是软件链路覆盖，不代替真实硬件验收；AI-8 实机恢复出厂是明确的能力边界。
- [菜单架构](menu_architecture.md) 的空勾选项是新菜单的开发检查模板，不是当前缺陷列表。
- 最新 UI 修复和验证结果记录在 [工程验证](engineering_repair_validation.md) 的 2026-09-27 小节，历史通过结果不覆盖后续代码变更。
