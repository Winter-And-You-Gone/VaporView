# VaporView Agent Rules

## Default Delivery Rule

本地软件诊断日志允许保留原始连接凭据，便于现场排查问题；但生成的日志、截图、崩溃 dump、导出包和包含凭据的测试样例不得提交或推送到 GitHub。提交前必须检查 staged 文件，不得把这些生成物作为项目 artifact 纳入版本库。

在这个仓库中，只要完成了以下任一类型的工作，就把“提交并推送到 GitHub”视为默认收尾步骤，而不是可选步骤:

- 修复 bug
- 新增功能
- 调整构建配置
- 更新文档以匹配代码事实

除非用户在当前对话里明确说“不要提交”或“不要推送”，否则按下面流程执行。只读检查、讨论和建议任务不触发修改、构建或提交流程。

## Required End-of-Task Flow

1. 固定使用 `build/Release` 作为构建目录，不使用其它临时构建目录作为交付验证依据。
2. 影响可执行结果的最终代码、构建脚本或资源改动，必须使用 `build/Release` 构建成功；纯文档改动无需构建。
3. 回滚改变了可执行结果时，按同一规则验证最终版本；中间回滚状态无需逐次构建。
4. push 前确认已有成功构建覆盖将要推送的相关构建输入；输入未变化时复用结果，提交动作本身不触发重建。构建失败时先修复本次改动导致的问题，不推送未经验证的可执行改动。
5. 默认只在当前本机环境执行配置、构建和测试，不要求远程主机或 SSH 验证。
6. 确认需要提交的文件，只包含本次任务相关改动，不回滚用户自己的其它改动。
7. 使用清晰的非交互式 git 命令创建提交。
8. 默认将本次提交推送到 `origin/main`。
9. 在最终回复里说明:
   - 做了什么
   - 如何验证
   - 使用的构建命令与是否构建成功；无需构建时说明原因
   - 提交哈希或提交标题
   - push 是否成功

## Long-Running Build/Test Hygiene

- 如果仓库根目录存在未跟踪的 `LOCAL_PITFALLS.md`，开始构建、测试、GUI 验证或排查环境问题前先读取它；该文件用于记录本机专属路径、代理、工具链位置、历史失败症状和规避命令，不应提交到 Git。
- 每轮首次 GUI 验证前读取 `LOCAL_PITFALLS.md`（若存在）；文件或环境变化、出现相关故障时重读。按其中记录选择验证路径，禁止重试已知不兼容的接口。
- Windows/MSVC 下执行 `cmake --build`、`ctest` 或任何依赖 MSVC 编译器的验证前，必须先进入 VS/MSVC Developer 环境；不要直接在普通 PowerShell/Codex shell 里跑 Release 构建：即使能找到 `cl.exe`，也可能缺少标准库 `INCLUDE/LIB` 环境，典型失败是 Qt 头间接包含 `<utility>`、`<type_traits>` 等标准库头时出现 `fatal error C1083`。本机专属的 VS 安装路径、代理端口、工具链位置等不要写进本文件，记录到未跟踪的 `LOCAL_PITFALLS.md`。
- 长任务续接与等待：
  - 仅使用本次会话中工具实际返回、仍有效的句柄：`functions.exec` 的 `cell_id` 用 `functions.wait`；shell 的 `session_id` 用 `write_stdin`。两者不可混用，不猜测句柄。
  - 句柄完成、失效或工具明确禁止继续等待时，立即停止续接。新的命令使用新的 exec，不重复启动尚在执行的任务。
  - 连续两次无新输出或阶段进展时，先检查任务所属进程活动、日志末尾和产物时间戳；根据实际活动、预计耗时和超时边界决定继续、缩小范围或终止，不按固定等待次数判断卡死。
  - 等待期间保持简短进度沟通；不为等待而重复读取上下文。不要主动将普通构建、测试或 Git 命令转为后台任务。
- 对可能超过 30 秒或输出很大的命令，尤其是 `cmake --build`、`ctest`、GUI 布局测试和部署步骤，优先分段执行：先构建相关目标并运行直接相关测试，是否扩大验证范围按下述改动面规则决定，不要把所有步骤塞进一条很长的链式命令。
- 避免把 MSVC include trace、部署日志等海量输出直接刷到对话里。长构建可以把 stdout/stderr 写入 `build/Release` 下唯一命名的临时日志，只摘录 `warning`、`error`、`FAIL`、`ninja:`、测试摘要等关键行；任务结束前删除这些临时日志，除非用户明确需要保留。
- 测试范围默认按改动面收敛，不要每次无脑跑全量 `ctest`。只改某个模块或 UI 局部时，优先构建相关 target 并只跑直接相关测试；例如主窗口 UI 跑 `main_window_layout_test`，session viewer 跑 `session_viewer_theme_test`，涉及 3D 地图窗口时再加 `map3d_window_smoke_test` 或 `main_window_map3d_live_test`。常规快速回归可用 `ctest -L fast`，但不能替代按改动面选择的 focused tests。
- 只有改到共享底层、CMake/链接结构、协议/记录链路、跨进程 SkyCore/SkyTui、公共数据格式、或大范围重构时，才默认跑完整 `ctest`。发布前或用户明确要求全量验证时也跑完整 `ctest`。
- push 前复用覆盖最终相关输入的成功构建和测试结果；仅在输入变化、失败或未解决风险需要时重新验证。
- 修改 `VAPORVIEW_ENABLE_OSGEARTH` 开关、依赖、链接或条件编译边界时，先验证 OFF、后验证 ON，并让最终 `build/Release` 保持 ON。地图内部局部改动仅验证 ON，并运行直接相关测试。
- 如果用户中断、工具超时、或上一条命令疑似卡住，继续前先检查残留进程，例如 `cmake`、`ctest`、`ninja`、`MSBuild`、`cl`、`link`、`VaporView` 和相关测试 exe。只结束能确认属于本次任务的残留进程，不要误杀无关桌面程序。
- UI/布局验证需要截图时，截图导出代码和截图文件默认只作临时检查；视觉确认后移除临时代码和产物，再进入最终构建、测试、提交。
- 不要在构建或测试进程仍在后台运行时提交或 push。提交前确认暂存区及本次待推送提交只包含本次任务相关改动，保留用户其它工作区改动；push 前按上述规则确认验证结果可复用。

## Test State Restoration

- 任何测试、GUI 验证或临时诊断结束后，都必须恢复测试开始前的应用状态、配置状态和工作区环境，不能把测试过程中的临时状态留给后续人工验证或下一项任务。
- 涉及字体/界面缩放、窗口尺寸、主题、语言、侧栏宽度、滚动位置或持久化配置时，测试结束必须恢复原值；如果测试前使用的是默认状态，放缩测试结束后必须恢复为项目约定的“标准”大小（100% 字体缩放和正常窗口尺寸）。
- 优先使用隔离的临时 `QSettings`、临时配置目录和测试专用窗口，配合 RAII、清理钩子或等价的 finally 路径保证恢复；测试中断、失败或超时也必须执行清理。
- 测试结束前确认当前任务启动的进程已经退出，并删除临时截图、诊断代码、构建日志和其它测试产物；不得把这些状态或产物提交、推送到 GitHub。

## Qt Popup Styling Guardrails

- `QComboBox` 的下拉框是 Qt 内部创建的原生 popup 顶层窗口，不能用自绘菜单的方式改它的 `view()->window()` 来做外侧阴影。不要给 combo popup 容器设置 `WA_TranslucentBackground`、`WA_NoSystemBackground`、透明 stylesheet、外扩 geometry、shadow margin 或自定义 shadow host；Windows 下这些透明 backing store 很容易显示成黑色外框/黑块。
- `QComboBox` 下拉框的原生容器只做安全样式：`QAbstractItemView` stylesheet、面板圆角、上下留白、整行高亮、必要的 rounded mask；下拉框应贴合触发控件下沿，不要人为加入锚点空隙。不要给原生 combo popup 添加自绘外侧阴影；Windows 下这类辅助透明窗口容易造成上下重叠、左右半透明残影或黑边。combo popup 的 container、view 和 viewport 都必须保持非透明、可填充背景，不能用透明 viewport 去露出底层或辅助层。需要阴影质感时，迁移为项目自绘的 `SingleLevelPopupMenu`。
- 固定选项、非可编辑的短下拉框如果需要和 TCP 显示菜单一致的圆角/阴影，应优先使用 `SingleLevelPopupMenu` 路径（例如保留 `QComboBox` 数据接口但覆盖 popup 的轻量子类），不要继续在原生 combo popup 上补阴影。可编辑串口、端口、mountpoint 等输入型 combo 默认保留原生 popup 路径。
- 修改 popup/菜单视觉后，必须用真实 GUI 截图或用户截图确认没有黑边、黑块、直角残留；无头布局测试只能覆盖属性和 stylesheet，不能证明 Windows 透明合成正确。

## Git Safety

- 不要改写历史，除非用户明确要求。
- 不要默认 `--amend`。
- 不要 push 与当前任务无关的改动。
- 如果工作区里混有无关改动，只提交本次任务相关文件。
- 如果 push 失败，保留本地提交，并在回复里明确说明失败原因和下一步建议。

---

# Shared Behavioral Guidelines

Behavioral guidelines to reduce common LLM coding mistakes. Merge with project-specific instructions as needed.

**Language:** 默认用中文回复用户，除非用户主动用英文交流。

**Tradeoff:** These guidelines bias toward caution over speed. For trivial tasks, use judgment.

### 1. Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

Before implementing:
- 先查代码与已有上下文，明确请求范围。
- 可逆、低风险的实现选择自行判断，必要时简要说明假设。
- 仅在缺失信息会实质改变目标或造成不可逆后果时询问；等待期间继续不依赖答案的工作。
- 存在明显更简单的方案时说明取舍，不扩展任务范围。

### 2. Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

### 3. Surgical Changes

**Touch only what you must. Clean up only your own mess.**

When editing existing code:
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it - don't delete it.

When your changes create orphans:
- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: Every changed line should trace directly to the user's request.

### 4. Goal-Driven Execution

**Define success criteria. Loop until verified.**

Transform tasks into verifiable goals:
- "Add validation" -> "Write tests for invalid inputs, then make them pass"
- "Fix the bug" -> "Reproduce the failure and verify the fix; add a regression test when it provides meaningful coverage"
- "Refactor X" -> "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:
```text
1. [Step] -> verify: [check]
2. [Step] -> verify: [check]
3. [Step] -> verify: [check]
```

完成请求范围内的改动和直接相关验证后即进入交付。已有有效验证且相关输入未变化时，不重复执行；只有新改动、失败或未解决风险才扩大验证范围。仅修复本次改动造成的问题，无关失败如实报告。

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation rather than after mistakes.
