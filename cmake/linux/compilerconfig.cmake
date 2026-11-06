# CMake Linux compiler configuration module

include_guard(GLOBAL)

include(ccache)
include(compiler_common)

add_compile_options(
  "$<$<COMPILE_LANGUAGE:C,CXX>:-fno-strict-aliasing>"
  "$<$<COMPILE_LANGUAGE:C,CXX>:-Wall>"
  "$<$<COMPILE_LANGUAGE:C,CXX>:-Wextra>"
  "$<$<COMPILE_LANGUAGE:C,CXX>:-Werror=return-type>"
  "$<$<COMPILE_LANGUAGE:C,CXX>:-Wformat-security>"
  "$<$<COMPILE_LANGUAGE:C,CXX>:-Wvla>"
  "$<$<COMPILE_LANGUAGE:C,CXX>:-Wno-missing-field-initializers>"
  "$<$<COMPILE_LANGUAGE:C,CXX>:-Wno-unknown-pragmas>"
)

add_compile_definitions($<$<CONFIG:DEBUG>:DEBUG> $<$<CONFIG:DEBUG>:_DEBUG>)
