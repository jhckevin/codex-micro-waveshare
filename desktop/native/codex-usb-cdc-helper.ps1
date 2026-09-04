$ErrorActionPreference = 'Stop'

function Find-CodexPort {
  $registryRoot = 'HKLM:\SYSTEM\CurrentControlSet\Enum\USB\VID_303A&PID_8360&MI_01'
  $port = Get-ChildItem $registryRoot -ErrorAction SilentlyContinue |
    ForEach-Object {
      (Get-ItemProperty (Join-Path $_.PSPath 'Device Parameters') `
        -ErrorAction SilentlyContinue).PortName
    } | Where-Object { $_ -match '^COM\d+$' } | Select-Object -First 1
  if ($port) { return $port }
  $port = Get-CimInstance Win32_SerialPort -ErrorAction SilentlyContinue |
    Where-Object { $_.PNPDeviceID -match 'VID_303A&PID_8360' } |
    Select-Object -First 1 -ExpandProperty DeviceID
  if ($port) { return $port }
  $device = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
    Where-Object {
      $_.InstanceId -match 'VID_303A&PID_8360' -and
      $_.FriendlyName -match '\(COM\d+\)'
    } | Select-Object -First 1
  if ($device -and $device.FriendlyName -match '\((COM\d+)\)') {
    return $Matches[1]
  }
  throw 'Codex Micro USB CDC port is not connected'
}

$serial = [System.IO.Ports.SerialPort]::new((Find-CodexPort), 115200, 'None', 8, 'One')
$serial.NewLine = "`n"
$serial.ReadTimeout = 100
$serial.WriteTimeout = 1500
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$serial.Encoding = [Text.UTF8Encoding]::new($false)
$serial.Open()
# Windows can deliver a stale empty-frame error generated while the CDC port
# is opening.  Drain only that pre-session input before advertising readiness;
# application replies are read normally after the ready event.
Start-Sleep -Milliseconds 120
$serial.DiscardInBuffer()
$readerPowerShell = [PowerShell]::Create()
$null = $readerPowerShell.AddScript({
  param($Port, $DebugMode)
  try {
    while ($Port.IsOpen) {
      try {
        $reply = $Port.ReadLine().TrimEnd("`r")
        if ($DebugMode) { [Console]::Error.WriteLine("serial: $reply") }
        [Console]::Out.WriteLine($reply)
        [Console]::Out.Flush()
      } catch [System.TimeoutException] {}
    }
  } catch {
    if ($Port.IsOpen) { [Console]::Error.WriteLine($_.Exception.Message) }
  }
}).AddArgument($serial).AddArgument($env:CODEX_CDC_DEBUG -eq '1')
$readerAsync = $readerPowerShell.BeginInvoke()
try {
  [Console]::Out.WriteLine('{"event":"ready"}')
  [Console]::Out.Flush()
  while (($line = [Console]::In.ReadLine()) -ne $null) {
    if ($env:CODEX_CDC_DEBUG -eq '1') { [Console]::Error.WriteLine("stdin: $line") }
    if ($line -eq '__close__') { break }
    $serial.WriteLine($line)
    $serial.BaseStream.Flush()
  }
} finally {
  if ($serial.IsOpen) { $serial.Close() }
  $readerPowerShell.Stop()
  $readerPowerShell.Dispose()
  $serial.Dispose()
}
