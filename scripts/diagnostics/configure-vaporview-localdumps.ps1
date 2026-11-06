#Requires -Version 5.1
[CmdletBinding()]
param(
    [switch]$Enable,
    [Alias('Remove')][switch]$Disable,
    [Alias('Check')][switch]$Status,
    [ValidateSet('VaporView.exe', 'VaporViewSkyCore.exe', 'VaporViewSkyTui.exe')]
    [string[]]$Executable = @('VaporView.exe', 'VaporViewSkyCore.exe', 'VaporViewSkyTui.exe'),
    [string]$DumpFolder = (Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'VaporView\Dumps'),
    [ValidateRange(1, 1000)][int]$DumpCount = 10,
    [ValidateSet(1, 2)][int]$DumpType = 2
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$registryRoot = 'HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps'

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-DumpStatus {
    param([string]$Name, [string]$ExpectedFolder)
    $path = Join-Path $registryRoot $Name
    $problems = @()
    if (-not (Test-Path -LiteralPath $path)) {
        $problems += 'registry key missing'
    } else {
        $key = Get-Item -LiteralPath $path
        $folder = $key.GetValue('DumpFolder', $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        $count = $key.GetValue('DumpCount', $null)
        $type = $key.GetValue('DumpType', $null)
        foreach ($entry in @(@('DumpFolder','ExpandString'), @('DumpCount','DWord'), @('DumpType','DWord'))) {
            if ($key.GetValueNames() -notcontains $entry[0] -or $key.GetValueKind($entry[0]).ToString() -ne $entry[1]) {
                $problems += "$($entry[0]) missing or wrong registry type"
            }
        }
        if ($folder -ne $ExpectedFolder) { $problems += 'DumpFolder differs from expected path' }
        if ($count -ne $DumpCount) { $problems += 'DumpCount differs from expected value' }
        if ($type -ne $DumpType) { $problems += 'DumpType differs from expected value' }
        if (-not (Test-Path -LiteralPath $ExpectedFolder -PathType Container)) { $problems += 'dump directory missing' }
    }
    [pscustomobject]@{ Executable = $Name; RegistryPath = $path; DumpFolder = $ExpectedFolder;
        Valid = ($problems.Count -eq 0); Problems = ($problems -join '; ') }
}

$modes = @(@($Enable, $Disable, $Status) | Where-Object { $_.IsPresent })
if ($modes.Count -gt 1) { throw 'Choose only one mode: -Enable, -Disable, or -Status.' }
if ($modes.Count -eq 0) { $Status = $true }
if (($Enable -or $Disable) -and -not (Test-IsAdministrator)) {
    throw 'Changing HKLM WER LocalDumps requires Administrator. Re-run from an elevated PowerShell session.'
}
$expandedFolder = [Environment]::ExpandEnvironmentVariables($DumpFolder)
if ($expandedFolder -match '%[^%]+%') {
    throw 'DumpFolder contains an unresolved environment variable for the configuring account.'
}
$baseFolder = [System.IO.Path]::GetFullPath($expandedFolder)
$results = @()
foreach ($name in ($Executable | Select-Object -Unique)) {
    $path = Join-Path $registryRoot $name
    $folder = Join-Path $baseFolder ([System.IO.Path]::GetFileNameWithoutExtension($name))
    if ($Enable) {
        New-Item -ItemType Directory -Path $folder -Force | Out-Null
        if (-not (Test-Path -LiteralPath $path)) { New-Item -Path $path -Force | Out-Null }
        New-ItemProperty -LiteralPath $path -Name DumpFolder -PropertyType ExpandString -Value $folder -Force | Out-Null
        New-ItemProperty -LiteralPath $path -Name DumpCount -PropertyType DWord -Value $DumpCount -Force | Out-Null
        New-ItemProperty -LiteralPath $path -Name DumpType -PropertyType DWord -Value $DumpType -Force | Out-Null
    }
    if ($Disable) {
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Recurse -Force }
        if (Test-Path -LiteralPath $path) { throw "Failed to remove $path" }
        Write-Host "Removed or already absent: $path"
    } else {
        $results += Get-DumpStatus -Name $name -ExpectedFolder $folder
    }
}
if (-not $Disable) {
    $results | Format-List | Out-Host
    if (@($results | Where-Object { -not $_.Valid }).Count -gt 0) {
        throw 'LocalDumps configuration is missing or differs from expected settings. Pass custom settings to -Status when checking them.'
    }
    Write-Host 'Registry settings and dump directories verified. An actual crash dump depends on Windows Error Reporting and filesystem access.'
}
