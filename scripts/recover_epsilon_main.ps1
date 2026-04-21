param(
    [string[]]$Ports = @(),
    [int[]]$Bauds = @(921600, 115200, 230400, 460800, 76800, 38400, 19200, 9600),
    [int]$ObserveSeconds = 2,
    [switch]$RebootUnsaved,
    [switch]$FactoryReset,
    [switch]$QueryConfig,
    [switch]$ListPortsOnly
)

$ErrorActionPreference = "Stop"

$Crc8Table = @(
    0,94,188,226,97,63,221,131,194,156,126,32,163,253,31,65,
    157,195,33,127,252,162,64,30,95,1,227,189,62,96,130,220,
    35,125,159,193,66,28,254,160,225,191,93,3,128,222,60,98,
    190,224,2,92,223,129,99,61,124,34,192,158,29,67,161,255,
    70,24,250,164,39,121,155,197,132,218,56,102,229,187,89,7,
    219,133,103,57,186,228,6,88,25,71,165,251,120,38,196,154,
    101,59,217,135,4,90,184,230,167,249,27,69,198,152,122,36,
    248,166,68,26,153,199,37,123,58,100,134,216,91,5,231,185,
    140,210,48,110,237,179,81,15,78,16,242,172,47,113,147,205,
    17,79,173,243,112,46,204,146,211,141,111,49,178,236,14,80,
    175,241,19,77,206,144,114,44,109,51,209,143,12,82,176,238,
    50,108,142,208,83,13,239,177,240,174,76,18,145,207,45,115,
    202,148,118,40,171,245,23,73,8,86,180,234,105,55,213,139,
    87,9,235,181,54,104,138,212,149,203,41,119,244,170,72,22,
    233,183,85,11,136,214,52,106,43,117,151,201,74,20,246,168,
    116,42,200,150,21,75,169,247,182,232,10,84,215,137,107,53
)

function New-ByteList {
    return ,(New-Object 'System.Collections.Generic.List[System.Byte]')
}

function Get-Crc8 {
    param([byte[]]$Bytes, [int]$Offset, [int]$Length)
    [int]$crc = 0
    for ($i = 0; $i -lt $Length; $i++) {
        $crc = $script:Crc8Table[($crc -bxor [int]$Bytes[$Offset + $i]) -band 0xFF]
    }
    return $crc -band 0xFF
}

function Get-Crc16 {
    param([byte[]]$Bytes, [int]$Offset, [int]$Length)
    [int]$crc = 0
    for ($i = 0; $i -lt $Length; $i++) {
        $crc = $crc -bxor (([int]$Bytes[$Offset + $i]) -shl 8)
        for ($bit = 0; $bit -lt 8; $bit++) {
            if (($crc -band 0x8000) -ne 0) {
                $crc = (($crc -shl 1) -bxor 0x1021) -band 0xFFFF
            } else {
                $crc = ($crc -shl 1) -band 0xFFFF
            }
        }
    }
    return $crc -band 0xFFFF
}

function New-EpsilonSerialPort {
    param([string]$Port, [int]$Baud)
    $serial = [System.IO.Ports.SerialPort]::new($Port, $Baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
    $serial.ReadTimeout = 50
    $serial.WriteTimeout = 1000
    $serial.Handshake = [System.IO.Ports.Handshake]::None
    $serial.DtrEnable = $true
    $serial.RtsEnable = $true
    return $serial
}

function Read-PrintableResponse {
    param([System.IO.Ports.SerialPort]$Serial, [int]$WaitMs)
    $deadline = [DateTime]::UtcNow.AddMilliseconds($WaitMs)
    $bytes = New-ByteList
    $chunk = New-Object byte[] 4096
    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            $available = $Serial.BytesToRead
            if ($available -gt 0) {
                $n = $Serial.Read($chunk, 0, [Math]::Min($chunk.Length, $available))
                for ($i = 0; $i -lt $n; $i++) { $bytes.Add($chunk[$i]) }
            } else {
                Start-Sleep -Milliseconds 20
            }
        } catch [System.TimeoutException] {
        }
    }

    $filtered = New-Object System.Text.StringBuilder
    foreach ($b in $bytes) {
        if ($b -eq 9 -or $b -eq 10 -or $b -eq 13 -or ($b -ge 32 -and $b -le 126)) {
            [void]$filtered.Append([char]$b)
        }
    }
    return $filtered.ToString()
}

function Send-EpsilonCommand {
    param(
        [System.IO.Ports.SerialPort]$Serial,
        [string]$Command,
        [int]$WaitMs = 1200
    )
    $line = "$Command`r`n"
    $bytes = [System.Text.Encoding]::ASCII.GetBytes($line)
    Write-Host "TX: $Command"
    $Serial.Write($bytes, 0, $bytes.Length)
    Start-Sleep -Milliseconds $WaitMs
    $response = Read-PrintableResponse $Serial ([Math]::Max(250, $WaitMs))
    if ($response.Trim().Length -gt 0) {
        $response.Trim() -split "(`r`n|`n|`r)" |
            Where-Object { $_.Trim().Length -gt 0 } |
            ForEach-Object { Write-Host "RX: $($_.Trim())" }
    } else {
        Write-Host "RX: <empty>"
    }
    return $response
}

function Test-HasAck {
    param([string]$Text)
    return $Text.Contains("*#OK") -or $Text.Contains("#OK") -or
        $Text.Contains("ERROR") -or $Text.Contains("error") -or
        $Text.Contains("(y/n)")
}

function Read-FdilinkSummary {
    param([System.IO.Ports.SerialPort]$Serial, [double]$Seconds)
    $buffer = New-ByteList
    $counts = @{}
    $invalid = 0
    $chunk = New-Object byte[] 4096
    $sw = [System.Diagnostics.Stopwatch]::StartNew()

    while ($sw.Elapsed.TotalSeconds -lt $Seconds) {
        try {
            $available = $Serial.BytesToRead
            if ($available -gt 0) {
                $n = $Serial.Read($chunk, 0, [Math]::Min($chunk.Length, $available))
                for ($i = 0; $i -lt $n; $i++) { $buffer.Add($chunk[$i]) }
            } else {
                Start-Sleep -Milliseconds 5
            }
        } catch [System.TimeoutException] {
        }

        while ($buffer.Count -ge 8) {
            $head = -1
            for ($i = 0; $i -lt $buffer.Count; $i++) {
                if ($buffer[$i] -eq 0xFC) { $head = $i; break }
            }
            if ($head -lt 0) {
                $buffer.Clear()
                break
            }
            if ($head -gt 0) { $buffer.RemoveRange(0, $head) }
            if ($buffer.Count -lt 8) { break }

            $payloadLen = [int]$buffer[2]
            $frameSize = $payloadLen + 8
            if ($buffer.Count -lt $frameSize) { break }

            $frame = New-Object byte[] $frameSize
            for ($i = 0; $i -lt $frameSize; $i++) { $frame[$i] = $buffer[$i] }
            $crc16 = (([int]$frame[5]) -shl 8) -bor [int]$frame[6]
            $valid = ((Get-Crc8 $frame 0 4) -eq [int]$frame[4]) -and
                     ((Get-Crc16 $frame 7 $payloadLen) -eq $crc16) -and
                     ([int]$frame[$frameSize - 1] -eq 0xFD)
            if (-not $valid) {
                $invalid++
                $buffer.RemoveAt(0)
                continue
            }

            $key = "{0:X2}" -f [int]$frame[1]
            if (-not $counts.ContainsKey($key)) { $counts[$key] = 0 }
            $counts[$key]++
            $buffer.RemoveRange(0, $frameSize)
        }
    }

    [pscustomobject]@{
        Counts = $counts
        Invalid = $invalid
        Total = ($counts.Values | Measure-Object -Sum).Sum
    }
}

function Show-FdilinkSummary {
    param($Summary, [double]$Seconds)
    if ($Summary.Total -gt 0) {
        $parts = foreach ($key in ($Summary.Counts.Keys | Sort-Object)) {
            $rate = [double]$Summary.Counts[$key] / [double]$Seconds
            "$key=$('{0:n1}' -f $rate)Hz"
        }
        Write-Host "FDILink frames: $($Summary.Total), $($parts -join ', ')"
    } else {
        Write-Host "FDILink frames: none"
    }
}

function Invoke-ConfigQuery {
    param([System.IO.Ports.SerialPort]$Serial)
    $entered = Send-EpsilonCommand $Serial "#fconfig" 1500
    if (-not (Test-HasAck $entered)) {
        Write-Host "Could not enter config mode for query."
        return
    }
    for ($i = 1; $i -le 5; $i++) {
        [void](Send-EpsilonCommand $Serial "#fparam get COMM_STREAM_TYP$i" 900)
        [void](Send-EpsilonCommand $Serial "#fparam get COMM_BAUD$i" 900)
    }
    [void](Send-EpsilonCommand $Serial "#fdeconfig" 1200)
}

function Invoke-FactoryReset {
    param([System.IO.Ports.SerialPort]$Serial)
    Write-Host "Factory reset requested. This clears user configuration."
    $entered = Send-EpsilonCommand $Serial "#fconfig" 1500
    if (-not (Test-HasAck $entered)) {
        Write-Host "Factory reset aborted: could not enter config mode."
        return $false
    }
    $reset = Send-EpsilonCommand $Serial "#freset" 2000
    if (-not (Test-HasAck $reset)) {
        Write-Host "Factory reset command did not acknowledge."
        return $false
    }
    [void](Send-EpsilonCommand $Serial "#fdeconfig" 1500)
    return $true
}

function Invoke-RebootUnsaved {
    param([System.IO.Ports.SerialPort]$Serial)
    Write-Host "Reboot requested to discard unsaved configuration."
    [void](Send-EpsilonCommand $Serial "#freboot" 1500)
    [void](Send-EpsilonCommand $Serial "y" 1000)
}

if ($Ports.Count -eq 0) {
    $Ports = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
}

$Ports = @($Ports | ForEach-Object { $_ -split "," } | ForEach-Object { $_.Trim() } | Where-Object { $_.Length -gt 0 })

if ($ListPortsOnly) {
    Write-Host "Available ports: $($Ports -join ', ')"
    exit 0
}

Write-Host "Ports: $($Ports -join ', ')"
Write-Host "Bauds: $($Bauds -join ', ')"
Write-Host "Default action: scan and send #fdeconfig only."
if ($RebootUnsaved) { Write-Host "Extra action: #freboot + y when a responsive port is found." }
if ($FactoryReset) { Write-Host "Extra action: #fconfig / #freset / #fdeconfig when a responsive port is found." }

$found = $false
foreach ($port in $Ports) {
    foreach ($baud in $Bauds) {
        Write-Host ""
        Write-Host "=== $port @ $baud ==="
        $serial = New-EpsilonSerialPort $port $baud
        try {
            $serial.Open()
            $serial.DiscardInBuffer()
            $serial.DiscardOutBuffer()
            Start-Sleep -Milliseconds 150

            $before = Read-FdilinkSummary $serial $ObserveSeconds
            Show-FdilinkSummary $before $ObserveSeconds
            if ($before.Total -gt 0) {
                Write-Host "Navigation stream is already visible on $port @ $baud."
                $found = $true
                if ($QueryConfig) { Invoke-ConfigQuery $serial }
                $serial.Close()
                break
            }

            Write-Host "Trying #fdeconfig to leave config mode..."
            $response = Send-EpsilonCommand $serial "#fdeconfig" 1500
            $after = Read-FdilinkSummary $serial $ObserveSeconds
            Show-FdilinkSummary $after $ObserveSeconds
            if ($after.Total -gt 0 -or (Test-HasAck $response)) {
                $found = $true
                if ($QueryConfig) { Invoke-ConfigQuery $serial }
                if ($RebootUnsaved) { Invoke-RebootUnsaved $serial }
                if ($FactoryReset) { [void](Invoke-FactoryReset $serial) }
                $serial.Close()
                break
            }

            if ($FactoryReset) {
                if (Invoke-FactoryReset $serial) {
                    $found = $true
                    $afterReset = Read-FdilinkSummary $serial ([Math]::Max(3, $ObserveSeconds))
                    Show-FdilinkSummary $afterReset ([Math]::Max(3, $ObserveSeconds))
                    $serial.Close()
                    break
                }
            }

            $serial.Close()
        } catch {
            Write-Host "ERROR: $($_.Exception.Message)"
            if ($serial.IsOpen) { $serial.Close() }
        }
    }
    if ($found) { break }
}

if (-not $found) {
    Write-Host ""
    Write-Host "No responsive EPSILON port was found."
    Write-Host "Power-cycle the device, then rerun this script starting with the Main port at 921600."
    exit 2
}

Write-Host ""
Write-Host "Recovery attempt finished."
