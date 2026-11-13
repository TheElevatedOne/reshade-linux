/*
 * Copyright (C) 2014 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause OR MIT
 *
 * Embeds the binary resources (the Windows version of ReShade stores these in a Win32 resource script).
 */

#define RESHADE_RESOURCE(name, file) \
	.section .rodata.reshade_resources, "a"; \
	.balign 16; \
	.global reshade_resource_##name; \
	.hidden reshade_resource_##name; \
	reshade_resource_##name: \
	.incbin file; \
	.global reshade_resource_##name##_end; \
	.hidden reshade_resource_##name##_end; \
	reshade_resource_##name##_end: \
	.byte 0;

RESHADE_RESOURCE(imgui_vs_spirv, "res/shaders/imgui_vs_450.spv")
RESHADE_RESOURCE(imgui_ps_spirv, "res/shaders/imgui_ps_450.spv")
RESHADE_RESOURCE(license_glad, "deps/glad/LICENSE")
RESHADE_RESOURCE(license_imgui, "deps/imgui/LICENSE.txt")
RESHADE_RESOURCE(license_reshade, "LICENSE.md")
RESHADE_RESOURCE(license_spirv, "deps/spirv/LICENSE")
RESHADE_RESOURCE(license_stb, "deps/stb/LICENSE")
RESHADE_RESOURCE(license_utfcpp, "deps/utfcpp/LICENSE")
RESHADE_RESOURCE(license_vma, "deps/vma/LICENSE.txt")
RESHADE_RESOURCE(license_s_jxl, "deps/jxl_simple_lossless/LICENSE")

.section .note.GNU-stack, "", @progbits
