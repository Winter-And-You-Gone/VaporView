[CmdletBinding()]
param(
    [string]$ProjectRoot,
    [string]$DemName = "copernicus_dem_glo30",
    [string]$DemDir,
    [string]$GdalBin,
    [switch]$Srtm,
    [switch]$Check
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
    $ProjectRoot = Split-Path -Parent $PSScriptRoot
}

$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path

function Get-GdalTool {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $candidateDirs = @()
    if (-not [string]::IsNullOrWhiteSpace($GdalBin)) {
        $candidateDirs += [System.IO.Path]::GetFullPath($GdalBin)
    }
    if (-not [string]::IsNullOrWhiteSpace($env:GDAL_BIN)) {
        $candidateDirs += $env:GDAL_BIN
    }
    $candidateDirs += @(
        (Join-Path $ProjectRoot ".local_deps/vcpkg_installed/x64-windows/tools/gdal"),
        (Join-Path $ProjectRoot ".local_deps/vcpkg_installed/x64-windows/bin"),
        "C:/OSGeo4W/bin",
        "C:/OSGeo4W64/bin",
        "C:/Program Files/QGIS 3.40/bin",
        "C:/Program Files/QGIS 3.38/bin",
        "C:/Program Files/QGIS 3.36/bin",
        "C:/Program Files/QGIS 3.34/bin",
        "C:/Program Files/QGIS 3.32/bin"
    )

    foreach ($dir in ($candidateDirs | Select-Object -Unique)) {
        $candidate = Join-Path $dir "$Name.exe"
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }

    return $null
}

function Get-GdalToolHint {
    $candidateDirs = @()
    if (-not [string]::IsNullOrWhiteSpace($GdalBin)) {
        $candidateDirs += [System.IO.Path]::GetFullPath($GdalBin)
    }
    if (-not [string]::IsNullOrWhiteSpace($env:GDAL_BIN)) {
        $candidateDirs += $env:GDAL_BIN
    }
    $candidateDirs += @(
        (Join-Path $ProjectRoot ".local_deps/vcpkg_installed/x64-windows/tools/gdal"),
        (Join-Path $ProjectRoot ".local_deps/vcpkg_installed/x64-windows/bin"),
        "C:/OSGeo4W/bin",
        "C:/OSGeo4W64/bin",
        "C:/Program Files/QGIS 3.40/bin",
        "C:/Program Files/QGIS 3.38/bin",
        "C:/Program Files/QGIS 3.36/bin",
        "C:/Program Files/QGIS 3.34/bin",
        "C:/Program Files/QGIS 3.32/bin"
    )
    return (($candidateDirs | Select-Object -Unique) -join "; ")
}

$demFolderName = if ($Srtm) { "srtm" } else { $DemName }
$vrtFileName = if ($Srtm) { "srtm.vrt" } else { "copernicus_dem_glo30.vrt" }
$earthFileName = if ($Srtm) { "vaporview_with_srtm.earth" } else { "vaporview_with_dem.earth" }

$terrainDir = Join-Path $ProjectRoot "data/maps/terrain/$demFolderName"
$vrtPath = Join-Path $terrainDir $vrtFileName
$earthPath = Join-Path $ProjectRoot "data/maps/$earthFileName"
$tileDir = if ([string]::IsNullOrWhiteSpace($DemDir)) {
    $terrainDir
} else {
    [System.IO.Path]::GetFullPath($DemDir)
}

if (-not (Test-Path -LiteralPath $terrainDir -PathType Container)) {
    New-Item -ItemType Directory -Path $terrainDir | Out-Null
}

$tiles = Get-ChildItem -LiteralPath $tileDir -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Extension -in @(".tif", ".tiff") }
if (-not $tiles -or $tiles.Count -eq 0) {
    throw "No GeoTIFF DEM tiles found in $tileDir. Place local DEM .tif/.tiff files there first. This script does not download data."
}

$gdalBuildVrt = Get-GdalTool -Name "gdalbuildvrt"
if (-not $gdalBuildVrt) {
    throw "gdalbuildvrt was not found. Install GDAL tools, add them to PATH, set GDAL_BIN, or pass -GdalBin. Searched: $(Get-GdalToolHint)"
}

if (-not (Test-Path -LiteralPath $earthPath -PathType Leaf)) {
    throw "Expected earth template not found: $earthPath"
}

$earthText = Get-Content -LiteralPath $earthPath -Raw
$expectedRelative = if ($Srtm) { "terrain/srtm/srtm.vrt" } else { "terrain/copernicus_dem_glo30/copernicus_dem_glo30.vrt" }
if ($earthText -notmatch [regex]::Escape($expectedRelative)) {
    throw "$earthFileName does not reference $expectedRelative"
}

if ($Check) {
    if (-not (Test-Path -LiteralPath $vrtPath -PathType Leaf)) {
        throw "Expected VRT does not exist yet: $vrtPath"
    }
} else {
    if (Test-Path -LiteralPath $vrtPath) {
        Remove-Item -LiteralPath $vrtPath -Force
    }

    if ($tileDir -ne $terrainDir) {
        Write-Host "Using DEM tiles from: $tileDir"
        Write-Host "Writing auto-load VRT to: $vrtPath"
    }

    $tilePaths = @($tiles | ForEach-Object { $_.FullName })
    & $gdalBuildVrt $vrtPath @tilePaths
    if ($LASTEXITCODE -ne 0) {
        throw "gdalbuildvrt failed with exit code $LASTEXITCODE"
    }

    if (-not (Test-Path -LiteralPath $vrtPath -PathType Leaf)) {
        throw "Expected VRT was not created: $vrtPath"
    }
}

$gdalInfo = Get-GdalTool -Name "gdalinfo"
if ($gdalInfo) {
    & $gdalInfo $vrtPath | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "gdalinfo could not read generated VRT: $vrtPath"
    }
}

if ($Check) {
    Write-Host "DEM check passed: $vrtPath"
} else {
    Write-Host "Prepared DEM VRT: $vrtPath"
}
Write-Host "Validated earth template: $earthPath"
Write-Host "Start VaporView with -DVAPORVIEW_ENABLE_OSGEARTH=ON build; MapDataManager should select $earthFileName automatically."
