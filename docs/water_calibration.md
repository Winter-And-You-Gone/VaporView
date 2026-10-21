# 水汽标气定标与初始浓度：实现及算法待补全清单

本文同时是实现说明和后续真实算法交接清单。首版是**软件流程验证用的线性最小二乘模型**，不表示最终物理定标公式、仪器浓度精度、柱浓度或三维水汽廓线已经验收。

## 功能入口和操作

主侧栏 **水汽定标**，包含“标气记录”“拟合与版本”“初始浓度”三个页签。

1. 新建任务，填写名称，选择 816/1368 nm、对应的 DLIA0/0x30 或 DLIA1/0x31、浓度单位。**必须按实际光路核实映射**；UI 初值不是硬件结论。有记录或已保存后不可更改波长、通道和单位，应另建任务。
2. 选择完整 Session 目录，录入本次已知标气浓度，导入已有特征。支持多 Session、多浓度、同浓度重复测量。相同来源记录跳过并报告数量，不重算原始波形峰值。
3. 检查质量、温压、来源、各浓度有效/总数量、均值、总体标准差、极值。记录每页 200 条；选择记录查看原始 CSV 字段和来源。支持编辑浓度/备注、排除/恢复、确认后删除。删除只影响当前编辑修订，已保存快照保留。
4. “存为新修订”保存数据；“线性拟合并保存新版本”同时保存输入快照和不可变参数。检查公式、散点、拟合线、R²、RMSE、逐记录残差和有效范围后，手动启用；不自动启用。可切回历史参数或停用。两波长独立启停，禁止同时启用同一 DLIA 的两个波长映射。
5. “初始浓度”对所选 Session 的已落盘峰值分别应用两波长的 active 版本。可每 2 秒跟随正在录制的 Session。远程场景需先取得完整 Session，不把抽稀预览当作正式计算输入。异常结果显示原因，不输出有效浓度。
6. 结果表显示最近 2000 条，双击查看完整追溯。JSON 导出包含本次计算全部结果及原始输入，不修改原 Session。

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
| 库规模/迁移 | 原子 JSON、全历史核对；旧来源路径回退 | 大库可拆分不可变快照；补 schema 迁移、备份恢复、历史路径重定位 UI |
| 廓线/柱浓度/三维融合 | 本次未实现 | 独立物理算法、量纲、时空匹配和验证资料，单峰浓度不等同廓线 |

更换算法必须增加模型/算法版本，保留旧库可读性及旧参数含义。逐项将本清单更新为批准的公式、资料来源和实际执行的验证记录。

## 软件测试

`water_calibration_test` 用真实录制服务写出的**软件合成**数据，覆盖系数/预测、质量过滤、原序号/tick、温压快照、重复导入、单位/通道隔离、新进程重载/版本切换/停用、并发覆盖保护、字段/篡改检查、范围外处理和导出追溯，并检查已有历史读取/回放及原 Session 文件不变。

`water_calibration_ui_test` 使用隔离设置，执行两种标气导入→拟合保存→启用→计算→关闭重建→重载→停用。另运行既有 Session、FPGA 记录/回放/导出和主窗口回归。所有合成数据及结果标记为 `software_test`，不作为实机浓度验收证据。

### 2026-10-21 本机执行记录

- 基于 `origin/main` 的 `a82220f`，在 VS/MSVC Developer 环境中执行 `cmake --build build/Release --config Release --parallel 4`，完整构建成功。
- `ctest --test-dir build/Release --output-on-failure -j4`：135 项中 132 通过、1 失败、2 跳过，耗时 340.87 秒；新增两个定标测试及现有 FPGA、Session 记录/回放/导出、新增页面导航覆盖通过。
- 唯一失败为 `combination_navigation_page_test` 的 `overview titles remain fully visible in both languages`，与 [既有架构执行记录](architecture_execution_plan.md) 中的修改前基线失败一致；本次没有改动该总览标题布局。
- 跳过项：`update_relauncher_elevated_parent_test` 需要管理员测试进程；`ifw_staging_test` 缺少本机 IFW stage 目录。未将跳过项视为通过。
- 已检查亮/暗主题页面，临时截图与诊断代码已移除；测试完成后恢复应用配置，确认任务启动的测试进程退出。未连接实机进行标气或浓度精度验证。
