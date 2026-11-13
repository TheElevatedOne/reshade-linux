/*
 * Copyright (C) 2014 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Functions shared between the Linux specific parts of ReShade.
 */

#pragma once

#include <cstdint>
#include <cstdlib>

namespace reshade::linux_platform
{
	/// <summary>
	/// Initializes ReShade on the first call into the Vulkan layer (see dll_main_linux.cpp).
	/// </summary>
	void initialize();

	/// <summary>
	/// Checks whether an on/off environment variable is turned on (set to anything but an empty string or "0").
	/// </summary>
	inline bool is_env_enabled(const char *name)
	{
		const char *const value = std::getenv(name);
		return value != nullptr && value[0] != '\0' && !(value[0] == '0' && value[1] == '\0');
	}

	/// <summary>
	/// Registers a native Wayland surface, so that the input manager knows to use the Wayland seat of its display for input.
	/// The window handle used for a Wayland surface is the 'wl_surface' pointer.
	/// </summary>
	void register_wayland_surface(void *surface, void *display);
	/// <summary>
	/// Removes a window handle again when its Vulkan surface is destroyed (releases the input resources of a Wayland display when its last surface is gone).
	/// </summary>
	void unregister_window(void *window);
	/// <summary>
	/// Updates the size of the swap chain that renders to a window, which is used when the window size cannot be queried (e.g. on Wayland).
	/// </summary>
	void set_window_size(void *window, uint32_t width, uint32_t height);
}
