/*
 * Copyright (C) 2014 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Global shortcuts through the XDG desktop portal (org.freedesktop.portal.GlobalShortcuts).
 *
 * ReShade announces its actions (toggle overlay, toggle effects, screenshot, ...) to the desktop, which lets the user bind keys to
 * them in the desktop settings (e.g. "Shortcuts" on KDE Plasma, or a "global" bind in Hyprland). Activations then trigger the same
 * actions as the key bindings configured in ReShade itself, regardless of whether ReShade can see the keyboard input of the game.
 */

#pragma once

namespace reshade::global_shortcuts
{
	enum class action
	{
		toggle_overlay,
		toggle_effects,
		screenshot,
		reload_effects,
		next_preset,
		previous_preset,
		toggle_fps,
		count
	};

	/// <summary>
	/// Registers the global shortcuts with the desktop portal on a background thread (does nothing when already started or disabled with RESHADE_DISABLE_GLOBAL_SHORTCUTS=1).
	/// </summary>
	void start();

	/// <summary>
	/// Returns <see langword="true"/> once for every activation of the global shortcut for the specified <paramref name="action"/> since the last call.
	/// </summary>
	bool consume(action action);
}
