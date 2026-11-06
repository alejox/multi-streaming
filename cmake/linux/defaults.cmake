# CMake Linux defaults module
#
# Unlike macOS and Windows, this does not fetch buildspec.json dependencies. libobs,
# obs-frontend-api and Qt6 come from system packages, so the plugin builds against the
# libraries the OBS on that system actually loads. See "Build on Linux" in README.md.

include_guard(GLOBAL)

# The per-user plugin path. OBS 33 calls it legacy but still loads it, and OBS 32 knows no
# other, so one package layout serves both.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(
    CMAKE_INSTALL_PREFIX
    "$ENV{HOME}/.config/obs-studio/plugins/${CMAKE_PROJECT_NAME}"
    CACHE STRING
    "Default plugin installation directory"
    FORCE
  )
endif()

# Enable find_package targets to become globally available targets
set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL TRUE)
