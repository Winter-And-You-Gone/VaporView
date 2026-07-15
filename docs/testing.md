# VaporView 测试流程

所有本地构建和测试统一使用 `build/Release`。Windows 下先进入 VS/MSVC Developer 环境；仓库脚本会自动完成这一步。

## 常用入口

Windows：

```powershell
# 只构建桌面程序，不编译全部测试目标
powershell -ExecutionPolicy Bypass -File .\scripts\build-windows-msvc2022.ps1 -Action BuildApp

# 构建全部目标并运行 fast 标签测试
powershell -ExecutionPolicy Bypass -File .\scripts\build-windows-msvc2022.ps1 -Action TestFast

# 构建全部目标并运行完整 CTest
powershell -ExecutionPolicy Bypass -File .\scripts\build-windows-msvc2022.ps1 -Action Test
```

Linux ARM64：

```bash
./scripts/build-linux-arm64.sh app
./scripts/build-linux-arm64.sh test-fast
./scripts/build-linux-arm64.sh test
```

## 按改动面运行

小改动优先构建直接相关的目标，再用测试名或标签收敛 CTest。以下命令需要在已经初始化的 MSVC Developer 环境中执行。

主窗口布局、导航或设备配置：

```powershell
cmake --build build/Release --config Release --target main_window_layout_test
ctest --test-dir build/Release -C Release -R "^main_window_layout_test$" --output-on-failure
```

Session 数据查看、轨迹查看或主题：

```powershell
cmake --build build/Release --config Release --target session_viewer_theme_test
ctest --test-dir build/Release -C Release -R "^session_viewer_theme_test$" --output-on-failure
```

协议、Telemetry 或 RD105：

```powershell
cmake --build build/Release --config Release --target temperature_controller_protocol_test telemetry_codec_test telemetry_tcp_link_test
ctest --test-dir build/Release -C Release -L protocol --output-on-failure
```

## 标签

| 标签 | 用途 |
|---|---|
| `fast` | PR 和常规回归使用的快速集合 |
| `unit` | 核心算法、编解码和小型组件测试 |
| `integration` | 跨组件或真实窗口集成测试 |
| `ui` | Qt Widgets 窗口、布局、主题和启动测试 |
| `slow` | 大型 GUI 或真实数据测试 |
| `protocol` | Telemetry、TCP 和温控协议测试 |
| `3d` | osgEarth/OSG 三维地图测试 |
| `real-data` | 依赖仓库本地西湖三维数据的测试 |

直接运行标签：

```powershell
ctest --test-dir build/Release -C Release -L fast --output-on-failure
ctest --test-dir build/Release -C Release -L 3d --output-on-failure
ctest --test-dir build/Release -C Release -L real-data --output-on-failure
```

每个测试在 CMake 中设置了 30 至 180 秒的超时，避免 GUI 或渲染生命周期异常无限阻塞测试流程。

## osgEarth 验证顺序

三维地图相关修改按以下顺序验证：

1. 配置 `VAPORVIEW_ENABLE_OSGEARTH=OFF`，确认默认桌面程序可以构建。
2. 配置 `VAPORVIEW_ENABLE_OSGEARTH=ON`，构建相关 3D/UI 测试目标。
3. 先跑 `3d` 或更小的测试集合；大范围改动再跑完整 CTest。
4. 最终让 `build/Release` 保持 ON，便于直接启动程序检查三维效果。

真实西湖数据未安装时，三个 `real-data` 测试会以 CTest 的 Skipped 状态结束。`map_data_script_test` 找不到 Python 时也使用相同的 skip 语义，不会伪装成 Passed。

## 视觉验收

布局测试只能验证几何、属性、stylesheet 和交互契约。Popup 阴影、透明合成、黑边和真实字体渲染仍需在 Windows 桌面上启动 `VaporView.exe` 并用真实截图确认。
