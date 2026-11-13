/*
 * Copyright (C) 2021 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Minimal module (shared object) helpers for the Linux build, mirroring the Win32 module functions used by ReShade.
 * A module handle is the 'link_map' pointer of a shared object, which is what 'dlopen' returns with glibc and what 'dladdr1' reports for an address.
 */

#pragma once

#include <atomic>
#include <filesystem>
#include <dlfcn.h>
#include <link.h>

typedef void *HMODULE;

extern HMODULE g_module_handle;

/// <summary>
/// Gets the handle of the module that contains the specified <paramref name="address"/>.
/// </summary>
inline HMODULE get_module_handle_from_address(const void *address)
{
	Dl_info info = {};
	link_map *map = nullptr;
	if (!dladdr1(address, &info, reinterpret_cast<void **>(&map), RTLD_DL_LINKMAP))
		return nullptr;
	return map;
}

/// <summary>
/// Returns the path to the module file identified by the specified <paramref name="module"/> handle.
/// </summary>
inline std::filesystem::path get_module_path(HMODULE module)
{
	if (module == nullptr)
	{
		std::error_code ec;
		return std::filesystem::read_symlink("/proc/self/exe", ec);
	}
	const link_map *const map = static_cast<const link_map *>(module);
	return map->l_name != nullptr ? std::filesystem::path(map->l_name) : std::filesystem::path();
}

inline void *GetProcAddress(HMODULE module, const char *name)
{
	return module != nullptr ? dlsym(module, name) : nullptr;
}

inline bool FreeLibrary(HMODULE module)
{
	return dlclose(module) == 0;
}

template <typename T>
inline T InterlockedIncrement(volatile T *value)
{
	return __atomic_add_fetch(value, 1, __ATOMIC_SEQ_CST);
}
template <typename T>
inline T InterlockedDecrement(volatile T *value)
{
	return __atomic_sub_fetch(value, 1, __ATOMIC_SEQ_CST);
}
