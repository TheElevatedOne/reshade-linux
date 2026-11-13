cmake_minimum_required(VERSION 3.23)
project(ReShade LANGUAGES C;CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "This is the Linux port of ReShade. For Windows, use the original at https://github.com/crosire/reshade.")
endif()

set(CMAKE_CXX_STANDARD 17)

add_subdirectory(deps)

# ReShade FX (effect compiler)

add_library(ReShadeFX STATIC)

target_sources(
  ReShadeFX
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS
      source
    FILES
      source/effect_codegen.hpp
      source/effect_expression.hpp
      source/effect_lexer.hpp
      source/effect_module.hpp
      source/effect_parser.hpp
      source/effect_preprocessor.hpp
      source/effect_symbol_table.hpp
      source/effect_token.hpp
  PRIVATE
    source/effect_codegen_glsl.cpp
    source/effect_codegen_hlsl.cpp
    source/effect_codegen_spirv.cpp
    source/effect_expression.cpp
    source/effect_lexer.cpp
    source/effect_parser_exp.cpp
    source/effect_parser_stmt.cpp
    source/effect_preprocessor.cpp
    source/effect_symbol_table.cpp
    source/effect_symbol_table_intrinsics.inl
)

target_compile_options(
  ReShadeFX
  PRIVATE
    -fPIC
    $<$<CONFIG:Release>:-fno-rtti>
)

target_link_libraries(ReShadeFX PRIVATE SPIRV)

# ReShade (Vulkan layer enabled with RESHADE=1)

add_subdirectory(linux)
