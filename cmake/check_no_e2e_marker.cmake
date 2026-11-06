# Fails the build when a default-configured binary contains the end-to-end secret store.
# Usage: cmake -DBINARY=<path> -P check_no_e2e_marker.cmake
if(NOT EXISTS "${BINARY}")
  message(FATAL_ERROR "check_no_e2e_marker: ${BINARY} does not exist")
endif()
file(READ "${BINARY}" contents HEX)
string(HEX "MULTI_STREAM_E2E_SECRETS_ACTIVE" marker)
string(TOLOWER "${marker}" marker)
string(FIND "${contents}" "${marker}" position)
if(NOT position EQUAL -1)
  message(FATAL_ERROR "check_no_e2e_marker: ${BINARY} contains the end-to-end secret store")
endif()
message(STATUS "check_no_e2e_marker: ${BINARY} does not contain the end-to-end secret store")
