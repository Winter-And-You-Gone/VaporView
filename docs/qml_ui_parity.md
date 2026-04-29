# VaporView QML 界面一致性记录

本文档记录从 Qt Widgets 界面迁移到 QML 前端时发现的可见 UI / 功能差异。视觉参考为 `Dyad/VaporView-QML`，真实业务能力以当前 C++ 项目为准。

| 页面 | React / Dyad 项 | 当前 C++ 真实能力 | QML 实现状态 | 原因 / 后续修复建议 |
| --- | --- | --- | --- | --- |
| 设备 | TCP 波形源和串口设备并列展示 | TCP 波形源与串口采集器是独立连接流程 | 已实现为设备卡片，并在“波形”页提供详细连接控制 | 保留现状；后续可在 TCP 设备卡片中直接增加 host / port 编辑字段。 |
| 设备 | 每个设备都有独立连接 / 测试 / 详情按钮 | 旧 QWidget 对选中的串口设备使用全局连接流程，没有为每个设备提供独立测试 / 详情入口 | QML 保留全局串口连接流程，TCP 卡片单独路由到波形连接 | 等后端支持单设备长耗时采集器所有权后，再补独立测试 / 详情命令。 |
| 设备 | EPSILON 每个 packet row 的完整包频率配置弹窗 | 旧 QWidget 有完整包频率配置弹窗 | QML 目前只有分组保存 / 重配快捷入口 | 需要补一个专用 QML 包频率编辑器，覆盖 packet ID `0x40/0x41/0x42/0x50/0x59/0x5A/0x5C/0x5D`。 |
| RTK | 检测 mountpoint | 旧 QWidget 可以在后台线程获取 mountpoint 列表 | QML 暂未暴露 | 增加 `RtkBackend::fetchMountpoints()` 和选择弹窗。 |
| RTK | GGA 实时监视 | 旧 QWidget 有 GGA 文本监视和实测速率显示 | QML 已暴露 RTK 统计，并可从最新 EPSILON 坐标发送 NMEA | 如果现场调试需要，再补完整 GGA 文本 / 频率监视面板。 |
| Sessions | 轨迹地图视图 | 旧 QWidget 有 OSM / 天地图瓦片地图弹窗 | QML 已显示 session 摘要、CSV 预览和波形预览 | 后续将地图控件迁移为 `QQuickPaintedItem` 或 Qt Location / Map。 |
| Sessions | 环境曲线与波形 slider 同步 | 旧 QWidget 会把 CSV 行与波形帧预览同步 | QML 目前只预览 CSV 行和第一帧波形 | 增加帧 slider、CSV 行高亮和环境趋势图。 |
| Sessions | React mock 中出现 MAT / HDF5 导出 | 当前 QWidget 中没有找到 MAT / HDF5 导出器 | 未实现 | 在真实导出需求明确前，按 React mock-only 项处理。 |
| Raw Parser | 完整解码字段树和异常过滤 | 旧 QWidget 有深度 packet 解码，以及列表 / 选中 BIN / decoded CSV / decoded JSON 导出 | QML 已列出 raw 记录和基础元数据，支持列表 CSV / JSON / 选中 BIN 导出 | 将 `RawDataParserWindow.cpp` 中的 decoder helper 迁入非 Widget 服务后再补齐。 |
| 设置 | 显示密度 | React mock 包含 compact / normal 密度 | QML 暴露字体比例，默认视觉密度偏紧凑 | 如果用户需要运行时改变间距，再增加持久化 density token。 |
