idf_component_register(
    SRCS "control_layout.cpp" "dial_images_generated.cpp"
         "glow_images_generated.cpp" "theme.cpp" "ui.cpp"
         "keycap_icons_generated.cpp"
    INCLUDE_DIRS "include"
    REQUIRES codex_core codex_config codex_update heap lvgl__lvgl
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
