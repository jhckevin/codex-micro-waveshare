idf_component_register(
    SRCS "device_reducer.cpp" "lighting_compositor.cpp" "lighting_mailbox.cpp"
    INCLUDE_DIRS "include"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
