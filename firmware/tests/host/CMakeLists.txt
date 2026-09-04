cmake_minimum_required(VERSION 3.20)
project(codex_micro_host_tests LANGUAGES CXX)

enable_testing()

add_executable(test_recovery_guard test_recovery_guard.cpp)
target_compile_features(test_recovery_guard PRIVATE cxx_std_20)
target_include_directories(test_recovery_guard PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../main)

add_test(NAME recovery_guard COMMAND test_recovery_guard)
