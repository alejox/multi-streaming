# CMake Windows compiler configuration module

include_guard(GLOBAL)

include(compiler_common)

add_compile_definitions(
  UNICODE
  _UNICODE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  $<$<NOT:$<COMPILE_LANGUAGE:Swift>>:$<$<CONFIG:DEBUG>:DEBUG>>
  $<$<NOT:$<COMPILE_LANGUAGE:Swift>>:$<$<CONFIG:DEBUG>:_DEBUG>>
)
