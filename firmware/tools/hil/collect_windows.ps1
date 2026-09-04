param(
  [Parameter(Mandatory = $true)] [string]$OutputDirectory
)

$ErrorActionPreference = "Stop"
$output = New-Item -ItemType Directory -Force -Path $OutputDirectory

$metadata = [ordered]@{
  captured_at = [DateTimeOffset]::UtcNow.ToString("o")
  computer = $env:COMPUTERNAME
  os = Get-CimInstance Win32_OperatingSystem |
    Select-Object Caption, Version, BuildNumber, LastBootUpTime
  codex_processes = Get-Process -ErrorAction SilentlyContinue |
    Where-Object { $_.ProcessName -match "Codex|ChatGPT" } |
    Select-Object ProcessName, Id, StartTime, CPU, WorkingSet64, Path
  serial_ports = Get-CimInstance Win32_SerialPort -ErrorAction SilentlyContinue |
    Select-Object DeviceID, Name, PNPDeviceID
  bluetooth_devices = Get-PnpDevice -Class Bluetooth -ErrorAction SilentlyContinue |
    Where-Object { $_.FriendlyName -match "Codex|Micro" } |
    Select-Object Status, Class, FriendlyName, InstanceId, Problem
}
$metadata | ConvertTo-Json -Depth 6 |
  Set-Content -LiteralPath (Join-Path $output "windows-state.json") -Encoding utf8

$providers = @(
  "Microsoft-Windows-Bluetooth-BthLEPrepairing",
  "Microsoft-Windows-Bluetooth-MTPEnum",
  "Microsoft-Windows-UserPnp"
)
foreach ($provider in $providers) {
  $safeName = $provider -replace "[^A-Za-z0-9_-]", "_"
  Get-WinEvent -FilterHashtable @{
    ProviderName = $provider
    StartTime = (Get-Date).AddHours(-2)
  } -ErrorAction SilentlyContinue |
    Select-Object TimeCreated, Id, LevelDisplayName, ProviderName, Message |
    ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath (Join-Path $output "$safeName.json") -Encoding utf8
}

Write-Output "Windows diagnostics captured at $($output.FullName)"
