param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path,
    [string]$Proxy = 'http://127.0.0.1:7890'
)

$ErrorActionPreference = 'Stop'

$url = 'https://naturalearth.s3.amazonaws.com/50m_raster/NE2_50M_SR_W.zip'
$expectedBytes = 88903451
$mapsDir = Join-Path $ProjectRoot 'data\maps'
$naturalEarthDir = Join-Path $mapsDir 'natural_earth'
$extractDir = Join-Path $naturalEarthDir 'NE2_50M_SR_W'
$zipPath = Join-Path $naturalEarthDir 'NE2_50M_SR_W.zip'
$earthPath = Join-Path $mapsDir 'vaporview_default.earth'
$vrtPath = Join-Path $extractDir 'NE2_50M_SR_W.vrt'

New-Item -ItemType Directory -Force -Path $naturalEarthDir | Out-Null

function Invoke-MapDownload {
    param([switch]$UseProxy)

    $curlArgs = @(
        '--fail',
        '--location',
        '--show-error',
        '--progress-bar',
        '--output', $zipPath,
        $url
    )
    if ($UseProxy) {
        $curlArgs = @('--proxy', $Proxy) + $curlArgs
    }

    & curl.exe @curlArgs
    if ($LASTEXITCODE -ne 0) {
        throw "curl exited with code $LASTEXITCODE"
    }
}

if ((Test-Path -LiteralPath $zipPath) -and ((Get-Item -LiteralPath $zipPath).Length -eq $expectedBytes)) {
    Write-Host "Using existing $zipPath"
} else {
    Remove-Item -LiteralPath $zipPath -Force -ErrorAction SilentlyContinue
    try {
        Invoke-MapDownload
    } catch {
        Write-Warning "Direct download failed: $($_.Exception.Message)"
        Write-Host "Retrying through proxy $Proxy"
        Remove-Item -LiteralPath $zipPath -Force -ErrorAction SilentlyContinue
        Invoke-MapDownload -UseProxy
    }
}

$actualBytes = (Get-Item -LiteralPath $zipPath).Length
if ($actualBytes -ne $expectedBytes) {
    throw "Unexpected zip size: $actualBytes, expected $expectedBytes"
}

Expand-Archive -LiteralPath $zipPath -DestinationPath $naturalEarthDir -Force

@'
<VRTDataset rasterXSize="10800" rasterYSize="5400">
  <SRS dataAxisToSRSAxisMapping="2,1">GEOGCS["WGS 84",DATUM["WGS_1984",SPHEROID["WGS 84",6378137,298.257223563,AUTHORITY["EPSG","7030"]],AUTHORITY["EPSG","6326"]],PRIMEM["Greenwich",0,AUTHORITY["EPSG","8901"]],UNIT["degree",0.0174532925199433,AUTHORITY["EPSG","9122"]],AUTHORITY["EPSG","4326"]]</SRS>
  <GeoTransform>-180.0, 0.03333333333333, 0.0, 90.0, 0.0, -0.03333333333333</GeoTransform>
  <VRTRasterBand dataType="Byte" band="1">
    <ColorInterp>Red</ColorInterp>
    <SimpleSource>
      <SourceFilename relativeToVRT="1">NE2_50M_SR_W.tif</SourceFilename>
      <SourceBand>1</SourceBand>
      <SourceProperties RasterXSize="10800" RasterYSize="5400" DataType="Byte" BlockXSize="10800" BlockYSize="1" />
      <SrcRect xOff="0" yOff="0" xSize="10800" ySize="5400" />
      <DstRect xOff="0" yOff="0" xSize="10800" ySize="5400" />
    </SimpleSource>
  </VRTRasterBand>
  <VRTRasterBand dataType="Byte" band="2">
    <ColorInterp>Green</ColorInterp>
    <SimpleSource>
      <SourceFilename relativeToVRT="1">NE2_50M_SR_W.tif</SourceFilename>
      <SourceBand>2</SourceBand>
      <SourceProperties RasterXSize="10800" RasterYSize="5400" DataType="Byte" BlockXSize="10800" BlockYSize="1" />
      <SrcRect xOff="0" yOff="0" xSize="10800" ySize="5400" />
      <DstRect xOff="0" yOff="0" xSize="10800" ySize="5400" />
    </SimpleSource>
  </VRTRasterBand>
  <VRTRasterBand dataType="Byte" band="3">
    <ColorInterp>Blue</ColorInterp>
    <SimpleSource>
      <SourceFilename relativeToVRT="1">NE2_50M_SR_W.tif</SourceFilename>
      <SourceBand>3</SourceBand>
      <SourceProperties RasterXSize="10800" RasterYSize="5400" DataType="Byte" BlockXSize="10800" BlockYSize="1" />
      <SrcRect xOff="0" yOff="0" xSize="10800" ySize="5400" />
      <DstRect xOff="0" yOff="0" xSize="10800" ySize="5400" />
    </SimpleSource>
  </VRTRasterBand>
</VRTDataset>
'@ | Set-Content -LiteralPath $vrtPath -Encoding ASCII

@'
<map name="VaporView Natural Earth">
    <GDALImage name="Natural Earth II 1:50m shaded relief with water">
        <url>natural_earth/NE2_50M_SR_W/NE2_50M_SR_W.vrt</url>
        <interpolation>bilinear</interpolation>
    </GDALImage>
</map>
'@ | Set-Content -LiteralPath $earthPath -Encoding ASCII

Write-Host "Natural Earth map is ready:"
Write-Host "  $earthPath"
