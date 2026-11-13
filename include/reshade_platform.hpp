/*
 * Copyright (C) 2021 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause OR MIT
 */

#pragma once

// Support compilers without Microsoft extensions when not targeting Windows (e.g. GCC and Clang when building for Linux)
#if !defined(_WIN32) && !defined(RESHADE_PLATFORM_COMPAT)
#define RESHADE_PLATFORM_COMPAT 1

#include <cstdint>
#include <cstring>

#define __declspec(x) __reshade_declspec_##x
#define __reshade_declspec_novtable
#define __reshade_declspec_dllexport __attribute__((visibility("default")))
#define __reshade_declspec_dllimport
#define __reshade_declspec_uuid(x)

namespace reshade::internal
{
	struct type_guid { uint8_t data[16]; };

	/// <summary>
	/// Replacement for '__uuidof', which is only available with Microsoft compilers.
	/// Produces an identifier that is unique per type and module (made from the address of a per-module object and a hash of the type name).
	/// This is sufficient for private data, which is only ever accessed with the types of the module that stored it.
	/// </summary>
	template <typename T>
	inline const type_guid &uuidof()
	{
		static const type_guid guid = []() {
			uint64_t hash = 14695981039346656037ull;
			for (const char *name = __PRETTY_FUNCTION__; *name != '\0'; ++name)
				hash = (hash ^ static_cast<uint8_t>(*name)) * 1099511628211ull;

			static const char module_marker = 0;
			const uint64_t address = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&module_marker));

			type_guid result;
			std::memcpy(result.data, &address, 8);
			std::memcpy(result.data + 8, &hash, 8);
			return result;
		}();
		return guid;
	}
}

#define __uuidof(T) (::reshade::internal::uuidof<T>())

#endif
