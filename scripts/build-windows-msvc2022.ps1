param(
    [ValidateSet("Configure", "Build", "Rebuild", "Test", "Clean")]
    [string]$Action = "Build",
    [string]$QtPrefix = $env:VAPORVIEW_QT_MSVC_PREFIX
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$presetName = "windows-msvc2022-x64-release"
$buildDir = Join-Path $repoRoot "build\Release"

if ([string]::IsNullOrWhiteSpace($QtPrefix)) {
    $QtPrefix = "D:\QT\6.8.3\msvc2022_64"
}

$qtConfig = Join-Path $QtPrefix "lib\cmake\Qt6\Qt6Config.cmake"
if (-not (Test-Path $qtConfig)) {
    throw "Qt 6 MSVC kit was not found at '$QtPrefix'. Set VAPORVIEW_QT_MSVC_PREFIX or pass -QtPrefix."
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe was not found. Install Visual Studio 2022 Build Tools."
}

$vsInstall = & $vswhere -version "[17.0,18.0)" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ([string]::IsNullOrWhiteSpace($vsInstall)) {
    throw "Visual Studio 2022 Build Tools with MSVC x64 tools was not found."
}

$devCmd = Join-Path $vsInstall "Common7\Tools\VsDevCmd.bat"
if (-not (Test-Path $devCmd)) {
    throw "VsDevCmd.bat was not found under '$vsInstall'."
}

$cmakeExe = "cmake"
$repoCmake = "D:\Cmake\bin\cmake.exe"
if (Test-Path $repoCmake) {
    $cmakeExe = $repoCmake
}

$ninjaExe = Join-Path $vsInstall "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if (-not (Test-Path $ninjaExe)) {
    $foundNinja = Get-Command ninja -ErrorAction SilentlyContinue
    if ($foundNinja) {
        $ninjaExe = $foundNinja.Source
    } else {
        throw "ninja.exe was not found. Install the Visual Studio CMake tools component or Ninja."
    }
}

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
$testCommand = "`"$cmakeExe`" --build --preset $presetName && ctest --preset $presetName"

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
    "Rebuild" {
        if (Test-Path $buildDir) {
            Remove-Item -LiteralPath $buildDir -Recurse -Force
        }
        Invoke-VaporViewVsCommand "$configureCommand && $buildCommand"
    }
    "Test" {
        Invoke-VaporViewVsCommand "$configureCommand && $testCommand"
    }
}
