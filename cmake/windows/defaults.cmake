# CMake Windows defaults module

include_guard(GLOBAL)

include(buildspec)

# Use the OBS plugins directory as the default install destination
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(
    CMAKE_INSTALL_PREFIX
    "$ENV{ProgramFiles}/obs-studio/obs-plugins/64bit"
    CACHE STRING
    "Default plugin installation directory"
    FORCE
  )
endif()

# Enable find_package targets to become globally available targets
set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL TRUE)
