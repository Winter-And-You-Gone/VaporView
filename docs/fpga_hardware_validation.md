# FPGA 实机验收清单与证据边界

状态：本轮未连接 GP01 实板，以下实机项目均未执行。本机无 Cypress CyAPI SDK；Cypress 条件分支的 SDK 编译/链接也未执行。软件 replay、测试 fixture、源码对照及 WinUSB 构建只能支持各自软件结论，不可以勾选本清单。

适用资料、模块期望版本、操作语义见 `fpga_vlp1_integration.md` 与同级下位机 `Docs/protocol/HOST_HANDOFF_20261001.md`、`FPGA_HOST_PROTOCOL_V1.0.md`。旧 BIT/IMG 与下位机已有板测报告仅作来源背景，不充当本轮上位机验收证据。

## 1. 现场记录模板

验收记录至少保留：

| 项目 | 必填证据 |
|---|---|
| 时间、操作者、板卡 | 日期、板号/序列号、接线、供电、CON23/CON15/CON16、传感器型号 |
| 软件 | VaporView commit、Release构建、配置snapshot、选用后端、Windows/驱动版本 |
| FPGA/FX3 | 实际下载BIT/IMG路径与SHA256、模块ID/profile、GP01 B1/B0 |
| 驱动分支 | WinUSB或Cypress明确选定；CyAPI SDK版本、头文件/库架构、编译与链接记录 |
| 运行原始证据 | Session目录、原始USB IN、OUT命令、配置/校准/连接快照、状态/错误初末值 |
| 结果 | 每项通过/失败/未执行；失败时间、完整错误及恢复过程，不只截图成功值 |
| 清理 | RAW关闭、设备enable恢复、应用配置恢复、验收启动进程退出 |

现场日志、配置、截图、dump、导出和可能含凭据的测试数据保留在本地验收目录，不纳入Git提交。项目内可保留不含凭据的人工结果摘要和复现步骤。

## 2. SDK 与驱动验收

- [ ] 默认WinUSB构建在 `build/Release` 成功；明确此设备已绑定兼容WinUSB驱动。
- [ ] 枚举出 `04B4:00F1` 应用，而非BootLoader；配置/接口/alternate=`1/0/0`，端点OUT02/IN86正确。
- [ ] EP0 B1/B0 value/index=0各读32字节，核对GP01与0x10AC；记录原始hex及诊断字段。
- [ ] 默认构建在无SDK时显式选择Cypress得到明确不可用错误，不能伪装连接成功。
- [ ] **独立待验分支**：装有匹配架构CyAPI SDK的环境编译/链接原生分支成功，记录SDK与驱动版本。
- [ ] **独立待验分支**：Cypress后端在实际CyUSB设备上枚举、B1/B0、端点读写成功。
- [ ] 两后端分别验证无设备、权限/驱动错误、短读/短写、超时、拔插、取消及重新打开。一个后端通过不能替另一后端勾选。
- [ ] Control Center已关闭端点占用；尝试并发占用时给出明确错误，不通过共享占用制造不稳定成功。

不要为验收自动替换现场驱动或烧录固件。驱动/固件变更按现场明确计划执行，并在记录里说明前后版本。

## 3. 连接、协议与版本门控

- [ ] 未连接时独立FPGA页面显示离线，在线操作不可用，不显示虚构传感器读数。
- [ ] 打开连接后唯一后台IN持续接收；等待命令、启停及排空时不暂停接收。
- [ ] PING完整往返、GET_CAPABILITIES合理，协议1.0、100MHz时间时钟、8192payload上限与当前协议匹配。
- [ ] 模块ID：ADC0/1 `00200101/00210101`，descriptor均`00011800`；DLIA0/1 `00300101/00310101`，profile均`00040101`；AI8 `00460200`。
- [ ] 旧模块或profile不匹配时拒绝应用当前配置；仍能查看明确诊断，不悄悄改为在线就绪。
- [ ] USB connected=true但ready=false时可以刷新/断开，采集和配置操作保持禁用；断开后两状态都清除。
- [ ] VLP半帧、多帧、非read边界、4字节填充正确；CRC错误被保留且不生成有效测量。
- [ ] RESP按会话/sequence/source/msg匹配；迟到、错误source/msg、PROTOCOL_ERROR不能完成别的事务。
- [ ] 写超时之后先读回确认；不盲目重试，不把超时解释为“仪表一定没执行”。

## 4. 参数、WMS/DAC与独立启停

验收前保存配置、CONTROL、STATUS、ERROR和计数初值；有在途数据时先按停止/排空顺序处理。

- [ ] WMS0/1 mHz/Q1.31/U32相位/更新率输入与发送原码一致；shadow逐项读回。
- [ ] WMS COMMIT_CONFIG联合协调DAC、等待pending完成、读STATUS/ERROR与实际更新率；没有把普通WRITE ACK当成生效。
- [ ] DAC限幅20bit、clear在min/max范围内，gain UQ2.30、signed offset及SPI请求合法；核对实际SPI/更新率。
- [ ] 独立WMS0启停只改变WMS0；WMS1、DAC和其他采集模块状态按按钮约定保持。对WMS1重复验证。
- [ ] 独立DAC0/1启停不调用联动通用START/STOP，不停止另一条扫描或模拟输出。
- [ ] 普通停止采集只停止/排空ADC和DLIA，未清global或无条件停止WMS/DAC。
- [ ] 示波器在真实DAC引脚核对两路频率、幅度、偏置、相位、clamp与启停。数字write_count/ready只能证明本地数字路径，不证明模拟信号正确。
- [ ] 500k更新、20MHz SPI请求等示例按actual读回核对，不能以请求值冒充真实速率。

## 5. 双ADC、DLIA与RAW诊断

- [ ] 双ADC停止且PHY空闲、旧队列排空后，只通过4010修改共同采样率；未写4110/4130/4134、未访问4140。
- [ ] ADC0 commit完成后4014/4114实际速率一致；随后重新提交两DLIA并核验504C/5050、514C/5150。
- [ ] 同路WMS提供有效周期标签；MODE2独立参考场景仍有WMS周期，不把停机4096点前缀当连续解调通过。
- [ ] RAW默认关闭；六传感器与DLIA连续接收、记录不依赖RAW开启。
- [ ] 显式启动默认10秒RAW诊断窗口；到期发送关闭并读回403C/413C与raw_stream_mask，两路RAW已关闭。
- [ ] RAW开启失败、到期关闭失败、命令busy/超时等场景显示未确认状态，不能仅凭本地定时器显示硬件已停止上传。
- [ ] 主stream_mask保持管理/时间事件可达，不将六传感器START mask直接写为stream_mask。
- [ ] RAW schema1短周期和schema2长周期均能接收；两路24位符号正确，无符号格式不误作signed。
- [ ] DLIA format0 H1/H2正确；若实板允许其他编译format，独立验证format1 IQ、format2 IQ+H1/H2及8/16/24字节点宽，不通过MODE冒称改变format。
- [ ] 片乱序、重复、重叠、缺片、PARTIAL/OVERFLOW、未知schema都保留证据；不跨cycle/tick补齐，不产生假完整曲线。
- [ ] 对有限pending缓存产生的淘汰与停机flush，保存partial/continuityError及fragment spans；没有静默丢失缺片周期。
- [ ] 仅在有独立通道VREF/增益/极性/数字校准证据且snapshot标为已确认后显示电压；否则显示原码。
- [ ] DLIA只显示数字幅值/IQ及明确质量；没有未经光路和标气模型确认的ppm。
- [ ] DLIA按实际format提供H1/H2、IQ或六分量选择；切换只影响绘图，不改硬件MODE/schema或原始记录；异常周期清线而非跨缺口连线。

## 6. 六传感器与AI8设温

六路逐步启用，初步每路持续检查后共同连续采集至少60秒；长期采集时长由现场吞吐/稳定性目标另定并记录。

- [ ] HMP41及未验电机禁用，未被六传感器启用动作误开。
- [ ] BMP禁用时6310=77并读回；21字节校准来自本次连接，24bit原码边界正确；补偿与下位机bmp_compensation.py同输入比对Pa/℃。
- [ ] BMP缺校准、掉线或重新连接时显示未补偿/无效状态，不能沿用上次设备校准。
- [ ] PTB210为默认压力来源，Pa=I32 mPa/1000；选择BMP时明确来源，不无提示回退。
- [ ] SHT45温湿单位、TFA1500-L mm正确，不重复将距离乘10。
- [ ] EPSILON内部CRC、packet ID、IMU/姿态/导航字段与原始FDILink交叉核验；状态新鲜、导航/UTC模式有效性分别验证。
- [ ] EPSILON仅通信正常、status缺失/过期/模式失效时，不能宣称GNSS定位或UTC有效。
- [ ] AI8禁用时修改地址/通道/poll/6660 timeout/6664 retry并读回；默认200ms/1，边界输入合法。
- [ ] AI8 PV/SP/SV按0.1℃原码，alarm/control/host、设备错误、set_result保留；有报警时不伪装无报警在线。
- [ ] 设温前保存6648基线；0.1℃步长目标正确，事务串行。
- [ ] 设温最终证据：pending=0、664C相关状态=10000、6648相对基线加1（32bit回绕）、6654高低I16读回/请求均等于目标。
- [ ] 普通shadow WRITE ACK、设温失败/超时/禁用中止、计数不增或读回不等都不能显示“设备设定已确认”。
- [ ] 设定确认与PV达到目标分开；试验不把设温成功当温控动态准确度验收。

## 7. 记录、回放、导出与重连

- [ ] 开始记录时保存后端/版本/配置/前端校准/压力来源及诊断基线。
- [ ] raw/fpga_vlp1.dat同时包含精确USB IN、OUT命令、完整IN帧及snapshot；host time、tick、flags/source/msg/seq/cycle保留。
- [ ] 关闭传感器/DLIA/RAW解析记录筛选后，type1记录/摘要按选项改变，但活动Session的精确type4 USB流仍完整保留；硬件RAW关闭另外检查。
- [ ] rawFpgaRecords及manifest计数包含全部四类成功写入记录；pause/stop排空后与扫描索引匹配，不误称为采样点数。
- [ ] device.fpga结构化日志具有event/operation/ready/busy，错误附error_code；页面message、操作失败snapshot及原始OUT/RESP可以交叉追溯。
- [ ] CRC损坏、半帧及不可解析USB字节仍能从BIN重现；未把FPGA数据写到PTB/HMP旧raw文件。
- [ ] 高频记录在后台队列执行，界面交互、缩放和关闭不引发GUI同步落盘卡顿。
- [ ] pause排空/resume同session、stop关闭flush正常；写/队列/flush错误显示incomplete而非完整成功。
- [ ] 重连时取消pending、重置半帧/组包/状态与BMP校准，不把前后tick当同一连续采样。
- [ ] 断开/重连/配置/校准snapshot可在离线回放观察；不会重放OUT到实板。
- [ ] USB优先回放不重复计入同一IN frame；未知schema仅呈原始证据；坏CRC不产生测量。
- [ ] CSV/JSON字段与原始帧交叉比对，uint64 tick无JSON精度丢失；BIN保持精确USB序列且不混入OUT/JSON。
- [ ] 导出取消/错误不会覆盖原归档；不能选源归档作为输出。
- [ ] 原始解析页识别FPGA、筛选、详情、异常及批量导出正常；旧session解析与文件别名仍有效。
- [ ] 拔插、接收超时、命令中断、应用退出等场景仅恢复现场确认属于本任务的状态，不误停其他程序。

## 8. 验收结论填写规则

分别填写“软件fixture通过”“WinUSB实机通过”“CyAPI SDK编译通过”“Cypress实机通过”“数字采集通过”“DAC示波器通过”“物理校准通过”，不能以一个结论代替另一个。每项结论附对应commit/后端/模块版本/Session及证据路径；失败项写复现、风险与未验证范围。

验收结束关闭RAW、恢复验收前设备enable和应用配置、确认后台命令/记录任务退出，并确认生成文件未暂存进Git。如果现场需要保留不同状态，明确记录人工交接值。
