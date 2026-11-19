# GUI 内置 Gateway

Gateway 复用 `GroundTelemetryService` 已接收并通过帧校验的数据。地面端可以主动向云端转发，也可以通过 HTTP 查询、控制和 WebSocket 订阅向外部测试机提供地面端入口。两个方向分别配置，共用设备 ID、现有 SkyCore 遥测连接和命令回执。网络、缓存和 Session 文件工作在独立线程执行。

## 出站转发配置

从主窗口“数据”菜单打开 Gateway 设置。填写接收服务器的根地址和设备 ID，再启用云端转发。局域网接收端可使用 HTTP，例如 `http://192.168.1.20:8765`，已有 HTTPS 服务也可使用。设备 ID 用于区分数据来源，由 1 到 128 个不含空白的可打印 ASCII 字符组成。网关无需账号或令牌。默认关闭云端转发与云端远程控制。

远程控制需在本机开启，并按命令白名单授权。默认仅勾选读取状态，启动 / 恢复记录、停止记录需分别勾选。命令还必须指定目标设备、唯一 ID 和有效期。断开 SkyCore 时不排队等待执行，过期命令不执行，命令 ID 与结果保留 48 小时以抑制重启后的重复执行（最多 4096 条，满额时停止接收新命令，不淘汰未到期记录）。ACK、超时和拒绝结果回传服务器。SkyCore ACK 的语义沿用现有协议，不能把传输成功等同于设备完成物理动作。

可选托盘运行支持关闭主窗口后继续转发；使用退出菜单会真正结束应用。退出应用后 Gateway 停止。界面测试模式暂停 Gateway，退出测试模式后恢复保存的配置。

Session 同步仅针对已经落到地面端且结束写入的 Session（v1 `session.json`，状态为 `complete` 或 `recovered`）。手动选择目录后分块上传并记录进度，重启或断网后续传；不会自动从 SkyCore 下载历史文件。请在 Session 完整复制或记录结束后提交同步，上传期间保持源文件可用且不再修改。修改连接设置后需先保存并重新打开对话框，才能提交同步到新目的地。

## 4.19 网页直连地面网关

本机联调只让 SkyCore 模拟设备输入。地面端运行 `VaporView.exe`，浏览器直接访问它提供的网页。地面端在同一端口提供网页、HTTP 接口和 WebSocket 订阅。

1. 在地面机开两个终端，从仓库根目录分别运行下面两个命令。`39100` 是本例的机地数传端口。

   ```powershell
   .\build\Release\VaporViewSkyCore.exe --fpga-simulate-data --telemetry-transport tcp --telemetry-host 127.0.0.1 --telemetry-tcp-port 39100
   .\build\Release\VaporView.exe --source remote --telemetry-transport tcp --telemetry-host 127.0.0.1 --telemetry-tcp-port 39100
   ```

2. 在地面端关闭“界面测试模式”，打开数传并连接设备。等首页开始更新 FPGA 数据，确认已有可用于启动记录的定标参数。
3. 地面端启动后默认监听 `127.0.0.1:8443`。同机测试可直接打开下一步的网页。需要修改监听地址或端口时，打开“数据 → Gateway 网关”修改并保存。手动关闭监听后，地面端会记住该选择。
4. 同机测试时，在浏览器打开 `http://127.0.0.1:8443/`。局域网测试时，将监听 IP 改为**地面机**在测试局域网的网卡 IP，例如 `192.168.1.20`，在地面机防火墙为测试网放行该 TCP 端口，然后在测试机打开 `http://192.168.1.20:8443/`。如果修改了端口，浏览器地址也使用新端口。旧验证台的“4.19 地面网关入站测试”也可填这个地址，点击“打开入站测试页”。
5. “地面网关”页打开后自动查询状态。核对设备标识，以及“机地数传”显示“已连接”、“SkyCore 状态”显示“已收到”。点“查询最新测量”可读当前值，点“连接 WS”后观察实时表的序号、通道、峰值和质量位持续更新。
6. 在“SkyCore 命令”依次点“读取状态”“启动／恢复记录”“停止记录”。每次等到“SkyCore 已应答”，记下命令 ID 和回执序号。启动时核对“记录中”和 Session 名，在记录期间采几条测量，再停止并确认状态回到“停止”。可用“断开”和“连接 WS”复测订阅恢复。

若命令提交返回 `403`，按响应中的错误类型检查。`forbidden-origin` 表示浏览器来源不匹配，请从地面网关首页操作。`forbidden` 表示接口不支持该命令编号。若网关可达但机地数传未连接，先检查地面端到 SkyCore 的 TCP 连接。原生峰值的单位为 `native`。需和机载文件逐点对账时，WebSocket 消息中的 `hostTimeUs`、`channel` 和 `cycleId` 对应 Session 的 `sensors/waveform_features.csv` 中的主机时间、通道和周期。

## 地面端入站 v1 接口

入站使用明文 HTTP 和 WebSocket。仅在同机、可信测试局域网或专用 VPN 内开启监听。监听启用后，能访问该地址的测试机就可以提交本节列出的三种命令。地面机需要 Qt 6.10 或更新版本及匹配的 HttpServer、WebSockets 模块。[Qt HTTP Server 的安全说明](https://doc.qt.io/qt-6.10/qthttpserver-security.html)

网页、查询、命令和订阅使用同一地址及端口。HTTP 接口返回 UTF-8 JSON，每条 WebSocket 文本消息包含一条 JSON 事件。浏览器从地面网关首页调用同源接口。命令提交及 WebSocket 升级若携带 `Origin` 请求头，网关检查它是否与请求的 HTTP 来源匹配。

| 方法与路径 | 返回内容 |
| --- | --- |
| `GET /` | 地面网关测试页，浏览器现场操作入口。 |
| `GET /api/v1/status` | SkyCore 链路状态，最近一次设备和录制状态（收到后可用）。 |
| `GET /api/v1/measurements/latest` | `deviceId`、`linkConnected` 和最近一次 `measurement`。启动或断线后尚无新测量时，`measurement` 为 `null`。 |
| `POST /api/v1/commands` | 首次受理受支持的命令时，返回 HTTP 202 和 `status: "accepted"`。 |
| `GET /api/v1/commands/<id>` | HTTP 200 返回命令当前状态或最终结果。未知 ID 返回 HTTP 404 和 `command-not-found`。 |
| `WS /api/v1/stream` | 连接成功先送状态快照，再送实时测量和状态。升级路径错误返回 404，来源不匹配返回 403，订阅满额返回 503。 |

### 入站控制

开启入站监听后，接口支持请求 SkyCore 状态（10）、启动或恢复记录（1）、停止记录（3）。应用使用地面 GUI 当前的定标准备流程启动记录。命令体只接受以下三个字段，设备标识取地面端配置。

```json
{
  "id": "example-status-request",
  "commandId": 10,
  "expiresAt": "2026-11-19T12:00:30.000Z"
}
```

`id` 使用 1 至 64 个英文字母、数字、下划线或连字符，`expiresAt` 为地面机时钟当前时间之后 5 分钟以内的 UTC ISO 时间。示例时间需替换为实际测试时间。重复提交相同 ID、命令和期限时，网关返回已有记录。待处理返回 202，完成返回 200。每次监听期间最多保留 4096 条，记录在有效期结束后保留约 24 小时。应用退出或重新启动监听会开始新的命令查询和去重记录。

| 提交失败的 HTTP 状态 | 响应中的 `error` | 原因 |
| --- | --- | --- |
| 400 | `invalid-command` | 命令体格式或字段无效。 |
| 400 | `expired-or-expiry-out-of-range` | 新命令的期限已过，或超过地面机时钟 5 分钟。 |
| 403 | `forbidden-origin` | 请求的 `Origin` 与网关来源不一致。 |
| 403 | `forbidden` | 命令编号有效，但入站接口不支持。 |
| 409 | `command-id-conflict` | 同一 ID 已用于不同命令或期限。 |
| 413 | `body-too-large` | 命令体超过 2048 字节。 |
| 503 | `command-capacity` | 命令记录达到容量上限。 |

结果含 `id`、`deviceId`、`skyCommandId`、`status`。终态带有 `completedAt`，SkyCore 回执还包含 `sequence`、`errorCode` 和 `result`，有对应值时附带 `correlationId` 和 `sessionId`。可能的终态包括 `acknowledged`、`rejected`、`timeout`、`disconnected`、`busy`、`expired`、`not-sent`、`device-busy` 和 `calibration-required`。取得启动记录回执后，再通过状态及实际 Session 核实录制结果。若遇到超时或连接中断，先查询实际状态，再决定是否执行新命令。

### 实时消息

| 字段 | 含义 |
| --- | --- |
| `kind` | `measurement` 或 `status`。 |
| `deviceId`、`streamId`、`sequence`、`ingressDropped` | 设备标识、监听实例标识、实例内事件序号、该实例入口累计丢帧数，均为字符串。初始状态快照含 `snapshot:true`，序号为当时的流进度。 |
| `frameSequence`、`timeUs` | 若事件源自 SkyCore 遥测，含原始帧序号和以十进制字符串表示的微秒帧时间。 |
| `linkConnected`、`telemetryAvailable` | 状态事件中的机地链路连通性和最近一次 SkyCore 状态的可用性。有状态时另含 `recordingState`（0 停止、1 记录、2 暂停）、`sessionName` 和 `devices`；单设备行给出协议设备 ID、状态、错误码和计数。 |
| `source`、`channel`、`value`、`unit`、`valid`、`qualityFlags` | 测量来源、协议通道号、峰值、单位、有效性和原始质量位。单位为 `native`，即原始处理量纲。 |

`fpga-cycle-feature` 使用 SkyCore 的 FPGA 全周期特征，另外含 `cycleId`、`hostTimeUs`、`validPoints`。其 `qualityFlags` 位掩码 1、2、4、8、16 对应非有限样本、无有效点、部分、溢出、连续性错误，存在有效点时仍可报告有质量标记的峰值。`waveform-feature` 的质量位为零且峰值有限时有效。无有效峰值时 `value` 为 `null`。两种来源均附带 `peakIndex`、`mean` 和 `rms`，非有限统计值为 `null`。

监听实例最多允许 16 个 WebSocket 客户端，单个客户端的待写预算为 256 KiB，超过预算会断开该客户端。HTTP 状态的 `slowSubscribersDisconnected` 以十进制字符串报告当前监听实例因此断开的客户端数。遥测入口拥有单独的有界队列，持续采集时可用 `ingressDropped` 检测入口丢帧。重连先看状态快照，再按 `streamId` 和 `sequence` 接收后续事件。需补查当前值时使用 HTTP 查询。

本机 `gateway_listener_test` 连接实际监听端口，验证 HTTP 查询、命令和 WebSocket 订阅。合同 A.19 现场记录还需来自约定平台、真实设备以及独立测试机。

### 用独立客户端核验

在测试机上将地址替换成地面机的实际监听地址。HTTP 查询和命令使用 Python 标准库，WebSocket 订阅需安装 `websocket-client`。`stream --count` 控制接收的消息条数，包含初始状态快照，省略时为 5 条。`result` 的位置参数取命令输出的 ID。[Python 客户端](../tools/gateway_client.py)

```powershell
$ground = "http://192.168.1.20:8443"
python tools/gateway_client.py $ground status
python tools/gateway_client.py $ground latest
python tools/gateway_client.py $ground stream --count 20
python tools/gateway_client.py $ground command request-status
python tools/gateway_client.py $ground command start-recording
python tools/gateway_client.py $ground command stop-recording
$commandId = "<某次命令输出的 ID>"
python tools/gateway_client.py $ground result $commandId
```

命令调用会分别输出受理记录和最终结果。验收时保留命令 ID、SkyCore 序号及 Session 名，按 `hostTimeUs`、`channel` 和 `cycleId` 到机载 Session 的周期特征 CSV 核对 WebSocket 峰值与质量标志。

### 自动复测浏览器链路

`gateway_browser_workflow_test` 启动独立的 SkyCore 模拟进程，在隔离配置中打开正式地面主窗口和网关，再通过 Playwright 驱动真实 Chromium 浏览器点击同一组按钮。测试用临时主机名模拟浏览器访问普通 HTTP 地址，核对命令回执和订阅重连，并将浏览器收到的 5 条测量逐条与 SkyCore 的 CSV 对账。测试结束后清理本次 Session。

在 MSVC Developer 环境构建 Release 后，将 `VAPORVIEW_TEST_GATEWAY_NODE` 设为能够加载 `playwright` 的 Node 可执行文件。使用本机已安装的 Chrome 或 Edge 时，把 `VAPORVIEW_TEST_CHROMIUM` 设为浏览器可执行文件的完整路径，否则使用 Playwright 安装的 Chromium。可将 `VAPORVIEW_TEST_BROWSER_SCREENSHOT` 设为临时 PNG 路径，留存复测完成时的页面截图。手工使用网页无需这些测试工具。

```powershell
ctest --test-dir build/Release -C Release -R '^gateway_browser_workflow_test$' -V
```

## 云端转发 v1 接口

仓库提供可直接运行的[本地网页验证台](../tools/gateway_demo/README.md)，包含协议参考服务端、实时请求面板和真实 Gateway 的一键联调。它使用模拟 SkyCore 验证遥测、命令结果、Session 上传及断网恢复，适合在甲方服务器确定前验证接口。

地面端使用 Qt Network 通过 HTTP 或 HTTPS 上传数据和轮询命令。以下是本仓库定义的云端 v1 对接协议，上线前应与甲方服务端确认并实现对应接口。

所有路径相对于配置的服务器根地址。请求使用 `X-VaporView-Device-Id` 标明设备，JSON 为 UTF-8。服务端按设备 ID 归集数据和分发命令，按消息 ID 去重。客户端不跟随重定向。命令正常约每 5 秒轮询一次；请求超时为 15 秒，失败逐步退避到最多 60 秒，命令轮询失败不阻止数据上传。响应最大 1 MiB。

| 方法与路径 | 用途 |
| --- | --- |
| `POST gateway/v1/telemetry` | 上传实时遥测及离线数据 |
| `GET gateway/v1/commands?deviceId=<id>` | 轮询待处理命令 |
| `POST gateway/v1/command-results` | 上传命令结果 |
| `POST gateway/v1/sessions` | 上传地面 Session 文件块及完成信息 |

### 遥测

以下请求使用示例 ID、时间与载荷。

```json
{
  "deviceId": "example-device",
  "items": [{
    "id": "example-message-id",
    "kind": "telemetry",
    "receivedAt": "2026-11-14T00:00:00.000Z",
    "frames": [{
      "type": 1,
      "flags": 0,
      "sequence": 1,
      "timeUs": "1794614400000000",
      "payloadBase64": ""
    }]
  }]
}
```

`type`、`flags`、`sequence`、`timeUs` 与 `payloadBase64` 对应现有 `TelemetryFrame`。载荷采用已有遥测协议，无需为 Gateway 在接收侧重复解析。`timeUs` 用十进制字符串保存微秒整数精度，`receivedAt` 是地面端组批时间。转发仅包含基础遥测、状态、波形 / 特征、温控、FPGA 数据、内部环境和心跳，不转发配置、日志、RTCM 或任意未知帧。

只有 HTTP 成功且响应 `{"acceptedIds":["example-message-id"]}` 明确确认的项才从缓存删除。丢失响应会导致重传，服务器应按消息 ID 去重，不应假设只投递一次。

### 命令

轮询返回：

```json
{
  "commands": [{
    "id": "example-command-id",
    "deviceId": "example-device",
    "commandId": 10,
    "payloadBase64": "",
    "expiresAt": "2026-11-14T00:01:00.000Z"
  }]
}
```

`commandId` 采用 `TelemetryTypes.h` 的整数 `CommandId`，载荷编码沿用对应命令的二进制格式。`id` 最长 128 字符，载荷最多 64 KiB。设备和白名单必须匹配，`expiresAt` 必须为带 `Z` 的 UTC ISO 时间，且距当前时间不超过 24 小时，因此地面电脑需要正确系统时间。每次最多处理 32 条，最多允许 32 条在途命令，现有重试层同样遵守命令有效期。

命令结果同样使用 `{deviceId,items:[...]}` 封装并通过 `acceptedIds` 确认。项的 `id` 是 `result-` 加云端命令 ID 的 SHA256，`kind` 为 `command-result`，`completedAt` 为 UTC 时间；`commandId` 为原云端命令的字符串 ID。`skyCommandId` 为已有天空端协议中的数值命令，`sequence` 为天空端序列号，ACK 结果包含 `errorCode`、`result`。`status` 包括 `acknowledged`、`rejected`、`timeout`、`forbidden`、`wrong-device`、`expired`、`invalid-payload`、`disconnected`、`busy`、`not-sent`、`configuration-changed` 和 `unknown-after-restart`。重复下发已完成命令会重发原结果，不再次执行。

命令在发送前记录 ID；遇到崩溃或丢失 ACK 时，不能通过重新下发同一 ID 判断动作是否发生，应结合设备状态人工或业务层核实。重启时尚未得到结果的命令报告 `unknown-after-restart`；去重文件损坏时停止执行远程命令，避免遗忘已执行 ID。48 小时后回收记录，服务器必须始终生成新 ID，不能给旧命令换一个新的有效期后复用 ID。

### Session

每个文件块请求携带 `deviceId`、`sessionId`、`relativePath`、`offset`、`totalBytes`、`dataBase64`、`chunkSha256` 与 `complete:false`。`offset`、`totalBytes` 为十进制字符串，块大小最大 256 KiB。服务器只在数据已可靠保存后返回 `{"accepted":true,"nextOffset":"<下一偏移>"}`，客户端随后保存进度。服务器必须按 Session、相对路径和偏移幂等写入，校验块哈希，拒绝路径穿越与不一致的偏移。

全部文件上传后发送 `{deviceId,sessionId,complete:true,files:[{path,size,modified},...]}`，其中 `size` 和 `modified` 为十进制字符串（修改时间为 Unix 毫秒）。服务器检查全部文件后返回 `{"accepted":true}`。应清理长期未完成的服务端分块，不能将未收到完成请求的 Session 标记为完整。

## 缓存与运行边界

实时数据缓存默认上限 64 MiB、保留 24 小时；容量不足时先淘汰低优先级旧数据，命令结果优先。内存入口上限 4 MiB，拥塞或单帧超过 256 KiB 预算时丢弃该转发帧并报告状态，不阻塞本地接收。超过保留期限的数据会被清理，因此离线缓存并不承诺无限期无损保存。

Session 通过持久任务和分块读取处理，不把整套文件载入内存或复制进实时队列；最多 16 个待同步任务、每 Session 4096 个文件，任务元数据总量最多 1 MiB，任务期限采用配置的缓存保留时间。修改或缺失的源文件会中止该任务。

缓存按服务器地址和设备 ID 分开保存，切换配置不会把旧设备数据上传到新服务器。默认位置是 Qt 的 `AppLocalDataLocation/gateway`。停用或退出时将剩余入口数据落盘；应用再次启用同一目的地后继续等待云端确认。

缓存、命令去重记录及 Session 同步状态属于本地运行数据，不能提交到 Git。自动化验证使用临时缓存、本机模拟服务器和隔离配置，不连接真实设备或甲方服务器。
