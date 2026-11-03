param(
    [Parameter(Mandatory = $true)][string]$IfwBin,
    [Parameter(Mandatory = $true)][string]$PackageDirectory,
    [Parameter(Mandatory = $true)][string]$Configuration,
    [Parameter(Mandatory = $true)][string]$CalibrationProbe,
    [Parameter(Mandatory = $true)][string]$WorkDirectory
)
$ErrorActionPreference = 'Stop'
$work = [IO.Path]::GetFullPath($WorkDirectory)
if (Test-Path -LiteralPath $work) { throw "Lifecycle workspace must be new: $work" }
New-Item -ItemType Directory -Path $work | Out-Null
$target = Join-Path $work '中文安装目录'
$packages = Join-Path $work 'packages'
$identity = 'VaporViewQuality-' + [Guid]::NewGuid().ToString('N')

function Invoke-Stage([string]$Name, [string]$Program, [string[]]$Arguments, [int[]]$AcceptedExitCodes = @(0)) {
    Write-Host "IFW lifecycle: $Name"
    $log = Join-Path $work ($Name + '.log')
    & $Program @Arguments *> $log
    $code = $LASTEXITCODE
    if ($code -notin $AcceptedExitCodes) { throw "$Name failed ($code); see $log" }
    Write-Host "$Name exit=$code"
    if (-not (Test-Path -LiteralPath $log)) { throw "$Name did not create its log" }
}
function Verify-UserData([string]$Name) {
    Invoke-Stage $Name $CalibrationProbe @('--verify-deployment-fixture', $target)
}
function Wait-Uninstall {
    # IFW removes its own executable through a detached cscript after purge exits.
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while (Test-Path -LiteralPath $maintenance) {
        if ($timer.Elapsed.TotalSeconds -gt 30) { throw 'IFW maintenance executable did not finish removing itself' }
        Start-Sleep -Milliseconds 100
    }
    $normalizedTarget = $target.Replace('\','/')
    $removers = Get-CimInstance Win32_Process -Filter "Name = 'cscript.exe'" |
        Where-Object { $_.CommandLine -and $_.CommandLine.Replace('\','/').Contains($normalizedTarget) }
    foreach ($remover in $removers) {
        $process = Get-Process -Id $remover.ProcessId -ErrorAction SilentlyContinue
        if ($process -and -not $process.WaitForExit(30000)) { throw 'IFW detached removal did not finish' }
    }
}
function Get-PayloadHash([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash($stream)) }
    finally { $sha.Dispose(); $stream.Dispose() }
}
function Update-MaintenanceResource([string]$Name) {
    # Essential self-update must keep the isolated product registration too.
    Push-Location $work
    try { Invoke-Stage $Name (Join-Path $IfwBin 'binarycreator.exe') @('-c',$configPath,'-p',$packages,'-rcc') }
    finally { Pop-Location }
    Copy-Item -LiteralPath (Join-Path $work 'update.rcc') -Destination (Join-Path $packages 'com.vaporview.maintenancetool/data/update.rcc') -Force
}
function Verify-Payload {
    foreach ($name in @('VaporView.exe','VaporViewSky.exe','VaporViewSkyCore.exe','VaporViewSkyTui.exe','VaporViewUpdateRelauncher.exe','VaporViewPermissionTool.exe')) {
        $source = Join-Path $PackageDirectory "com.vaporview.core/data/$name"
        $installed = Join-Path $target $name
        if ((Get-PayloadHash $source) -ne (Get-PayloadHash $installed)) { throw "Installed payload SHA mismatch: $name" }
    }
    Invoke-Stage 'verify-permissions' (Join-Path $target 'VaporViewPermissionTool.exe') @('verify','--target-dir',$target)
}

# Use the real staged payload and component operations. Isolate the Windows
# product registration and shortcuts, and suppress update-time GUI relaunch.
# Calibration directory creation, ACL operations and undo behavior are unchanged.
Copy-Item -LiteralPath $PackageDirectory -Destination $packages -Recurse
[xml]$config = [IO.File]::ReadAllText($Configuration)
if ($config.Installer.RemoveTargetDir -ne 'false') { throw 'Lifecycle test requires the production non-recursive removal policy' }
$config.Installer.Name = $identity
$config.Installer.SelectSingleNode('TargetDir').InnerText = [string]$target
if ($config.Installer.RemoteRepositories) { [void]$config.Installer.RemoveChild($config.Installer.RemoteRepositories) }
$configPath = Join-Path $work 'config.xml'
$config.Save($configPath)
$configurationRoot = Split-Path -Parent $Configuration
foreach ($file in @('control.qs','installer.qss','VaporViewInstallerIcon.ico','VaporViewInstallerIcon.png','ifw_zh_CN.qm','qt_zh_CN.qm')) {
    $path = Join-Path $configurationRoot $file
    if (Test-Path -LiteralPath $path) { Copy-Item -LiteralPath $path -Destination $work }
}
$component = Join-Path $packages 'com.vaporview.core/meta/installscript.qs'
$script = [IO.File]::ReadAllText($component)
$script = $script.Replace('@StartMenuDir@','@TargetDir@/quality-shortcuts/start-menu').Replace('@DesktopDir@','@TargetDir@/quality-shortcuts/desktop')
$script = $script.Replace('component.createOperations();', 'component.createOperations(); component.addOperation("Mkdir", "@TargetDir@/quality-shortcuts/start-menu"); component.addOperation("Mkdir", "@TargetDir@/quality-shortcuts/desktop");')
$script += "`nComponent.prototype.launchVaporViewAfterUpdate = function() { return true; };`n"
[IO.File]::WriteAllText($component,$script,[Text.UTF8Encoding]::new($false))
$metadata = Join-Path $packages 'com.vaporview.core/meta/package.xml'
[xml]$package = [IO.File]::ReadAllText($metadata)
$baseVersion = [string]$package.Package.Version
Write-Host "Testing component metadata $baseVersion.0 -> $baseVersion.1 with the current payload and safe installation policy; not a released legacy installer migration"
$package.Package.Version = $baseVersion + '.0'
$package.Save($metadata)
$maintenanceMetadata = Join-Path $packages 'com.vaporview.maintenancetool/meta/package.xml'
[xml]$maintenancePackage = [IO.File]::ReadAllText($maintenanceMetadata)
$maintenanceVersion = [string]$maintenancePackage.Package.Version
$maintenancePackage.Package.Version = $maintenanceVersion + '.0'
$maintenancePackage.Save($maintenanceMetadata)
Update-MaintenanceResource 'create-old-maintenance-resource'
$installer = Join-Path $work 'quality-setup.exe'
Invoke-Stage 'create-installer' (Join-Path $IfwBin 'binarycreator.exe') @('--offline-only','-c',$configPath,'-p',$packages,$installer)
Invoke-Stage 'fresh-install' $installer @('--no-proxy','--accept-licenses','--default-answer','--confirm-command','--root',$target,'install')
foreach ($directory in @('calibrations','calibrations/drafts')) {
    if (-not (Test-Path -LiteralPath (Join-Path $target $directory) -PathType Container)) { throw "Installer did not create $directory" }
}
Verify-Payload
Invoke-Stage 'create-user-data' $CalibrationProbe @('--deployment-fixture',$target)
Verify-UserData 'verify-fresh-user-data'

$package.Package.Version = $baseVersion + '.1'
$package.Save($metadata)
$maintenancePackage.Package.Version = $maintenanceVersion + '.1'
$maintenancePackage.Save($maintenanceMetadata)
Update-MaintenanceResource 'create-new-maintenance-resource'
$repository = Join-Path $work 'repository'
Invoke-Stage 'create-update-repository' (Join-Path $IfwBin 'repogen.exe') @('-p',$packages,$repository)
$repositoryUri = [Uri]::new($repository + [IO.Path]::DirectorySeparatorChar).AbsoluteUri
$maintenance = Join-Path $target 'VaporViewMaintenanceTool.exe'
Invoke-Stage 'upgrade-maintenance' $maintenance @('--no-proxy','--set-temp-repository',$repositoryUri,'--accept-licenses','--default-answer','--confirm-command','update','com.vaporview.core') @(0,6)
# An Essential maintenance update returns 6 and requires a fresh invocation.
Invoke-Stage 'upgrade-core' $maintenance @('--no-proxy','--set-temp-repository',$repositoryUri,'--accept-licenses','--default-answer','--confirm-command','update','com.vaporview.core')
Verify-Payload
Verify-UserData 'verify-upgrade-user-data'
Invoke-Stage 'uninstall' $maintenance @('--no-proxy','--default-answer','--confirm-command','purge')
Wait-Uninstall
Verify-UserData 'verify-uninstalled-user-data'
if (Test-Path -LiteralPath (Join-Path $target 'VaporView.exe')) { throw 'Uninstall left the packaged executable installed' }
# This isolated root deliberately contains the retained fixture; IFW defaults
# to No for its generic non-empty-directory prompt even with RemoveTargetDir=false.
Invoke-Stage 'reinstall' $installer @('--no-proxy','--accept-licenses','--auto-answer','OverwriteTargetDirectory=Yes','--default-answer','--confirm-command','--root',$target,'install')
Verify-Payload
Verify-UserData 'verify-reinstalled-user-data'
Invoke-Stage 'final-uninstall' $maintenance @('--no-proxy','--default-answer','--confirm-command','purge')
Wait-Uninstall
Verify-UserData 'verify-final-user-data'

$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
Write-Host ('IFW lifecycle PASS: fresh install / upgrade / uninstall / reinstall; user data and history SHA retained; elevated=' + $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator))
# Remove only this newly created test workspace, after all IFW processes exit.
$resolved = (Resolve-Path -LiteralPath $work).Path
if ($resolved -ne $work -or [IO.Path]::GetPathRoot($resolved) -eq $resolved) { throw 'Refusing unexpected cleanup path' }
Remove-Item -LiteralPath $resolved -Recurse -Force
