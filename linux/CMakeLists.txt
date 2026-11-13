# Native Linux build of ReShade
#
# Builds ReShade as a Vulkan layer (libreshade.so) that is loaded implicitly when the RESHADE=1 environment variable is
# set, the same way MangoHud works. Proton/Wine games are covered too, because DXVK and VKD3D-Proton translate Direct3D
# to Vulkan, which then passes through this layer.
#
# 32-bit build (for 32-bit games, including 32-bit Proton games):
#   cmake -S . -B build-linux32 -G Ninja -DCMAKE_TOOLCHAIN_FILE=linux/toolchain-i686.cmake -DCMAKE_INSTALL_LIBDIR=lib32 -DRESHADE_INSTALL_SHARED_FILES=OFF

option(RESHADE_INSTALL_SHARED_FILES "Install the architecture independent files (launcher, desktop entry), disable for the 32-bit build" ON)

include(GNUInstallDirs)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
# X11 (XInput2 raw events), Wayland (seat on the application connection), keyboard layouts and D-Bus (global shortcuts portal)
pkg_check_modules(RESHADE_PLATFORM REQUIRED IMPORTED_TARGET xcb xcb-xinput wayland-client xkbcommon dbus-1)
# Only the headers are needed for the Vulkan window system integration structures of Xlib
pkg_check_modules(RESHADE_WSI REQUIRED x11)

# Generate version header from the latest git tag
set(RESHADE_VERSION_MAJOR 0)
set(RESHADE_VERSION_MINOR 0)
set(RESHADE_VERSION_REVISION 0)
set(RESHADE_VERSION_BUILD 0)
find_package(Git QUIET)
if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --tags --long
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    OUTPUT_VARIABLE RESHADE_GIT_DESCRIBE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  # Upstream tags are "v<major>.<minor>.<revision>", releases of this port add the port release: "v<major>.<minor>.<revision>.<release>"
  if(RESHADE_GIT_DESCRIBE MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)\\.([0-9]+)-[0-9]+-g")
    set(RESHADE_VERSION_MAJOR ${CMAKE_MATCH_1})
    set(RESHADE_VERSION_MINOR ${CMAKE_MATCH_2})
    set(RESHADE_VERSION_REVISION ${CMAKE_MATCH_3})
    set(RESHADE_VERSION_BUILD ${CMAKE_MATCH_4})
  elseif(RESHADE_GIT_DESCRIBE MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)-([0-9]+)-g")
    set(RESHADE_VERSION_MAJOR ${CMAKE_MATCH_1})
    set(RESHADE_VERSION_MINOR ${CMAKE_MATCH_2})
    set(RESHADE_VERSION_REVISION ${CMAKE_MATCH_3})
    set(RESHADE_VERSION_BUILD ${CMAKE_MATCH_4})
  endif()
endif()
if(RESHADE_VERSION_MAJOR EQUAL 0 AND NOT DEFINED RESHADE_VERSION_OVERRIDE)
  message(WARNING "Could not get the ReShade version from 'git describe' (are the 'v*' tags fetched?). Building as version 0.0.0, set -DRESHADE_VERSION_OVERRIDE=<major.minor.revision> to override.")
endif()
if(DEFINED RESHADE_VERSION_OVERRIDE AND RESHADE_VERSION_OVERRIDE MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)\\.?([0-9]*)")
  set(RESHADE_VERSION_MAJOR ${CMAKE_MATCH_1})
  set(RESHADE_VERSION_MINOR ${CMAKE_MATCH_2})
  set(RESHADE_VERSION_REVISION ${CMAKE_MATCH_3})
  if(CMAKE_MATCH_4)
    set(RESHADE_VERSION_BUILD ${CMAKE_MATCH_4})
  endif()
endif()
configure_file(version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h @ONLY)

set(RESHADE_LAYER_NAME "VK_LAYER_reshade" CACHE STRING "Vulkan layer name")
# Directory with the system-wide default effects ("<data dir>/reshade-shaders") and compatibility list
if(NOT RESHADE_DATA_DIR)
  set(RESHADE_DATA_DIR "${CMAKE_INSTALL_FULL_DATADIR}/reshade")
endif()

add_library(ReShade SHARED)
set_target_properties(ReShade PROPERTIES OUTPUT_NAME reshade)

set(RESHADE_ROOT ${PROJECT_SOURCE_DIR})

target_sources(
  ReShade
  PRIVATE
    ${RESHADE_ROOT}/examples/09-depth/generic_depth_addon.cpp
    ${RESHADE_ROOT}/examples/15-effect_runtime_sync/runtime_sync_addon.cpp
    ${RESHADE_ROOT}/source/addon.cpp
    ${RESHADE_ROOT}/source/addon_manager.cpp
    ${RESHADE_ROOT}/source/dll_log.cpp
    ${RESHADE_ROOT}/source/imgui_code_editor.cpp
    ${RESHADE_ROOT}/source/imgui_function_table.cpp
    ${RESHADE_ROOT}/source/imgui_function_table_18600.cpp
    ${RESHADE_ROOT}/source/imgui_function_table_18971.cpp
    ${RESHADE_ROOT}/source/imgui_function_table_19000.cpp
    ${RESHADE_ROOT}/source/imgui_function_table_19040.cpp
    ${RESHADE_ROOT}/source/imgui_function_table_19180.cpp
    ${RESHADE_ROOT}/source/imgui_function_table_19191.cpp
    ${RESHADE_ROOT}/source/imgui_function_table_19222.cpp
    ${RESHADE_ROOT}/source/imgui_function_table_19250.cpp
    ${RESHADE_ROOT}/source/imgui_widgets.cpp
    ${RESHADE_ROOT}/source/ini_file.cpp
    ${RESHADE_ROOT}/source/input.cpp
    ${RESHADE_ROOT}/source/runtime.cpp
    ${RESHADE_ROOT}/source/runtime_api.cpp
    ${RESHADE_ROOT}/source/runtime_gui.cpp
    ${RESHADE_ROOT}/source/runtime_manager.cpp
    ${RESHADE_ROOT}/source/state_block.cpp
    ${RESHADE_ROOT}/source/vulkan/vulkan.cpp
    ${RESHADE_ROOT}/source/vulkan/vulkan_hooks_command_list.cpp
    ${RESHADE_ROOT}/source/vulkan/vulkan_hooks_device.cpp
    ${RESHADE_ROOT}/source/vulkan/vulkan_hooks_instance.cpp
    ${RESHADE_ROOT}/source/vulkan/vulkan_hooks_swapchain.cpp
    ${RESHADE_ROOT}/source/vulkan/vulkan_impl_command_list.cpp
    ${RESHADE_ROOT}/source/vulkan/vulkan_impl_command_list_immediate.cpp
    ${RESHADE_ROOT}/source/vulkan/vulkan_impl_command_queue.cpp
    ${RESHADE_ROOT}/source/vulkan/vulkan_impl_device.cpp
    ${RESHADE_ROOT}/source/vulkan/vulkan_impl_swapchain.cpp
    ${RESHADE_ROOT}/source/vulkan/vulkan_impl_type_convert.cpp
    ${RESHADE_ROOT}/source/linux/dll_main_linux.cpp
    ${RESHADE_ROOT}/source/linux/dll_resources_linux.cpp
    ${RESHADE_ROOT}/source/linux/dll_resources_linux.S
    ${RESHADE_ROOT}/source/linux/input_linux.cpp
    ${RESHADE_ROOT}/source/linux/platform_utils_linux.cpp
    ${RESHADE_ROOT}/source/linux/runtime_linux.cpp
    ${RESHADE_ROOT}/source/linux/global_shortcuts_linux.cpp
)

set_source_files_properties(${RESHADE_ROOT}/source/vulkan/vulkan_impl_device.cpp PROPERTIES COMPILE_DEFINITIONS VMA_IMPLEMENTATION)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i[3-6]86")
  # Screenshot color conversion uses SSE4.1 and F16C intrinsics
  set_source_files_properties(${RESHADE_ROOT}/source/runtime.cpp PROPERTIES COMPILE_OPTIONS "-msse4.1;-mf16c")
endif()
set_source_files_properties(
  ${RESHADE_ROOT}/examples/09-depth/generic_depth_addon.cpp
  ${RESHADE_ROOT}/examples/15-effect_runtime_sync/runtime_sync_addon.cpp
  PROPERTIES
    COMPILE_DEFINITIONS BUILTIN_ADDON
    COMPILE_FLAGS "-include reshade.hpp"
)
# The assembler file embeds the shader and license resources with '.incbin', so rebuild it when those change
set_source_files_properties(${RESHADE_ROOT}/source/linux/dll_resources_linux.S PROPERTIES
  INCLUDE_DIRECTORIES "${RESHADE_ROOT}"
  OBJECT_DEPENDS "${RESHADE_ROOT}/res/shaders/imgui_vs_450.spv;${RESHADE_ROOT}/res/shaders/imgui_ps_450.spv")
enable_language(ASM)

target_include_directories(
  ReShade
  PRIVATE
    ${RESHADE_WSI_INCLUDE_DIRS}
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${RESHADE_ROOT}/include
    ${RESHADE_ROOT}/res
    ${RESHADE_ROOT}/source
    ${RESHADE_ROOT}/source/linux
)

target_compile_definitions(
  ReShade
  PRIVATE
    RESHADE_GUI
    RESHADE_API_LIBRARY_EXPORT
    RESHADE_ADDON=2
    RESHADE_LAYER_NAME="${RESHADE_LAYER_NAME}"
    RESHADE_DATA_DIR="${RESHADE_DATA_DIR}"
    $<$<CONFIG:Debug>:RESHADE_VERBOSE_LOG>
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

target_compile_options(
  ReShade
  PRIVATE
    -fPIC
    -fvisibility=hidden
    -Wno-changes-meaning
    -Wno-attributes
    $<$<COMPILE_LANGUAGE:CXX>:-include ${RESHADE_ROOT}/source/linux/platform_compat.hpp>
)

target_link_options(
  ReShade
  PRIVATE
    -Wl,--no-undefined
    -Wl,-z,nodelete
    -Wl,--exclude-libs,ALL
    # Avoid depending on the C++ runtime of the host, since games may run in a container (e.g. the Steam Linux Runtime) with a different one
    -static-libstdc++
    -static-libgcc
)

target_link_libraries(ReShade PRIVATE ReShadeFX fpng glad ImGui jxl SPIRV stb utfcpp VMA PkgConfig::RESHADE_PLATFORM Threads::Threads ${CMAKE_DL_LIBS})

# Vulkan layer manifest, enabled through the RESHADE environment variable (like MANGOHUD=1)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(RESHADE_ARCH x86_64)
else()
  set(RESHADE_ARCH x86)
endif()
configure_file(reshade_layer.json.in ${CMAKE_CURRENT_BINARY_DIR}/reshade_layer.json @ONLY)

install(TARGETS ReShade LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/reshade)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/reshade_layer.json DESTINATION ${CMAKE_INSTALL_DATADIR}/vulkan/implicit_layer.d RENAME reshade.${RESHADE_ARCH}.json)
if(RESHADE_INSTALL_SHARED_FILES)
  configure_file(reshade ${CMAKE_CURRENT_BINARY_DIR}/reshade @ONLY)
  install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/reshade DESTINATION ${CMAKE_INSTALL_BINDIR})
  # Desktop entry for the application ID announced to the global shortcuts portal (gives the shortcuts a name and icon in the desktop settings)
  install(FILES me.reshade.ReShade.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES ${RESHADE_ROOT}/res/main_icon.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps RENAME me.reshade.ReShade.svg)
endif()
