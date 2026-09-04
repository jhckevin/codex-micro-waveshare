param(
  [string]$Port = 'COM5',
  [int]$Seconds = 120,
  [string]$OutputPath = ''
)
$ErrorActionPreference = 'Stop'
$writer = if ($OutputPath) {
  [System.IO.StreamWriter]::new($OutputPath, $false, [Text.UTF8Encoding]::new($false))
} else { $null }
$serial = [System.IO.Ports.SerialPort]::new($Port, 115200, 'None', 8, 'One')
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$serial.ReadTimeout = 250
$serial.Open()
try {
  $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
  while ([DateTime]::UtcNow -lt $deadline) {
    try {
      $line = $serial.ReadLine().TrimEnd("`r")
      if ($writer) { $writer.WriteLine($line); $writer.Flush() }
      else { Write-Output $line }
    } catch [System.TimeoutException] {}
  }
} finally {
  $serial.Close()
  $serial.Dispose()
  if ($writer) { $writer.Dispose() }
}
