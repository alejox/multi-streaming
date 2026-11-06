# Standalone core tests: configures with plain CMake and needs neither OBS nor Qt.
#
#   cmake -S tests -B build_tests
#   cmake --build build_tests
#   ctest --test-dir build_tests --output-on-failure
cmake_minimum_required(VERSION 3.28...3.30)

project(obs-multi-stream-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_EXTENSIONS FALSE)
add_compile_options(-Wall -Wextra)

add_subdirectory(../src src)

add_library(test_harness STATIC support/test_harness.cpp)
target_include_directories(test_harness PUBLIC support)

enable_testing()

function(add_core_test name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE test_harness multistream_core)
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES TIMEOUT 60)
endfunction()

add_core_test(runtime_requirements_test destinations/runtime_requirements_test.cpp)
add_core_test(reconnect_policy_test destinations/reconnect_policy_test.cpp)
add_core_test(destination_validation_test destinations/destination_validation_test.cpp)
add_core_test(config_codec_test destinations/config_codec_test.cpp)
add_core_test(stream_stats_test destinations/stream_stats_test.cpp)
add_core_test(session_test destinations/session_test.cpp)
add_core_test(encoder_groups_test destinations/encoder_groups_test.cpp)
