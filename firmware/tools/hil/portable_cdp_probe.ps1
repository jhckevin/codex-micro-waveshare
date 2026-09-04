param(
  [Parameter(Mandatory = $true)][string]$Expression,
  [int]$Port = 9222
)

$target = (Invoke-RestMethod "http://127.0.0.1:$Port/json") |
  Where-Object { $_.type -eq 'page' } |
  Select-Object -First 1
if ($null -eq $target) { throw 'No Electron renderer CDP target found' }

$socket = [System.Net.WebSockets.ClientWebSocket]::new()
$socket.ConnectAsync([Uri]$target.webSocketDebuggerUrl,
                     [Threading.CancellationToken]::None).GetAwaiter().GetResult()
try {
  $request = @{
    id = 1
    method = 'Runtime.evaluate'
    params = @{
      expression = $Expression
      awaitPromise = $true
      returnByValue = $true
    }
  } | ConvertTo-Json -Depth 8 -Compress
  $bytes = [Text.Encoding]::UTF8.GetBytes($request)
  $segment = [ArraySegment[byte]]::new($bytes)
  $socket.SendAsync($segment,
                    [System.Net.WebSockets.WebSocketMessageType]::Text,
                    $true,
                    [Threading.CancellationToken]::None).GetAwaiter().GetResult()

  $buffer = New-Object byte[] 65536
  $stream = [IO.MemoryStream]::new()
  do {
    $receive = $socket.ReceiveAsync(
      [ArraySegment[byte]]::new($buffer),
      [Threading.CancellationToken]::None).GetAwaiter().GetResult()
    $stream.Write($buffer, 0, $receive.Count)
  } while (-not $receive.EndOfMessage)
  [Text.Encoding]::UTF8.GetString($stream.ToArray())
} finally {
  if ($socket.State -eq [System.Net.WebSockets.WebSocketState]::Open) {
    $socket.CloseAsync(
      [System.Net.WebSockets.WebSocketCloseStatus]::NormalClosure,
      'done',
      [Threading.CancellationToken]::None).GetAwaiter().GetResult()
  }
  $socket.Dispose()
}
