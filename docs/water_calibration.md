# 水汽标气定标与初始浓度：实现及算法待补全清单

本文同时是实现说明和后续真实算法交接清单。首版是**软件流程验证用的线性最小二乘模型**，不表示最终物理定标公式、仪器浓度精度、柱浓度或三维水汽廓线已经验收。

## 功能入口和操作

主侧栏 **水汽定标**，包含“标气记录”“拟合与版本”“初始浓度”三个页签。

1. 新建任务，填写名称，选择 816/1368 nm、对应的 DLIA0/0x30 或 DLIA1/0x31、浓度单位。**必须按实际光路核实映射**；UI 初值不是硬件结论。有记录或已保存后不可更改波长、通道和单位，应另建任务。
2. 选择完整 Session 目录，录入本次已知标气浓度，导入已有特征。支持多 Session、多浓度、同浓度重复测量。相同来源记录跳过并报告数量，不重算原始波形峰值。
3. 检查质量、温压、来源、各浓度有效/总数量、均值、总体标准差、极值。记录每页 200 条；选择记录查看原始 CSV 字段和来源。支持编辑浓度/备注、排除/恢复、确认后删除。删除只影响当前编辑修订，已保存快照保留。
4. “存为新修订”保存数据；“线性拟合并保存新版本”同时保存输入快照和不可变参数。检查公式、散点、拟合线、R²、RMSE、逐记录残差和有效范围后，手动启用；不自动启用。可切回历史参数或停用。两波长独立启停，禁止同时启用同一 DLIA 的两个波长映射。
5. “初始浓度”对所选 Session 的已落盘峰值分别应用两波长的 active 版本。**默认每 2 秒自动跟随所选 Session**，无需反复刷新；不自动选择最新录制任务。远程场景需先取得完整 Session，不把抽稀预览当作正式计算输入。异常结果显示具体原因，不输出有效浓度。
6. 结果表显示最近 2000 条、主机记录时间、通道/原始序号、数据来源及版本；双击查看完整输入和原拟合参数。参数摘要展示两波长的启用版本、单位及 R/C 范围。JSON 导出包含本次计算全部结果、原始输入及完整定标参数，不修改原 Session。
7. 需要保存历史时点击“导出可追溯结果”。应用重启后点击“读取浓度快照”，选择导出的 JSON；读取会暂停自动跟随，保留原计算时间和原参数，不改变当前定标库或启用状态。可再次导出该历史快照。恢复当前计算时重新选择来源 Session 并勾选自动跟随，或点击计算按钮。

切换来源、模拟标记或启停定标版本时，当前结果和导出缓存失效；需重新计算，跟随模式会在下一轮计算。**当前视图仍是整份所选 Session 按当前启用版本重新计算的快照，不是逐周期追加的浓度日志**。已经导出的文件保留原参数和结果；若要保留版本切换前的结果，必须先导出。页面关闭不会自动保存当前浓度，下一次打开可读取已导出快照或重新选择 Session 计算。专项审查与缺陷修复证据见 [水汽定标代码审查记录](water_calibration_review.md)。

录制暂停后不接收新周期，恢复沿用原 Session 并增加 segment；停止后录制服务拒绝迟到输入。计算侧只读已经落盘的特征，不向 Session 写回浓度。来源切换使进行中的旧计算失效；页面销毁后异步只读任务可安全结束，不回填已销毁页面。文件大小/修改时间、启用版本或测试标志变化才触发下一次跟随计算；读取失败会停止自动跟随并显示原因。

模拟测量必须勾选“软件测试数据”。同时检测配置中的 `backend=simulator` / `mode=simulation`；模拟样本拟合的版本会使后续结果继续标记 `software_test`。其余来源标为 `unverified`（待核实），不宣称为真实仪器测量；未检测到模拟标志不等于实机来源已验证。

## 复用链路及真实格式

| 层 | 现有实现 |
|---|---|
| 接收/周期组装 | `FpgaDeviceController`、`FpgaWave::CompletedStream`；下位机解调 H1/H2 |
| 逐点归一化 | `FpgaHarmonics.h::normalizedSecondHarmonic` |
| 峰值持久化 | `shared/session/HarmonicWaveform.cpp::harmonicFeatureCsv`，`max_i(H2[i]/H1[i])` |
| 记录 | `GroundRecordingService`、`SkySessionRecorder` 的共用 harmonic pair 编码与队列 |
| 历史读取 | `SessionWaveformRepository`；新旧功能共用 `SessionWaveformFeatures` 特征读取器 |
| CSV/温压对齐 | `SessionCsv::parseCsvLine`、`SessionLoader`、`Session::closestTimestampIndex`、Session 路径解析器 |
| 配置/日志 | `applicationConfigFilePath()` 路径约定、`QSaveFile` 原子写入、现有 `LogService` |

不改 FPGA 协议、CSV 字段和 Session 结构。`raw/waveform.dat` 保持 VVRAWDAT v2：20 字节文件头、36 字节记录头；FPGA source=5/type=2，payload 为 48 字节头和 H1/H2 成对样本（int32 或 IQ 合成 float64）。归一化数组不落盘，查看时按帧计算。详见 [FPGA Session 格式](fpga_session_recording.md)。

Ground/Sky 的既有 harmonic 特征行写入后增加 QFile flush，使低周期速率下也可在停止录制前读取；flush 失败走原有 incomplete/写失败路径。它是读取可见性边界，不是磁盘物理同步承诺，也不增加一套特征或波形记录。

`sensors/waveform_features.csv` 保持当前 21 列：

```text
host_time_us,epsilon_time_us,original_point_count,search_start_index,search_end_index,channel_id,peak,mean,rms,peak_index,peak_x,min_value,max_value,quality_flags,source,component,algorithm_version,cycle_id,segment_id,fpga_tick,waveform_sequence
```

只使用 `source=fpga, component=H2/H1, algorithm_version=1`、全扫描 `search_start_index=0, search_end_index=0` 的特征。end=0 是扫描末尾哨兵。channel_id=48/49（0x30/31）。host_time_us 为 epoch 微秒，fpga_tick 为独立 100 MHz 计数，不能当 UTC。waveform_sequence 是共用 DAT 的记录序号；同时保留 cycle、segment、CSV 行号及原始字段。

质量位：1=存在无效参考点，2=无有效点，4=周期不完整，8=溢出，16=连续性错误。**位 1 单独出现且仍有有限峰值和有效位置时可用**：已有算法已跳过异常采样点，不能把异常比值加回。其它质量位（含未知位）、空峰值或无效位置不参与拟合和有效浓度计算，保留记录供审计。直连 TCP 行不冒充 FPGA H2/H1 特征。共享查看器对坏周期仍显示断点。

记录 ID 为 Session manifest 的 `session_name + start_time_utc + recording_origin`、通道、waveform sequence 的 SHA-256；标准 Session 移动/复制后仍可识别重复。旧文件缺少 manifest 身份时回退到规范路径。保留导入时路径，不猜测搬迁位置；来源缺失显示提示，不删除历史快照。

## 模型和范围

`FitModel` 解耦数据管理、拟合和预测；工厂当前注册 `linear` / version **1**：`C = a * R + b`，R 是已保存的完整扫描归一化峰值，不是 `max(H2)/max(H1)`。

使用未排除且质量可用的每个周期做**等权最小二乘**，不先按浓度平均。中心化协方差求系数。至少需要两个有效样本、两个不同浓度和非零峰值方差。非有限输入、负标气浓度、重复来源、混合通道、未知单位和不可解矩阵拒绝拟合。支持 ppmv、mol/mol、mg/m3，各单位独立，**不自动换算**。

残差为 `已知 C − 预测 C`，RMSE 为训练残差均方根，R² 为 `1−SSE/SST`。它们不是独立验证浓度误差，也不会自动判定仪器精度合格。大样本散点只抽稀绘制；拟合、统计、分页、逐记录残差和导出使用完整输入。

保存实际观察浓度范围 `[c_min,c_max]` 和有效峰值范围 `[r_min,r_max]`。R 超范围返回“峰值超出定标范围；未外推”，浓度无效。非有限/负预测也无效。首版没有允许外推开关。

## 温压与结果追溯

从既有传感器 CSV 装载器取得温度 °C、压力 hPa，复用最近时间索引；**2 秒**软件关联窗口内保留最近记录快照时间、时差、文件及行号。时间不单调、超窗或缺测时保持 null。此窗口不是物理同步精度承诺。

运行时 `HmpData` / `PtbData` 含有效性及 `steady_clock::time_point` 时间，不能直接当 epoch UTC。现有 HMP/PTB 统一 CSV 没有独立实际传感器采样时间列，不能把 `record_timestamp_us` 快照伪装成采集时间。不用零、其它 Session 或推断高度/位置补缺。

`CompensationModel` 是独立接口，当前实现只保留原值并返回未补偿状态。有效结果明确为**初始浓度**，`compensated=false, compensation_model=unconfigured`；保留初始值、温压及其快照来源。`ConcentrationResult` 包含原 Session/特征、波长、定标 ID、数据集 ID/修订、单位、算法 `water_initial_linear_v1`、计算 UTC、来源类型、质量和失败原因。

## 存储和完整性

位置：**当前应用配置文件所在目录的 `water_calibration/library.json`**。沿用便携/安装及 `VAPORVIEW_CONFIG_FILE` 隔离路径约定，不向历史 Session 添加文件。

库 schema v1 保存追加式数据集修订、不可变拟合版本、两个波长的 active ID 和 SHA-256 完整性摘要。模型内嵌输入完整快照和摘要。保存使用 QLockFile、磁盘摘要并发检查、QSaveFile 原子提交。失败不改旧文件。重载验证字段、单位、schema、模型版本、历史引用及摘要，按保存快照重算核对系数。摘要用于损坏检测，不是电子签名或安全认证。

编辑/删除新增修订，归档新增带标记修订，历史不覆盖。归档不自动停用已有参数，页面明确提示。切换/停用只改 active 引用。切换任务、重载或新建前提示放弃未保存编辑；其它进程更新库时拒绝覆盖，需重新加载。

浓度结果继续采用**用户选择位置的手动 JSON 快照导出**，不添加 Session 文件或自动存档系统。`ConcentrationResults.cpp` 提供 `exportResults` / `readResults`：

- 新导出 schema **v2**：`kind=water_initial_concentrations`，`results` 保存所有计算结果和输入，`calibrations` 按版本 ID 保存每个实际引用的完整 `Fit` 一次（含模型、系数、范围、数据集及其原始标气记录），`sha256` 校验整个快照。
- 读取先检查摘要，再复用 `Fit::fromJson` 校验模型版本和拟合数据；逐条复用 `calculate` 核对结果、质量、通道及引用。通过后恢复**原** `calculated_utc`，保留微秒字符串原始记录时间；不使用当前 active 版本替换历史参数。损坏或不支持的文件整体拒绝，保留此前显示的结果。
- v2 文件可在没有定标库的另一进程中独立读取，不要求原 Session 仍然存在；它保留来源路径和标识，不伪造原始文件可用性。双击历史行可查看内嵌的原模型和数据集。
- 兼容原 schema **v1** 导出：必须在当前库中存在结果引用的**原版本 ID**，否则明确报缺失参数。读取成功给出旧版提示，建议重新导出为 v2；不会用最新版本猜测历史参数。
- 每条 JSON 结果包含 `compensation_status=未进行温压补偿`，包括无效结果；`compensated=false`、`compensation_model=unconfigured`。摘要用于损坏检测，不是防篡改签名或仪器精度证明。

## 待正式算法 / 功能补全（后续持续更新）

| 待确认项 | 当前边界 | 接入点与验收证据 |
|---|---|---|
| 真实定标函数 | 验证用线性等权回归 | 新 `FitModel` 类型/版本与工厂项；真实标气/留出验证集、重复性、残差和浓度误差、适用范围及失效条件 |
| 波长—硬件映射 | 用户明确选择，不推断 | 提供实际光路接线、固件标识和可靠配置来源 |
| 正式温压补偿 | 未配置，未补偿 | 公式、单位/参考状态、系数/版本、范围、不确定度及缺测策略；接入 `CompensationModel`，保留初始值和中间量 |
| 浓度单位/标气口径 | 三种单位独立、不换算 | 湿基/干基、摩尔/体积定义、标准状态、标气证书和转换规则 |
| 实际传感器时间及新鲜度 | CSV 记录快照、2 秒上限 | 如需物理同步，兼容增加实际采样时间及有效性；实机验证时钟、延迟、过期规则 |
| 稳态/权重/异常值策略 | 所有周期，人工排除 | 稳态区间、漂移、噪声/饱和阈值、加权和交叉验证依据；不能仅凭 R² 合格 |
| 真实数据来源确认 | 自动识别部分模拟标记，其余待核实 | 统一模拟/实测 provenance 与实机验收；未核实不称真实浓度 |
| 无 Session 浓度流 | 跟随已落盘特征 | 如需要，建立带完整周期质量和来源 ID 的正式事件，不从 UI 抽稀波形提峰；超大 Session 可增加增量文件游标 |
| 自动逐周期浓度归档 | 当前版本整 Session 计算、手动保存自包含历史快照 | 如需自动追加归档或在同一 Session 中永久保留逐周期启用版本，需单独确定录制时版本捕获、归档生命周期、兼容格式和恢复策略；本阶段不改变 Session 结构 |
| 库规模/迁移 | 原子 JSON、全历史核对；旧来源路径回退 | 大库可拆分不可变快照；补 schema 迁移、备份恢复、历史路径重定位 UI |
| 廓线/柱浓度/三维融合 | 本次未实现 | 独立物理算法、量纲、时空匹配和验证资料，单峰浓度不等同廓线 |

更换算法必须增加模型/算法版本，保留旧库可读性及旧参数含义。逐项将本清单更新为批准的公式、资料来源和实际执行的验证记录。

## 软件测试

`water_calibration_test` 用真实录制服务写出的**软件合成**数据，覆盖系数/预测、质量过滤、原序号/tick、温压快照、重复导入、单位/通道隔离、新进程重载/版本切换/停用、并发覆盖保护、字段/篡改检查、范围外处理和导出追溯，并检查已有历史读取/回放及原 Session 文件不变。

`water_calibration_ui_test` 使用隔离设置，执行两种标气导入→拟合保存→启用→计算→关闭重建→重载→停用。另运行既有 Session、FPGA 记录/回放/导出和主窗口回归。所有合成数据及结果标记为 `software_test`，不作为实机浓度验收证据。

`water_calibration_e2e_test` 通过生产 `GroundRecordingService::recordFpgaHarmonics` 写出真实格式的**合成测试** Session，执行下列闭环，不自行生成另一套峰值算法：

| 场景 | 实际断言 |
|---|---|
| A 标气→拟合→保存→重载 | 4 个独立 Session 的 R=[2,4,6,2]、C=[5,9,12,5]；重复周期跳过、独立同浓度保留；独立核对 a=39/22、b=17/11、R²=1521/1529、RMSE=√(1/22)、4 样本/3 浓度及范围；排除样本、退化输入拒绝；两通道独立拟合、重载预测 |
| B 录制→自动浓度→历史/导出 | 使用页面实际默认 2 秒跟随，连续两个通道；结果输入 JSON 与 CSV 解析结果逐条相等（Session、channel、sequence、host time、tick）；导出后新进程无库读取；暂停/恢复相同 cycle/time 仍按 segment/sequence 区分；停止拒绝迟到周期；切换 Session 不继承旧结果 |
| C 版本切换 | 同一 R 使用另一组系数，816 切换不影响 1368；旧导出字节、原计算时间和原参数保持；读取历史暂停跟随，重导出保持相同内容；停用后新结果无效 |
| D 异常/生命周期 | 无有效分母、溢出、超范围、缺失/停用/通道不符；切换来源丢弃待完成任务，页面销毁后任务安全结束；库/快照损坏与不支持模型拒绝；旧 v1 必须有原参数；测试数据、未补偿状态始终保留；原始 DAT/CSV 不变 |

测试使用隔离配置和临时 Session，退出后清理；不修改用户定标库。开发机在初始化 VS/MSVC 环境后执行 `cmake --build build/Release --config Release --parallel 4`，再执行 `ctest --test-dir build/Release -R "^water_calibration_.*test$" --output-on-failure -j3`。全量回归使用 `ctest --test-dir build/Release --output-on-failure -j4`。

### 2026-10-21 本机执行记录

- 基于 `origin/main` 的 `a82220f`，在 VS/MSVC Developer 环境中执行 `cmake --build build/Release --config Release --parallel 4`，完整构建成功。
- `ctest --test-dir build/Release --output-on-failure -j4`：135 项中 132 通过、1 失败、2 跳过，耗时 340.87 秒；新增两个定标测试及现有 FPGA、Session 记录/回放/导出、新增页面导航覆盖通过。
- 唯一失败为 `combination_navigation_page_test` 的 `overview titles remain fully visible in both languages`，与 [既有架构执行记录](architecture_execution_plan.md) 中的修改前基线失败一致；本次没有改动该总览标题布局。
- 跳过项：`update_relauncher_elevated_parent_test` 需要管理员测试进程；`ifw_staging_test` 缺少本机 IFW stage 目录。未将跳过项视为通过。
- 已检查亮/暗主题页面，临时截图与诊断代码已移除；测试完成后恢复应用配置，确认任务启动的测试进程退出。未连接实机进行标气或浓度精度验证。

### 2026-10-21 端到端流程补全与回归

- 起点为 `31212c1`，开发前同步到 `62f61e8`，期间再次同步主线到 `040c0c4`；保留其首页布局改动。本次修改范围为定标模块、直接测试及本文，不修改 FPGA 算法/协议、Session 格式或 PPK。
- 最终在 VS/MSVC Developer 环境执行 `cmake --build build/Release --config Release --parallel 4`，完整 Release 构建成功。本工作树原有配置为 `VAPORVIEW_ENABLE_OSGEARTH=OFF`，本次未改变开关；没有据此宣称验证了 3D 地图或实机算法。
- 全量首轮 `ctest --test-dir build/Release --output-on-failure -j4`：138 项中 **127 通过、9 失败、2 跳过**，496.98 秒。包括 Session 读写/历史/导出、FPGA 谐波录制、PPK 管线和 PPK 页面在内的相关测试通过。跳过原因仍为管理员权限及缺少 IFW stage，未视为通过。
- 新端到端测试在该轮暴露等待短暂“已启用”状态文案的竞态：自动跟随可立即覆盖文案。已改为等待实际启用参数摘要，再核对计算结果；另检查独立残差与适用范围，以及在摘要正确但浓度不符时拒绝读取快照。
- 同步 `040c0c4`、重建后，对首轮 9 个失败项串行复测：**8 通过、1 失败**，513.75 秒；定标端到端测试、FPGA 控制/启动告警、主窗口、首页布局、标题菜单、导航覆盖及独立查看器均通过。
- 恢复本次改动并完成最终完整 Release 构建后，`ctest --test-dir build/Release -R "^water_calibration_.*test$" --output-on-failure --repeat until-fail:3 -j3`：**4 项各连续 3 次通过**（共 12 次，11.52 秒），覆盖最后新增的残差、结果一致性检查及状态等待修正。
- 剩余 `fpga_local_simulator_test` 在带本次改动时失败于 `coalesced simulator packets populate home LLH plus navigation position, speed and attitude`（44.94 秒）；临时保存全部本次改动，在**工作区干净的 `040c0c4`** 重新构建同目标后，测试也失败于 `fresh navigation returns after actual EPSILON reconnect`（62.63 秒）。首轮则失败于 RD105 异步读取。证明该测试在未修改主线也不稳定，但不宣称这些不同断言已经定位到同一根因；本次未改动相关断言或放宽超时。
- 开发前 `62f61e8` 基线的 `combination_navigation_page_test` 失败于标题可见性（44.10 秒），全量首轮同测试通过（74.14 秒），未作无关修复，也不将其记为稳定失败。
- 已以 Qt 实际渲染快照检查初始浓度页，修正状态和测试来源的列宽；临时截图与截图代码移除后完成最终构建。历史快照读写、独立进程重载、版本切换、暂停/恢复及来源切换均由测试检查实际数据，而非只检查控件存在。
