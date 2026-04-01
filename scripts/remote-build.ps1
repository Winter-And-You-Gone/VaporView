param(
    [string]$SshExe = 'C:\Program Files\Git\usr\bin\ssh.exe',
    [string]$KeyFile = 'C:\Users\Winter\.ssh\id_ed25519',
    [string]$RemoteHost = 'Administrator@100.102.21.86',
    [string]$RemoteRepoDir = 'C:\WorkSpace\NAV\VaporView',
    [string]$RemoteBuildDir = 'C:\WorkSpace\NAV\VaporView\build',
    [string]$MsysPrefixPath = 'C:\msys64\ucrt64',
    [string]$Generator = 'Ninja',
    [string]$BuildType = 'Release',
    [int]$Parallel = 4,
    [switch]$Reconfigure
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-RemoteCommand {
    param([Parameter(Mandatory = $true)][string]$Command)

    Write-Host "[$RemoteHost] $Command"
    & $script:SshExe -i $script:KeyFile $script:RemoteHost $Command
    if ($LASTEXITCODE -ne 0) {
        throw "Remote command failed with exit code $LASTEXITCODE."
    }
}

$SshExe = (Resolve-Path -LiteralPath $SshExe).Path
$KeyFile = (Resolve-Path -LiteralPath $KeyFile).Path

$msysBinDir = Join-Path $MsysPrefixPath 'bin'
$qtToolsDir = Join-Path $MsysPrefixPath 'share\qt6\bin'
$cmakeExe = Join-Path $msysBinDir 'cmake.exe'
$cacheFile = Join-Path $RemoteBuildDir 'CMakeCache.txt'

$remotePathSetup = 'set PATH={0};{1};%PATH%' -f $qtToolsDir, $msysBinDir
$cacheProbeCommand = "cmd /c dir $cacheFile >nul 2>nul"
& $SshExe -i $KeyFile $RemoteHost $cacheProbeCommand | Out-Null
$cacheExists = $LASTEXITCODE -eq 0

$shouldConfigure = $Reconfigure -or (-not $cacheExists)
if ($shouldConfigure) {
    $configureCommand = @(
        'cmd /c'
        $remotePathSetup + '^&^&'
        $cmakeExe
        '-S'
        $RemoteRepoDir
        '-B'
        $RemoteBuildDir
        '-G'
        $Generator
        "-DCMAKE_BUILD_TYPE=$BuildType"
        "-DCMAKE_PREFIX_PATH=$MsysPrefixPath"
    ) -join ' '

    Invoke-RemoteCommand -Command $configureCommand
}

$buildCommand = @(
    'cmd /c'
    $remotePathSetup + '^&^&'
    $cmakeExe
    '--build'
    $RemoteBuildDir
    '--parallel'
    $Parallel
) -join ' '

Invoke-RemoteCommand -Command $buildCommand
