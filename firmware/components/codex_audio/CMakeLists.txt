idf_component_register(
    SRCS "click_synth.cpp" "audio.cpp"
         "generated/switch_samples_generated.cpp"
    INCLUDE_DIRS "include" "generated"
    REQUIRES codex_core waveshare__esp32_s3_touch_lcd_4b espressif__esp_codec_dev
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
