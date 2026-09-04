idf_component_register(
    SRCS "axp2101_power.cpp"
    INCLUDE_DIRS "include"
    REQUIRES codex_core waveshare__esp32_s3_touch_lcd_4b esp_driver_i2c
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
