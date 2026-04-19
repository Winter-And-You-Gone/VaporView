param(
    [string]$Port = "COM1",
    [int[]]$BaudRates = @(921600, 460800, 115200),
    [int]$DurationSeconds = 3,
    [int]$ReadTimeoutMs = 50,
    [int]$MaxFramesToPrint = 12,
    [switch]$ShowFrames,
    [string[]]$Commands = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.IO.Ports

$KnownPacketNames = @{
    0x40 = "IMU"
    0x41 = "AHRS"
    0x42 = "INS_GPS"
    0x50 = "SYS_STATE"
    0x59 = "RAW_GNSS"
    0x5A = "SATELLITES"
    0x5C = "GEODETIC_POS"
    0x5D = "ECEF_POS"
}

$OfficialLengthMap = @{
    0x40 = 0x38
    0x41 = 0x30
    0x42 = 0x48
    0x50 = 0x64
    0x5C = 0x20
}

function Get-FdilinkCrc8 {
    param(
        [byte[]]$Data,
        [int]$Length
    )

    [byte[]]$table = @(
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

    [byte]$crc = 0
    for ($i = 0; $i -lt $Length; $i++) {
        $crc = $table[($crc -bxor $Data[$i])]
    }
    return $crc
}

function Get-FdilinkCrc16 {
    param(
        [byte[]]$Data,
        [int]$Offset,
        [int]$Length
    )

    [uint16[]]$table = @(
        0x0000,0x1021,0x2042,0x3063,0x4084,0x50A5,0x60C6,0x70E7,
        0x8108,0x9129,0xA14A,0xB16B,0xC18C,0xD1AD,0xE1CE,0xF1EF,
        0x1231,0x0210,0x3273,0x2252,0x52B5,0x4294,0x72F7,0x62D6,
        0x9339,0x8318,0xB37B,0xA35A,0xD3BD,0xC39C,0xF3FF,0xE3DE,
        0x2462,0x3443,0x0420,0x1401,0x64E6,0x74C7,0x44A4,0x5485,
        0xA56A,0xB54B,0x8528,0x9509,0xE5EE,0xF5CF,0xC5AC,0xD58D,
        0x3653,0x2672,0x1611,0x0630,0x76D7,0x66F6,0x5695,0x46B4,
        0xB75B,0xA77A,0x9719,0x8738,0xF7DF,0xE7FE,0xD79D,0xC7BC,
        0x48C4,0x58E5,0x6886,0x78A7,0x0840,0x1861,0x2802,0x3823,
        0xC9CC,0xD9ED,0xE98E,0xF9AF,0x8948,0x9969,0xA90A,0xB92B,
        0x5AF5,0x4AD4,0x7AB7,0x6A96,0x1A71,0x0A50,0x3A33,0x2A12,
        0xDBFD,0xCBDC,0xFBBF,0xEB9E,0x9B79,0x8B58,0xBB3B,0xAB1A,
        0x6CA6,0x7C87,0x4CE4,0x5CC5,0x2C22,0x3C03,0x0C60,0x1C41,
        0xEDAE,0xFD8F,0xCDEC,0xDDCD,0xAD2A,0xBD0B,0x8D68,0x9D49,
        0x7E97,0x6EB6,0x5ED5,0x4EF4,0x3E13,0x2E32,0x1E51,0x0E70,
        0xFF9F,0xEFBE,0xDFDD,0xCFFC,0xBF1B,0xAF3A,0x9F59,0x8F78,
        0x9188,0x81A9,0xB1CA,0xA1EB,0xD10C,0xC12D,0xF14E,0xE16F,
        0x1080,0x00A1,0x30C2,0x20E3,0x5004,0x4025,0x7046,0x6067,
        0x83B9,0x9398,0xA3FB,0xB3DA,0xC33D,0xD31C,0xE37F,0xF35E,
        0x02B1,0x1290,0x22F3,0x32D2,0x4235,0x5214,0x6277,0x7256,
        0xB5EA,0xA5CB,0x95A8,0x8589,0xF56E,0xE54F,0xD52C,0xC50D,
        0x34E2,0x24C3,0x14A0,0x0481,0x7466,0x6447,0x5424,0x4405,
        0xA7DB,0xB7FA,0x8799,0x97B8,0xE75F,0xF77E,0xC71D,0xD73C,
        0x26D3,0x36F2,0x0691,0x16B0,0x6657,0x7676,0x4615,0x5634,
        0xD94C,0xC96D,0xF90E,0xE92F,0x99C8,0x89E9,0xB98A,0xA9AB,
        0x5844,0x4865,0x7806,0x6827,0x18C0,0x08E1,0x3882,0x28A3,
        0xCB7D,0xDB5C,0xEB3F,0xFB1E,0x8BF9,0x9BD8,0xABBB,0xBB9A,
        0x4A75,0x5A54,0x6A37,0x7A16,0x0AF1,0x1AD0,0x2AB3,0x3A92,
        0xFD2E,0xED0F,0xDD6C,0xCD4D,0xBDAA,0xAD8B,0x9DE8,0x8DC9,
        0x7C26,0x6C07,0x5C64,0x4C45,0x3CA2,0x2C83,0x1CE0,0x0CC1,
        0xEF1F,0xFF3E,0xCF5D,0xDF7C,0xAF9B,0xBFBA,0x8FD9,0x9FF8,
        0x6E17,0x7E36,0x4E55,0x5E74,0x2E93,0x3EB2,0x0ED1,0x1EF0
    )

    [uint16]$crc = 0
    for ($i = 0; $i -lt $Length; $i++) {
        $index = ((($crc -shr 8) -bxor $Data[$Offset + $i]) -band 0xFF)
        $crc = ($table[$index] -bxor (($crc -shl 8) -band 0xFFFF))
    }
    return $crc
}

function Get-HexPreview {
    param(
        [byte[]]$Data,
        [int]$Count = 32
    )

    if (-not $Data -or $Data.Length -eq 0) {
        return ""
    }

    $previewCount = [Math]::Min($Count, $Data.Length)
    return (($Data[0..($previewCount - 1)] | ForEach-Object { $_.ToString("X2") }) -join " ")
}

function Format-PacketId {
    param([int]$PacketId)

    if ($KnownPacketNames.ContainsKey($PacketId)) {
        return ("0x{0:X2}({1})" -f $PacketId, $KnownPacketNames[$PacketId])
    }
    return ("0x{0:X2}" -f $PacketId)
}

function Invoke-BaudProbe {
    param(
        [string]$PortName,
        [int]$BaudRate
    )

    $serial = [System.IO.Ports.SerialPort]::new($PortName, $BaudRate, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
    $serial.ReadTimeout = $ReadTimeoutMs
    $serial.WriteTimeout = 500
    $serial.Handshake = [System.IO.Ports.Handshake]::None
    $serial.DtrEnable = $false
    $serial.RtsEnable = $false
    $serial.NewLine = "`r`n"
    $serial.ReadBufferSize = 65536
    $serial.WriteBufferSize = 8192

    $stats = [ordered]@{
        port = $PortName
        baud = $BaudRate
        total_bytes = 0
        fc_bytes = 0
        loose_frames = 0
        official_demo_frames = 0
        strict_crc_frames = 0
        tail_failures = 0
        crc8_failures = 0
        crc16_failures = 0
        first_bytes_hex = ""
        packet_counts = @{}
        errors = @()
    }

    $bytePreview = [System.Collections.Generic.List[byte]]::new()
    $buffer = [System.Collections.Generic.List[byte]]::new()
    $printedFrames = 0

    try {
        $serial.Open()
        Start-Sleep -Milliseconds 120

        foreach ($command in $Commands) {
            if ([string]::IsNullOrWhiteSpace($command)) {
                continue
            }
            $line = $command
            if (-not $line.EndsWith("`r") -and -not $line.EndsWith("`n")) {
                $line += "`r`n"
            }
            $serial.Write($line)
            Write-Host ("[{0}@{1}] TX {2}" -f $PortName, $BaudRate, $command)
            Start-Sleep -Milliseconds 150
        }

        $deadline = [DateTime]::UtcNow.AddSeconds($DurationSeconds)
        while ([DateTime]::UtcNow -lt $deadline) {
            $available = $serial.BytesToRead
            if ($available -le 0) {
                Start-Sleep -Milliseconds 10
                continue
            }

            $chunk = New-Object byte[] $available
            $readCount = $serial.Read($chunk, 0, $available)
            if ($readCount -le 0) {
                continue
            }

            for ($i = 0; $i -lt $readCount; $i++) {
                $b = $chunk[$i]
                $stats.total_bytes++
                if ($b -eq 0xFC) {
                    $stats.fc_bytes++
                }
                if ($bytePreview.Count -lt 64) {
                    $bytePreview.Add($b)
                }
                $buffer.Add($b)
            }

            while ($buffer.Count -ge 8) {
                $headIndex = -1
                for ($i = 0; $i -lt $buffer.Count; $i++) {
                    if ($buffer[$i] -eq 0xFC) {
                        $headIndex = $i
                        break
                    }
                }

                if ($headIndex -lt 0) {
                    $buffer.Clear()
                    break
                }

                if ($headIndex -gt 0) {
                    $buffer.RemoveRange(0, $headIndex)
                }

                if ($buffer.Count -lt 8) {
                    break
                }

                $packetId = [int]$buffer[1]
                $payloadLength = [int]$buffer[2]
                $frameSize = 8 + $payloadLength
                if ($frameSize -le 0 -or $frameSize -gt 4096) {
                    $buffer.RemoveAt(0)
                    continue
                }
                if ($buffer.Count -lt $frameSize) {
                    break
                }

                [byte[]]$frame = $buffer.GetRange(0, $frameSize).ToArray()
                $tailOk = $frame[$frameSize - 1] -eq 0xFD
                if (-not $tailOk) {
                    $stats.tail_failures++
                    $buffer.RemoveAt(0)
                    continue
                }

                $stats.loose_frames++
                if (-not $stats.packet_counts.Contains($packetId)) {
                    $stats.packet_counts[$packetId] = 0
                }
                $stats.packet_counts[$packetId]++

                $officialFrame = $KnownPacketNames.ContainsKey($packetId)
                if ($officialFrame -and $OfficialLengthMap.ContainsKey($packetId)) {
                    $officialFrame = ($payloadLength -eq $OfficialLengthMap[$packetId])
                }
                if ($officialFrame) {
                    $stats.official_demo_frames++
                }

                $crc8Ok = (Get-FdilinkCrc8 -Data $frame -Length 4) -eq $frame[4]
                [uint16]$headerCrc16 = (([uint16]$frame[5] -shl 8) -bor [uint16]$frame[6])
                $crc16Ok = (Get-FdilinkCrc16 -Data $frame -Offset 7 -Length $payloadLength) -eq $headerCrc16

                if (-not $crc8Ok) {
                    $stats.crc8_failures++
                }
                if (-not $crc16Ok) {
                    $stats.crc16_failures++
                }
                if ($crc8Ok -and $crc16Ok) {
                    $stats.strict_crc_frames++
                }

                if ($ShowFrames -and $printedFrames -lt $MaxFramesToPrint) {
                    Write-Host ("[{0}@{1}] frame {2}: len={3}, tail={4}, crc8={5}, crc16={6}, hex={7}" -f `
                        $PortName,
                        $BaudRate,
                        (Format-PacketId -PacketId $packetId),
                        $payloadLength,
                        $tailOk,
                        $crc8Ok,
                        $crc16Ok,
                        (Get-HexPreview -Data $frame -Count ([Math]::Min(24, $frame.Length))))
                    $printedFrames++
                }

                $buffer.RemoveRange(0, $frameSize)
            }
        }
    }
    catch {
        $stats.errors += $_.Exception.Message
    }
    finally {
        if ($serial.IsOpen) {
            $serial.Close()
        }
        $serial.Dispose()
    }

    $stats.first_bytes_hex = Get-HexPreview -Data $bytePreview.ToArray()
    return [pscustomobject]$stats
}

$ports = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object -Unique
Write-Host ("Available ports: {0}" -f (($ports) -join ", "))
if ($ports -notcontains $Port) {
    throw "Port $Port is not present in current system port list."
}

$results = @()
foreach ($baud in $BaudRates) {
    Write-Host ""
    Write-Host ("=== Probing {0} @ {1} N81 for {2}s ===" -f $Port, $baud, $DurationSeconds)
    $result = Invoke-BaudProbe -PortName $Port -BaudRate $baud
    $results += $result

    Write-Host ("bytes={0}, 0xFC={1}, loose={2}, official={3}, strict_crc={4}" -f `
        $result.total_bytes,
        $result.fc_bytes,
        $result.loose_frames,
        $result.official_demo_frames,
        $result.strict_crc_frames)
    Write-Host ("tail_fail={0}, crc8_fail={1}, crc16_fail={2}" -f `
        $result.tail_failures,
        $result.crc8_failures,
        $result.crc16_failures)
    if ($result.first_bytes_hex) {
        Write-Host ("first_bytes: {0}" -f $result.first_bytes_hex)
    }
    if ($result.packet_counts.Count -gt 0) {
        $packetSummary = $result.packet_counts.GetEnumerator() |
            Sort-Object Name |
            ForEach-Object { "{0}={1}" -f (Format-PacketId -PacketId ([int]$_.Key)), $_.Value }
        Write-Host ("packets: {0}" -f ($packetSummary -join ", "))
    }
    if ($result.errors.Count -gt 0) {
        Write-Host ("errors: {0}" -f ($result.errors -join " | "))
    }
}

$best = $results | Sort-Object -Property @{Expression = "strict_crc_frames"; Descending = $true}, @{Expression = "official_demo_frames"; Descending = $true}, @{Expression = "total_bytes"; Descending = $true} | Select-Object -First 1
Write-Host ""
Write-Host ("Best result: {0} @ {1}, bytes={2}, official={3}, strict_crc={4}" -f `
    $best.port,
    $best.baud,
    $best.total_bytes,
    $best.official_demo_frames,
    $best.strict_crc_frames)

if ($best.strict_crc_frames -gt 0 -or $best.official_demo_frames -gt 0) {
    exit 0
}
elseif ($best.total_bytes -gt 0) {
    exit 2
}
else {
    exit 3
}
