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

function Find-OptionalRuntimeDll {
    param(
        [Parameter(Mandatory = $true)]
        [string]$DllName
    )

    $candidateDirs = [System.Collections.Generic.List[string]]::new()

    foreach ($pathEntry in ($env:PATH -split ';')) {
        if (-not [string]::IsNullOrWhiteSpace($pathEntry) -and (Test-Path -LiteralPath $pathEntry)) {
            $candidateDirs.Add($pathEntry)
        }
    }

    @(
        (Join-Path $runtimeDirResolved "..\\bin"),
        (Join-Path $runtimeDirResolved "..\\..\\bin"),
        "D:\\cursor",
        "D:\\LM Studio",
        "D:\\JetBrains\\PyCharm\\jbr\\bin",
        "C:\\Program Files\\Microsoft Visual Studio",
        "C:\\Program Files (x86)\\Windows Kits",
        "C:\\Program Files\\Windows Kits"
    ) | ForEach-Object {
        if (-not [string]::IsNullOrWhiteSpace($_) -and (Test-Path -LiteralPath $_)) {
            $candidateDirs.Add($_)
        }
    }

    foreach ($candidateDir in ($candidateDirs | Select-Object -Unique)) {
        $directCandidate = Join-Path $candidateDir $DllName
        if (Test-Path -LiteralPath $directCandidate) {
            return (Resolve-Path -LiteralPath $directCandidate).Path
        }

        $recursiveCandidate = Get-ChildItem -LiteralPath $candidateDir -Filter $DllName -Recurse -File -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($recursiveCandidate) {
            return $recursiveCandidate.FullName
        }
    }

    return $null
}

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

foreach ($optionalDll in @("dxcompiler.dll", "dxil.dll")) {
    $targetDllPath = Join-Path $buildDirResolved $optionalDll
    if (Test-Path -LiteralPath $targetDllPath) {
        continue
    }

    $sourceDllPath = Find-OptionalRuntimeDll -DllName $optionalDll
    if ($sourceDllPath) {
        Copy-Item -LiteralPath $sourceDllPath -Destination $targetDllPath -Force
    }
}
