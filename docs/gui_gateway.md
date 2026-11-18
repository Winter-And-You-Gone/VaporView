# GUI 内置 Gateway

Gateway 复用 `GroundTelemetryService` 已接收并通过帧校验的数据。地面端可以主动向云端转发，也可以开放 HTTPS 查询、授权控制与 WSS 订阅给外部测试机。两个方向各自配置和授权，共用设备 ID、现有 SkyCore 遥测连接和命令回执。网络、缓存和 Session 文件工作在独立线程执行。

## 使用

从主窗口“数据”菜单打开 Gateway 设置，填写服务器根地址、设备 ID 和服务器颁发的 Bearer token，再启用转发。设备 ID 最长 128 字符，设备 ID 和 token 使用不含空白的可打印 ASCII 字符。生产地址必须使用 HTTPS，使用系统信任链验证服务器证书；HTTP 仅允许本机回环地址用于开发测试。默认关闭转发与远程控制。

远程控制需在本机开启，并按命令白名单授权。默认仅勾选读取状态，启动 / 恢复记录、停止记录需分别勾选。命令还必须指定目标设备、唯一 ID 和有效期。断开 SkyCore 时不排队等待执行，过期命令不执行，命令 ID 与结果保留 48 小时以抑制重启后的重复执行（最多 4096 条，满额时停止接收新命令，不淘汰未到期记录）。ACK、超时和拒绝结果回传服务器。SkyCore ACK 的语义沿用现有协议，不能把传输成功等同于设备完成物理动作。

可选托盘运行支持关闭主窗口后继续转发；使用退出菜单会真正结束应用。退出应用后 Gateway 停止。界面测试模式暂停 Gateway，退出测试模式后恢复保存的配置。

Session 同步仅针对已经落到地面端且结束写入的 Session（v1 `session.json`，状态为 `complete` 或 `recovered`）。手动选择目录后分块上传并记录进度，重启或断网后续传；不会自动从 SkyCore 下载历史文件。请在 Session 完整复制或记录结束后提交同步，上传期间保持源文件可用且不再修改。修改连接设置后需先保存并重新打开对话框，才能提交同步到新目的地。

## 地面端入站 v1 接口

在“数据 → Gateway 网关”中填写设备标识，再启用“入站 HTTPS / WSS”。监听 IP 默认是 `127.0.0.1`。独立测试机访问时填写地面机在测试网中的网卡 IP，并按部署网络放行该 TCP 端口。Qt HTTP Server 面向可信网络，应通过测试局域网或专用 VPN 提供访问。地面机需要 Qt 6.10 或更新版本及匹配的 HttpServer、WebSockets 模块与可用的 Qt TLS 后端。[Qt HTTP Server 的安全说明](https://doc.qt.io/qt-6.10/qthttpserver-security.html)

选择 PEM 证书链和与之匹配的未加密 RSA 或 EC PEM 私钥。证书链先放服务器证书，再放中间证书。服务器证书的 SAN 应与客户端使用的 DNS 名或 IP 匹配，客户端须信任签发 CA。入站令牌应单独生成，使用 32 至 256 个可见 ASCII 字符，例如通过 `openssl rand -hex 32` 生成。保存后重新打开设置页查看监听状态，证书换新后停用再启用以重新加载。私钥的读取权限应限制到运行程序的账号，令牌随本地应用配置保存。

所有接口使用同一个 TLS 端口和 `Authorization: Bearer <token>`。HTTPS 返回 UTF-8 JSON。WSS 在升级阶段校验同一令牌，每个文本消息包含一条 JSON 事件。

| 方法与路径 | 返回内容 |
| --- | --- |
| `GET /api/v1/status` | SkyCore 链路状态，最近一次设备和录制状态（收到后可用）。 |
| `GET /api/v1/measurements/latest` | `deviceId`、`linkConnected` 和最近一次 `measurement`。启动或断线后尚无新测量时，`measurement` 为 `null`。 |
| `POST /api/v1/commands` | 受理白名单命令，返回 HTTP 202 和 `status: "accepted"`。 |
| `GET /api/v1/commands/<id>` | 查询命令受理或最终结果。未知 ID 返回 404。 |
| `WSS /api/v1/stream` | 连接成功先送状态快照，再送实时测量和状态。 |

### 入站控制

在入站区域单独开启控制权限，并分别勾选允许的命令。首版支持请求 SkyCore 状态（10）、启动或恢复记录（1）、停止记录（3）。应用使用地面 GUI 当前的定标准备流程启动记录。命令体只接受以下三个字段，设备标识取地面端配置。

```json
{
  "id": "example-status-request",
  "commandId": 10,
  "expiresAt": "2026-11-19T12:00:30.000Z"
}
```

`id` 使用 1 至 64 个英文字母、数字、下划线或连字符，`expiresAt` 为地面机时钟当前时间之后 5 分钟以内的 UTC ISO 时间。示例时间需替换为实际测试时间。未认证返回 401，控制关闭或命令未授权返回 403，无效或过期请求返回 400，同一 ID 与不同命令或期限组合返回 409。相同 ID、命令及期限可重试查询原结果，待处理返回 202，完成返回 200。每次监听期间最多保留 4096 条，记录在有效期结束后保留约 24 小时。应用退出或重新启动监听会开始新的命令查询和去重记录。

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

监听实例最多允许 16 个 WSS 客户端，单个客户端的待写预算为 256 KiB，统计 Qt 中的 WSS 明文和 TLS 密文待写队列，超过预算会断开该客户端。HTTPS 状态的 `slowSubscribersDisconnected` 以十进制字符串报告当前监听实例因此断开的客户端数。遥测入口拥有单独的有界队列，持续采集时可用 `ingressDropped` 检测入口丢帧。重连先看状态快照，再按 `streamId` 和 `sequence` 接收后续事件。需补查当前值时使用 HTTPS 查询。

本机 `gateway_listener_test` 使用 `openssl` 生成临时证书，按正常证书验证连接真实监听端口，并通过模拟 SkyCore 验证协议。若 `openssl` 未在 PATH 中，可将 `VAPORVIEW_TEST_OPENSSL` 设为程序路径。合同 A.19 现场记录还需来自约定平台、真实设备以及独立测试机。

### 用独立客户端核验

仓库提供 [Python 客户端](../tools/gateway_client.py)。在独立测试机上设置环境变量 `VAPORVIEW_GATEWAY_TOKEN`，使用地面端证书匹配的主机名和签发 CA。HTTPS 使用 Python 标准库，WSS 另需 `websocket-client`。在地面端启用控制权限并勾选相应命令后，可以执行录制启停。将示例地址、CA 路径和查询 ID 换成现场值，`$commandId` 取命令输出的 UUID。

```powershell
$ground = "https://ground.example:8443"
$ca = "C:/field/gateway-ca.pem"
python tools/gateway_client.py $ground --ca $ca status
python tools/gateway_client.py $ground --ca $ca latest
python tools/gateway_client.py $ground --ca $ca stream --count 20
python tools/gateway_client.py $ground --ca $ca command start-recording
python tools/gateway_client.py $ground --ca $ca command stop-recording
python tools/gateway_client.py $ground --ca $ca result $commandId
```

命令调用会分别输出受理记录和最终结果。验收时保留命令 ID、SkyCore 序号及 Session 名，按 `hostTimeUs`、`channel` 和 `cycleId` 到机载 Session 的周期特征 CSV 核对 WSS 峰值与质量标志。

## 云端转发 v1 接口

仓库提供可直接运行的[本地网页验证台](../tools/gateway_demo/README.md)，包含协议参考服务端、实时请求面板和真实 Gateway 的一键联调。它使用模拟 SkyCore 验证遥测、命令结果、Session 上传及断网恢复，适合在甲方服务器确定前验证接口。

云端链路使用 Qt Network 提供 HTTPS REST 上传和异步命令轮询。以下是本仓库定义的云端 v1 对接协议，上线前应与甲方服务端确认并实现对应接口。

所有路径相对于配置的服务器根地址。请求使用 `Authorization: Bearer <token>` 和 `X-VaporView-Device-Id`，JSON 为 UTF-8。服务端必须将 token 与设备权限绑定，校验设备 ID，并对上传消息 ID 幂等处理。客户端不跟随重定向。命令正常约每 5 秒轮询一次；请求超时为 15 秒，失败逐步退避到最多 60 秒，命令轮询失败不阻止数据上传。响应最大 1 MiB。

| 方法与路径 | 用途 |
| --- | --- |
| `POST gateway/v1/telemetry` | 上传实时遥测及离线数据 |
| `GET gateway/v1/commands?deviceId=<id>` | 轮询待处理命令 |
| `POST gateway/v1/command-results` | 上传命令结果 |
| `POST gateway/v1/sessions` | 上传地面 Session 文件块及完成信息 |

### 遥测

请求示例中的 ID、时间与载荷均为虚构协议样例，不含连接凭据。

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

缓存、命令去重记录及 Session 同步状态属于本地运行数据，不能提交到 Git。Token 不应出现在日志、截图或测试样例中。自动化验证使用临时缓存、本机模拟服务器和隔离配置，不连接真实设备或甲方服务器。
