# VaporView Agent Rules

## Default Delivery Rule

在这个仓库中，只要完成了以下任一类型的工作，就把“提交并推送到 GitHub”视为默认收尾步骤，而不是可选步骤:

- 修复 bug
- 新增功能
- 调整构建配置
- 更新文档以匹配代码事实

除非用户在当前对话里明确说“不要提交”或“不要推送”，否则按下面流程执行。

## Required End-of-Task Flow

1. 固定使用 `build/Release` 作为构建目录，不使用其它临时构建目录作为交付验证依据。
2. 只要改动了代码、构建脚本或会影响可执行结果的资源文件，就必须重新构建，而不是跳过构建。
3. 每次执行回滚操作后，必须使用 `build/Release` 重新构建当前回滚后的版本。
4. 每次执行 push 前，必须先使用 `build/Release` 重新构建当前将要推送的版本；构建失败时先修复问题，不要 push 失败状态的改动。
5. 默认只在当前本机环境执行配置、构建和测试，不要求远程主机或 SSH 验证。
6. 确认需要提交的文件，只包含本次任务相关改动，不回滚用户自己的其它改动。
7. 使用清晰的非交互式 git 命令创建提交。
8. 默认将本次提交推送到 `origin/main`。
9. 在最终回复里说明:
   - 做了什么
   - 如何验证
   - 使用的构建命令与是否构建成功
   - 提交哈希或提交标题
   - push 是否成功

## Long-Running Build/Test Hygiene

- 对可能超过 30 秒或输出很大的命令，尤其是 `cmake --build`、`ctest`、GUI 布局测试和部署步骤，优先分段执行：先构建目标或相关测试，再跑完整构建/完整测试，不要把所有步骤塞进一条很长的链式命令。
- 避免把 MSVC include trace、部署日志等海量输出直接刷到对话里。长构建可以把 stdout/stderr 写入 `build/Release` 下唯一命名的临时日志，只摘录 `warning`、`error`、`FAIL`、`ninja:`、测试摘要等关键行；任务结束前删除这些临时日志，除非用户明确需要保留。
- 测试范围默认按改动面收敛，不要每次无脑跑全量 `ctest`。只改某个模块或 UI 局部时，优先构建相关 target 并只跑直接相关测试；例如 UI 样式/布局改动通常跑 `main_window_layout_test`、`session_viewer_theme_test`，涉及 3D 地图窗口时再加 `map3d_window_smoke_test` 或 `main_window_map3d_live_test`。
- 只有改到共享底层、CMake/链接结构、协议/记录链路、跨进程 SkyCore/SkyTui、公共数据格式、或大范围重构时，才默认跑完整 `ctest`。发布前或用户明确要求全量验证时也跑完整 `ctest`。
- push 前的硬性要求是使用 `build/Release` 重新构建将要推送的版本；除非本轮改动风险需要或用户要求，push 前不必额外重复全量测试。
- 对 `VAPORVIEW_ENABLE_OSGEARTH` 相关工作，验证顺序默认先 OFF、后 ON，并让最终 `build/Release` 保持 ON，方便用户直接看 3D 地图效果。OFF 阶段通常只需确认默认构建可用；ON 阶段按改动范围跑相关 3D/UI 测试。
- 如果用户中断、工具超时、或上一条命令疑似卡住，继续前先检查残留进程，例如 `cmake`、`ctest`、`ninja`、`MSBuild`、`cl`、`link`、`VaporView` 和相关测试 exe。只结束能确认属于本次任务的残留进程，不要误杀无关桌面程序。
- UI/布局验证需要截图时，截图导出代码和截图文件默认只作临时检查；视觉确认后移除临时代码和产物，再进入最终构建、测试、提交。
- 不要在构建或测试进程仍在后台运行时提交或 push。提交前确认工作区只包含本次任务相关文件，push 前仍按上面的 `build/Release` 规则重新构建。

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
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them - don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

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
- "Fix the bug" -> "Write a test that reproduces it, then make it pass"
- "Refactor X" -> "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:
```text
1. [Step] -> verify: [check]
2. [Step] -> verify: [check]
3. [Step] -> verify: [check]
```

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation rather than after mistakes.
