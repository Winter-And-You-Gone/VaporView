param(
    [string]$PythonExe = $env:VAPORVIEW_PYTHON_EXE,
    [ValidateRange(1, 65535)][int]$Port = 8765,
    [switch]$NoBrowser
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$pythonArguments = @()
if ([string]::IsNullOrWhiteSpace($PythonExe)) {
    $launcher = Get-Command py.exe -ErrorAction SilentlyContinue
    if ($launcher) {
        $PythonExe = $launcher.Source
        $pythonArguments = @('-3')
    } else {
        $python = Get-Command python.exe -ErrorAction SilentlyContinue
        if (!$python -or $python.Source -like '*\Microsoft\WindowsApps\*') {
            throw 'Python 3.10+ is required. Install Python or pass -PythonExe <path-to-python.exe>.'
        }
        $PythonExe = $python.Source
    }
}
$server = Join-Path $repoRoot 'tools\gateway_demo\server.py'
$client = Join-Path $repoRoot 'build\Release\gateway_service_test.exe'
if (!(Test-Path -LiteralPath $client -PathType Leaf)) {
    throw 'Build gateway_service_test in build/Release first; see tools/gateway_demo/README.md.'
}
$serverArguments = @($server, '--port', $Port, '--client', $client)
if (!$NoBrowser) { $serverArguments += '--open-browser' }
& $PythonExe @pythonArguments @serverArguments
exit $LASTEXITCODE
