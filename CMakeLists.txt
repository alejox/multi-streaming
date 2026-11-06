cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)
option(MULTI_STREAM_E2E_SECRETS "Read fake stream keys from a file, for runtime tests. Never install such a build." OFF)

if(NOT ENABLE_FRONTEND_API OR NOT ENABLE_QT)
  message(FATAL_ERROR "obs-multi-stream requires ENABLE_FRONTEND_API and ENABLE_QT.")
endif()

include(compilerconfig)
include(defaults)
include(helpers)

add_subdirectory(src)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  PRIVATE multistream_adapters OBS::libobs OBS::obs-frontend-api Qt6::Core Qt6::Widgets
)
target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
)
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/dock/destination_editor_dialog.cpp
    src/dock/destinations_dock.cpp
    src/dock/dock_controller.cpp
    src/dock/icons.cpp
    src/dock/vertical_controller.cpp
    src/dock/vertical_dock.cpp
    src/dock/vertical_preview.cpp
    src/plugin_main.cpp
    src/streaming/main_stream_encoders.cpp
    src/streaming/streaming_engine.cpp
    src/vertical_canvas/vertical_canvas.cpp
)

if(MULTI_STREAM_E2E_SECRETS)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/e2e/e2e_commands.cpp src/secrets/e2e_file_secret_store.cpp)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE MULTI_STREAM_E2E_SECRETS)
  # Runs first among the install scripts, so nothing reaches the install prefix.
  install(CODE "message(FATAL_ERROR \"A MULTI_STREAM_E2E_SECRETS build must never be installed.\")")
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

if(NOT MULTI_STREAM_E2E_SECRETS)
  add_custom_command(
    TARGET ${CMAKE_PROJECT_NAME}
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" "-DBINARY=$<TARGET_FILE:${CMAKE_PROJECT_NAME}>" -P
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_no_e2e_marker.cmake"
    COMMENT "Checking that the binary does not contain the end-to-end secret store"
  )
endif()
