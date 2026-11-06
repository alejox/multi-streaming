# CMake Windows build dependencies module

include_guard(GLOBAL)

include(buildspec_common)

# _check_dependencies_windows_x64: Set up the Windows x64 slice for _check_dependencies
function(_check_dependencies_windows_x64)
  set(arch x64)
  set(platform windows-x64)

  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/buildspec.json" buildspec)

  set(dependencies_dir "${CMAKE_CURRENT_SOURCE_DIR}/.deps")
  set(prebuilt_filename "windows-deps-VERSION-ARCH_REVISION.tar.xz")
  set(prebuilt_destination "obs-deps-VERSION-ARCH")
  set(qt6_filename "windows-deps-qt6-VERSION-ARCH_REVISION.tar.xz")
  set(qt6_destination "obs-deps-qt6-VERSION-ARCH")
  set(obs-studio_filename "VERSION.tar.gz")
  set(obs-studio_destination "obs-studio-VERSION")
  set(dependencies_list prebuilt qt6 obs-studio)

  _check_dependencies()

  list(APPEND CMAKE_PREFIX_PATH "${dependencies_dir}")
  set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} PARENT_SCOPE)
endfunction()

_check_dependencies_windows_x64()
