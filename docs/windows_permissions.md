# Windows 安装权限模型

VaporView 的 Windows 日常程序以当前调用者的普通权限运行。以下自有可执行文件的 manifest 均显式使用 `asInvoker`，不要求管理员权限，也不会在启动后主动重新提权：

```text
VaporView.exe
VaporViewSky.exe
VaporViewSkyCore.exe
VaporViewSkyTui.exe
VaporViewUpdateRelauncher.exe
VaporViewPermissionTool.exe
```

安装器和 `VaporViewMaintenanceTool.exe` 仍可能在安装、修复、更新或替换维护工具自身时请求 UAC。这只用于写入安装目录、替换文件和配置 ACL；安装完成后启动的主程序应保持非提升状态。用户不应在文件属性或兼容性设置中勾选“以管理员身份运行此程序”。

## 安装目录

默认安装目录仍是：

```text
C:\VaporView
```

也支持用户在安装器中选择其他普通目录，例如：

```text
D:\Tools\VaporView
C:\My Apps\VaporView
C:\包含中文的目录\VaporView
```

当前项目有意不划分只读程序区和可写数据区，也不把运行数据迁移到 `%LOCALAPPDATA%`、`%APPDATA%` 或 `%PROGRAMDATA%`。安装目录根部继续包含 EXE、DLL、Qt 插件、`resources`、`drivers`、`data`、日志、配置、地图数据、缓存、记录目录、维护工具和更新辅助程序。

这意味着同一 Windows 用户权限下运行的其他程序也可以修改 VaporView 的 EXE、DLL 和资源文件。这是当前交付模型接受的取舍；项目不在应用层增加文件防篡改系统。

## ACL 规则

安装或更新完成后，`VaporViewPermissionTool.exe` 会为发起安装的交互会话用户 SID 添加显式、可继承的 Full Control ACE：

```text
目标用户：(OI)(CI)F
```

规则如下：

- 使用 SID 写 ACL，不依赖本地化用户名。
- 从当前 Windows Session 的 `explorer.exe` 获取交互用户 SID，避免把权限授予 UAC 凭据输入的备用管理员账户。
- 保留 `SYSTEM` 和内置 `Administrators` 的 Full Control。
- 不向 `Everyone`、`Users` 或 `Authenticated Users` 授予 Full Control。
- 对安装目录根、已有文件和子目录递归生效，后续新增文件和目录通过继承获得权限。
- 重复执行是幂等的，不会不断增加重复 ACE。
- 目录树中的 reparse point 默认跳过，不跟随到安装目录之外。

权限工具在修改 ACL 前会拒绝危险路径，包括空路径、相对路径、盘符根目录、Windows 目录、系统目录、Program Files 根目录、用户配置文件根目录以及目标目录本身是 reparse point 的路径，避免变量为空或路径规范化错误导致递归修改系统范围。

## 只读属性

ACL Full Control 不等于清除 `FILE_ATTRIBUTE_READONLY`。安装和更新完成后，权限工具会递归清除安装目录内普通文件的只读属性，但不会把目录的资源管理器“只读”复选框当作权限依据，也不会无故修改隐藏、系统、压缩、稀疏等其他属性。

IFW 打包脚本在生成 stage 时也会清除 staged 普通文件的只读属性，避免把意外只读状态带入安装包或更新仓库。

## 更新后启动

更新完成后，`VaporViewUpdateRelauncher.exe` 会等待维护工具退出，然后从同一交互 Session 的 `explorer.exe` 获取非提升用户令牌，并以 `@TargetDir@` 作为工作目录启动 `VaporView.exe`。它会验证目标 EXE 名称为 `VaporView.exe`，且与 `VaporViewMaintenanceTool.exe` 位于同一个安装根目录。

如果无法可靠获得非提升令牌，重启器会返回失败；IFW 脚本不会回退到直接执行 `VaporView.exe`，以免主程序继承维护工具的管理员令牌。用户可稍后从桌面或开始菜单快捷方式手动启动。

## 检查和修复

检查进程是否提升：

```powershell
Get-Process VaporView | Select-Object Id, Path
```

如需查看令牌提升类型，可使用 Process Explorer，或运行自动化测试中的 `update_relauncher_elevation_test` 探针。

检查安装目录 ACL：

```powershell
icacls C:\VaporView
```

输出中目标交互用户应具有 `(OI)(CI)(F)` 或等效继承 Full Control；`SYSTEM` 和 `Administrators` 应保留 Full Control；不应看到 `Everyone`、`Users` 或 `Authenticated Users` 拥有 Full Control。

运行权限诊断：

```powershell
C:\VaporView\VaporViewPermissionTool.exe verify --target-dir "C:\VaporView"
```

修复旧版本安装目录：

```powershell
C:\VaporView\VaporViewPermissionTool.exe apply --target-dir "C:\VaporView"
C:\VaporView\VaporViewPermissionTool.exe verify --target-dir "C:\VaporView"
```

如果旧版本目录缺少权限工具，请先安装或更新到包含 `VaporViewPermissionTool.exe` 的版本，再执行上述命令。
