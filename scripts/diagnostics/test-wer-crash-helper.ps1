#Requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$Helper)
$ErrorActionPreference = 'Stop'
$helperPath = (Resolve-Path -LiteralPath $Helper).Path
if ([IO.Path]::GetFileName($helperPath) -ine 'wer_crash_helper.exe') {
    throw 'Safe self-test requires the unrenamed wer_crash_helper.exe.'
}
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('VaporViewWerHelper_' + [guid]::NewGuid().ToString('N'))
$saved = [Environment]::GetEnvironmentVariable('VAPORVIEW_WER_TEST_CONFIRM', 'Process')
New-Item -ItemType Directory -Path $testRoot | Out-Null
try {
    $validationScript = Join-Path $PSScriptRoot 'validate-wer-isolated-vm.ps1'
    $validatorSource = Get-Content -Raw -LiteralPath $validationScript
    $tokens = $null
    $parseErrors = $null
    $validatorAst = [System.Management.Automation.Language.Parser]::ParseInput($validatorSource, [ref]$tokens, [ref]$parseErrors)
    if ($parseErrors.Count -ne 0) { throw 'WER validator contains PowerShell syntax errors.' }
    $exitChecks = @($validatorAst.FindAll({
        param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
            $node.Name -eq 'Assert-ExpectedCrashExitCode'
    }, $true))
    if ($exitChecks.Count -ne 1 -or -not $validatorSource.Contains('Assert-ExpectedCrashExitCode -ExitCode $process.ExitCode -Name $name')) {
        throw 'WER validator is missing its helper exit-code check.'
    }
    . ([scriptblock]::Create($exitChecks[0].Extent.Text))
    Assert-ExpectedCrashExitCode -ExitCode -532523452 -Name 'test-helper.exe'
    foreach ($unexpected in @(0, 2, -1073741819)) {
        $rejected = $false
        try { Assert-ExpectedCrashExitCode -ExitCode $unexpected -Name 'test-helper.exe' }
        catch { $rejected = $_.Exception.Message -like '*Unexpected helper exit*expected 0xE0425644*' }
        if (-not $rejected) { throw "Unexpected helper exit code was not rejected: $unexpected" }
    }
    $shellPath = (Get-Process -Id $PID).Path
    foreach ($fixtureCode in @(-532523452, 2)) {
        $fixture = Start-Process -FilePath $shellPath -ArgumentList @('-NoProfile', '-NonInteractive', '-Command', "exit $fixtureCode") -PassThru -WindowStyle Hidden
        try {
            if (-not $fixture.WaitForExit(10000)) { throw "Exit-code fixture timed out: $fixtureCode" }
            if ($fixture.ExitCode -ne $fixtureCode) { throw "Exit-code fixture returned $($fixture.ExitCode), expected $fixtureCode" }
            if ($fixtureCode -eq -532523452) {
                Assert-ExpectedCrashExitCode -ExitCode $fixture.ExitCode -Name 'test-helper.exe'
            } else {
                $rejected = $false
                try { Assert-ExpectedCrashExitCode -ExitCode $fixture.ExitCode -Name 'test-helper.exe' }
                catch { $rejected = $_.Exception.Message -like '*Unexpected helper exit*expected 0xE0425644*' }
                if (-not $rejected) { throw 'Unexpected subprocess exit code was not rejected.' }
            }
        } finally {
            if (-not $fixture.HasExited) {
                $fixture.Kill()
                $fixture.WaitForExit()
            }
            $fixture.Dispose()
        }
    }
    $refusalRoot = Join-Path $testRoot 'must-not-be-created'
    foreach ($mode in @('neither', 'execute', 'confirm')) {
        $refused = $false
        try {
            if ($mode -eq 'execute') { & $validationScript -Helper $helperPath -TestRoot $refusalRoot -Execute }
            elseif ($mode -eq 'confirm') { & $validationScript -Helper $helperPath -TestRoot $refusalRoot -ConfirmIsolatedVm }
            else { & $validationScript -Helper $helperPath -TestRoot $refusalRoot }
        } catch {
            if ($_.Exception.Message -notlike '*Explicit -Execute and -ConfirmIsolatedVm*') { throw }
            $refused = $true
        }
        if (-not $refused -or (Test-Path -LiteralPath $refusalRoot)) { throw "WER validation refusal failed: $mode" }
    }
    $untrustedDir = Join-Path $testRoot 'untrusted'
    New-Item -ItemType Directory -Path $untrustedDir | Out-Null
    $untrustedHelper = Join-Path $untrustedDir 'wer_crash_helper.exe'
    Set-Content -LiteralPath $untrustedHelper -Value 'not an executable' -Encoding ASCII
    $refused = $false
    try {
        & $validationScript -Helper $untrustedHelper -TestRoot $refusalRoot -Execute -ConfirmIsolatedVm
    } catch {
        if ($_.Exception.Message -notlike '*arbitrary executables are refused*') { throw }
        $refused = $true
    }
    if (-not $refused -or (Test-Path -LiteralPath $refusalRoot)) { throw 'Untrusted executable was not refused before side effects' }
    [Environment]::SetEnvironmentVariable('VAPORVIEW_WER_TEST_CONFIRM', $null, 'Process')
    foreach ($name in @('VaporView.exe', 'VaporViewSkyCore.exe', 'VaporViewSkyTui.exe')) {
        $copy = Join-Path $testRoot $name
        Copy-Item -LiteralPath $helperPath -Destination $copy
        $description = & $copy --describe
        if ($LASTEXITCODE -ne 0 -or $description -ne "VaporView WER test helper v1 7E4A9C52: $name") {
            throw "Helper identity self-check failed: $name"
        }
        & $copy --trigger-controlled-crash 2>$null
        if ($LASTEXITCODE -ne 2) { throw "Helper did not refuse without explicit confirmation: $name" }
    }
    [Environment]::SetEnvironmentVariable('VAPORVIEW_WER_TEST_CONFIRM', 'ISOLATED_VM_ONLY', 'Process')
    & $helperPath --trigger-controlled-crash 2>$null
    if ($LASTEXITCODE -ne 2) { throw 'Unrenamed helper must refuse the crash request' }
    & (Join-Path $PSScriptRoot 'test-wer-validation-restore.ps1') -Helper $helperPath
    Write-Host 'WER helper safe self-test passed; no crash or WER registry write was performed.'
} finally {
    [Environment]::SetEnvironmentVariable('VAPORVIEW_WER_TEST_CONFIRM', $saved, 'Process')
    if (Test-Path -LiteralPath $testRoot) { Remove-Item -LiteralPath $testRoot -Recurse -Force }
}
