idf_component_register(
    SRCS "codex_protocol.cpp"
    INCLUDE_DIRS "include"
    REQUIRES codex_core codex_update
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
