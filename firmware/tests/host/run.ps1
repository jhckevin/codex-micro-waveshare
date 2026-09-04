param(
  [string]$LlvmBin = "C:\Program Files\LLVM\bin"
)

$ErrorActionPreference = "Stop"
$testRoot = $PSScriptRoot
$repoRoot = (Resolve-Path (Join-Path $testRoot "..\..")).Path
$buildRoot = Join-Path $repoRoot "build-host"

$sdkconfigDefaults = Get-Content -LiteralPath (Join-Path $repoRoot "sdkconfig.defaults") -Raw
if ($sdkconfigDefaults -notmatch "(?m)^CONFIG_ESP_CONSOLE_UART_DEFAULT=y\s*$" -or
    $sdkconfigDefaults -match "(?m)^CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y\s*$" -or
    $sdkconfigDefaults -notmatch "(?m)^CONFIG_ESP_CONSOLE_SECONDARY_NONE=y\s*$" -or
    $sdkconfigDefaults -notmatch "(?m)^CONFIG_CODEX_RECOVERY_PATH_PROVEN=y\s*$") {
  throw "UART0 must remain the recovery console while native USB is reserved for TinyUSB"
}
Write-Host "PASS dual_usb_console_split"

if ($sdkconfigDefaults -notmatch "(?m)^CONFIG_ESP_MAIN_TASK_STACK_SIZE=(\d+)\s*$" -or
    [int]$Matches[1] -lt 20480) {
  throw "main task stack budget must be at least 20480 bytes"
}
Write-Host "PASS main_task_stack_budget"

if ($sdkconfigDefaults -notmatch "(?m)^CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y\s*$") {
  throw "BLE transport requires explicitly enabled external task stacks"
}
Write-Host "PASS external_task_stack_support"
if ($sdkconfigDefaults -notmatch "(?m)^CONFIG_BT_ALLOCATION_FROM_SPIRAM_FIRST=y\s*$") {
  throw "Bluedroid allocations must prefer PSRAM to protect cold-start internal RAM"
}
Write-Host "PASS bluedroid_psram_preference"

python (Join-Path $testRoot "test_ble_ultra_suspend_contract.py")
Write-Host "PASS ble_ultra_suspend_contract"
python (Join-Path $testRoot "test_display_teardown_contract.py")
Write-Host "PASS display_teardown_contract"
python (Join-Path $testRoot "test_display_suspend_contract.py")
Write-Host "PASS display_suspend_contract"
python (Join-Path $testRoot "test_ultra_usb_retention_contract.py")
Write-Host "PASS ultra_usb_retention_contract"

New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null

$clangCl = Join-Path $LlvmBin "clang-cl.exe"
$lldLink = Join-Path $LlvmBin "lld-link.exe"
$dllTool = Join-Path $LlvmBin "llvm-dlltool.exe"

foreach ($tool in @($clangCl, $lldLink, $dllTool)) {
  if (-not (Test-Path -LiteralPath $tool)) {
    throw "Required local LLVM tool not found: $tool"
  }
}

$importLibrary = Join-Path $buildRoot "kernel32.lib"
& $dllTool -m i386:x86-64 -d (Join-Path $testRoot "kernel32.def") -l $importLibrary
if ($LASTEXITCODE -ne 0) { throw "llvm-dlltool failed" }

function Invoke-FreestandingTest {
  param(
    [Parameter(Mandatory)] [string]$Name,
    [Parameter(Mandatory)] [string[]]$Sources,
    [string[]]$IncludeDirectories = @()
  )

  $objects = @()
  foreach ($source in $Sources) {
    $sourcePath = if ([System.IO.Path]::IsPathRooted($source)) {
      $source
    } else {
      Join-Path $repoRoot $source
    }
    $objectName = [System.IO.Path]::GetFileNameWithoutExtension($sourcePath) + ".obj"
    $object = Join-Path $buildRoot "$Name-$objectName"
    $includeArgs = @("/I$repoRoot\main")
    foreach ($directory in $IncludeDirectories) {
      $includeArgs += "/I$(Join-Path $repoRoot $directory)"
    }
    & $clangCl /nologo /std:c++20 /O2 /GS- /Gs9999999 /EHsc- /GR- @includeArgs /c `
        $sourcePath "/Fo$object"
    if ($LASTEXITCODE -ne 0) { throw "$Name compilation failed" }
    $objects += $object
  }

  $binary = Join-Path $buildRoot "$Name.exe"
  & $lldLink /entry:mainCRTStartup /subsystem:console /nodefaultlib /dynamicbase:no `
      @objects $importLibrary "/out:$binary"
  if ($LASTEXITCODE -ne 0) { throw "$Name link failed" }

  & $binary
  if ($LASTEXITCODE -ne 0) {
    throw "$Name failed with $LASTEXITCODE assertion(s)"
  }
  Write-Output "PASS $Name"
}

Invoke-FreestandingTest -Name "recovery_guard" -Sources @(
  "tests\host\test_recovery_guard.cpp",
  "tests\host\freestanding_runtime.cpp"
)

Invoke-FreestandingTest -Name "device_reducer" -Sources @(
  "tests\host\test_device_reducer.cpp",
  "components\codex_core\device_reducer.cpp",
  "tests\host\freestanding_runtime.cpp"
) -IncludeDirectories @("components\codex_core\include")

Invoke-FreestandingTest -Name "app_lease" -Sources @(
  "tests\host\test_app_lease.cpp",
  "components\codex_core\device_reducer.cpp",
  "tests\host\freestanding_runtime.cpp"
) -IncludeDirectories @("components\codex_core\include")

Invoke-FreestandingTest -Name "routed_release" -Sources @(
  "tests\host\test_routed_release.cpp",
  "components\codex_core\device_reducer.cpp",
  "tests\host\freestanding_runtime.cpp"
) -IncludeDirectories @("components\codex_core\include")

Invoke-FreestandingTest -Name "power_policy" -Sources @(
  "tests\host\test_power_policy.cpp",
  "tests\host\freestanding_runtime.cpp"
) -IncludeDirectories @("components\codex_core\include")

Invoke-FreestandingTest -Name "ambient_frame" -Sources @(
  "tests\host\test_ambient_frame.cpp",
  "tests\host\freestanding_runtime.cpp"
) -IncludeDirectories @(
  "components\codex_core\include",
  "components\codex_ui\include"
)

Invoke-FreestandingTest -Name "ambient_strip" -Sources @(
  "tests\host\test_ambient_strip.cpp",
  "tests\host\freestanding_runtime.cpp"
) -IncludeDirectories @("components\codex_ui\include")

Invoke-FreestandingTest -Name "settings_paging" -Sources @(
  "tests\host\test_settings_paging.cpp",
  "tests\host\freestanding_runtime.cpp"
) -IncludeDirectories @("components\codex_ui\include")

Invoke-FreestandingTest -Name "ble_session" -Sources @(
  "tests\host\test_ble_session.cpp",
  "tests\host\freestanding_runtime.cpp"
) -IncludeDirectories @(
  "components\codex_transport\include",
  "components\codex_protocol\include",
  "components\codex_core\include"
)

Invoke-FreestandingTest -Name "protocol_trace" -Sources @(
  "tests\host\test_protocol_trace.cpp",
  "tests\host\freestanding_runtime.cpp"
) -IncludeDirectories @("components\codex_transport\include")

Invoke-FreestandingTest -Name "battery_policy" -Sources @(
  "tests\host\test_battery_policy.cpp",
  "tests\host\freestanding_runtime.cpp"
) -IncludeDirectories @("components\codex_core\include")

Invoke-FreestandingTest -Name "protected_unlock" -Sources @(
  "tests\host\test_protected_unlock.cpp",
  "tests\host\freestanding_runtime.cpp"
) -IncludeDirectories @("components\codex_input\include")

Invoke-FreestandingTest -Name "lighting_compositor" -Sources @(
  "tests\host\test_lighting_compositor.cpp",
  "components\codex_core\device_reducer.cpp",
  "components\codex_core\lighting_compositor.cpp",
  "tests\host\freestanding_runtime.cpp"
) -IncludeDirectories @("components\codex_core\include")

Invoke-FreestandingTest -Name "lighting_mailbox" -Sources @(
  "tests\host\test_lighting_mailbox.cpp",
  "components\codex_core\device_reducer.cpp",
  "components\codex_core\lighting_mailbox.cpp",
  "tests\host\freestanding_runtime.cpp"
) -IncludeDirectories @("components\codex_core\include")

Invoke-FreestandingTest -Name "control_layout" -Sources @(
  "tests\host\test_control_layout.cpp",
  "components\codex_ui\control_layout.cpp",
  "tests\host\freestanding_runtime.cpp"
) -IncludeDirectories @("components\codex_ui\include")

Invoke-FreestandingTest -Name "input" -Sources @(
  "tests\host\test_input.cpp",
  "components\codex_input\input.cpp",
  "components\codex_ui\control_layout.cpp",
  "components\codex_core\device_reducer.cpp",
  "tests\host\freestanding_runtime.cpp"
) -IncludeDirectories @(
  "components\codex_input\include",
  "components\codex_ui\include",
  "components\codex_core\include"
)

Invoke-FreestandingTest -Name "input_priority" -Sources @(
  "tests\host\test_input_priority.cpp",
  "tests\host\freestanding_runtime.cpp"
) -IncludeDirectories @(
  "components\codex_core\include",
  "components\codex_input\include"
)

Invoke-FreestandingTest -Name "battery_publish_policy" -Sources @(
  "tests\host\test_battery_publish_policy.cpp",
  "tests\host\freestanding_runtime.cpp"
) -IncludeDirectories @("components\codex_transport\include")

Invoke-FreestandingTest -Name "battery_level_status" -Sources @(
  "tests\host\test_battery_level_status.cpp",
  "tests\host\freestanding_runtime.cpp"
) -IncludeDirectories @("components\esp_hid\include")

Invoke-FreestandingTest -Name "dial_frames" -Sources @(
  "tests\host\test_dial_frames.cpp",
  "tests\host\freestanding_runtime.cpp"
) -IncludeDirectories @("components\codex_ui\include")

Invoke-FreestandingTest -Name "button_gesture" -Sources @(
  "tests\host\test_button_gesture.cpp",
  "components\codex_board\button_gesture.cpp",
  "tests\host\freestanding_runtime.cpp"
) -IncludeDirectories @("components\codex_board\include")

Invoke-FreestandingTest -Name "ble_advertising_plan" -Sources @(
  "tests\host\test_ble_advertising_plan.cpp",
  "tests\host\freestanding_runtime.cpp"
) -IncludeDirectories @("components\codex_transport\include")

Invoke-FreestandingTest -Name "config_gatt_limits" -Sources @(
  "tests\host\test_config_gatt_limits.cpp",
  "tests\host\freestanding_runtime.cpp"
) -IncludeDirectories @("components\codex_transport\include")

Invoke-FreestandingTest -Name "hid_report_route" -Sources @(
  "tests\host\test_hid_report_route.cpp",
  "tests\host\freestanding_runtime.cpp"
) -IncludeDirectories @("components\codex_transport\include")

Invoke-FreestandingTest -Name "control_router" -Sources @(
  "tests\host\test_control_router.cpp",
  "tests\host\freestanding_runtime.cpp"
) -IncludeDirectories @("components\codex_core\include")

Invoke-FreestandingTest -Name "app_event_json" -Sources @(
  "tests\host\test_app_event_json.cpp",
  "tests\host\freestanding_runtime.cpp"
) -IncludeDirectories @(
  "components\codex_transport\include",
  "components\codex_core\include"
)

Invoke-FreestandingTest -Name "reliable_tx" -Sources @(
  "tests\host\test_reliable_tx.cpp",
  "tests\host\freestanding_runtime.cpp"
) -IncludeDirectories @(
  "components\codex_transport\include",
  "components\codex_core\include"
)

Invoke-FreestandingTest -Name "transport_policy" -Sources @(
  "tests\host\test_transport_policy.cpp",
  "tests\host\freestanding_runtime.cpp"
) -IncludeDirectories @(
  "components\codex_transport\include",
  "components\codex_core\include"
)

Invoke-FreestandingTest -Name "persistent_settings" -Sources @(
  "tests\host\test_persistent_settings.cpp",
  "components\codex_config\persistent_settings.cpp",
  "tests\host\freestanding_runtime.cpp"
) -IncludeDirectories @("components\codex_config\include", "components\codex_core\include")

Invoke-FreestandingTest -Name "config_protocol" -Sources @(
  "tests\host\test_config_protocol.cpp",
  "components\codex_config\config_protocol.cpp",
  "components\codex_config\keycap_catalog.cpp",
  "components\codex_core\device_reducer.cpp",
  "tests\host\freestanding_runtime.cpp"
) -IncludeDirectories @("components\codex_config\include", "components\codex_core\include")

Invoke-FreestandingTest -Name "codex_protocol" -Sources @(
  "tests\host\test_codex_protocol.cpp",
  "components\codex_protocol\codex_protocol.cpp",
  "components\codex_core\device_reducer.cpp",
  "tests\host\freestanding_runtime.cpp"
) -IncludeDirectories @("components\codex_protocol\include", "components\codex_core\include")

Invoke-FreestandingTest -Name "click_synth" -Sources @(
  "tests\host\test_click_synth.cpp",
  "components\codex_audio\click_synth.cpp",
  "tests\host\freestanding_runtime.cpp"
) -IncludeDirectories @("components\codex_audio\include", "components\codex_core\include")
