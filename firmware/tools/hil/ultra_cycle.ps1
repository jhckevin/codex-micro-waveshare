param(
  [string]$Port = "COM5",
  [int]$Cycles = 3,
  [string]$Output = "diagnostics/ultra-cycle.log"
)

$ErrorActionPreference = "Stop"
$outputPath = [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $Output))
[System.IO.Directory]::CreateDirectory(
  [System.IO.Path]::GetDirectoryName($outputPath)) | Out-Null

$serial = [System.IO.Ports.SerialPort]::new($Port, 115200)
$serial.NewLine = "`n"
$serial.ReadTimeout = 100
$serial.WriteTimeout = 1000
$serial.DtrEnable = $false
$serial.RtsEnable = $false

function Read-For([int]$Milliseconds) {
  $deadline = [DateTime]::UtcNow.AddMilliseconds($Milliseconds)
  $captured = [System.Text.StringBuilder]::new()
  while ([DateTime]::UtcNow -lt $deadline) {
    $chunk = $serial.ReadExisting()
    if ($chunk.Length -gt 0) {
      [void]$captured.Append($chunk)
    }
    Start-Sleep -Milliseconds 10
  }
  $text = $captured.ToString()
  if ($text.Length -gt 0) {
    [System.IO.File]::AppendAllText($outputPath, $text)
  }
  return $text
}

function Send-Command([string]$Command, [int]$WaitMilliseconds = 500) {
  [System.IO.File]::AppendAllText(
    $outputPath, "`r`n# $([DateTimeOffset]::UtcNow.ToString('o')) $Command`r`n")
  $serial.WriteLine($Command)
  return Read-For $WaitMilliseconds
}

try {
  [System.IO.File]::WriteAllText(
    $outputPath,
    "# Ultra cycle HIL $([DateTimeOffset]::UtcNow.ToString('o'))`r`n")
  $serial.Open()
  $boot = Read-For 9000
  $ping = Send-Command "@hil ping" 800
  if ($ping -notmatch '"pong":true') {
    throw "HIL ping failed; inspect $outputPath"
  }

  for ($cycle = 1; $cycle -le $Cycles; $cycle++) {
    [void](Send-Command "@hil snapshot" 500)
    $entered = Send-Command "@hil ultra" 2600
    if ($entered -notmatch "ultra standby hardware entered") {
      # A stale partial UART line immediately after opening COM can consume one
      # development command. Ultra entry itself is idempotent, so retry once.
      $entered += Send-Command "@hil ultra" 2600
      if ($entered -notmatch "ultra standby hardware entered") {
        throw "cycle $cycle did not enter Ultra"
      }
    }
    # The CPU has narrow awake windows in Ultra. Repeating the development-only
    # idempotent wake command is safe: it exits Ultra once and is a no-op after
    # the state has become Active.
    $wakeDeadline = [DateTime]::UtcNow.AddSeconds(4)
    $wakeOutput = Send-Command "" 30
    while ([DateTime]::UtcNow -lt $wakeDeadline) {
      $wakeOutput += Send-Command "@hil wake" 220
      if ($wakeOutput -match "ultra heap stage=exit-begin") {
        break
      }
    }
    [void](Read-For 7500)
    # Re-establish a clean line boundary before the assertion command. The
    # first UART bytes can be consumed as a light-sleep wake preamble when no
    # BLE link is active.
    $sync = Send-Command "@hil ping" 350
    if ($sync -notmatch '"pong":true') {
      [void](Send-Command "" 30)
      $sync += Send-Command "@hil ping" 350
    }
    if ($sync -notmatch '"pong":true') {
      throw "cycle $cycle HIL parser did not resynchronize"
    }
    $snapshot = Send-Command "@hil snapshot" 700
    if ([string]$snapshot -notmatch '"power":0') {
      throw "cycle $cycle did not return to Active"
    }
  }
  [void](Send-Command "@hil metrics" 800)
} finally {
  if ($serial.IsOpen) {
    $serial.Close()
  }
}

$lines = Get-Content -LiteralPath $outputPath
$lines |
  Where-Object {
    $_ -match "ultra heap|BLE suspended|BLE resumed|light sleep failed|" +
      "assert failed|Guru Meditation|rst:|codex_ready|black_faults"
  }
