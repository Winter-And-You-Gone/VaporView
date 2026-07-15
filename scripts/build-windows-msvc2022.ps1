param(
    [ValidateSet("Configure", "Build", "BuildApp", "Rebuild", "Test", "TestFast", "Clean")]
    [string]$Action = "Build",
    [string]$QtPrefix = $env:VAPORVIEW_QT_MSVC_PREFIX,
    [string]$VisualStudioInstall = $env:VAPORVIEW_VS2022_INSTALL,
    [string]$CMakeExe = $env:VAPORVIEW_CMAKE_EXE,
    [string]$NinjaExe = $env:VAPORVIEW_NINJA_EXE
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$presetName = "windows-msvc2022-x64-release"
$buildDir = Join-Path $repoRoot "build\Release"

function Resolve-ExistingFile {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Description
    )

    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description was not found at '$Path'."
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Resolve-ExistingDirectory {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Description
    )

    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "$Description was not found at '$Path'."
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Resolve-QtPrefix {
    param([string]$Prefix)

    if ([string]::IsNullOrWhiteSpace($Prefix)) {
        throw "Qt 6 MSVC kit path is not configured. Set VAPORVIEW_QT_MSVC_PREFIX, pass -QtPrefix, or call this script from scripts\build-windows-msvc2022.local.ps1."
    }

    $resolvedPrefix = Resolve-ExistingDirectory $Prefix "Qt 6 MSVC kit"
    $qtConfig = Join-Path $resolvedPrefix "lib\cmake\Qt6\Qt6Config.cmake"
    [void](Resolve-ExistingFile $qtConfig "Qt6Config.cmake")
    return $resolvedPrefix
}

function Resolve-VsWhere {
    $programFilesX86 = ${env:ProgramFiles(x86)}
    if (-not [string]::IsNullOrWhiteSpace($programFilesX86)) {
        $defaultVsWhere = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path -LiteralPath $defaultVsWhere -PathType Leaf) {
            return (Resolve-Path -LiteralPath $defaultVsWhere).Path
        }
    }

    $foundVsWhere = Get-Command vswhere -ErrorAction SilentlyContinue
    if ($foundVsWhere) {
        return $foundVsWhere.Source
    }

    throw "vswhere.exe was not found. Install Visual Studio 2022 Build Tools or make vswhere.exe available in PATH."
}

function Resolve-VisualStudioInstall {
    param([string]$InstallPath)

    if (-not [string]::IsNullOrWhiteSpace($InstallPath)) {
        return Resolve-ExistingDirectory $InstallPath "Visual Studio 2022 installation"
    }

    $vswhere = Resolve-VsWhere
    $detectedInstall = & $vswhere -version "[17.0,18.0)" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($detectedInstall)) {
        throw "Visual Studio 2022 Build Tools with MSVC x64 tools was not found."
    }
    return (Resolve-Path -LiteralPath $detectedInstall).Path
}

function Resolve-CMakeExe {
    param([string]$RequestedPath, [string]$VsInstall)

    if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
        return Resolve-ExistingFile $RequestedPath "cmake.exe"
    }

    $vsCmake = Join-Path $VsInstall "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (Test-Path -LiteralPath $vsCmake -PathType Leaf) {
        return (Resolve-Path -LiteralPath $vsCmake).Path
    }

    $foundCMake = Get-Command cmake -ErrorAction SilentlyContinue
    if ($foundCMake) {
        return $foundCMake.Source
    }

    throw "cmake.exe was not found. Install the Visual Studio CMake tools component or set VAPORVIEW_CMAKE_EXE."
}

function Resolve-NinjaExe {
    param([string]$RequestedPath, [string]$VsInstall)

    if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
        return Resolve-ExistingFile $RequestedPath "ninja.exe"
    }

    $vsNinja = Join-Path $VsInstall "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
    if (Test-Path -LiteralPath $vsNinja -PathType Leaf) {
        return (Resolve-Path -LiteralPath $vsNinja).Path
    }

    $foundNinja = Get-Command ninja -ErrorAction SilentlyContinue
    if ($foundNinja) {
        return $foundNinja.Source
    }

    throw "ninja.exe was not found. Install the Visual Studio CMake tools component, install Ninja, or set VAPORVIEW_NINJA_EXE."
}

$QtPrefix = Resolve-QtPrefix $QtPrefix
$vsInstall = Resolve-VisualStudioInstall $VisualStudioInstall
$devCmd = Resolve-ExistingFile (Join-Path $vsInstall "Common7\Tools\VsDevCmd.bat") "VsDevCmd.bat"
$cmakeExe = Resolve-CMakeExe $CMakeExe $vsInstall
$ninjaExe = Resolve-NinjaExe $NinjaExe $vsInstall

function Invoke-VaporViewVsCommand {
    param([Parameter(Mandatory = $true)][string]$Command)

    Push-Location $repoRoot
    try {
        & cmd.exe /d /s /c "`"$devCmd`" -arch=x64 -host_arch=x64 >nul && $Command"
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    } finally {
        Pop-Location
    }
}

$configureCommand = "`"$cmakeExe`" --preset $presetName -DCMAKE_PREFIX_PATH=`"$QtPrefix`" -DCMAKE_MAKE_PROGRAM=`"$ninjaExe`""
$buildCommand = "`"$cmakeExe`" --build --preset $presetName"
$buildAppCommand = "`"$cmakeExe`" --build --preset $presetName-app-only"
$testCommand = "`"$cmakeExe`" --build --preset $presetName && ctest --preset $presetName"
$fastTestCommand = "`"$cmakeExe`" --build --preset $presetName && ctest --preset $presetName-fast"

switch ($Action) {
    "Clean" {
        if (Test-Path $buildDir) {
            Remove-Item -LiteralPath $buildDir -Recurse -Force
        }
    }
    "Configure" {
        Invoke-VaporViewVsCommand $configureCommand
    }
    "Build" {
        Invoke-VaporViewVsCommand "$configureCommand && $buildCommand"
    }
    "BuildApp" {
        Invoke-VaporViewVsCommand "$configureCommand && $buildAppCommand"
    }
    "Rebuild" {
        if (Test-Path $buildDir) {
            Remove-Item -LiteralPath $buildDir -Recurse -Force
        }
        Invoke-VaporViewVsCommand "$configureCommand && $buildCommand"
    }
    "Test" {
        Invoke-VaporViewVsCommand "$configureCommand && $testCommand"
    }
    "TestFast" {
        Invoke-VaporViewVsCommand "$configureCommand && $fastTestCommand"
    }
}
