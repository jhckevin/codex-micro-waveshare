idf_component_register(
    SRCS "config_protocol.cpp" "keycap_catalog.cpp" "persistent_settings.cpp"
         "settings_store.cpp"
    INCLUDE_DIRS "include"
    REQUIRES codex_core nvs_flash
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
