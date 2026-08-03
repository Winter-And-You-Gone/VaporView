# 自绘菜单架构

VaporView 的下拉菜单继续保留项目自绘 Popup 外壳：顶层窗口、圆角裁剪、阴影、透明背景、宿主窗口边界约束、弹出定位和关闭行为仍由 `VaporView::SingleLevelPopupMenu` 负责。菜单项交互语义从纯 `QWidget` 行迁移到真实 `QToolButton` 行，当前实现类仍为 `SingleLevelPopupMenuRow`，用于兼容既有调用点和测试。

## 当前结构

```text
SingleLevelPopupMenu : QMenu
├─ 保留自绘 Popup panel、阴影、圆角、mask、边界避让
├─ 统一处理打开/关闭、Esc、方向键、Home/End、Enter/Space
└─ QWidgetAction
   └─ SingleLevelPopupMenuRow : QToolButton
      ├─ setDefaultAction(action)
      ├─ 内部继续使用 QLabel 绘制文字和 check slot
      ├─ paintEvent 自绘 hover / pressed / keyboard focus
      └─ 暴露 QWidget/QAbstractButton/Qt Accessibility 语义
```

这个结构的目标是“视觉继续自定义，交互改为标准 Qt 控件”。不得把这些菜单替换成系统默认 `QMenu` 样式，也不得回退成透明按钮覆盖纯绘制区域。

## 标题栏应用菜单

标题栏应用菜单继续使用现有的三个 `FloatingTitleMenuPanel` 浮动容器，以及其中的 `mainMenu`、`subMenu` 和 `nestedMenu` 内容容器。它们仍负责自绘背景、圆角、阴影、定位、子菜单层级和关闭行为；标题栏菜单没有改为系统 `QMenu`。

```text
FloatingTitleMenuPanel
├─ mainMenu
├─ subMenu
└─ nestedMenu
   └─ TitleApplicationMenuRow : QToolButton
      ├─ setDefaultAction(command.action)
      ├─ QLabel 主文字、快捷键、check 和 arrow 布局
      └─ keyboardFocus 与 selected 状态分离
```

标题栏菜单的 `TitleMenuCommand` 为每个命令创建对应 `QAction`，并使用稳定的英文命令 ID 作为 QAction 和行控件的 `objectName`。当前对自动化最重要的 ID 包括：

- `titleMenuRecordingFolderAction`
- `titleMenuDataViewerAction`
- `titleMenuExitAction`
- `titleMenuViewLogPanelAction`
- `titleMenuLanguageChineseAction`
- `titleMenuLanguageEnglishAction`

标题栏菜单打开后聚焦第一个可用行，`Up` / `Down` / `Home` / `End` 移动行焦点，`Right` / `Left` 进入或返回子菜单，`Enter` / `Return` / `Space` 触发当前 QAction，`Esc` 逐级关闭并最终把焦点恢复到 `titleBarMenuButton`。鼠标 hover 使用 `selected`，键盘导航使用 `keyboardFocus`，两者不共用状态。

## QAction 是状态和行为来源

每个交互菜单行通过 `setDefaultAction(action)` 绑定对应 `QAction`。核心状态来自 `QAction`：

- `text`：同步到行内文本和本地化 `accessibleName`。
- `enabled` / `visible`：同步到真实按钮状态，disabled 行不可聚焦也不可触发。
- `checkable` / `checked`：同步到真实按钮 toggle 状态，同时保留项目 check icon 视觉。
- `toolTip` / `statusTip`：同步到 `accessibleDescription`。
- `triggered`：按钮 click、Enter、Return、Space 都只触发一次 `QAction`。

扩展视觉字段（例如 check icon、行高、左右 padding、危险操作、子菜单箭头）可以继续通过 `SingleLevelPopupMenuRow` setter 或 QAction property 扩展，但不要复制一套独立的 enabled/checked/triggered 状态。

## objectName 规范

每个可交互菜单行必须有稳定、唯一、与语言无关的 `objectName`。推荐在创建 QAction 或 row 时显式设置：

```cpp
auto *row = new VaporView::SingleLevelPopupMenuRow(menu);
row->setObjectName(QStringLiteral("menuActionExample"));
row->setText(tr("示例操作"));

QAction *action = menu->addRow(row);
action->setObjectName(QStringLiteral("menuActionExample"));
action->setText(tr("示例操作"));
```

命名要求：

- 使用稳定英文业务语义，不使用显示文本。
- 不使用数组索引、内存地址、当前语言或排序位置作为稳定 selector。
- 同一窗口层级内唯一。
- 按菜单/功能域分组，例如 `logFilterAttentionMenuAction`、`tcpWaveDisplayRawMenuAction`、`trajectoryHeatMetricMenuAction_peak`。
- `SingleLevelPopupComboBox` 的 popup row 会使用 combo 的 `objectName` 加 item data 生成 `<combo>MenuAction_<token>`；固定选项应提供稳定 item data。

## accessibleName 与本地化

`SingleLevelPopupMenuRow` 会从 QAction/row 文本生成本地化 `accessibleName`，并去掉助记符 `&`。如果菜单项语义不等同于显示文本，调用点应显式设置更准确的 accessible 文案。描述性信息优先放在 QAction tooltip/statusTip，它会同步到 `accessibleDescription`。

要求：

- 名称使用当前界面语言。
- 不把快捷键或装饰符当成名称主体。
- disabled / checked / focused 使用真实 Qt 控件状态，不只靠颜色或图标表达。
- 隐藏项必须 `setVisible(false)`，不要用高度为 0 的可见按钮伪装。

## 键盘导航和焦点

`SingleLevelPopupMenu` 统一处理菜单式键盘导航：

- 打开菜单时记录触发控件，并聚焦 checked 行；没有 checked 行时聚焦第一个 enabled/visible 行。
- Up / Down 在 enabled/visible 行之间移动，并跳过 disabled 行。
- Home / End 跳到第一个/最后一个 enabled/visible 行。
- Enter / Return / Space 调用当前行 `click()`，避免双触发。
- Escape 关闭菜单，并用 `QPointer<QWidget>` 安全恢复触发控件焦点。
- Tab / Shift+Tab 当前策略为关闭菜单并恢复焦点。
- Right 会尝试打开当前 QAction 上挂载的 `SingleLevelPopupMenu` 子菜单；Left 关闭当前菜单。

鼠标 hover 和键盘 focus 分离：hover 使用 `hovered` 状态和菜单 hover 背景，键盘导航使用 `keyboardFocus` 动态属性和自绘焦点框，不使用系统默认虚线焦点框。

## 生命周期

`QWidgetAction` / `QAction` 仍是菜单项所有权和行为入口。若 QAction 被销毁而 row 仍短暂存活，row 会清理 default action 并 `deleteLater()`，避免 UIA 或后续事件看到 orphan menu row。菜单关闭时会清理 hover/focus 视觉，并把焦点恢复到打开菜单的控件；触发控件销毁或隐藏时安全跳过。

`QMenu` 在 QAction 触发后可能自动隐藏；对 `closeOnClick=false` 的多选行，Popup 会在触发前记录当前位置和当前行，并在自动隐藏后立即恢复可见状态与键盘焦点，确保多选菜单不会因迁移到真实按钮而改变原交互。

## 新增菜单项清单

新增或修改自绘菜单项时检查：

- [ ] 使用 `SingleLevelPopupMenuRow`，不要新增纯 QRect hit test 的交互行。
- [ ] QAction/row 有稳定英文 `objectName`。
- [ ] `text` / `accessibleName` 是本地化用户文案。
- [ ] disabled、hidden、checkable、checked 都用真实 QAction / QToolButton 状态。
- [ ] 点击、Enter、Space 只触发一次 QAction。
- [ ] closeOnClick 按原交互语义设置，多选菜单可保持 false。
- [ ] 行高、padding、check slot、check icon 继续匹配现有视觉。
- [ ] 对应测试覆盖 objectName、accessibility、键盘导航和 QAction 同步。
