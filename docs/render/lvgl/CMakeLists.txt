cmake_minimum_required(VERSION 3.16)
project(codex_documentation_screenshots C CXX)
set(CMAKE_CXX_STANDARD 20)
get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(LV_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE STRING "" FORCE)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON CACHE BOOL "" FORCE)
add_subdirectory("${REPO}/firmware/managed_components/lvgl__lvgl" lvgl)
set(UI "${REPO}/firmware/components/codex_ui")
add_executable(capture capture.cpp
 "${REPO}/firmware/components/codex_config/keycap_catalog.cpp" "${UI}/theme.cpp" "${UI}/control_layout.cpp"
 "${UI}/dial_images_generated.cpp" "${UI}/glow_images_generated.cpp"
 "${UI}/keycap_icons_generated.cpp"
 "${REPO}/firmware/components/codex_core/lighting_compositor.cpp")
target_include_directories(capture PRIVATE . "${UI}" "${UI}/include"
 "${REPO}/firmware/components/codex_core/include"
 "${REPO}/firmware/components/codex_config/include"
 "${REPO}/firmware/components/codex_update/include")
target_link_libraries(capture PRIVATE lvgl m)
