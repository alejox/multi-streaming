# Targets shared by the plugin build and the standalone test build. Nothing here
# may depend on OBS or Qt.
include(FetchContent)
FetchContent_Declare(
  nlohmann_json_header
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp
  URL_HASH SHA256=aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63
  DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_MakeAvailable(nlohmann_json_header)

add_library(
  multistream_core
  STATIC
  destinations/config_codec.cpp
  destinations/connect_gate.cpp
  destinations/deferred_work.cpp
  destinations/destination.cpp
  destinations/destination_editing.cpp
  destinations/destination_removal.cpp
  destinations/encoder_groups.cpp
  destinations/encoder_leases.cpp
  destinations/encoder_plan.cpp
  destinations/reconnect_policy.cpp
  destinations/runtime_requirements.cpp
  destinations/session.cpp
  destinations/session_selection.cpp
  destinations/start_tickets.cpp
  destinations/stream_stats.cpp
  destinations/validation.cpp
  destinations/vertical_settings.cpp
  vertical_canvas/preview_geometry.cpp
)
target_include_directories(multistream_core PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_include_directories(multistream_core SYSTEM PRIVATE "${nlohmann_json_header_SOURCE_DIR}")
target_compile_features(multistream_core PUBLIC cxx_std_17)
set_target_properties(multistream_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Adapters without OBS or Qt dependencies: config file and secret stores.
add_library(
  multistream_adapters
  STATIC
  persistence/file_config_repository.cpp
  persistence/vertical_settings_file.cpp
  secrets/platform_secret_store.cpp
)
target_link_libraries(multistream_adapters PUBLIC multistream_core)
set_target_properties(multistream_adapters PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(APPLE)
  target_sources(multistream_adapters PRIVATE secrets/keychain_secret_store.cpp)
  target_link_libraries(multistream_adapters PUBLIC "-framework CoreFoundation" "-framework Security")
endif()
