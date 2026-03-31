param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDir,

    [Parameter(Mandatory = $true)]
    [string]$RuntimeDir,

    [Parameter(Mandatory = $true)]
    [string]$BashExe
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Convert-ToMsysPath {
    param([string]$WindowsPath)

    $resolved = (Resolve-Path -LiteralPath $WindowsPath).Path
    return '/' + ($resolved -replace '\\', '/' -replace '^([A-Za-z]):', '$1')
}

function Convert-ToWindowsPath {
    param([string]$MsysPath)

    if ($MsysPath -match '^/ucrt64/(.*)$') {
        return 'C:\msys64\ucrt64\{0}' -f ($matches[1] -replace '/', '\')
    }

    if ($MsysPath -match '^/([A-Za-z])/(.*)$') {
        return '{0}:\{1}' -f $matches[1].ToUpperInvariant(), ($matches[2] -replace '/', '\')
    }

    throw "Unsupported MSYS path: $MsysPath"
}

$buildDir = (Resolve-Path -LiteralPath $BuildDir).Path
$runtimeDir = (Resolve-Path -LiteralPath $RuntimeDir).Path
$bashExe = (Resolve-Path -LiteralPath $BashExe).Path

$buildDirMsys = Convert-ToMsysPath -WindowsPath $buildDir
$runtimeDirMsys = Convert-ToMsysPath -WindowsPath $runtimeDir

$targets = Get-ChildItem -LiteralPath $buildDir -Recurse -File |
    Where-Object { $_.Extension -in '.exe', '.dll' }

$copySources = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)

foreach ($target in $targets) {
    $targetMsys = Convert-ToMsysPath -WindowsPath $target.FullName
    $bashCommand = "cd `"${buildDirMsys}`" && PATH=`"${buildDirMsys}:${runtimeDirMsys}:`$PATH`" ldd `"${targetMsys}`""
    $lddOutput = & $bashExe -lc $bashCommand 2>$null

    foreach ($line in $lddOutput) {
        if ($line -match '=>\s+(/[^ ]+)') {
            $resolvedMsysPath = $matches[1]
            if ($resolvedMsysPath -ne 'not') {
                $resolvedWindowsPath = Convert-ToWindowsPath -MsysPath $resolvedMsysPath
                if (-not $resolvedWindowsPath.StartsWith($runtimeDir, [System.StringComparison]::OrdinalIgnoreCase)) {
                    continue
                }
                [void]$copySources.Add($resolvedWindowsPath)
            }
        }
    }
}

foreach ($source in $copySources) {
    Copy-Item -LiteralPath $source -Destination (Join-Path $buildDir ([System.IO.Path]::GetFileName($source))) -Force
}
