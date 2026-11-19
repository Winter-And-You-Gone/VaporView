# Gateway 本地网页验证台

网页展示演示服务器实际收到的 HTTP 请求，用来对接和验证 [Gateway v1 协议](../../docs/gui_gateway.md)。它包含本地参考服务端、无第三方依赖的网页，以及使用真实 C++ `GatewayService` 的一键验证入口。SkyCore 和设备在该入口中由本机 TCP 模拟器替代，不连接现场设备。

## 测试大纲 4.19 的网页入口

浏览器主动查状态、发命令、订阅实时测量时，直接打开地面端的 HTTPS 根地址。也可在本地验证台的“4.19 地面网关入站测试”填入该地址，点击“打开入站测试页”。入站测试页由正在运行的 VaporView 提供，只需在该页输入地面端的入站令牌。[只模拟 SkyCore 的完整步骤](../../docs/gui_gateway.md#419-网页直连地面网关)

## 启动

需要 Python 3.10+，只使用 Python 标准库。先在 MSVC Developer 环境中构建验证客户端：

```powershell
cmake --build build/Release --config Release --parallel 6 --target gateway_service_test
```

从仓库根目录运行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/start-gateway-demo.ps1
```

未安装 Python launcher 时，可通过 `-PythonExe <python.exe路径>` 或环境变量 `VAPORVIEW_PYTHON_EXE` 指定解释器。`-NoBrowser` 阻止自动打开浏览器，`-Port` 修改端口。

也可以直接运行：

```text
python tools/gateway_demo/server.py --port 8765 --open-browser
```

默认网页位于 `http://127.0.0.1:8765/`。服务仅监听本机回环，不需安装 Web 框架或 npm 依赖，按 Ctrl+C 退出。运行数据默认置于临时目录；需要保留服务端数据时使用 `--data-dir build/Release/gateway-demo-data`，这些生成物不能提交到 Git。

## 验证步骤

1. 打开网页，点击“运行验证”。服务端启动 `build/Release/gateway_service_test.exe --web-demo`，通过环境变量传递随机令牌，运行时生成临时配置、缓存和 Session。
2. 本机 SkyCore 模拟器持续发送已有遥测协议帧；真实 `GroundTelemetryService` 解析，同一份数据供本地处理与 Gateway 分流。网页显示实际接收批次和帧。
3. 服务端下发读取状态命令，Gateway 经同一条 SkyCore 连接发送，模拟器返回 ACK，网页显示最终结果。
4. 服务端短暂拒绝 Gateway 请求并恢复，检查恢复后接收到中断期间的数据。网页请求记录保留 503 和随后成功响应。
5. 完整上传临时 Session 的文件块和完成清单，服务端校验偏移、SHA256 与文件大小。单次分块确认后才允许客户端继续。
6. 验证结束显示逐项结果；只有实际检查全部满足才显示通过。失败时显示原因，可排查后再次运行。

“运行验证”模拟设备的命令权限仅在临时客户端中开启，不会修改桌面应用保存的 Gateway 配置。网页数据反映当前服务端收到的内容，不能将这次模拟设备 ACK 当作真实硬件动作完成。

## 对接桌面 GUI

### 只模拟 SkyCore，在网页测试正式地面端与网关

在地面电脑上，从仓库根目录分别执行以下三个命令。每个命令使用独立终端，测试期间保持运行。

```powershell
.\build\Release\VaporViewSkyCore.exe --fpga-simulate-data --telemetry-transport tcp --telemetry-host 127.0.0.1 --telemetry-tcp-port 39100
.\build\Release\VaporView.exe
python tools/gateway_demo/server.py --open-browser
```

1. 地面端关闭“界面测试模式”，选择天空-地面接收（远程），将数传 TCP 连接到 `127.0.0.1:39100`，再连接设备。先在地面端确认模拟测量持续更新。网关会复用这条数传连接。
2. 浏览器中保持“模拟互联网 · 在线”。直接到页底“连接你的 VaporView GUI”复制服务器地址、设备标识和访问令牌。上方“运行真实 Gateway 验证”会另外启动一套测试客户端及模拟器，本次跳过该按钮。
3. 在地面端“数据 → Gateway 网关…”填入上述三项，勾选“启用云端转发”、“允许云端下发命令”和“读取设备状态”，保存。
4. 回到网页看“实时遥测”的新帧序号和采集时间，点击“远程命令 → 读取状态”。用本次生成的命令 ID 对应最终的 `acknowledged` 回执。帧持续更新能确认上行，状态回执能确认命令到达 SkyCore 并返回。只测状态时无需授权录制启停。

这组操作测试地面网关向网页后台转发遥测、轮询命令并回传结果。A.19 入站网页的入口见页首。出站区域的“服务器地址”供地面端复制。

网页连接信息使用本机回环 HTTP。实际互联网部署应使用 HTTPS 并采用甲方的鉴权与设备管理。

网页可下发状态查询、启动 / 恢复记录与停止记录，后两项仍需 GUI 本机白名单授权。命令有效期、去重、断线结果均由真实 Gateway 检查。不要同时运行自动验证客户端与配置为同一设备 ID 的现场 GUI，以免两者竞争处理同一命令。

此服务端是本机协议参考实现。它没有多租户、互联网账号系统或公网部署配置，不能作为公网服务直接使用。日志不输出令牌，网页令牌默认隐藏；截图、数据库、下载文件、验证报告和运行时凭据均属于本地生成物。

## 自动验证

```text
python tests/gateway_demo_server_test.py
```

后端测试使用随机凭据和临时数据库，覆盖鉴权、上传确认、重复消息、命令轮询、Session 分块、错误路径与故障恢复。C++ 客户端的常规回归测试仍使用 `ctest --test-dir build/Release -R "^gateway_" --output-on-failure`。
