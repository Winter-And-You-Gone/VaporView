# FPGA HCD2 适配说明（2026-11-14）

本文是当前 FPGA 模式的优先入口。依据本机 `Vapor_Radar_App_V2` 的 `7cf6986c0e3bfd6a1ed0101d0433a2689754b4c5`，以其中 `Docs/LOWER_COMPUTER_RELEASE_20261113.md`、`Docs/integration/sensors/HC2A_ROASCII.md`、`GNSS_PPS_DM422_20261027.md` 和 `LC_DUAL_UART_20261027.md` 为接口依据。其他 FPGA 文档中的旧型号、旧扩展或旧电机步骤与本文冲突时，以本文为准。

下位机正式归档日期为2026-11-13；所附BIT在2026-10-28构建、2026-11-12继续实测，不能把归档日期当成新综合或新板测。本文描述上位机适配，不改变下位机代码或烧录状态。

## 识别与能力边界

USB仍为`04B4:00F1`、OUT `0x02`、IN `0x86`，VLP1帧格式不变。连接先读取模块ID及能力，再允许对应操作；不能仅凭旧source名称或地址决定设备类型。

| 模块 | 当前ID / 地址 | 上位机行为 |
|---|---|---|
| HC2A | source `0x41`，page `0x6100`，ID `0x00410106`；`0x614C=0x48434432`（HCD2） | 环境温湿度设备，型号、配置、数据显示与新记录统一为HC2A |
| AI8 | source `0x46`，ID `0x00460200` | schema 2，读取已确认的一个选定通道；禁用后才能修改`0x661C` |
| TIME | ID `0x00020200` | 读取PPS原子快照，分别显示沿新鲜、频率合格、UTC锚点有效 |
| DM422 | source `0x47`，ID `0x00470200` | 有限脉冲运动、真实主机心跳；与独立USB串口EM-FWC3控制分开 |
| LC0 / LC1 | source `0x48/0x49`，page `0x6800/0x6900`，ID `0x00480100/0x00490100` | 本次仅显示模块匹配、UART状态及字节计数 |

当前扩展字为`0x29`：RAW schema 2、EPSILON下行/RTCM邮箱和DLIA截止频率扩展保留。AI8自动八通道扫描、通用参数事务和历史独立RD105扩展均已撤回，不能按历史`0x3F`启用。`0x6800`当前是LC0，不能发送历史RD105命令。旧扩展读取兼容只用于明确匹配的历史软件测试，不代表HCD2具有这些能力。

这里的`0x29`指已归档并在初测加载的正式BIT。2026-11-14后续扫描修复新增仅含Ai8Scan的`0x2B`，不恢复参数事务或RD105；补装KU11P并解决构建进程的许可证搜索路径后，新BIT已生成并于16:52通过JTAG加载到SRAM。只读核验确认板上能力`0x2B`、AI8 feature`0x0B`，扫描仍默认关闭，八路实物采集待验。上位机已增加独立扫描契约回归，新BIT的哈希、实现/接口检查、CDC边界及加载证据详见[联调问题记录](onsite_test_issues_2026-11-14.md)。

## HC2A环境温湿度

HC2A使用独立UART和RO-ASCII RDD，默认19200/8N1。地址`99`用于发现唯一探头，合法应答后学习实际地址`0..64`；它不是探头出厂地址。上位机直连也使用真实`{FxxRDD}\r`请求，检查19字段、checksum、地址、数字探头类型、温湿度单位、数值范围和报警，禁止用旧Modbus RTU查询帧只改名冒充HC2A。

FPGA的RH/T沿用source `0x41`、schema 1、F32 tags `0x0100/0x0101`，设备报警仍参与测量有效性判断。FPGA等待间隔范围为100..3600000ms，默认1000ms，实际周期还包含串口事务与解析时间。直连默认1Hz是主机请求节奏，不改变探头内部转换速度。

HC2A地址、串口格式及参数按当前模块ID校验。FPGA配置页波特率显示`0x6110`实际读值，禁用探头后可暂存1200..115200范围的设置，连接时下发并读回，格式保持8N1。禁止HMP旧版的地址240、8N2、压力补偿、CONTROL.bit2 COMMIT和SENSOR_ACTION16。`0x6134`是只读设备报警，`0x6144`是成功记录数，`0x6148`是格式错误次数。普通读回不扫描需要先禁用采集才能保持一致的160字节诊断快照。

本地直连、本地FPGA和SkyCore远程路径都将HC2A作为环境温湿度来源；PTB210仍提供环境气压。BMP390/SHT45只提供独立内部环境，不能覆盖HC2A环境值或用过期数据补位。本地与天空端需同步更新程序，构建新版SkyCore不等于已向现场主机部署。

## AI8、时间与电机

AI8保留八个显示槽位，但HCD2一次只采集`0x661C`确认的一个通道。其他通道没有新数据时保持不可用，不复制该通道值，也不在后台反复停用/改通道/启用来模拟自动扫描。通道设温保留ACTION17和写后读回，报警PV不能显示为正常温度；被撤回的扫描、通用PID/参数及RD105入口按能力禁用。

TIME读取顺序为先读`0x1044`锁存，再读`0x1048..0x1060`同一快照。状态bit0表示频率合格，bit1表示UTC锚点有效，bit2表示近期PPS沿，bit3表示近期串口时间标签。页面显示实测tick/s、PPS序号、tick及有效时的UTC；旧time_sync标志或串口标签新鲜都不能单独证明绝对UTC有效。本次不提供UTC锚点写入，不按电脑时间或最新串口时间自动猜测PPS整秒对应关系。

DM422上位机与5000脉冲修订固件允许20..400Hz、有符号`±1..5000`脉冲；零脉冲无效，默认仍为40脉冲、100Hz。旧HCD2及本日八路扫描首版BIT仍仅允许`±1..400`，超过400必须加载新的5000脉冲BIT；仅更新上位机不能扩展板上范围，旧BIT会拒绝较大请求，不拆成多次运动或自动重试。模块ID和命令格式保持兼容。`0x6718/0x671C`为只读固定50%占空比半周期和10ms建立时间，`0x6730`固定4，移除旧可写脉宽/建立时间/极性入口。先建立真实主机心跳再显式启用，运动期间持续转发当前控制主机心跳；状态轮询不能代替主机存活。心跳间隔目标不超过250ms，下位机2秒失联停用；失联后需显式停用/启用，不自动重放运动。

连接、读取参数及开始采集不触发运动。软件脉冲计数不能当机械位置或到位反馈；“软件脉冲位置清零”不等于回零。当前装配记录中正目标为镜子上移接入方向，机械端点、总行程和反向情况仍需现场确认。EM-FWC3现有独立串口操作保持原语义。

## LC资料与本次范围

已核对用户提供的《LC激光器驱动板-产品说明书v2.1-光测未来》，与下位机资料副本`Docs/【资料】FPGA设备接入手册/11-LC激光器驱动板-产品说明书v2.1-光测未来.pdf`的SHA-256完全一致，共12页。它是产品说明书，没有提供可据以实现的UART命令字节格式、checksum、应答或控制事务规范；不能据此编造激光启停、功率或电流命令。

因此本次仅核对两路LC模块ID并读取UART状态/累计字节数，不启用LC、不写TX、不消费RX。模块存在、启用状态或TX字节计数都不能证明激光器在线或已经执行命令。普通状态读回避开`0x681C/0x691C`有出队副作用的RX_DATA。后续实物控制需厂家通信协议和明确的设备参数。

## 新记录与历史迁移

| 数据 | 新写入 | 历史读取 |
|---|---|---|
| 设备配置 | `hc2a`节，型号`HC2A`、协议`RO-ASCII RDD`、格式`8N1` | 旧`hmp` JSON与`hmp/hmp3`串口预设迁移，显式HC2A配置优先 |
| 传感器摘要 | `hc2a_temperature_c`、`hc2a_humidity_rh` | 旧`hmp_temperature_c/hmp_humidity_rh`列仍可读取 |
| 温湿度原始记录 | `raw/temperature_humidity.dat`，source 3/type 1，完整RDD含CR | `raw/hmp.dat`及旧manifest别名仍可读取；旧type 3按原HMP Modbus RTU解释 |
| FPGA波形 | 共用`raw/waveform.dat`的H1/H2和周期元数据，详见[Session说明](fpga_session_recording.md) | 原有归档读取兼容保留，旧数据不改写成HC2A测量 |

numeric设备/source IDs保留，新的型号和列名不要求改写历史文件。原始波形、H1/H2、归一化与峰值记录继续沿用现有规则，FPGA tick仍是本地计数，不能当UTC。定标中的环境关联保存真实来源和接收时间不确定性，不宣称已实现正式温压补偿。

## 验证范围

本轮按用户要求不连接实体板卡，不加载BIT/FX3，不操作电机、激光或DAC输出。验证对象为`build/Release`中的上位机、SkyCore、协议模拟器、配置/记录兼容和相关测试。以下命令均先初始化本机VS/MSVC环境，测试PATH使用已配置的Qt与Python：

```text
cmake --build build/Release --config Release --parallel 6
ctest --test-dir build/Release --output-on-failure --parallel 2
ctest --test-dir build/Release -R "^(fpga_.*|sky_fpga_.*|hc2a_protocol_test|remote_connection_workflow_test)$" --output-on-failure --parallel 2
ctest --test-dir build/Release -R "^(fpga_controller_test|fpga_extensions_test|fpga_selective_connection_test|fpga_simulator_test)$" --output-on-failure --parallel 2
ctest --test-dir build/Release -R "^fpga_(config_action_readback|relay_configuration|sensor_draft_ui)_test$" --output-on-failure --parallel 2
```

Release构建成功。执行过全量168项；修复并定向复测后，FPGA、Sky FPGA、HC2A协议与远程连接37项全部通过；最后的LC0恢复门控修改再通过控制器、扩展、选择性连接、模拟器4项测试，HC2A波特率修改再通过配置读回、配置中继和传感器草稿3项界面测试。覆盖HC2A配置/历史记录兼容、实际波特率展示/范围/提交确认、默认单通道、PPS原子读取、LC不写入/不出队，以及电机真实心跳超过2秒运行、失去心跳停用、STOP抢占和读回完成。

全量检查仍有4项通用界面断言未通过：`main_window_layout_test`的串口卡片圆角/边线，`device_config_layout_test`的数据率胶囊重叠，`ui_test_mode_window_test`的时间轴刻度密度，`main_window_session_viewer_test`的滑块抓取偏移。本轮未修改这些对应实现，也未另行运行原版本确认原因；不把全量测试报告为通过。`update_relauncher_elevated_parent_test`因当前非管理员上下文按预期跳过。

下位机原有实板报告是该固件的历史证据，不能替代本轮上位机、真实USB/驱动、HC2A精度、PPS UTC关系、机械行程或LC设备的现场验收。当前现场检查应从本文接口及[实机清单](fpga_hardware_validation.md)的HCD2修订开始，禁止照搬历史HMP补偿、已撤回扫描/RD105或旧电机配置步骤。
