/*
 * Copyright (C) 2014 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause OR MIT
 *
 * Included in every source file of the Linux build (see linux/CMakeLists.txt).
 */

#pragma once

#include "reshade_platform.hpp"

#include <cstdio>
#include <string>

// Share modes of '_wfsopen' are not supported on Linux (and not needed, since files are not locked by default)
#define SH_DENYNO 0x40
#define SH_DENYWR 0x20

inline FILE *_wfsopen(const char *path, const wchar_t *mode, int)
{
	std::string narrow_mode;
	for (; *mode != L'\0'; ++mode)
		narrow_mode += static_cast<char>(*mode);
	return std::fopen(path, narrow_mode.c_str());
}

#include <cerrno>
#include <cstdlib>
#include <ctime>

// Window handles are X11 window IDs (or Wayland surface pointers) on Linux
typedef void *HWND;

// Microsoft secure CRT functions used by ReShade (note the argument order differs from the POSIX variants)
inline int localtime_s(std::tm *result, const std::time_t *time)
{
	return localtime_r(time, result) != nullptr ? 0 : EINVAL;
}

inline int getenv_s(size_t *required_size, char *buffer, size_t buffer_size, const char *name)
{
	const char *const value = std::getenv(name);
	if (value == nullptr)
	{
		*required_size = 0;
		if (buffer != nullptr && buffer_size != 0)
			buffer[0] = '\0';
		return EINVAL; // Treat missing variables as failure, so that callers keep their default value
	}
	const size_t length = std::strlen(value);
	*required_size = length + 1;
	if (buffer == nullptr || buffer_size <= length)
		return ERANGE;
	std::memcpy(buffer, value, length + 1);
	return 0;
}
