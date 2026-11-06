#Requires -Version 5.1
$ErrorActionPreference = 'Stop'
$source = Get-Content -Raw (Join-Path $PSScriptRoot 'configure-vaporview-localdumps.ps1')
$productionRoot = 'HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps'
$productionAssignment = '$registryRoot = ' + "'" + $productionRoot + "'"
if (-not $source.Contains($productionAssignment)) { throw 'Refusing test: registry isolation replacement did not match.' }
$id = [guid]::NewGuid().ToString('N')
$root = "HKCU:\Software\VaporViewLocalDumpsTest_$id"
$temp = Join-Path ([IO.Path]::GetTempPath()) "VaporView LocalDumps Test $id"
$copy = Join-Path $temp 'test-script.ps1'
$previousDumpRoot = [Environment]::GetEnvironmentVariable('VAPORVIEW_TEST_DUMP_ROOT', 'Process')
New-Item -ItemType Directory -Path $temp | Out-Null
try {
    $source = $source.Replace($productionAssignment, ('$registryRoot = ' + "'" + $root + "'"))
    if ($source.IndexOf('HKLM:', [StringComparison]::OrdinalIgnoreCase) -ge 0 -or -not $source.Contains($root)) { throw 'Refusing test: registry isolation failed.' }
    $source = [regex]::Replace($source, '(?s)function Test-IsAdministrator \{.*?\n\}', 'function Test-IsAdministrator { return $true }')
    Set-Content -LiteralPath $copy -Value $source -Encoding UTF8
    $other = Join-Path $root 'Other.exe'
    New-Item -Path $other -Force | Out-Null
    [Environment]::SetEnvironmentVariable('VAPORVIEW_TEST_DUMP_ROOT', $temp, 'Process')
    & $copy -Enable -Executable VaporViewSkyCore.exe -DumpFolder '%VAPORVIEW_TEST_DUMP_ROOT%\expanded path'
    $expandedFolder = Join-Path (Join-Path $temp 'expanded path') 'VaporViewSkyCore'
    $expandedKey = Get-Item -LiteralPath (Join-Path $root 'VaporViewSkyCore.exe')
    if ($expandedKey.GetValue('DumpFolder', '', [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames) -ne $expandedFolder) { throw 'Environment variable path was not expanded correctly' }
    & $copy -Status -Executable VaporViewSkyCore.exe -DumpFolder '%VAPORVIEW_TEST_DUMP_ROOT%\expanded path'
    $unknownVariable = "VAPORVIEW_TEST_UNDEFINED_$id"
    $unknownFolder = '%' + $unknownVariable + '%\invalid'
    $failed = $false
    try { & $copy -Enable -Executable VaporView.exe -DumpFolder $unknownFolder } catch { $failed = $_.ToString() -match 'unresolved environment variable' }
    if (-not $failed -or (Test-Path (Join-Path $root 'VaporView.exe'))) { throw 'Unresolved environment variable accepted or modified settings' }
    & $copy -Enable -Executable VaporViewSkyCore.exe -DumpFolder $temp
    if ((Test-Path (Join-Path $root 'VaporView.exe')) -or (Test-Path (Join-Path $root 'VaporViewSkyTui.exe'))) { throw 'Single executable enable modified other processes' }
    $failed = $false
    try { & $copy -Enable -Status -DumpFolder $temp } catch { $failed = $true }
    if (-not $failed) { throw 'Multiple modes accepted' }
    $failed = $false
    try { & $copy -Enable -Executable Other.exe -DumpFolder $temp } catch { $failed = $true }
    if (-not $failed) { throw 'Unsupported executable accepted' }
    & $copy -Enable -DumpFolder $temp
    foreach ($exe in @('VaporView.exe','VaporViewSkyCore.exe','VaporViewSkyTui.exe')) {
        $key = Get-Item -LiteralPath (Join-Path $root $exe)
        if ($key.GetValue('DumpType') -ne 2 -or $key.GetValue('DumpCount') -ne 10) { throw 'Wrong dump settings' }
        if ($key.GetValue('DumpFolder') -ne (Join-Path $temp ([IO.Path]::GetFileNameWithoutExtension($exe)))) { throw 'Wrong dump folder' }
    }
    & $copy -Disable -Executable VaporViewSkyCore.exe -DumpFolder $temp
    if ((Test-Path (Join-Path $root 'VaporViewSkyCore.exe')) -or -not (Test-Path (Join-Path $root 'VaporView.exe'))) { throw 'Single executable removal failed' }
    $failed = $false
    try { & $copy -Status -DumpFolder $temp } catch { $failed = $true }
    if (-not $failed) { throw 'Missing key accepted' }
    & $copy -Enable -Executable VaporViewSkyCore.exe -DumpFolder $temp
    Set-ItemProperty -Path (Join-Path $root 'VaporView.exe') -Name DumpType -Value 1
    $failed = $false
    try { & $copy -Status -DumpFolder $temp } catch { $failed = $true }
    if (-not $failed) { throw 'Incorrect setting accepted' }
    & $copy -Disable -DumpFolder $temp
    if (-not (Test-Path $other)) { throw 'Unrelated key removed' }
    $nonAdmin = $source.Replace('function Test-IsAdministrator { return $true }', 'function Test-IsAdministrator { return $false }')
    Set-Content -LiteralPath $copy -Value $nonAdmin -Encoding UTF8
    $failed = $false
    try { & $copy -Enable -DumpFolder $temp } catch { $failed = $_.ToString() -match 'requires Administrator' }
    if (-not $failed) { throw 'Non-admin check failed' }
    if (Test-Path (Join-Path $root 'VaporView.exe')) { throw 'Non-admin modified settings' }
    Write-Host 'LocalDumps tests passed.'
} finally {
    [Environment]::SetEnvironmentVariable('VAPORVIEW_TEST_DUMP_ROOT', $previousDumpRoot, 'Process')
    if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
    if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Recurse -Force }
}
