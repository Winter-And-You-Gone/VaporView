# Changelog

## v1.0.0 (2026-05-05)

VaporView 首个正式发布版本。

### 主要功能

- **设备支持** — 同时接入 EPSILON 组合导航（FDILink）、PTB210 气压计、HMP3 温湿度传感器、TFA1005-L 激光测距模块
- **TCP 波形监视** — 实时显示原始信号、归一化二次谐波和峰值趋势，支持多端序自动识别
- **RTK NTRIP** — 基于 RTKLIB 的 NTRIP 客户端，将差分数据转发至串口或 TCP
- **会话记录** — 手动控制记录启停，按 session 组织 CSV 摘要、原始帧和波形文件
- **数据查看器** — 离线回放会话数据，查看波形、峰值、传感器图表和关联 CSV
- **轨迹查看器** — 从 CSV 提取 RTK 轨迹点，支持 OpenStreetMap 和天地图底图
- **串口自动识别** — 自动探测 EPSILON、PTB210、HMP3、TFA1005-L 设备端口
- **界面** — 中英文切换、F11 全屏、多级字号缩放、QSS 现代风格

### 支持的平台

- Windows 64 位（MSVC 2022 + Qt 6 MSVC Kit）
- Linux ARM64（GCC + Qt 6）

### 工具与诊断

- EPSILON 串口探测脚本（Python / PowerShell）
- EPSILON 主口恢复脚本
- TCP 波形模拟发送脚本
- Python 辅助模块（配置管理、文件日志、数据导出）

### 注意事项

- 第三方 RTKLIB 源码采用 BSD 2-Clause 许可证
- 本项目采用 GPLv3 许可证发布
- 天地图底图需要用户自行申请 Key
