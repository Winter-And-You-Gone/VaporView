#Requires -Version 5.1

[CmdletBinding()]
param(
    [switch]$Enable,
    [switch]$Disable,
    [switch]$Status,
    [string]$DumpFolder = (Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'VaporView\Dumps'),
    [ValidateRange(1, 1000)]
    [int]$DumpCount = 10,
    [ValidateSet(1, 2)]
    [int]$DumpType = 2
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$registryPath = 'HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\VaporView.exe'

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-RegistryValueOrMissing {
    param(
        [Parameter(Mandatory = $true)]
        [psobject]$Item,
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    $property = $Item.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) {
        return '<missing>'
    }
    return $property.Value
}

function Show-LocalDumpStatus {
    if (-not (Test-Path -LiteralPath $registryPath)) {
        Write-Host 'VaporView.exe LocalDumps is not configured.'
        return
    }

    $item = Get-ItemProperty -LiteralPath $registryPath
    $dumpFolderValue = Get-RegistryValueOrMissing -Item $item -Name 'DumpFolder'
    $dumpCountValue = Get-RegistryValueOrMissing -Item $item -Name 'DumpCount'
    $dumpTypeValue = Get-RegistryValueOrMissing -Item $item -Name 'DumpType'
    $configured = if ($dumpFolderValue -eq '<missing>' -or
                      $dumpCountValue -eq '<missing>' -or
                      $dumpTypeValue -eq '<missing>') {
        'partial'
    }
    else {
        'full'
    }

    [pscustomobject]@{
        Configured   = $configured
        RegistryPath = $registryPath
        DumpFolder   = $dumpFolderValue
        DumpCount    = $dumpCountValue
        DumpType     = $dumpTypeValue
    } | Format-List
}

$selectedModes = @()
if ($Enable.IsPresent) { $selectedModes += 'Enable' }
if ($Disable.IsPresent) { $selectedModes += 'Disable' }
if ($Status.IsPresent) { $selectedModes += 'Status' }
if ($selectedModes.Count -gt 1) {
    throw 'Choose only one mode: -Enable, -Disable, or -Status.'
}
if ($selectedModes.Count -eq 0) {
    $Status = $true
}

if ($Status) {
    Show-LocalDumpStatus
    return
}

if (-not (Test-IsAdministrator)) {
    Write-Error 'Changing HKLM WER LocalDumps requires Administrator. Re-run this script from an elevated PowerShell session.'
    exit 1
}

if ($Enable) {
    $resolvedDumpFolder = [System.IO.Path]::GetFullPath($DumpFolder)
    New-Item -ItemType Directory -Path $resolvedDumpFolder -Force | Out-Null
    New-Item -Path $registryPath -Force | Out-Null
    New-ItemProperty -LiteralPath $registryPath -Name DumpFolder -PropertyType ExpandString -Value $resolvedDumpFolder -Force | Out-Null
    New-ItemProperty -LiteralPath $registryPath -Name DumpCount -PropertyType DWord -Value $DumpCount -Force | Out-Null
    New-ItemProperty -LiteralPath $registryPath -Name DumpType -PropertyType DWord -Value $DumpType -Force | Out-Null

    Write-Host "Enabled VaporView.exe LocalDumps at $registryPath"
    Show-LocalDumpStatus
    return
}

if ($Disable) {
    if (Test-Path -LiteralPath $registryPath) {
        Remove-Item -LiteralPath $registryPath -Recurse -Force
        Write-Host "Removed VaporView.exe LocalDumps from $registryPath"
    }
    else {
        Write-Host 'VaporView.exe LocalDumps was already not configured.'
    }
    return
}