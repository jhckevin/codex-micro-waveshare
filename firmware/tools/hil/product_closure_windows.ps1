param(
  [string]$Port = "COM5",
  [int]$StressSeconds = 30
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Receive-Hil {
  param([System.IO.Ports.SerialPort]$Serial, [string]$Expected, [int]$TimeoutMs = 3000)
  $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
  while ([DateTime]::UtcNow -lt $deadline) {
    try { $line = $Serial.ReadLine().Trim() } catch [TimeoutException] { continue }
    $marker = $line.IndexOf('@hil:')
    if ($marker -lt 0) { continue }
    $payload = $line.Substring($marker + 5) | ConvertFrom-Json
    if ($payload.ok -eq $false) { throw "HIL error: $line" }
    if ($payload.PSObject.Properties.Name -contains $Expected) { return $payload }
  }
  throw "Timed out waiting for HIL field '$Expected'"
}

function Send-Hil {
  param(
    [System.IO.Ports.SerialPort]$Serial,
    [string]$Command,
    [string]$Expected = "accepted",
    [int]$TimeoutMs = 3000
  )
  $Serial.Write(($Command + "`n"))
  return Receive-Hil -Serial $Serial -Expected $Expected -TimeoutMs $TimeoutMs
}

$serial = [System.IO.Ports.SerialPort]::new($Port, 115200, 'None', 8, 'One')
$serial.ReadTimeout = 100
$serial.WriteTimeout = 1000
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$serial.NewLine = "`n"
$serial.Open()

try {
  $ready = $false
  $bootDeadline = [DateTime]::UtcNow.AddSeconds(15)
  while (-not $ready -and [DateTime]::UtcNow -lt $bootDeadline) {
    try {
      $null = Send-Hil -Serial $serial -Command '@hil ping' -Expected 'pong' -TimeoutMs 900
      $ready = $true
    } catch { Start-Sleep -Milliseconds 150 }
  }
  if (-not $ready) { throw 'Board did not expose HIL within 15 seconds' }

  $initial = (Send-Hil $serial '@hil snapshot' 'snapshot').snapshot
  $initialMetrics = (Send-Hil $serial '@hil metrics' 'metrics').metrics
  if ([int]$initial.power -ne 0) {
    Send-Hil $serial '@hil power' | Out-Null
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
      Start-Sleep -Milliseconds 50
      $initial = (Send-Hil $serial '@hil snapshot' 'snapshot').snapshot
    } while ([int]$initial.power -ne 0 -and [DateTime]::UtcNow -lt $deadline)
    if ([int]$initial.power -ne 0) { throw 'Unable to normalize device to active power mode' }
  }
  if ([int]$initial.layer -ne 1) {
    Send-Hil $serial '@hil layer 1' | Out-Null
    Start-Sleep -Milliseconds 100
    $initial = (Send-Hil $serial '@hil snapshot' 'snapshot').snapshot
    if ([int]$initial.layer -ne 1) { throw 'Unable to normalize device to Layer 1' }
  }

  Send-Hil $serial '@hil touch down 0 237 384' | Out-Null
  Start-Sleep -Milliseconds 250
  $micDown = (Send-Hil $serial '@hil snapshot' 'snapshot').snapshot
  Start-Sleep -Milliseconds 1200
  $micHeld = (Send-Hil $serial '@hil snapshot' 'snapshot').snapshot
  Send-Hil $serial '@hil touch up 0' | Out-Null
  Start-Sleep -Milliseconds 150
  $micUp = (Send-Hil $serial '@hil snapshot' 'snapshot').snapshot

  Send-Hil $serial '@hil app connect' | Out-Null
  Send-Hil $serial '@hil routing layers 3' | Out-Null
  Send-Hil $serial '@hil routing layer1 4 2' | Out-Null
  Send-Hil $serial '@hil routing passthrough 2 agent 0x02' | Out-Null
  Send-Hil $serial '@hil layer 2' | Out-Null
  Send-Hil $serial '@hil key down ag00' | Out-Null
  Start-Sleep -Milliseconds 100
  $routeHeld = (Send-Hil $serial '@hil snapshot' 'snapshot').snapshot
  Send-Hil $serial '@hil key up ag00' | Out-Null
  Send-Hil $serial '@hil app heartbeat' | Out-Null
  Send-Hil $serial '@hil app disconnect' | Out-Null
  Start-Sleep -Milliseconds 100
  $routeFallback = (Send-Hil $serial '@hil snapshot' 'snapshot').snapshot

  $inputBefore = [int]$routeFallback.input_count
  for ($index = 0; $index -lt 20; $index++) {
    $angle = ($index * 17) % 360
    Send-Hil $serial "@hil joystick $angle 0.80" | Out-Null
  }
  Send-Hil $serial '@hil joystick 0 0' | Out-Null
  $burstDeadline = [DateTime]::UtcNow.AddSeconds(2)
  do {
    $burst = (Send-Hil $serial '@hil snapshot' 'snapshot').snapshot
    if ([int]$burst.input_count -ge ($inputBefore + 21)) { break }
    Start-Sleep -Milliseconds 40
  } while ([DateTime]::UtcNow -lt $burstDeadline)

  Send-Hil $serial '@hil lighting stress on' | Out-Null
  Start-Sleep -Milliseconds 600
  $stressStart = (Send-Hil $serial '@hil metrics' 'metrics').metrics
  $samples = @()
  $stressDeadline = [DateTime]::UtcNow.AddSeconds($StressSeconds)
  while ([DateTime]::UtcNow -lt $stressDeadline) {
    $samples += (Send-Hil $serial '@hil metrics' 'metrics').metrics
    Start-Sleep -Milliseconds 700
  }
  Send-Hil $serial '@hil lighting stress off' | Out-Null
  $stressEnd = (Send-Hil $serial '@hil metrics' 'metrics').metrics
  $screen = (Send-Hil $serial '@hil screen crc' 'screen_crc' 6000).screen_crc

  Send-Hil $serial '@hil power' | Out-Null
  Start-Sleep -Milliseconds 700
  $standby = (Send-Hil $serial '@hil snapshot' 'snapshot').snapshot
  Send-Hil $serial '@hil power' | Out-Null
  Start-Sleep -Milliseconds 900
  $awake = (Send-Hil $serial '@hil snapshot' 'snapshot').snapshot

  # Leave the board in the same user-facing baseline expected after a normal
  # product test.  Test routing preferences remain persisted by design, but a
  # synthetic App lease and a non-default active layer must not leak into the
  # following Codex/manual test.
  Send-Hil $serial '@hil app disconnect' | Out-Null
  Send-Hil $serial '@hil layer 1' | Out-Null
  Start-Sleep -Milliseconds 100
  $restored = (Send-Hil $serial '@hil snapshot' 'snapshot').snapshot

  $routeMask = [Convert]::ToUInt64(([string]$routeHeld.app.held[0]).Substring(2), 16)
  $failures = @()
  if (-not $micDown.mic_pressed -or -not $micHeld.mic_pressed -or $micUp.mic_pressed) {
    $failures += 'MIC hold/release invariant failed'
  }
  if (($routeMask -band (1L -shl 6)) -eq 0) { $failures += 'Layer 2 AG00 did not route to flat id 6' }
  if (-not $routeFallback.routing.enabled) { $failures += 'Routing preferences lost after App disconnect' }
  if ([int]$burst.input_count -lt ($inputBefore + 21)) { $failures += 'Input burst lost events' }
  if ([int]$stressEnd.display.black_faults -ne [int]$stressStart.display.black_faults) { $failures += 'Black-block fault count increased' }
  if ([int]$stressEnd.display.timing_faults -ne [int]$stressStart.display.timing_faults) { $failures += 'Display timing fault count increased' }
  if ($screen.tiles.Count -ne 36 -or @($screen.tiles | Where-Object { $_ -eq '00000000' }).Count -gt 0) { $failures += 'Framebuffer tile CRC incomplete' }
  if ([int]$standby.power -ne 1 -or [int]$awake.power -ne 0) { $failures += 'Connected standby wake cycle failed' }
  if ([int]$restored.layer -ne 1 -or $restored.app.active) { $failures += 'Test baseline cleanup failed' }

  [ordered]@{
    schema = 1
    port = $Port
    captured_at = [DateTimeOffset]::UtcNow.ToString('o')
    passed = ($failures.Count -eq 0)
    failures = $failures
    initial = $initial
    mic = [ordered]@{ down = $micDown.mic_pressed; held = $micHeld.mic_pressed; up = $micUp.mic_pressed }
    routing = [ordered]@{ held_mask = $routeHeld.app.held[0]; fallback_active = $routeFallback.app.active; enabled = $routeFallback.routing.enabled }
    input_burst = [ordered]@{ before = $inputBefore; after = $burst.input_count; hid_failed = $burst.hid.failed }
    stress = [ordered]@{
      seconds = $StressSeconds
      samples = $samples.Count
      slow_frames_start = $stressStart.slow_frames
      slow_frames_end = $stressEnd.slow_frames
      black_faults_start = $stressStart.display.black_faults
      black_faults_end = $stressEnd.display.black_faults
      timing_faults_start = $stressStart.display.timing_faults
      timing_faults_end = $stressEnd.display.timing_faults
      unstable_start = $stressStart.display.unstable
      unstable_end = $stressEnd.display.unstable
      ble_rx_stack_low_water = $stressEnd.ble.rx_stack_low_water
    }
    screen_tiles = $screen.tiles.Count
    standby = [ordered]@{ entered = $standby.power; resumed = $awake.power }
    restored = [ordered]@{ layer = $restored.layer; app_active = $restored.app.active }
    initial_metrics = $initialMetrics
  } | ConvertTo-Json -Depth 10
} finally {
  if ($serial.IsOpen) { $serial.Close() }
  $serial.Dispose()
}
