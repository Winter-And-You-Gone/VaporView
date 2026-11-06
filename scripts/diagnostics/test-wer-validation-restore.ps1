#Requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$Helper)
$ErrorActionPreference = 'Stop'
$helperPath = (Resolve-Path -LiteralPath $Helper).Path
$id = [guid]::NewGuid().ToString('N')
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('VaporViewWerRestore_' + $id)
$registryRoot = 'HKCU:\Software\VaporViewWerRestore_' + $id
$absentRoot = 'HKCU:\Software\VaporViewWerAbsent_' + $id
$nativeRoot = 'HKCU\Software\VaporViewWerRestore_' + $id
$validatorSource = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'validate-wer-isolated-vm.ps1')
$configSource = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'configure-vaporview-localdumps.ps1')
$originalNative = 'HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps'
$originalProvider = 'HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps'
if (-not $validatorSource.Contains($originalNative) -or -not $validatorSource.Contains($originalProvider) -or -not $configSource.Contains($originalProvider)) { throw 'Refusing test: registry isolation replacement did not match.' }
New-Item -ItemType Directory -Path $testRoot | Out-Null
try {
    $existing = Join-Path $registryRoot 'VaporView.exe'
    $item = New-Item -Path $existing -Force
    $item.SetValue('DumpFolder', '%TEMP%\original', [Microsoft.Win32.RegistryValueKind]::ExpandString)
    $item.SetValue('DumpCount', 7, [Microsoft.Win32.RegistryValueKind]::DWord)
    $item.SetValue('Unrelated', 'keep', [Microsoft.Win32.RegistryValueKind]::String)
    New-Item -Path (Join-Path $existing 'Nested') -Force | Out-Null
    $originalAcl = (Get-Item -LiteralPath $existing).GetAccessControl().Sddl
    $validatorSource = $validatorSource.Replace($originalNative, $nativeRoot).Replace($originalProvider, $registryRoot)
    $validatorSource = $validatorSource.Replace('[Microsoft.Win32.Registry]::LocalMachine', '[Microsoft.Win32.Registry]::CurrentUser').Replace("'SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps'", "'Software\VaporViewWerRestore_$id'")
    $trustedExpression = "(Resolve-Path -LiteralPath (Join-Path `$PSScriptRoot '..\..\build\Release\wer_crash_helper.exe')).Path"
    if (-not $validatorSource.Contains($trustedExpression)) { throw 'Refusing test: trusted helper replacement did not match.' }
    $validatorSource = $validatorSource.Replace($trustedExpression, "(Resolve-Path -LiteralPath '" + $helperPath.Replace("'", "''") + "').Path")
    $validatorSource = $validatorSource.Replace('if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {', 'if ($false) {')
    $configSource = $configSource.Replace($originalProvider, $registryRoot)
    $configSource = [regex]::Replace($configSource, '(?s)function Test-IsAdministrator \{.*?\n\}', 'function Test-IsAdministrator { return $true }')
    Set-Content -LiteralPath (Join-Path $testRoot 'configure-vaporview-localdumps.ps1') -Value $configSource -Encoding UTF8
    $validatorCopy = Join-Path $testRoot 'validator.ps1'
    $beforeCrash = "[Environment]::SetEnvironmentVariable('VAPORVIEW_WER_TEST_CONFIRM', 'ISOLATED_VM_ONLY', 'Process')"
    if (-not $validatorSource.Contains($beforeCrash)) { throw 'Refusing test: crash interception did not match.' }
    Set-Content -LiteralPath $validatorCopy -Value $validatorSource.Replace($beforeCrash, "throw 'injected failure after enable'") -Encoding UTF8
    $failed = $false
    try { & $validatorCopy -Helper $helperPath -TestRoot (Join-Path $testRoot 'run') -Execute -ConfirmIsolatedVm } catch { if ($_.Exception.Message -notmatch 'injected failure after enable') { throw }; $failed = $true }
    if (-not $failed) { throw 'Injected failure was not observed' }
    $item = Get-Item -LiteralPath $existing
    if ($item.GetValue('DumpFolder', '', [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames) -ne '%TEMP%\original' -or $item.GetValueKind('DumpFolder') -ne [Microsoft.Win32.RegistryValueKind]::ExpandString -or $item.GetValue('DumpCount') -ne 7 -or $item.GetValue('Unrelated') -ne 'keep' -or $item.GetValueNames() -contains 'DumpType') { throw 'Original values were not restored' }
    if (-not (Test-Path -LiteralPath (Join-Path $existing 'Nested')) -or (Get-Item -LiteralPath $existing).GetAccessControl().Sddl -ne $originalAcl) { throw 'Original subkeys or ACL changed' }
    if ((Test-Path -LiteralPath (Join-Path $registryRoot 'VaporViewSkyCore.exe')) -or (Test-Path -LiteralPath (Join-Path $registryRoot 'VaporViewSkyTui.exe'))) { throw 'Originally absent keys were not removed' }
    $typeWrite = 'New-ItemProperty -LiteralPath $path -Name DumpType -PropertyType DWord -Value $DumpType -Force | Out-Null'
    if (-not $configSource.Contains($typeWrite)) { throw 'Refusing test: partial enable interception did not match.' }
    Set-Content -LiteralPath (Join-Path $testRoot 'configure-vaporview-localdumps.ps1') -Value $configSource.Replace($typeWrite, ($typeWrite + "`n        throw 'injected partial enable failure'")) -Encoding UTF8
    Set-Content -LiteralPath $validatorCopy -Value $validatorSource.Replace($beforeCrash, "throw 'unexpected crash path'") -Encoding UTF8
    $failed = $false
    try { & $validatorCopy -Helper $helperPath -TestRoot (Join-Path $testRoot 'partial-failure') -Execute -ConfirmIsolatedVm } catch { if ($_.Exception.Message -notmatch 'injected partial enable failure') { throw }; $failed = $true }
    if (-not $failed -or (Get-ItemPropertyValue -LiteralPath $existing -Name Unrelated) -ne 'keep' -or -not (Test-Path -LiteralPath (Join-Path $existing 'Nested'))) { throw 'Partial enable failed to restore original state' }
    $backupCall = '& reg.exe export "$nativeRoot\$name" $backup /y | Out-Null'
    if (-not $validatorSource.Contains($backupCall)) { throw 'Refusing test: backup interception did not match.' }
    Set-Content -LiteralPath $validatorCopy -Value $validatorSource.Replace($backupCall, "throw 'injected backup failure'") -Encoding UTF8
    $failed = $false
    try { & $validatorCopy -Helper $helperPath -TestRoot (Join-Path $testRoot 'backup-failure') -Execute -ConfirmIsolatedVm } catch { $failed = $_.Exception.Message -match 'injected backup failure' }
    if (-not $failed -or (Get-ItemPropertyValue -LiteralPath $existing -Name Unrelated) -ne 'keep') { throw 'Backup failure path modified existing state' }
    $absentName = 'VaporViewWerAbsent_' + $id
    if (Test-Path -LiteralPath $absentRoot) { throw 'Refusing test: absent root already exists' }
    $normalName = 'VaporViewWerRestore_' + $id
    $absentValidator = $validatorSource.Replace($normalName, $absentName).Replace($beforeCrash, "throw 'injected absent-root failure'")
    $absentConfig = $configSource.Replace($normalName, $absentName)
    Set-Content -LiteralPath $validatorCopy -Value $absentValidator -Encoding UTF8
    Set-Content -LiteralPath (Join-Path $testRoot 'configure-vaporview-localdumps.ps1') -Value $absentConfig -Encoding UTF8
    $failed = $false
    try { & $validatorCopy -Helper $helperPath -TestRoot (Join-Path $testRoot 'absent-root') -Execute -ConfirmIsolatedVm } catch { if ($_.Exception.Message -notmatch 'injected absent-root failure') { throw }; $failed = $true }
    if (-not $failed -or (Test-Path -LiteralPath $absentRoot)) { throw 'Originally absent LocalDumps root was not restored' }
    Write-Host 'WER isolated restore tests passed; no HKLM or crashes used.'
} finally {
    if (Test-Path -LiteralPath $absentRoot) { Remove-Item -LiteralPath $absentRoot -Recurse -Force }
    if (Test-Path -LiteralPath $registryRoot) { Remove-Item -LiteralPath $registryRoot -Recurse -Force }
    if (Test-Path -LiteralPath $testRoot) { Remove-Item -LiteralPath $testRoot -Recurse -Force }
}
