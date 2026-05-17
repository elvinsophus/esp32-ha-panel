param(
    [string]$Topic = "",
    [int]$TimeoutSeconds = 8,
    [switch]$Json
)

$ErrorActionPreference = "Stop"

function Get-HAPanelConfig {
    param(
        [string]$Name,
        [string]$Default = ""
    )

    foreach ($file in @("sdkconfig", "sdkconfig.defaults.local")) {
        if (Test-Path $file) {
            $pattern = "^$Name=`"(.*)`"$"
            $match = Select-String -Path $file -Pattern $pattern | Select-Object -Last 1
            if ($match) {
                return $match.Matches[0].Groups[1].Value
            }
        }
    }

    return $Default
}

function New-MqttString {
    param([string]$Value)

    $bytes = [System.Text.Encoding]::UTF8.GetBytes($Value)
    return [byte[]](@((($bytes.Length -shr 8) -band 255), ($bytes.Length -band 255)) + $bytes)
}

function New-MqttRemainingLength {
    param([int]$Value)

    $bytes = @()
    do {
        $encoded = $Value % 128
        $Value = [Math]::Floor($Value / 128)
        if ($Value -gt 0) {
            $encoded = $encoded -bor 128
        }
        $bytes += $encoded
    } while ($Value -gt 0)

    return [byte[]]$bytes
}

function Join-Bytes {
    param([object[]]$Parts)

    $length = 0
    foreach ($part in $Parts) {
        $length += $part.Length
    }

    $output = [byte[]]::new($length)
    $offset = 0
    foreach ($part in $Parts) {
        [Array]::Copy($part, 0, $output, $offset, $part.Length)
        $offset += $part.Length
    }

    return $output
}

function New-MqttPacket {
    param(
        [int]$Type,
        [byte[]]$Body = [byte[]]@()
    )

    return Join-Bytes @([byte[]]@($Type), (New-MqttRemainingLength $Body.Length), $Body)
}

function Read-Exact {
    param(
        [System.Net.Sockets.NetworkStream]$Stream,
        [int]$Length
    )

    $buffer = [byte[]]::new($Length)
    $offset = 0
    while ($offset -lt $Length) {
        $read = $Stream.Read($buffer, $offset, $Length - $offset)
        if ($read -le 0) {
            throw "MQTT stream closed while reading"
        }
        $offset += $read
    }

    return $buffer
}

function Read-MqttPacket {
    param([System.Net.Sockets.NetworkStream]$Stream)

    $header = Read-Exact $Stream 1
    $multiplier = 1
    $remaining = 0
    do {
        $encoded = (Read-Exact $Stream 1)[0]
        $remaining += ($encoded -band 127) * $multiplier
        $multiplier *= 128
        if ($multiplier -gt 2097152) {
            throw "Malformed MQTT remaining length"
        }
    } while (($encoded -band 128) -ne 0)

    $body = if ($remaining -gt 0) { Read-Exact $Stream $remaining } else { [byte[]]@() }
    return @{
        Type = $header[0]
        Body = $body
    }
}

function Send-MqttPacket {
    param(
        [System.Net.Sockets.NetworkStream]$Stream,
        [byte[]]$Packet
    )

    $Stream.Write($Packet, 0, $Packet.Length)
}

$brokerUri = Get-HAPanelConfig "CONFIG_HAPANEL_MQTT_BROKER_URI"
if ($brokerUri -eq "") {
    throw "CONFIG_HAPANEL_MQTT_BROKER_URI is not configured"
}

if ($Topic -eq "") {
    $Topic = Get-HAPanelConfig "CONFIG_HAPANEL_MQTT_DEVICE_STATE_TOPIC" "hapanel/device/state"
}

$uri = [Uri]$brokerUri
$hostName = $uri.Host
$port = if ($uri.Port -gt 0) { $uri.Port } else { 1883 }
$username = Get-HAPanelConfig "CONFIG_HAPANEL_MQTT_USERNAME"
$password = Get-HAPanelConfig "CONFIG_HAPANEL_MQTT_PASSWORD"
$clientId = (Get-HAPanelConfig "CONFIG_HAPANEL_MQTT_CLIENT_ID" "hapanel") + "-topic-dump"

$connectFlags = 2
if ($username -ne "") {
    $connectFlags = $connectFlags -bor 128
}
if ($password -ne "") {
    $connectFlags = $connectFlags -bor 64
}

$connectParts = @(
    (New-MqttString "MQTT"),
    [byte[]]@(4, $connectFlags, 0, 60),
    (New-MqttString $clientId)
)
if ($username -ne "") {
    $connectParts += (New-MqttString $username)
}
if ($password -ne "") {
    $connectParts += (New-MqttString $password)
}

$tcp = [Net.Sockets.TcpClient]::new()
$tcp.ReceiveTimeout = $TimeoutSeconds * 1000
$tcp.SendTimeout = $TimeoutSeconds * 1000
$tcp.Connect($hostName, $port)
$stream = $tcp.GetStream()

try {
    Send-MqttPacket $stream (New-MqttPacket 0x10 (Join-Bytes $connectParts))
    $connack = Read-MqttPacket $stream
    if ($connack.Type -ne 0x20 -or $connack.Body.Length -lt 2 -or $connack.Body[1] -ne 0) {
        throw "MQTT CONNACK failed"
    }

    $packetId = [byte[]]@(0, 1)
    $subscribeBody = Join-Bytes @($packetId, (New-MqttString $Topic), [byte[]]@(0))
    Send-MqttPacket $stream (New-MqttPacket 0x82 $subscribeBody)

    do {
        $packet = Read-MqttPacket $stream
    } while ($packet.Type -ne 0x90)

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $packet = Read-MqttPacket $stream
        if (($packet.Type -band 0xF0) -ne 0x30) {
            continue
        }

        $body = $packet.Body
        if ($body.Length -lt 2) {
            continue
        }

        $topicLength = ($body[0] -shl 8) -bor $body[1]
        if ($body.Length -lt (2 + $topicLength)) {
            continue
        }

        $topicText = [System.Text.Encoding]::UTF8.GetString($body, 2, $topicLength)
        if ($topicText -ne $Topic) {
            continue
        }

        $payloadOffset = 2 + $topicLength
        $payloadText = [System.Text.Encoding]::UTF8.GetString($body, $payloadOffset, $body.Length - $payloadOffset)
        if ($Json) {
            Write-Output ($payloadText | ConvertFrom-Json | ConvertTo-Json -Depth 16)
        } else {
            Write-Output $payloadText
        }
        return
    }

    throw "Timed out waiting for payload on $Topic"
} finally {
    if ($stream) {
        Send-MqttPacket $stream (New-MqttPacket 0xE0)
        $stream.Close()
    }
    $tcp.Close()
}
