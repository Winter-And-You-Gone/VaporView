param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '../build/Release'),
    [string]$ArchiveDirectory = ''
)
$ErrorActionPreference = 'Stop'
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$revision = (git -C $PSScriptRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify source revision.' }
if (!$ArchiveDirectory) {
    $ArchiveDirectory = Join-Path $build ('symbols/' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + $revision.Substring(0, 12))
}
$products = @('VaporView', 'VaporViewSky', 'VaporViewSkyCore', 'VaporViewSkyTui', 'VaporViewFpgaSimulator')
$files = @()
foreach ($product in $products) {
    $binary = Join-Path $build ($product + '.exe')
    if (!(Test-Path -LiteralPath $binary)) { continue }
    $pdb = Join-Path $build ($product + '.pdb')
    if (!(Test-Path -LiteralPath $pdb)) { throw "Missing matching symbols: $pdb" }
    $files += Get-Item -LiteralPath $binary, $pdb
}
if (!$files.Count) { throw 'No Release products found.' }
New-Item -ItemType Directory -Path $ArchiveDirectory -ErrorAction Stop | Out-Null
$manifest = foreach ($file in $files) {
    Copy-Item -LiteralPath $file.FullName -Destination $ArchiveDirectory
    [ordered]@{ name = $file.Name; sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash }
}
[ordered]@{
    source_revision = $revision
    source_dirty = [bool](git -C $PSScriptRoot status --porcelain)
    created_utc = [DateTime]::UtcNow.ToString('o')
    files = @($manifest)
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $ArchiveDirectory 'manifest.json') -Encoding utf8
Write-Output "Archived Release binaries and symbols: $ArchiveDirectory"
