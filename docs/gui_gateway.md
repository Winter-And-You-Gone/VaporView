# GUI 内置 Gateway

Gateway 复用 `GroundTelemetryService` 已接收并通过帧校验的数据，在地面端分流到服务器。它不创建 SkyCore 连接，不改变本地显示或天空端订阅；远程命令也经现有服务发送。网络请求、缓存写入和 Session 文件读取在独立工作线程执行，GUI 侧入口有容量上限。

## 使用

从主窗口“数据”菜单打开 Gateway 设置，填写服务器根地址、设备 ID 和服务器颁发的 Bearer token，再启用转发。设备 ID 最长 128 字符，设备 ID 和 token 使用不含空白的可打印 ASCII 字符。生产地址必须使用 HTTPS，使用系统信任链验证服务器证书；HTTP 仅允许本机回环地址用于开发测试。默认关闭转发与远程控制。

远程控制需在本机开启，并按命令白名单授权。默认仅勾选读取状态，启动 / 恢复记录、停止记录需分别勾选。命令还必须指定目标设备、唯一 ID 和有效期。断开 SkyCore 时不排队等待执行，过期命令不执行，命令 ID 与结果保留 48 小时以抑制重启后的重复执行（最多 4096 条，满额时停止接收新命令，不淘汰未到期记录）。ACK、超时和拒绝结果回传服务器。SkyCore ACK 的语义沿用现有协议，不能把传输成功等同于设备完成物理动作。

可选托盘运行支持关闭主窗口后继续转发；使用退出菜单会真正结束应用。退出应用后 Gateway 停止。界面测试模式暂停 Gateway，退出测试模式后恢复保存的配置。

Session 同步仅针对已经落到地面端且结束写入的 Session（v1 `session.json`，状态为 `complete` 或 `recovered`）。手动选择目录后分块上传并记录进度，重启或断网后续传；不会自动从 SkyCore 下载历史文件。请在 Session 完整复制或记录结束后提交同步，上传期间保持源文件可用且不再修改。修改连接设置后需先保存并重新打开对话框，才能提交同步到新目的地。

## 服务器 v1 接口

仓库提供可直接运行的[本地网页验证台](../tools/gateway_demo/README.md)，包含协议参考服务端、实时请求面板和真实 Gateway 的一键联调。它使用模拟 SkyCore 验证遥测、命令结果、Session 上传及断网恢复，适合在甲方服务器确定前验证接口。

当前使用 Qt Network 提供 HTTPS REST 上传和异步命令轮询，不依赖 Qt WebSockets。以下是本仓库定义的 v1 对接协议；上线前应与甲方服务端确认并实现对应接口。

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
