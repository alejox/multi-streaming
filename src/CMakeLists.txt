# Targets shared by the plugin build and the standalone test build. Nothing here
# may depend on OBS or Qt.
add_library(multistream_core STATIC destinations/runtime_requirements.cpp)
target_include_directories(multistream_core PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_compile_features(multistream_core PUBLIC cxx_std_17)
set_target_properties(multistream_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
