# UI Automation 与菜单 Selector

本文档记录 VaporView 对 ScreenShotTool-MCP、Windows UI Automation 和 Qt Accessibility 的菜单适配规则。目标是让自动化工具通过真实 Qt 控件语义操作菜单项，而不是依赖坐标、OCR 或截图模板。

## 菜单行语义

共享自绘菜单 `SingleLevelPopupMenu` 的每个交互行都是 `SingleLevelPopupMenuRow : QToolButton`。Popup 外壳仍自绘背景、圆角和阴影；行控件提供标准按钮语义：

- Role：按钮类控件语义。
- AutomationId：来自稳定 `objectName`。
- Name：来自本地化 `accessibleName`。
- Enabled：来自 QAction / QToolButton enabled。
- Focused：来自真实 Qt focus。
- Checked/Toggle：来自 QAction / QToolButton checkable + checked。
- Invoke：来自 QToolButton click → QAction triggered。

非交互元素（分隔线、标题、说明、留白）不应伪装成按钮。

标题栏应用菜单也遵循同一语义，但保留自己的浮动容器：`FloatingTitleMenuPanel` 内的 `mainMenu`、`subMenu` 和 `nestedMenu` 继续自绘，所有可交互行都是 `TitleApplicationMenuRow : QToolButton`。标题栏命令的 QAction 与行控件使用相同的稳定 command ID，自动化可直接按 `objectName` 查找并 Invoke。

## 推荐 selector

ScreenShotTool-MCP 和 UI 自动化脚本优先使用 objectName：

```text
objectName=logFilterAttentionMenuAction
objectName=tcpWaveDisplayRawMenuAction
objectName=temperatureOverviewChannel1MenuAction
objectName=trajectoryHeatMetricMenuAction_peak
objectName=trajectoryHeatPaletteMenuAction_candy
objectName=map3DLayer_satelliteImagery
```

不要使用以下 selector 作为主路径：

- 菜单中第几个元素。
- 当前屏幕坐标。
- 当前 DPI 或窗口大小。
- OCR 文本。
- 图像模板。
- 当前语言显示文本。

## 当前稳定菜单行示例

| 菜单 | objectName 示例 | 说明 |
| --- | --- | --- |
| 标题栏应用菜单 | `titleMenuRecordingFolderAction` / `titleMenuDataViewerAction` / `titleMenuExitAction` | 自绘标题栏菜单的文件命令 |
| 标题栏视图菜单 | `titleMenuViewLogPanelAction` / `titleMenuLanguageChineseAction` / `titleMenuLanguageEnglishAction` | 自绘标题栏菜单的视图和语言命令 |
| 日志筛选 | `logFilterAttentionMenuAction` / `logFilterAllMenuAction` / `logFilterDebugMenuAction` / `logFilterAutoFollowMenuAction` | 主窗口日志筛选 Popup |
| 3D 图层 | `map3DLayer_<layerKey>` | 3D 地图图层显隐，多选项 closeOnClick=false |
| 3D 本地影像 | `map3DLocalImagery_<optionKey>` | 动态本地影像模板项 |
| 温控通道 | `temperatureOverviewChannel1MenuAction` / `temperatureOverviewChannel2MenuAction` | 温控概览通道切换 |
| TCP 波形显示 | `tcpWaveDisplayShowAllMenuAction` / `tcpWaveDisplayRawMenuAction` / `tcpWaveDisplayHarmonicMenuAction` / `tcpWaveDisplayPeakTrendMenuAction` | 波形显示模式 |
| 轨迹热力图 | `trajectoryHeatMetricMenuAction_<metricKey>` / `trajectoryHeatPaletteMenuAction_<paletteKey>` | 轨迹热力图指标和色带 |
| SingleLevelPopupComboBox | `<comboObjectName>MenuAction_<itemDataToken>` | 固定选项 combo 的自绘菜单行 |

## 自动化验收路径

建议的 MCP / UIA 验收流程：

1. 启动 VaporView 的 UI test mode 或不依赖设备的页面。
2. 通过触发按钮 objectName 打开目标 `SingleLevelPopupMenu`。
3. 调用 UI catalog，确认菜单项可枚举为按钮控件。
4. 读取每行 `objectName`、`accessibleName`、enabled、focused、checked。
5. 对目标菜单项执行 invoke。
6. 验证对应 QAction 执行一次，菜单按原 closeOnClick 语义关闭或保持。
7. Esc 关闭菜单后确认焦点回到触发按钮。

如果 Windows UIA / MCP 实机环境不可用，至少必须保留 Qt 层自动化测试。`single_level_popup_menu_test` 覆盖共享 Popup 的真实 QToolButton 语义，`title_application_menu_test` 覆盖标题栏三个浮动容器、稳定 command ID、QAction 绑定、键盘导航、子菜单焦点恢复和 Esc 关闭。

## 维护规则

- 新增菜单项时先给 QAction 或 row 设置稳定 objectName。
- 标题栏应用菜单新增命令时同步更新 `TitleMenuCommand` 的 ID 清单，并保持 QAction 与 `TitleApplicationMenuRow` 使用同一 ID。
- 固定选项 combo 应给 itemData 写稳定英文 key，避免语言变化影响 selector。
- 不要为自动化新增网络控制后门；只使用 QWidget、Qt Accessibility 和 UIA 语义。
- 不要把 disabled 项做成可点击的灰色行；必须 `setEnabled(false)`。
- 不要把 checked 状态只画成图标；必须使用 checkable / checked 状态。
- 测试失败时优先修语义源头，不要让 MCP 回退到坐标点击。
