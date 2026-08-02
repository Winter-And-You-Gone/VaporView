param(
    [Parameter(Mandatory = $true)][string]$StageDir,
    [Parameter(Mandatory = $true)][string]$WorkDir
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $StageDir -PathType Container)) {
    Write-Host "SKIP: IFW stage directory does not exist: $StageDir"
    exit 77
}

$requiredRootExecutables = @(
    "VaporView.exe",
    "VaporViewSky.exe",
    "VaporViewSkyCore.exe",
    "VaporViewSkyTui.exe",
    "VaporViewUpdateRelauncher.exe",
    "VaporViewPermissionTool.exe"
)

foreach ($name in $requiredRootExecutables) {
    $path = Join-Path $StageDir $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing staged root executable: $name"
    }
}

$readonlyFiles = Get-ChildItem -LiteralPath $StageDir -Recurse -Force -File |
    Where-Object { ($_.Attributes -band [IO.FileAttributes]::ReadOnly) -ne 0 } |
    Select-Object -First 5
if ($readonlyFiles) {
    $names = ($readonlyFiles | ForEach-Object { $_.FullName }) -join "`n"
    throw "Staged regular files still have the ReadOnly attribute:`n$names"
}

$configPath = Join-Path $WorkDir "config.xml"
if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
    throw "Generated IFW config.xml was not found at $configPath"
}
$configText = Get-Content -LiteralPath $configPath -Raw
if ($configText -notmatch '<TargetDir>C:\\VaporView</TargetDir>') {
    throw "Generated IFW config.xml does not keep the default TargetDir as C:\VaporView"
}
if ($configText -notmatch '<AllowNonAsciiCharacters>true</AllowNonAsciiCharacters>') {
    throw "Generated IFW config.xml does not allow non-ASCII installation paths"
}

Write-Host "ifw_staging_test passed"
