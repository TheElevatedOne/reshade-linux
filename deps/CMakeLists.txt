add_compile_options(
  -fPIC
)

# fpng

add_library(fpng STATIC)
target_sources(
  fpng
  PUBLIC
    fpng/src/fpng.h
  PRIVATE
    fpng/src/fpng.cpp
)
target_include_directories(
  fpng
  PUBLIC
    fpng/src
)
target_compile_definitions(
  fpng
  PUBLIC
    FPNG_NO_STDIO
)
target_compile_options(
  fpng
  PRIVATE
    -mpclmul -msse2 -msse4
)

# glad

add_library(glad STATIC)
target_include_directories(
  glad
  PUBLIC
    glad/target/include
)
target_compile_definitions(
  glad
  PUBLIC
    VK_USE_PLATFORM_XLIB_KHR
    VK_USE_PLATFORM_XCB_KHR
    VK_USE_PLATFORM_WAYLAND_KHR
)

set(GLAD_APIS
  vulkan=1.4
)
set(GLAD_EXTENSIONS
  VK_EXT_conservative_rasterization;VK_EXT_custom_border_color;VK_EXT_debug_utils;VK_EXT_descriptor_indexing;VK_EXT_extended_dynamic_state;VK_EXT_extended_dynamic_state2;VK_EXT_extended_dynamic_state3;VK_EXT_full_screen_exclusive;VK_EXT_graphics_pipeline_library;VK_EXT_host_image_copy;VK_EXT_host_query_reset;VK_EXT_index_type_uint8;VK_EXT_load_store_op_none;VK_EXT_mesh_shader;VK_EXT_multi_draw;VK_EXT_private_data;VK_EXT_swapchain_colorspace;VK_EXT_tooling_info;VK_EXT_transform_feedback;VK_KHR_acceleration_structure;VK_KHR_bind_memory2;VK_KHR_buffer_device_address;VK_KHR_copy_commands2;VK_KHR_create_renderpass2;VK_KHR_dedicated_allocation;VK_KHR_depth_stencil_resolve;VK_KHR_descriptor_update_template;VK_KHR_device_group;VK_KHR_display_swapchain;VK_KHR_draw_indirect_count;VK_KHR_dynamic_rendering;VK_KHR_external_memory_capabilities;VK_KHR_format_feature_flags2;VK_KHR_get_memory_requirements2;VK_KHR_get_physical_device_properties2;VK_KHR_image_format_list;VK_KHR_incremental_present;VK_KHR_maintenance4;VK_KHR_maintenance5;VK_KHR_maintenance9;VK_KHR_pipeline_library;VK_KHR_push_descriptor;VK_KHR_ray_tracing_maintenance1;VK_KHR_ray_tracing_pipeline;VK_KHR_surface;VK_KHR_swapchain;VK_KHR_swapchain_mutable_format;VK_KHR_synchronization2;VK_KHR_timeline_semaphore
  VK_KHR_external_memory_fd;VK_KHR_external_semaphore_fd;VK_KHR_xlib_surface;VK_KHR_xcb_surface;VK_KHR_wayland_surface
)
set(GLAD_GENERATED_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/glad/target/src/vulkan.c
  ${CMAKE_CURRENT_SOURCE_DIR}/glad/target/include/glad/vulkan.h
)

string(REPLACE ";" "," GLAD_APIS "${GLAD_APIS}")
string(REPLACE ";" "," GLAD_EXTENSIONS "${GLAD_EXTENSIONS}")
# System Python is externally managed on most distributions, so expect Jinja2 to be installed already (e.g. python-jinja)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(
  OUTPUT
    ${GLAD_GENERATED_FILES}
  COMMAND
    ${Python3_EXECUTABLE} -c "import jinja2"
  COMMAND
    ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/khronos/vk.xml ./
  COMMAND
    ${Python3_EXECUTABLE} -m glad --out-path ./target --api ${GLAD_APIS} --extensions ${GLAD_EXTENSIONS} c --mx
  COMMAND
    ${CMAKE_COMMAND} -E remove vk.xml
  DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/khronos/vk.xml
  WORKING_DIRECTORY
    ${CMAKE_CURRENT_SOURCE_DIR}/glad
)

target_sources(
  glad
  PRIVATE
    ${GLAD_GENERATED_FILES}
)

# ImGui

add_library(ImGui STATIC)
target_sources(
  ImGui
  PUBLIC
    imgui/imgui.h
    imgui/imgui_internal.h
    imgui_config.hpp
  PRIVATE
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui_config.cpp
)
target_include_directories(
  ImGui
  PUBLIC
    imgui
)
target_compile_definitions(
  ImGui
  PUBLIC
    ImTextureID=ImU64
    IMGUI_USER_CONFIG="../imgui_config.hpp"
    IMGUI_DEFINE_MATH_OPERATORS
    IMGUI_DISABLE_OBSOLETE_FUNCTIONS
    IMGUI_DISABLE_FILE_FUNCTIONS
    IMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS
  PRIVATE
    "$<$<CONFIG:Release>:IMGUI_DISABLE_DEMO_WINDOWS>"
    "$<$<CONFIG:Release>:IMGUI_DISABLE_DEBUG_TOOLS>"
    "$<$<CONFIG:Release>:IMGUI_DEBUG_PRINTF=((void)0)>"
)

# jxl

add_library(jxl STATIC)
target_sources(
  jxl
  PUBLIC
    jxl_simple_lossless/simple_lossless.h
    jxl_simple_lossless/color_encoding.h
  PRIVATE
    jxl_simple_lossless/simple_lossless.cc
)
target_include_directories(
  jxl
  PUBLIC
    jxl_simple_lossless
)
target_compile_definitions(
  jxl
  PRIVATE
    main=simple_lossless
)

# SPIRV

add_library(SPIRV INTERFACE)
target_include_directories(
  SPIRV
  INTERFACE
    spirv/include/spirv/unified1
)

# stb

add_library(stb STATIC)
target_sources(
  stb
  PUBLIC
    stb/stb_image.h
    stb/stb_image_resize2.h
    stb/stb_image_write.h
    stb_image/stb_image_dds.h
    stb_image/stb_image_write_hdr_png.h
  PRIVATE
    stb_impl.c
)
target_include_directories(
  stb
  PUBLIC
    stb
    stb_image
)
target_compile_definitions(
  stb
  PUBLIC
    STBI_NO_STDIO
    STBI_WRITE_NO_STDIO
    _CRT_SECURE_NO_WARNINGS
)

# utfcpp

add_library(utfcpp INTERFACE)
target_include_directories(
  utfcpp
  INTERFACE
    utfcpp/source
)
target_compile_definitions(
  utfcpp
  INTERFACE
    UTF_CPP_CPLUSPLUS=201703L
)

# VMA

add_library(VMA INTERFACE)
target_include_directories(
  VMA
  INTERFACE
    vma/include
)
target_compile_definitions(
  VMA
  INTERFACE
    VMA_STATIC_VULKAN_FUNCTIONS=0
    VMA_DYNAMIC_VULKAN_FUNCTIONS=0
    VMA_STATS_STRING_ENABLED=0
)
