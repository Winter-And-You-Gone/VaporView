# AI8 连续绘图与 SOS / PPS 上位机适配（2026-11-17）

## 对接基线

下位机合并提交 `9c57d39`，包含 `6c81fd5` SOS 和 `48abb8f` PPS 扫描扩展。
接口依据下位机仓库 `Docs/protocol/DLIA_SOS_HOST_HANDOFF_20261116.md`、
`Docs/protocol/PPS_WMS_DLIA_HOST_HANDOFF_20261116.md`。
11 月 17 日进行中的 DLIA 流水时序修订保持寄存器、采集标签和帧协议不变，
因此无需额外字段适配；支持情况以实际烧录固件的 API 读回为准。

## AI8 卡顿原因与处理

- 原绘图定时器每 100ms 请求重绘，但时间轴右端仍取最新样本时间，
  两次轮询之间视口不动，下一条样本到达才跳动。改为独立时钟驱动、33ms 重绘，
  隐藏图停止定时器，已有测量点按真实接收时间连续移出视口。
- FPGA 扫描上报包含其他通道的缓存，原来每次收到聚合数据都给所有缓存追加新时间戳。
  现在保留每通道 FPGA tick，仅样本标识变化才追加历史，温度相同的新测量仍正常追加。
- 不插值生成测量，不把动画刷新率当采样率；真实轮询延迟和数据中断仍会表现为测量点间隔。
  远程 AI8 状态增加可选 `0x40` 标志及八个 U64 sample tick；新版兼容旧消息，
  远程使用时 Ground 与 SkyCore 需一起更新，旧客户端不支持新的可选尾部。

## FPGA 页面 → 高级控制

两路各有 SOS / PPS 控制卡：

- **SOS**：2–8 阶 Butterworth，整数 Hz 截止频率。自动读取实际 ADC 输入率，
  用双线性变换设计并量化为 signed Q2.46，低 Q 二阶节先行、奇数阶的一阶节最后，
  未用节填单位节。逐节校验稳定性、范围和 DC 增益。
  停止采集、写阶数及 Fs/Fc、完整写入并回读 30 个字，确认完整性后单次提交；
  核对 pending、生效代次、阶数、Fs/Fc、profile、错误，再报告成功。
  不自动重新开始，不自动重试不确定写入。
- **内置滤波**：显式按钮恢复 DLIA 页所选档位。普通 DLIA 参数提交保留自定义 SOS，
  不再无条件写旧截止寄存器。自定义 SOS 活动时拒绝直接改变 ADC 设计率；
  先恢复内置、改变 ADC 率，再生成新 SOS。
- **PPS**：连续 / PPS 单次模式与 signed Q1.31 等待码。先禁用 WMS、等扫描排空，
  再停采集并写入回读。等待码不是伏特，应用后需显式开始采集；
  TIME 同步、DAC、WMS、DLIA 原有配置仍须正确。
  页面显示实际模式、等待码、armed/busy/PPS 合格、接受/漏接/不合格/完成/中止计数。
- **一致扫描快照**：前后读取并核对 busy、接受计数、完成/中止计数、PPS 序号及 cycle ID，
  仅一致时记录 PPS / START / END tick，并单独标识窗口有效性和中止。
  快照经现有会话 snapshot 通路保存；需开启会话记录才能持久化。
  不从 PPS 推算 UTC，不把完成计数当成 USB 分片完整性或模拟输出电压证明。

SOS `+F0=0x534F5301`、WMS `+84=0x50505301` 单独探测；
只有明确 BAD_ADDRESS(5) 可以降级为旧固件，通信超时不会伪装成“不支持”。
旧固件不写新地址，新固件接受内置 profile `0x00040102` 与经 API 确认的自定义 profile。

## 验证范围

软件回归覆盖系数参考向量、2–8 阶稳定性、完整上传与 active 核验、普通提交保留 SOS、
PPS signed 等待码、无触发快照无有效窗口、旧固件握手、UI 能力门控、
AI8 样本标识远程往返与去重、无新数据时连续滚动。
协议模拟器仅验证主机事务，不替代真实 FPGA 的滤波数值、PPS 物理时序和温控现场验证。

### 本机验证结果

- 在 VS/MSVC Developer 环境执行 `cmake --build build/Release --config Release --parallel 6`，最终构建成功。
- 最终相关回归通过：`ai8_temperature_controller_panel_test`、`temperature_controller_panel_test`、
  `fpga_extensions_test`、`fpga_remote_controller_test`、`fpga_control_page_test`、
  `ui_test_mode_window_test`、`ui_test_navigation_coverage_test`。
  导航覆盖在并行全量运行中超时，串行复测 121 秒通过。
- 完整运行了 183 项 CTest；主窗口布局与独立查看器主题检查通过。
  本次涉及的旧时间轴断言已更新为分别验证时钟和样本时间；窗口宽度检查使用真实渲染间距，
  避免 Windows 桌面宽度限制导致错误地要求刻度数翻倍。
- 以下三项在串行复测后仍失败，相关产品代码未在本次任务修改，保留供后续处理：
  `fpga_transport_capabilities_test` 仍要求 AI8 提示包含 `VLP1`，与仓库原有提示文案不符；
  `device_config_layout_test` 的串口波特率控件尺寸断言；
  `main_window_session_viewer_test` 的滑块偏心抓取位置断言。
  `update_relauncher_elevated_parent_test` 由现有测试环境跳过。
- 未进行真实硬件采集、SOS 响应或 PPS 扫描验证。
