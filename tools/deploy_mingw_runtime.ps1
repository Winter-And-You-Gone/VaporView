[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDir,

    [Parameter(Mandatory = $true)]
    [string]$RuntimeDir
)

$ErrorActionPreference = "Stop"

$buildDirResolved = (Resolve-Path -LiteralPath $BuildDir).Path
$runtimeDirResolved = (Resolve-Path -LiteralPath $RuntimeDir).Path
$objdumpExe = Join-Path $runtimeDirResolved "objdump.exe"

if (-not (Test-Path -LiteralPath $objdumpExe)) {
    throw "objdump.exe was not found in runtime directory: $runtimeDirResolved"
}

$systemDlls = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
@(
    "KERNEL32.dll",
    "USER32.dll",
    "GDI32.dll",
    "ADVAPI32.dll",
    "SHELL32.dll",
    "OLE32.dll",
    "OLEAUT32.dll",
    "COMDLG32.dll",
    "COMCTL32.dll",
    "WS2_32.dll",
    "WINMM.dll",
    "IMM32.dll",
    "DWMAPI.dll",
    "SETUPAPI.dll",
    "VERSION.dll",
    "UXTHEME.dll",
    "SHLWAPI.dll",
    "CRYPT32.dll",
    "DNSAPI.dll",
    "IPHLPAPI.dll",
    "MSWSOCK.dll",
    "NETAPI32.dll",
    "USERENV.dll",
    "BCRYPT.dll",
    "NSI.dll",
    "WINNSI.dll",
    "DBGHELP.dll",
    "D3D11.dll",
    "D3DCOMPILER_47.dll",
    "DXGI.dll",
    "OPENGL32.dll"
) | ForEach-Object { [void]$systemDlls.Add($_) }

function Get-ImportedDllNames {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BinaryPath
    )

    $dllNames = [System.Collections.Generic.List[string]]::new()
    $output = & $objdumpExe -p $BinaryPath
    if ($LASTEXITCODE -ne 0) {
        throw "objdump failed for $BinaryPath with exit code $LASTEXITCODE."
    }

    foreach ($line in $output) {
        if ($line -match "DLL Name:\s+(.+)$") {
            $dllName = $matches[1].Trim()
            if (-not [string]::IsNullOrWhiteSpace($dllName)) {
                $dllNames.Add($dllName)
            }
        }
    }

    return $dllNames
}

$queue = [System.Collections.Generic.Queue[string]]::new()
$scanned = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)

Get-ChildItem -LiteralPath $buildDirResolved -Recurse -File | Where-Object {
    $_.Extension -in @(".exe", ".dll")
} | ForEach-Object {
    $queue.Enqueue($_.FullName)
}

while ($queue.Count -gt 0) {
    $binaryPath = $queue.Dequeue()
    if (-not $scanned.Add($binaryPath)) {
        continue
    }

    foreach ($dllName in Get-ImportedDllNames -BinaryPath $binaryPath) {
        if ($systemDlls.Contains($dllName) -or $dllName.StartsWith("api-ms-win-", [System.StringComparison]::OrdinalIgnoreCase)) {
            continue
        }

        $targetDllPath = Join-Path $buildDirResolved $dllName
        $runtimeDllPath = Join-Path $runtimeDirResolved $dllName

        if (Test-Path -LiteralPath $targetDllPath) {
            $queue.Enqueue($targetDllPath)
            continue
        }

        if (Test-Path -LiteralPath $runtimeDllPath) {
            Copy-Item -LiteralPath $runtimeDllPath -Destination $targetDllPath -Force
            $queue.Enqueue($targetDllPath)
        }
    }
}
