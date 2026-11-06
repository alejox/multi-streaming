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
  target_link_libraries(${name} PRIVATE test_harness multistream_core multistream_adapters)
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
add_core_test(destination_removal_test destinations/destination_removal_test.cpp)
add_core_test(destination_editing_test destinations/destination_editing_test.cpp)
add_core_test(encoder_plan_test destinations/encoder_plan_test.cpp)
add_core_test(encoder_leases_test destinations/encoder_leases_test.cpp)
add_core_test(start_tickets_test destinations/start_tickets_test.cpp)
add_core_test(session_selection_test destinations/session_selection_test.cpp)
add_core_test(vertical_settings_test destinations/vertical_settings_test.cpp)
add_core_test(connect_gate_test destinations/connect_gate_test.cpp)
add_core_test(deferred_work_test destinations/deferred_work_test.cpp)
add_core_test(platform_test destinations/platform_test.cpp)
add_core_test(preview_geometry_test vertical_canvas/preview_geometry_test.cpp)
add_core_test(source_icons_test vertical_canvas/source_icons_test.cpp)
add_core_test(file_config_repository_test persistence/file_config_repository_test.cpp)
add_core_test(vertical_settings_file_test persistence/vertical_settings_file_test.cpp)
add_core_test(in_memory_secret_store_test secrets/in_memory_secret_store_test.cpp)
add_core_test(unavailable_secret_store_test secrets/unavailable_secret_store_test.cpp)
add_core_test(e2e_file_secret_store_test secrets/e2e_file_secret_store_test.cpp ../src/secrets/e2e_file_secret_store.cpp)
target_compile_definitions(e2e_file_secret_store_test PRIVATE MULTI_STREAM_E2E_SECRETS)

if(APPLE)
  # Exit code 77 means the Keychain cannot be used from this session.
  add_core_test(keychain_secret_store_test secrets/keychain_secret_store_test.cpp)
  set_tests_properties(keychain_secret_store_test PROPERTIES SKIP_RETURN_CODE 77)
endif()
