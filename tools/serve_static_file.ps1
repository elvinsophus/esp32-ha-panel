param(
    [Parameter(Mandatory = $true)]
    [string]$FilePath,
    [int]$Port = 8000,
    [string]$Path = "/hapanel.bin"
)

$ErrorActionPreference = "Stop"

$resolved = Resolve-Path -LiteralPath $FilePath
$bytes = [System.IO.File]::ReadAllBytes($resolved)
$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Any, $Port)
$listener.Start()
Write-Output "Serving $resolved as $Path on port $Port"

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
            $requestPath = if ($parts.Length -gt 1) { $parts[1] } else { "" }

            if (($method -ne "GET" -and $method -ne "HEAD") -or $requestPath -ne $Path) {
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
