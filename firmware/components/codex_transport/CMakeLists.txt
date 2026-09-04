idf_component_register(
    SRCS "ble_transport.cpp" "config_gatt.cpp" "usb_transport.cpp"
    INCLUDE_DIRS "include"
    REQUIRES codex_core codex_protocol codex_config codex_update bt esp_hid nvs_flash espressif__esp_tinyusb
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
