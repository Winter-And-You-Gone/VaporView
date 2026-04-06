[CmdletBinding()]
param(
    [ValidateSet("Configure", "Build", "Rebuild")]
    [string]$Action = "Build"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$cmakeExe = "D:\msys64\ucrt64\bin\cmake.exe"
$ninjaExe = "D:\Ninja\ninja.exe"
$gccExe = "D:\msys64\ucrt64\bin\gcc.exe"
$gxxExe = "D:\msys64\ucrt64\bin\g++.exe"
$qtDeployExe = "D:\msys64\ucrt64\bin\windeployqt6.exe"
$preset = "windows-ucrt64-release"
$buildDir = Join-Path $repoRoot "build"
$legacyBuildDirs = @(
    (Join-Path $repoRoot "build-fluent"),
    (Join-Path $repoRoot "build-ucrt64-release")
)

$requiredPaths = @(
    $cmakeExe,
    $ninjaExe,
    $gccExe,
    $gxxExe,
    $qtDeployExe,
    (Join-Path $repoRoot "CMakePresets.json"),
    (Join-Path $repoRoot "CMakeLists.txt")
)

$missing = $requiredPaths | Where-Object { -not (Test-Path -LiteralPath $_) }
if ($missing.Count -gt 0) {
    $missingList = $missing -join [Environment]::NewLine
    throw "Required build dependencies are missing:`n$missingList"
}

function Invoke-CMake {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    & $cmakeExe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "CMake command failed with exit code $LASTEXITCODE."
    }
}

function Remove-LegacyBuildDirectories {
    foreach ($legacyDir in $legacyBuildDirs) {
        if (Test-Path -LiteralPath $legacyDir) {
            Remove-Item -LiteralPath $legacyDir -Recurse -Force
        }
    }
}

Push-Location $repoRoot
try {
    Remove-LegacyBuildDirectories

    if ($Action -eq "Rebuild" -and (Test-Path -LiteralPath $buildDir)) {
        Remove-Item -LiteralPath $buildDir -Recurse -Force
    }

    Invoke-CMake -Arguments @("--preset", $preset)

    if ($Action -ne "Configure") {
        Invoke-CMake -Arguments @("--build", "--preset", $preset)
    }
}
finally {
    Pop-Location
}
