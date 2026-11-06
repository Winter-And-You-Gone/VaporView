#Requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Helper,
    [Parameter(Mandatory=$true)][string]$TestRoot,
    [string]$Debugger,
    [ValidateRange(10, 600)][int]$TimeoutSeconds = 120,
    [switch]$Execute,
    [switch]$ConfirmIsolatedVm
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-Sha256([string]$FilePath) {
    $stream = [IO.File]::OpenRead($FilePath)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($algorithm.ComputeHash($stream)) }
    finally { $algorithm.Dispose(); $stream.Dispose() }
}

function Assert-ExpectedCrashExitCode([int]$ExitCode, [string]$Name) {
    $actual = [BitConverter]::ToUInt32([BitConverter]::GetBytes($ExitCode), 0)
    $expected = [uint32]3762443844 # 0xE0425644
    if ($actual -ne $expected) {
        throw ('Unexpected helper exit for {0}: 0x{1:X8}; expected 0xE0425644' -f $Name, $actual)
    }
}

# Both switches are intentional: this script modifies machine WER settings
# and deliberately crashes stand-in processes, so it is never a CTest action.
if (-not $Execute -or -not $ConfirmIsolatedVm) {
    throw 'Refusing to run. Explicit -Execute and -ConfirmIsolatedVm are required on an isolated Windows VM.'
}
$trustedHelper = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..\build\Release\wer_crash_helper.exe')).Path
$helperPath = (Resolve-Path -LiteralPath $Helper).Path
if ($helperPath -ine $trustedHelper) {
    throw 'Helper must be this trusted repository build/Release/wer_crash_helper.exe; arbitrary executables are refused.'
}
$trustedHash = Get-Sha256 $trustedHelper
$description = & $trustedHelper --describe
if ($LASTEXITCODE -ne 0 -or $description -ne 'VaporView WER test helper v1 7E4A9C52: wer_crash_helper.exe') {
    throw 'Trusted helper identity check failed. Rebuild it from this repository before proceeding.'
}
if ((Get-Sha256 $trustedHelper) -ne $trustedHash) { throw 'Trusted helper changed during identity verification.' }
$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'An elevated PowerShell session is required.'
}
$testPath = [IO.Path]::GetFullPath($TestRoot)
if (Test-Path -LiteralPath $testPath) { throw 'TestRoot must be a new, nonexistent directory.' }
if ($Debugger) { $Debugger = (Resolve-Path -LiteralPath $Debugger).Path }
$registryRoot = 'HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps'
$nativeRoot = 'HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps'
$registryHive = [Microsoft.Win32.Registry]::LocalMachine
$relativeRoot = 'SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps'
$rootExisted = Test-Path -LiteralPath $registryRoot
$managedValues = @('DumpFolder', 'DumpCount', 'DumpType')
$names = @('VaporView.exe', 'VaporViewSkyCore.exe', 'VaporViewSkyTui.exe')
$existingProcesses = @(Get-Process -Name @('VaporView', 'VaporViewSkyCore', 'VaporViewSkyTui') -ErrorAction SilentlyContinue)
if ($existingProcesses.Count) { throw 'Stop all production processes with these image names before starting.' }
New-Item -ItemType Directory -Path $testPath -ErrorAction Stop | Out-Null
$backups = @{}
$oldConfirm = [Environment]::GetEnvironmentVariable('VAPORVIEW_WER_TEST_CONFIRM', 'Process')
$modified = $false
try {
    foreach ($name in $names) {
        $key = Join-Path $registryRoot $name
        $exists = Test-Path -LiteralPath $key
        $backup = Join-Path $testPath ($name + '.reg')
        $acls = @()
        $values = @{}
        if ($exists) {
            $item = Get-Item -LiteralPath $key
            foreach ($valueName in $managedValues) {
                if ($item.GetValueNames() -contains $valueName) {
                    $values[$valueName] = @{ Value=$item.GetValue($valueName, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames); Kind=$item.GetValueKind($valueName) }
                }
            }
            $allKeys = @((Get-Item -LiteralPath $key)) + @(Get-ChildItem -LiteralPath $key -Recurse)
            $acls = @($allKeys | ForEach-Object { [pscustomobject]@{ Path=$_.PSPath; Sddl=$_.GetAccessControl().Sddl } })
            & reg.exe export "$nativeRoot\$name" $backup /y | Out-Null
            if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $backup)) { throw "Failed to back up $name" }
        }
        $backups[$name] = @{ Exists=$exists; File=$backup; Acls=$acls; Values=$values }
    }
    $backupManifest = foreach ($name in $names) {
        [pscustomobject]@{ Image=$name; Existed=$backups[$name].Exists; RegistryBackup=$backups[$name].File; Acls=$backups[$name].Acls }
    }
    $backupManifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $testPath 'registry-backup-manifest.json') -Encoding UTF8
    $helperHash = $trustedHash
    foreach ($name in $names) {
        $copy = Join-Path $testPath $name
        Copy-Item -LiteralPath $helperPath -Destination $copy
        if ((Get-Sha256 $copy) -ne $helperHash) { throw "Stand-in hash mismatch: $name" }
        $description = & $copy --describe
        if ($LASTEXITCODE -ne 0 -or $description -ne "VaporView WER test helper v1 7E4A9C52: $name") { throw "Stand-in identity mismatch: $name" }
    }
    $dumpRoot = Join-Path $testPath 'dumps'
    $modified = $true
    & (Join-Path $PSScriptRoot 'configure-vaporview-localdumps.ps1') -Enable -DumpFolder $dumpRoot -DumpCount 10 -DumpType 2
    & (Join-Path $PSScriptRoot 'configure-vaporview-localdumps.ps1') -Status -DumpFolder $dumpRoot -DumpCount 10 -DumpType 2
    [Environment]::SetEnvironmentVariable('VAPORVIEW_WER_TEST_CONFIRM', 'ISOLATED_VM_ONLY', 'Process')
    $results = @()
    foreach ($name in $names) {
        $started = [DateTime]::UtcNow
        $process = Start-Process -FilePath (Join-Path $testPath $name) -ArgumentList '--trigger-controlled-crash' -PassThru -WindowStyle Hidden
        try {
            if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
                throw "Crash helper timed out: $name (PID $($process.Id))"
            }
        } finally {
            if (-not $process.HasExited) {
                Stop-Process -Id $process.Id -Force -ErrorAction Stop
                if (-not $process.WaitForExit(5000)) { throw "Could not confirm helper exit: $name (PID $($process.Id))" }
            }
        }
        Assert-ExpectedCrashExitCode -ExitCode $process.ExitCode -Name $name
        $folder = Join-Path $dumpRoot ([IO.Path]::GetFileNameWithoutExtension($name))
        $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        $dump = $null
        $previousSize = -1
        while ([DateTime]::UtcNow -lt $deadline -and -not $dump) {
            $candidate = Get-ChildItem -LiteralPath $folder -Filter "$name.$($process.Id).dmp" -File | Where-Object { $_.Length -gt 0 -and $_.LastWriteTimeUtc -ge $started.AddSeconds(-2) } | Select-Object -First 1
            if ($candidate -and $candidate.Length -eq $previousSize) {
                try {
                    $stream = [IO.File]::Open($candidate.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
                    $stream.Dispose()
                    $dump = $candidate
                } catch [IO.IOException] { }
            }
            $previousSize = if ($candidate) { $candidate.Length } else { -1 }
            if (-not $dump) { Start-Sleep -Seconds 1 }
        }
        if (-not $dump) { throw "WER did not create a nonempty dump for $name" }
        $analysis = 'not run: debugger not supplied'
        if ($Debugger) {
            $analysisPath = Join-Path $testPath ($name + '.analysis.txt')
            $errorPath = Join-Path $testPath ($name + '.analysis.stderr.txt')
            $arguments = '-z "' + $dump.FullName + '" -c ".symfix; .reload; !analyze -v; .ecxr; k; q"'
            $debugProcess = Start-Process -FilePath $Debugger -ArgumentList $arguments -PassThru -WindowStyle Hidden -RedirectStandardOutput $analysisPath -RedirectStandardError $errorPath
            try {
                if (-not $debugProcess.WaitForExit($TimeoutSeconds * 1000)) { throw "Debugger timed out for $name" }
                if ($debugProcess.ExitCode -ne 0) { throw "Debugger failed for $name; see $analysisPath and $errorPath" }
            } finally {
                if (-not $debugProcess.HasExited) {
                    Stop-Process -Id $debugProcess.Id -Force -ErrorAction Stop
                    if (-not $debugProcess.WaitForExit(5000)) { throw "Could not confirm debugger exit for $name" }
                }
            }
            $analysis = Get-Content -LiteralPath $analysisPath -Raw
            if ($analysis -notmatch '(?i)e0425644' -or $analysis -notmatch '(?i)(wer_crash_helper|RaiseException|KERNELBASE)') {
                throw "Debugger output lacks expected exception or stack for $name; inspect $analysisPath"
            }
            $analysis = $analysisPath
        }
        $results += [pscustomobject]@{ Image=$name; ExitCode=$process.ExitCode; Dump=$dump.FullName; Bytes=$dump.Length; Analysis=$analysis }
    }
    $results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $testPath 'results.json') -Encoding UTF8
    $results | Format-Table -AutoSize | Out-Host
    Write-Host 'Inspect debugger output and verify full-dump type manually; test files are retained.'
} finally {
    [Environment]::SetEnvironmentVariable('VAPORVIEW_WER_TEST_CONFIRM', $oldConfirm, 'Process')
    if ($modified) {
        $restoreErrors = @()
        foreach ($name in $names) {
            try {
                $key = Join-Path $registryRoot $name
                if ($backups[$name].Exists) {
                    if (-not (Test-Path -LiteralPath $key)) { throw 'Original key disappeared; manual restore required' }
                    $item = $registryHive.OpenSubKey("$relativeRoot\$name", $true)
                    if ($null -eq $item) { throw 'Unable to open original key for restoration' }
                    try {
                        foreach ($valueName in $managedValues) {
                            if ($backups[$name].Values.ContainsKey($valueName)) {
                                $savedValue = $backups[$name].Values[$valueName]
                                $item.SetValue($valueName, $savedValue.Value, $savedValue.Kind)
                            } else {
                                $item.DeleteValue($valueName, $false)
                            }
                        }
                    } finally { $item.Dispose() }
                    foreach ($entry in $backups[$name].Acls) {
                        if ((Get-Item -LiteralPath $entry.Path).GetAccessControl().Sddl -ne $entry.Sddl) { throw "ACL changed; manual review required: $($entry.Path)" }
                    }
                    $restored = Join-Path $testPath ($name + '.restored.reg')
                    & reg.exe export "$nativeRoot\$name" $restored /y | Out-Null
                    if ($LASTEXITCODE -ne 0 -or (Get-Sha256 $restored) -ne (Get-Sha256 $backups[$name].File)) { throw 'Registry values or subkeys differ from backup' }
                } elseif (Test-Path -LiteralPath $key) {
                    $item = Get-Item -LiteralPath $key
                    $otherValues = @($item.GetValueNames() | Where-Object { $_ -notin $managedValues })
                    if ($item.SubKeyCount -gt 0 -or $otherValues.Count -gt 0) { throw 'New key contains unrelated data; retained for manual review' }
                    Remove-Item -LiteralPath $key -Force
                }
            } catch { $restoreErrors += "${name}: $_" }
        }
        if (-not $rootExisted -and (Test-Path -LiteralPath $registryRoot)) {
            try {
                $rootItem = Get-Item -LiteralPath $registryRoot
                if ($rootItem.SubKeyCount -eq 0 -and $rootItem.ValueCount -eq 0) { Remove-Item -LiteralPath $registryRoot -Force }
                else { throw 'New LocalDumps root contains other data; retained for manual review' }
            } catch { $restoreErrors += "LocalDumps root: $_" }
        }
        if ($restoreErrors.Count) { throw "WER registry restore failed. Retain VM and backups at $testPath. $($restoreErrors -join '; ')" }
    }
}
