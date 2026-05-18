param(
    [string]$FilePath = "build\hapanel.bin",
    [string]$HostIp = "",
    [int]$Port = 8000,
    [string]$Path = "/hapanel.bin",
    [int]$TimeoutSeconds = 120,
    [int]$PollSeconds = 3,
    [string]$ExpectedVersion = "",
    [switch]$NoReboot
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

function Get-LocalHostIp {
    $brokerUri = Get-HAPanelConfig "CONFIG_HAPANEL_MQTT_BROKER_URI"
    $preferredPrefix = ""
    if ($brokerUri -ne "") {
        $brokerHost = ([Uri]$brokerUri).Host
        if ($brokerHost -match "^(\d+\.\d+\.\d+)\.\d+$") {
            $preferredPrefix = $Matches[1] + "."
        }
    }

    $addresses = @()
    $ipconfig = ipconfig
    foreach ($line in $ipconfig) {
        if ($line -match "IPv4.*:\s*([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+)") {
            $address = $Matches[1]
            if ($address -ne "127.0.0.1") {
                $addresses += $address
            }
        }
    }

    if ($preferredPrefix -ne "") {
        $match = $addresses | Where-Object { $_.StartsWith($preferredPrefix) } | Select-Object -First 1
        if ($match) {
            return $match
        }
    }

    $private = $addresses | Where-Object {
        $_.StartsWith("192.168.") -or $_.StartsWith("10.") -or $_ -match "^172\.(1[6-9]|2[0-9]|3[0-1])\."
    } | Select-Object -First 1
    if ($private) {
        return $private
    }

    throw "Unable to infer a local IPv4 address. Pass -HostIp explicitly."
}

function Invoke-JsonTool {
    param([scriptblock]$Command)

    $output = & $Command
    $text = ($output | Out-String).Trim()
    if ($text -eq "") {
        throw "Command returned no JSON"
    }

    return $text | ConvertFrom-Json
}

function Get-DeviceState {
    return Invoke-JsonTool {
        & ".\tools\mqtt_topic_dump.ps1" -Json -TimeoutSeconds 10
    }
}

function Get-DeviceStatus {
    return Invoke-JsonTool {
        & ".\tools\mqtt_topic_dump.ps1" -Topic (Get-HAPanelConfig "CONFIG_HAPANEL_MQTT_DEVICE_STATUS_TOPIC" "hapanel/device/status") -Json -TimeoutSeconds 10
    }
}

function Invoke-PanelCommand {
    param(
        [string]$Id,
        [hashtable]$Body,
        [string]$ExpectStatus = "accepted",
        [int]$CommandTimeoutSeconds = 20
    )

    $payload = $Body | ConvertTo-Json -Compress
    return Invoke-JsonTool {
        & ".\tools\mqtt_command_test.ps1" `
            -Id $Id `
            -Payload $payload `
            -ExpectStatus $ExpectStatus `
            -TimeoutSeconds $CommandTimeoutSeconds
    }
}

function Start-StaticFileJob {
    param(
        [string]$ResolvedFilePath,
        [int]$ListenPort,
        [string]$RequestPath
    )

    return Start-Job -ArgumentList $ResolvedFilePath, $ListenPort, $RequestPath -ScriptBlock {
        param($ResolvedFilePath, $ListenPort, $RequestPath)

        $ErrorActionPreference = "Stop"
        $bytes = [System.IO.File]::ReadAllBytes($ResolvedFilePath)
        $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Any, $ListenPort)
        $listener.Start()

        try {
            while ($true) {
                $client = $listener.AcceptTcpClient()
                try {
                    $stream = $client.GetStream()
                    $buffer = [byte[]]::new(4096)
                    $read = $stream.Read($buffer, 0, $buffer.Length)
                    if ($read -le 0) {
                        $client.Close()
                        continue
                    }

                    $request = [System.Text.Encoding]::ASCII.GetString($buffer, 0, $read)
                    $firstLine = ($request -split "`r?`n", 2)[0]
                    $parts = $firstLine -split " "
                    $method = if ($parts.Length -gt 0) { $parts[0] } else { "" }
                    $requestTarget = if ($parts.Length -gt 1) { $parts[1] } else { "" }

                    if (($method -ne "GET" -and $method -ne "HEAD") -or $requestTarget -ne $RequestPath) {
                        $body = [System.Text.Encoding]::ASCII.GetBytes("Not found")
                        $header = "HTTP/1.1 404 Not Found`r`nContent-Length: $($body.Length)`r`nConnection: close`r`n`r`n"
                        $headerBytes = [System.Text.Encoding]::ASCII.GetBytes($header)
                        $stream.Write($headerBytes, 0, $headerBytes.Length)
                        if ($method -ne "HEAD") {
                            $stream.Write($body, 0, $body.Length)
                        }
                        $client.Close()
                        continue
                    }

                    $okHeader = "HTTP/1.1 200 OK`r`nContent-Type: application/octet-stream`r`nContent-Length: $($bytes.Length)`r`nConnection: close`r`n`r`n"
                    $okHeaderBytes = [System.Text.Encoding]::ASCII.GetBytes($okHeader)
                    $stream.Write($okHeaderBytes, 0, $okHeaderBytes.Length)
                    if ($method -ne "HEAD") {
                        $stream.Write($bytes, 0, $bytes.Length)
                    }
                    $client.Close()
                } catch {
                    try {
                        $client.Close()
                    } catch {
                    }
                }
            }
        } finally {
            $listener.Stop()
        }
    }
}

function Wait-ForState {
    param(
        [scriptblock]$Predicate,
        [string]$Description
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $lastPhase = ""
    while ((Get-Date) -lt $deadline) {
        $state = Get-DeviceState
        $phase = $state.ota.progress.phase
        $percent = $state.ota.progress.percent
        $running = $state.ota.inventory.running.label
        $boot = $state.ota.inventory.boot.label
        $rebootRequired = $state.ota.inventory.reboot_required

        $summary = "phase=$phase percent=$percent running=$running boot=$boot reboot_required=$rebootRequired"
        if ($summary -ne $lastPhase) {
            Write-Output "State: $summary"
            $lastPhase = $summary
        }

        if (& $Predicate $state) {
            return $state
        }

        Start-Sleep -Seconds $PollSeconds
    }

    throw "Timed out waiting for $Description"
}

$resolved = Resolve-Path -LiteralPath $FilePath
if ($HostIp -eq "") {
    $HostIp = Get-LocalHostIp
}

$initialState = Get-DeviceState
$initialRunning = $initialState.ota.inventory.running.label
$initialTarget = $initialState.ota.preflight.target
if (-not $initialState.ota.preflight.allowed) {
    throw "OTA preflight is not allowed: $($initialState.ota.preflight.reason)"
}

$url = "http://${HostIp}:${Port}${Path}"
Write-Output "Initial OTA state: running=$initialRunning target=$initialTarget"
Write-Output "Serving $resolved at $url"

$serverJob = $null
try {
    $serverJob = Start-StaticFileJob -ResolvedFilePath $resolved -ListenPort $Port -RequestPath $Path
    Start-Sleep -Seconds 1
    if ($serverJob.State -ne "Running") {
        Receive-Job $serverJob | Write-Output
        throw "Static file server did not start"
    }

    $updateId = "ota-update-" + ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $updateResult = Invoke-PanelCommand `
        -Id $updateId `
        -Body @{ id = $updateId; command = "ota_update"; url = $url } `
        -ExpectStatus "accepted" `
        -CommandTimeoutSeconds 30
    Write-Output "Update command: $($updateResult.status) ($($updateResult.reason))"

    $stagedState = Wait-ForState `
        -Description "staged OTA image" `
        -Predicate {
            param($state)
            return $state.ota.inventory.reboot_required -eq $true -and
                   $state.ota.progress.phase -eq "staged" -and
                   [int]$state.ota.progress.percent -eq 100
        }

    Write-Output "Staged OTA image: running=$($stagedState.ota.inventory.running.label) boot=$($stagedState.ota.inventory.boot.label)"

    if ($NoReboot) {
        Write-Output "Skipping ota_reboot because -NoReboot was set."
        return
    }

    $rebootId = "ota-reboot-" + ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $rebootResult = Invoke-PanelCommand `
        -Id $rebootId `
        -Body @{ id = $rebootId; command = "ota_reboot" } `
        -ExpectStatus "accepted" `
        -CommandTimeoutSeconds 15
    Write-Output "Reboot command: $($rebootResult.status) ($($rebootResult.reason))"

    Start-Sleep -Seconds 8
    $finalState = Wait-ForState `
        -Description "post-OTA boot validation" `
        -Predicate {
            param($state)
            return $state.ota.inventory.running.label -eq $initialTarget -and
                   $state.ota.inventory.boot_matches_running -eq $true -and
                   $state.ota.inventory.reboot_required -eq $false
        }

    $status = Get-DeviceStatus
    if ($ExpectedVersion -ne "" -and $status.app_version -ne $ExpectedVersion) {
        throw "Expected app_version $ExpectedVersion, got $($status.app_version)"
    }

    Write-Output "OTA round-trip passed: $initialRunning -> $($finalState.ota.inventory.running.label), app_version=$($status.app_version)"
} finally {
    if ($serverJob) {
        Stop-Job $serverJob -ErrorAction SilentlyContinue
        Remove-Job $serverJob -Force -ErrorAction SilentlyContinue
    }
}
