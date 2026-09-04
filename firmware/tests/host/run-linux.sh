#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
build_root="$(mktemp -d)"
trap 'rm -rf "$build_root"' EXIT
cd "$repo_root"

includes=(
  -Imain
  -Icomponents/codex_audio/include
  -Icomponents/codex_board/include
  -Icomponents/codex_config/include
  -Icomponents/codex_core/include
  -Icomponents/codex_input/include
  -Icomponents/codex_protocol/include
  -Icomponents/codex_transport/include
  -Icomponents/codex_ui/include
  -Icomponents/codex_update/include
  -Icomponents/esp_hid/include
)

run_test() {
  local name="$1"
  shift
  sed \
    -e '/__declspec(dllimport)/d' \
    -e 's/extern "C" void mainCRTStartup()/int main()/' \
    -e 's/ExitProcess(\(.*\));/return \1;/' \
    "tests/host/test_${name}.cpp" > "$build_root/test_${name}.cpp"
  g++ -std=c++20 -O2 "$build_root/test_${name}.cpp" "$@" \
      "${includes[@]}" -o "$build_root/$name"
  "$build_root/$name"
  echo "PASS $name"
}

run_test recovery_guard
run_test hil_protocol main/hil_protocol.cpp
run_test display_monitor main/display_monitor.cpp
if [[ "${CODEX_SKIP_RESOLVED_DEPENDENCY_CONTRACTS:-0}" == "1" ]]; then
  echo "SKIP lvgl_deinit_contract (runs after ESP-IDF dependency resolution)"
else
  python3 tests/host/test_lvgl_deinit_contract.py
  echo "PASS lvgl_deinit_contract"
fi
python3 tests/host/test_audio_suspend_contract.py
echo "PASS audio_suspend_contract"
python3 tests/host/test_ble_ultra_suspend_contract.py
echo "PASS ble_ultra_suspend_contract"
python3 tests/host/test_display_teardown_contract.py
echo "PASS display_teardown_contract"
python3 tests/host/test_display_suspend_contract.py
echo "PASS display_suspend_contract"
python3 tests/host/test_ui_resource_lifecycle.py
echo "PASS ui_resource_lifecycle"
python3 tests/host/test_touch_async_contract.py
echo "PASS touch_async_contract"
python3 tests/host/test_touch_polling_lock_contract.py
echo "PASS touch_polling_lock_contract"
python3 tests/host/test_critical_input_wake_contract.py
echo "PASS critical_input_wake_contract"
python3 tests/host/test_ultra_usb_retention_contract.py
echo "PASS ultra_usb_retention_contract"
run_test firmware_version
run_test visual_tuning
run_test ambient_frame
run_test ambient_strip
run_test ambient_render_policy
run_test ambient_path
run_test joystick_policy
run_test arcade_trigger components/codex_input/arcade_trigger.cpp
run_test arcade_input \
  components/codex_input/arcade_input.cpp \
  components/codex_core/device_reducer.cpp
run_test device_reducer components/codex_core/device_reducer.cpp
run_test app_lease components/codex_core/device_reducer.cpp
run_test routed_release components/codex_core/device_reducer.cpp
run_test power_policy
run_test settings_paging
run_test ble_session
run_test protocol_trace
run_test battery_policy
run_test protected_unlock
run_test lighting_compositor \
  components/codex_core/device_reducer.cpp \
  components/codex_core/lighting_compositor.cpp
python3 tests/host/test_glow_assets.py
echo "PASS glow_assets"
python3 tests/host/test_display_config.py
echo "PASS display_config"
python3 tests/host/test_settings_battery_layout.py
python3 tests/host/test_usb_descriptor_contract.py
echo "PASS usb_descriptor_contract"
run_test lighting_mailbox \
  components/codex_core/device_reducer.cpp \
  components/codex_core/lighting_mailbox.cpp
run_test control_layout components/codex_ui/control_layout.cpp
run_test input \
  components/codex_input/input.cpp \
  components/codex_ui/control_layout.cpp \
  components/codex_core/device_reducer.cpp
run_test input_priority
run_test touch_contact_policy
run_test battery_publish_policy
run_test battery_level_status
run_test battery_display
run_test dial_frames
run_test button_gesture components/codex_board/button_gesture.cpp
run_test ble_advertising_plan
run_test ble_latency_policy
run_test config_gatt_limits
run_test hid_report_route
run_test control_router
run_test app_event_json
run_test reliable_tx
run_test input_latency_trace components/codex_core/device_reducer.cpp
run_test update_manifest components/codex_update/update_manifest.cpp
run_test update_identity_policy
run_test update_crypto_vectors \
  -DCODEX_HOST_OPENSSL \
  components/codex_update/update_manifest.cpp \
  components/codex_update/update_crypto.cpp \
  -lcrypto
run_test update_session \
  -DCODEX_HOST_OPENSSL \
  components/codex_update/update_manifest.cpp \
  components/codex_update/update_crypto.cpp \
  components/codex_update/update_session.cpp \
  -lcrypto
run_test update_protocol \
  -DCODEX_HOST_OPENSSL \
  components/codex_update/update_manifest.cpp \
  components/codex_update/update_crypto.cpp \
  components/codex_update/update_session.cpp \
  components/codex_update/update_protocol.cpp \
  -lcrypto
run_test user_content_pack components/codex_update/user_content_pack.cpp
run_test user_content_protocol components/codex_update/user_content_protocol.cpp
run_test compatibility_pack components/codex_update/compatibility_pack.cpp
run_test service_pack components/codex_update/service_pack.cpp
run_test service_pack_storage \
  components/codex_update/service_pack.cpp \
  components/codex_update/service_pack_storage.cpp \
  components/codex_update/update_manifest.cpp \
  components/codex_update/update_crypto.cpp \
  -DCODEX_HOST_OPENSSL -lcrypto
run_test service_resource_runtime \
  components/codex_update/service_resource_runtime.cpp \
  components/codex_update/compatibility_pack.cpp
run_test transport_policy
run_test display_transition_policy
run_test persistent_settings components/codex_config/persistent_settings.cpp
run_test config_protocol \
  components/codex_config/config_protocol.cpp \
  components/codex_config/keycap_catalog.cpp \
  components/codex_core/device_reducer.cpp
run_test codex_protocol \
  components/codex_protocol/codex_protocol.cpp \
  components/codex_update/compatibility_pack.cpp \
  components/codex_core/device_reducer.cpp
run_test click_synth components/codex_audio/click_synth.cpp

g++ -std=c++20 -O2 -Wall -Wextra -Werror -Icomponents/codex_core/include tests/host/test_smart_charge.cpp -o "$build_root/smart_charge"
"$build_root/smart_charge"
