/*
 * Copyright (C) 2014 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause OR MIT
 */

#include "dll_resources.hpp"
#include <cassert>

#define RESHADE_DECLARE_RESOURCE(name) \
	extern "C" const unsigned char reshade_resource_##name[]; \
	extern "C" const unsigned char reshade_resource_##name##_end[];

RESHADE_DECLARE_RESOURCE(imgui_vs_spirv)
RESHADE_DECLARE_RESOURCE(imgui_ps_spirv)
RESHADE_DECLARE_RESOURCE(license_glad)
RESHADE_DECLARE_RESOURCE(license_imgui)
RESHADE_DECLARE_RESOURCE(license_reshade)
RESHADE_DECLARE_RESOURCE(license_spirv)
RESHADE_DECLARE_RESOURCE(license_stb)
RESHADE_DECLARE_RESOURCE(license_utfcpp)
RESHADE_DECLARE_RESOURCE(license_vma)
RESHADE_DECLARE_RESOURCE(license_s_jxl)

reshade::resources::data_resource reshade::resources::load_data_resource(unsigned short id)
{
#define RESHADE_RESOURCE_CASE(id, name) \
	case id: \
		return { static_cast<size_t>(reshade_resource_##name##_end - reshade_resource_##name), reshade_resource_##name }

	switch (id)
	{
	RESHADE_RESOURCE_CASE(IDR_IMGUI_VS_SPIRV, imgui_vs_spirv);
	RESHADE_RESOURCE_CASE(IDR_IMGUI_PS_SPIRV, imgui_ps_spirv);
	RESHADE_RESOURCE_CASE(IDR_LICENSE_GLAD, license_glad);
	RESHADE_RESOURCE_CASE(IDR_LICENSE_IMGUI, license_imgui);
	RESHADE_RESOURCE_CASE(IDR_LICENSE_RESHADE, license_reshade);
	RESHADE_RESOURCE_CASE(IDR_LICENSE_SPIRV, license_spirv);
	RESHADE_RESOURCE_CASE(IDR_LICENSE_STB, license_stb);
	RESHADE_RESOURCE_CASE(IDR_LICENSE_UTFCPP, license_utfcpp);
	RESHADE_RESOURCE_CASE(IDR_LICENSE_VMA, license_vma);
	RESHADE_RESOURCE_CASE(IDR_LICENSE_S_JXL, license_s_jxl);
	default:
		// Direct3D and OpenGL resources are not available in the Linux build
		assert(false);
		return { 0, nullptr };
	}

#undef RESHADE_RESOURCE_CASE
}
