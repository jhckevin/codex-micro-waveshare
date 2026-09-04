idf_component_register(
    SRCS "arcade_input.cpp" "arcade_trigger.cpp" "input.cpp"
    INCLUDE_DIRS "include"
    REQUIRES codex_core codex_ui
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
