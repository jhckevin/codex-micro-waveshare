set(codex_main_sources
    "app_main.cpp"
    "recovery_guard.cpp"
)

if(CONFIG_CODEX_DEVELOPMENT_HIL)
    list(APPEND codex_main_sources
        "hil_protocol.cpp"
        "hil_console.cpp"
        "display_monitor.cpp"
        "display_monitor_runtime.cpp"
    )
endif()

idf_component_register(
    SRCS ${codex_main_sources}
    INCLUDE_DIRS "."
    REQUIRES codex_core codex_ui codex_input codex_transport codex_audio codex_board codex_power codex_update esp_driver_gpio esp_driver_uart esp_timer esp_pm espressif__esp_lvgl_port espressif__esp_lcd_touch
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_20)
