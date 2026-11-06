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
