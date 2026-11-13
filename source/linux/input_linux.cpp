/*
 * Copyright (C) 2014 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Linux input implementation.
 *
 * ReShade is a Vulkan layer here, so it cannot intercept the window messages of the application like it does on Windows.
 * Instead it receives input the way the window system allows a library inside the application to:
 *
 * - X11 (including XWayland, which Wine/Proton uses by default):
 *   ReShade opens its own connection to the X server and listens to XInput2 raw events (which are delivered to every client that
 *   selects them on the root window). Input is only accepted while the window ReShade renders to is focused.
 *   To keep the application from reacting to input while the overlay is in use, ReShade grabs the pointer/keyboard on its own
 *   connection, so that the X server delivers the core input events only to ReShade (set RESHADE_DISABLE_INPUT_GRAB=1 to disable).
 *   XCB is used instead of Xlib, since Xlib reports protocol errors through a process-wide error handler, which would interfere
 *   with the application. XCB returns errors per request instead.
 *
 * - Wayland:
 *   ReShade binds its own 'wl_seat' on the 'wl_display' connection of the application, using a separate event queue.
 *   The compositor sends pointer and keyboard events of the client to every seat object the client bound, so ReShade receives the
 *   same input as the application while one of its surfaces has focus. Wayland offers no way to hide input from the application.
 *
 * In addition, the actions can be bound to keys in the desktop settings through the global shortcuts portal (see global_shortcuts_linux.cpp).
 */

#include "input.hpp"
#include "dll_log.hpp"
#include "platform_linux.hpp"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/input-event-codes.h>
#include <xcb/xcb.h>
#include <xcb/xinput.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>

using window_handle = reshade::input::window_handle;

static std::shared_mutex s_windows_mutex;
static std::unordered_map<window_handle, std::weak_ptr<reshade::input>> s_windows;
static std::atomic<bool> s_block_mouse = false;
static std::atomic<bool> s_block_keyboard = false;
static std::atomic<bool> s_block_cursor_warping = false;

// Swap chain sizes of windows, used when the window size cannot be queried
static std::unordered_map<window_handle, std::pair<uint32_t, uint32_t>> s_window_sizes;

static unsigned int get_time_ms()
{
	return static_cast<unsigned int>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

#pragma region Key mapping

/// <summary>
/// Converts a key symbol to the matching Windows virtual key code, which ReShade uses internally for key bindings.
/// </summary>
static unsigned int keysym_to_vk(xkb_keysym_t keysym)
{
	if (keysym >= XKB_KEY_a && keysym <= XKB_KEY_z)
		return 'A' + (keysym - XKB_KEY_a);
	if (keysym >= XKB_KEY_A && keysym <= XKB_KEY_Z)
		return 'A' + (keysym - XKB_KEY_A);
	if (keysym >= XKB_KEY_0 && keysym <= XKB_KEY_9)
		return '0' + (keysym - XKB_KEY_0);
	if (keysym >= XKB_KEY_F1 && keysym <= XKB_KEY_F24)
		return 0x70 + (keysym - XKB_KEY_F1);
	if (keysym >= XKB_KEY_KP_0 && keysym <= XKB_KEY_KP_9)
		return 0x60 + (keysym - XKB_KEY_KP_0);

	switch (keysym)
	{
	case XKB_KEY_BackSpace: return 0x08;
	case XKB_KEY_Tab: case XKB_KEY_ISO_Left_Tab: return 0x09;
	case XKB_KEY_Clear: case XKB_KEY_KP_Begin: return 0x0C;
	case XKB_KEY_Return: case XKB_KEY_KP_Enter: return 0x0D;
	case XKB_KEY_Pause: return 0x13;
	case XKB_KEY_Caps_Lock: return 0x14;
	case XKB_KEY_Escape: return 0x1B;
	case XKB_KEY_space: case XKB_KEY_KP_Space: return 0x20;
	case XKB_KEY_Prior: case XKB_KEY_KP_Prior: return 0x21;
	case XKB_KEY_Next: case XKB_KEY_KP_Next: return 0x22;
	case XKB_KEY_End: case XKB_KEY_KP_End: return 0x23;
	case XKB_KEY_Home: case XKB_KEY_KP_Home: return 0x24;
	case XKB_KEY_Left: case XKB_KEY_KP_Left: return 0x25;
	case XKB_KEY_Up: case XKB_KEY_KP_Up: return 0x26;
	case XKB_KEY_Right: case XKB_KEY_KP_Right: return 0x27;
	case XKB_KEY_Down: case XKB_KEY_KP_Down: return 0x28;
	case XKB_KEY_Select: return 0x29;
	case XKB_KEY_Print: case XKB_KEY_Sys_Req: return 0x2C;
	case XKB_KEY_Insert: case XKB_KEY_KP_Insert: return 0x2D;
	case XKB_KEY_Delete: case XKB_KEY_KP_Delete: return 0x2E;
	case XKB_KEY_Help: return 0x2F;
	case XKB_KEY_Super_L: return 0x5B;
	case XKB_KEY_Super_R: return 0x5C;
	case XKB_KEY_Menu: return 0x5D;
	case XKB_KEY_XF86Sleep: return 0x5F;
	case XKB_KEY_KP_Multiply: return 0x6A;
	case XKB_KEY_KP_Add: return 0x6B;
	case XKB_KEY_KP_Separator: return 0x6C;
	case XKB_KEY_KP_Subtract: return 0x6D;
	case XKB_KEY_KP_Decimal: return 0x6E;
	case XKB_KEY_KP_Divide: return 0x6F;
	case XKB_KEY_Num_Lock: return 0x90;
	case XKB_KEY_Scroll_Lock: return 0x91;
	case XKB_KEY_Shift_L: return 0xA0;
	case XKB_KEY_Shift_R: return 0xA1;
	case XKB_KEY_Control_L: return 0xA2;
	case XKB_KEY_Control_R: return 0xA3;
	case XKB_KEY_Alt_L: case XKB_KEY_Meta_L: return 0xA4;
	case XKB_KEY_Alt_R: case XKB_KEY_Meta_R: case XKB_KEY_ISO_Level3_Shift: case XKB_KEY_Mode_switch: return 0xA5;
	case XKB_KEY_XF86Back: return 0xA6;
	case XKB_KEY_XF86Forward: return 0xA7;
	case XKB_KEY_XF86Refresh: return 0xA8;
	case XKB_KEY_XF86Stop: return 0xA9;
	case XKB_KEY_XF86Search: return 0xAA;
	case XKB_KEY_XF86Favorites: return 0xAB;
	case XKB_KEY_XF86HomePage: return 0xAC;
	case XKB_KEY_XF86AudioMute: return 0xAD;
	case XKB_KEY_XF86AudioLowerVolume: return 0xAE;
	case XKB_KEY_XF86AudioRaiseVolume: return 0xAF;
	case XKB_KEY_XF86AudioNext: return 0xB0;
	case XKB_KEY_XF86AudioPrev: return 0xB1;
	case XKB_KEY_XF86AudioStop: return 0xB2;
	case XKB_KEY_XF86AudioPlay: case XKB_KEY_XF86AudioPause: return 0xB3;
	case XKB_KEY_XF86Mail: return 0xB4;
	case XKB_KEY_semicolon: case XKB_KEY_colon: return 0xBA;
	case XKB_KEY_equal: case XKB_KEY_plus: return 0xBB;
	case XKB_KEY_comma: case XKB_KEY_less: return 0xBC;
	case XKB_KEY_minus: case XKB_KEY_underscore: return 0xBD;
	case XKB_KEY_period: case XKB_KEY_greater: return 0xBE;
	case XKB_KEY_slash: case XKB_KEY_question: return 0xBF;
	case XKB_KEY_grave: case XKB_KEY_asciitilde: case XKB_KEY_dead_grave: return 0xC0;
	case XKB_KEY_bracketleft: case XKB_KEY_braceleft: return 0xDB;
	case XKB_KEY_backslash: case XKB_KEY_bar: return 0xDC;
	case XKB_KEY_bracketright: case XKB_KEY_braceright: return 0xDD;
	case XKB_KEY_apostrophe: case XKB_KEY_quotedbl: case XKB_KEY_dead_acute: return 0xDE;
	}
	return 0;
}

/// <summary>
/// Gets the virtual key code of a key that produces a character not covered by <see cref="keysym_to_vk"/> (e.g. on national layouts), based on its position on a US keyboard.
/// </summary>
/// <param name="keycode">X11/XKB key code (Linux evdev key code + 8).</param>
static unsigned int keycode_position_to_vk(uint32_t keycode)
{
	switch (keycode - 8)
	{
	case KEY_ESC: return 0x1B;
	case KEY_MINUS: return 0xBD;
	case KEY_EQUAL: return 0xBB;
	case KEY_BACKSPACE: return 0x08;
	case KEY_TAB: return 0x09;
	case KEY_LEFTBRACE: return 0xDB;
	case KEY_RIGHTBRACE: return 0xDD;
	case KEY_ENTER: return 0x0D;
	case KEY_LEFTCTRL: return 0xA2;
	case KEY_SEMICOLON: return 0xBA;
	case KEY_APOSTROPHE: return 0xDE;
	case KEY_GRAVE: return 0xC0;
	case KEY_LEFTSHIFT: return 0xA0;
	case KEY_BACKSLASH: return 0xDC;
	case KEY_COMMA: return 0xBC;
	case KEY_DOT: return 0xBE;
	case KEY_SLASH: return 0xBF;
	case KEY_RIGHTSHIFT: return 0xA1;
	case KEY_LEFTALT: return 0xA4;
	case KEY_SPACE: return 0x20;
	case KEY_102ND: return 0xE2;
	case KEY_RIGHTCTRL: return 0xA3;
	case KEY_RIGHTALT: return 0xA5;
	case KEY_HOME: return 0x24;
	case KEY_UP: return 0x26;
	case KEY_PAGEUP: return 0x21;
	case KEY_LEFT: return 0x25;
	case KEY_RIGHT: return 0x27;
	case KEY_END: return 0x23;
	case KEY_DOWN: return 0x28;
	case KEY_PAGEDOWN: return 0x22;
	case KEY_INSERT: return 0x2D;
	case KEY_DELETE: return 0x2E;
	}
	return 0;
}

/// <summary>
/// Input events gathered by a backend, which are applied to the input state in 'next_frame'.
/// </summary>
struct input_event
{
	enum type_t : uint8_t { key, button, wheel, text, focus_lost } type;
	bool down;
	uint32_t value; // Virtual key code, button index, wheel delta or character
};

static void apply_events(uint8_t keys[256], unsigned int keys_time[256], short &mouse_wheel_delta, std::wstring &text_input, const std::vector<input_event> &events)
{
	const unsigned int time = get_time_ms();

	for (const input_event &event : events)
	{
		switch (event.type)
		{
		case input_event::key:
		{
			const unsigned int vk = event.value;
			if (vk == 0 || vk >= 256)
				break;

			keys[vk] = event.down ? 0x88 : 0x08;
			keys_time[vk] = time;

			// Update the generic modifier keys as well, which is what Windows reports in 'WM_KEYDOWN'
			unsigned int generic_vk = 0;
			if (vk == 0xA0 || vk == 0xA1)
				generic_vk = reshade::input::key_shift;
			else if (vk == 0xA2 || vk == 0xA3)
				generic_vk = reshade::input::key_ctrl;
			else if (vk == 0xA4 || vk == 0xA5)
				generic_vk = reshade::input::key_alt;
			if (generic_vk != 0)
			{
				// Left and right variants of a modifier key only differ in the lowest bit (e.g. 0xA0 and 0xA1)
				const bool other_down = (keys[vk ^ 1] & 0x80) != 0;
				if (event.down)
					keys[generic_vk] = 0x88;
				else if (!other_down)
					keys[generic_vk] = 0x08;
			}
			break;
		}
		case input_event::button:
		{
			static constexpr unsigned int button_keys[] = {
				reshade::input::key_button_left, reshade::input::key_button_right, reshade::input::key_button_middle, reshade::input::key_button_xbutton1, reshade::input::key_button_xbutton2 };
			if (event.value < std::size(button_keys))
				keys[button_keys[event.value]] = event.down ? 0x88 : 0x08;
			break;
		}
		case input_event::wheel:
			mouse_wheel_delta += static_cast<short>(static_cast<int32_t>(event.value));
			break;
		case input_event::text:
			// Skip control characters (those are handled through the key states instead) and characters outside the BMP (since text input is UTF-16)
			if (event.value >= 0x20 && event.value != 0x7F && event.value < 0x10000)
				text_input += static_cast<wchar_t>(event.value);
			break;
		case input_event::focus_lost:
			// Release all keys when focus is lost, so that they do not get stuck
			for (unsigned int i = 0; i < 256; ++i)
				if ((keys[i] & 0x80) != 0)
					keys[i] = 0x08;
			break;
		}
	}
}

#pragma endregion

#pragma region X11

namespace
{
	template <typename T>
	struct xcb_reply
	{
		explicit xcb_reply(T *reply) : reply(reply) {}
		~xcb_reply() { std::free(reply); }
		T *operator->() const { return reply; }
		explicit operator bool() const { return reply != nullptr; }
		T *reply;
	};

	struct x11_connection
	{
		std::mutex mutex;
		xcb_connection_t *connection = nullptr;
		xcb_window_t root = XCB_NONE;
		uint8_t xi_opcode = 0;
		bool initialized = false;
		bool grab_enabled = true;

		// Core keyboard mapping (key code to key symbols for each shift level and group)
		xcb_keycode_t min_keycode = 0;
		uint8_t keysyms_per_keycode = 0;
		std::vector<xcb_keysym_t> keysyms;

		xcb_window_t grabbed_pointer_window = XCB_NONE;
		xcb_window_t grabbed_keyboard_window = XCB_NONE;

		// Events that were read while the polled window was not focused, kept for a moment in case another window of the application is
		std::vector<input_event> pending_events;
		unsigned int pending_events_time = 0;
		// Focus state of each polled window during its last poll
		std::unordered_map<xcb_window_t, bool> was_focused;

		xcb_connection_t *get()
		{
			if (initialized)
				return connection;
			initialized = true;

			if (std::getenv("DISPLAY") == nullptr)
			{
				reshade::log::message(reshade::log::level::warning, "No X11 display available (DISPLAY is not set), so ReShade cannot receive keyboard and mouse input for X11 windows.");
				return nullptr;
			}

			int screen_index = 0;
			connection = xcb_connect(nullptr, &screen_index);
			if (connection == nullptr || xcb_connection_has_error(connection))
			{
				reshade::log::message(reshade::log::level::warning, "Failed to connect to X11 display, so ReShade cannot receive keyboard and mouse input.");
				shutdown();
				return nullptr;
			}

			xcb_screen_iterator_t screen_it = xcb_setup_roots_iterator(xcb_get_setup(connection));
			for (int i = 0; i < screen_index && screen_it.rem; ++i)
				xcb_screen_next(&screen_it);
			if (screen_it.rem == 0)
			{
				shutdown();
				return nullptr;
			}
			root = screen_it.data->root;

			// Set up XInput2 raw events
			const xcb_query_extension_reply_t *const xi_extension = xcb_get_extension_data(connection, &xcb_input_id);
			if (xi_extension == nullptr || !xi_extension->present)
			{
				reshade::log::message(reshade::log::level::warning, "XInput extension is not available on the X11 display, so ReShade cannot receive keyboard and mouse input.");
				shutdown();
				return nullptr;
			}
			xi_opcode = xi_extension->major_opcode;

			if (const xcb_reply<xcb_input_xi_query_version_reply_t> version(xcb_input_xi_query_version_reply(connection, xcb_input_xi_query_version(connection, 2, 2), nullptr));
				!version || version->major_version < 2)
			{
				reshade::log::message(reshade::log::level::warning, "XInput2 is not available on the X11 display, so ReShade cannot receive keyboard and mouse input.");
				shutdown();
				return nullptr;
			}

			struct
			{
				xcb_input_event_mask_t header;
				uint32_t mask;
			} event_mask;
			event_mask.header.deviceid = XCB_INPUT_DEVICE_ALL_MASTER;
			event_mask.header.mask_len = 1;
			event_mask.mask =
				XCB_INPUT_XI_EVENT_MASK_RAW_KEY_PRESS | XCB_INPUT_XI_EVENT_MASK_RAW_KEY_RELEASE |
				XCB_INPUT_XI_EVENT_MASK_RAW_BUTTON_PRESS | XCB_INPUT_XI_EVENT_MASK_RAW_BUTTON_RELEASE;
			if (xcb_generic_error_t *const error = xcb_request_check(connection, xcb_input_xi_select_events_checked(connection, root, 1, &event_mask.header)))
			{
				reshade::log::message(reshade::log::level::warning, "Failed to select XInput2 raw events (error code %d), so ReShade cannot receive keyboard and mouse input.", static_cast<int>(error->error_code));
				std::free(error);
				shutdown();
				return nullptr;
			}

			update_keyboard_mapping();

			grab_enabled = !reshade::linux_platform::is_env_enabled("RESHADE_DISABLE_INPUT_GRAB");

			reshade::log::message(reshade::log::level::info, "Opened X11 display connection for input.");
			return connection;
		}

		void update_keyboard_mapping()
		{
			const xcb_setup_t *const setup = xcb_get_setup(connection);
			min_keycode = setup->min_keycode;

			const xcb_reply<xcb_get_keyboard_mapping_reply_t> mapping(xcb_get_keyboard_mapping_reply(connection,
				xcb_get_keyboard_mapping(connection, setup->min_keycode, static_cast<uint8_t>(setup->max_keycode - setup->min_keycode + 1)), nullptr));
			if (!mapping)
				return;

			keysyms_per_keycode = mapping->keysyms_per_keycode;
			const xcb_keysym_t *const data = xcb_get_keyboard_mapping_keysyms(mapping.reply);
			keysyms.assign(data, data + xcb_get_keyboard_mapping_keysyms_length(mapping.reply));
		}

		/// <summary>
		/// Looks up the key symbol for a key code from the core keyboard mapping.
		/// </summary>
		/// <param name="state">Core modifier state (as reported by 'QueryPointer'), which includes the XKB group in bits 13-14.</param>
		xcb_keysym_t lookup_keysym(xcb_keycode_t keycode, uint16_t state, bool for_text) const
		{
			if (keycode < min_keycode || keysyms_per_keycode == 0)
				return XCB_NO_SYMBOL;
			const size_t base = static_cast<size_t>(keycode - min_keycode) * keysyms_per_keycode;
			if (base + keysyms_per_keycode > keysyms.size())
				return XCB_NO_SYMBOL;

			const auto get = [&](unsigned int column) -> xcb_keysym_t {
				return column < keysyms_per_keycode ? keysyms[base + column] : XCB_NO_SYMBOL;
			};

			if (!for_text)
				return get(0);

			// Columns are: group 1 (level 1, level 2), group 2 (level 1, level 2), and with XKB group 1 (level 3, level 4)
			unsigned int column = 0;
			if ((state & XCB_MOD_MASK_5) != 0 && get(4) != XCB_NO_SYMBOL)
				column = 4; // AltGr (ISO_Level3_Shift is usually mapped to Mod5)
			else if (((state >> 13) & 0x3) != 0 && get(2) != XCB_NO_SYMBOL)
				column = 2; // Second group

			const xcb_keysym_t lower = get(column);
			xcb_keysym_t upper = get(column + 1);
			if (upper == XCB_NO_SYMBOL)
				upper = static_cast<xcb_keysym_t>(xkb_keysym_to_upper(lower));

			const bool shift = (state & XCB_MOD_MASK_SHIFT) != 0;
			const bool caps = (state & XCB_MOD_MASK_LOCK) != 0 && xkb_keysym_to_upper(lower) != xkb_keysym_to_lower(lower);
			return (shift != caps) ? upper : lower;
		}

		void shutdown()
		{
			if (connection != nullptr)
				xcb_disconnect(connection);
			connection = nullptr;
		}

		~x11_connection()
		{
			shutdown();
		}
	};

	x11_connection s_x11;
}

static xcb_window_t to_x11_window(window_handle window)
{
	return static_cast<xcb_window_t>(reinterpret_cast<uintptr_t>(window));
}

/// <summary>
/// Gets the top-level window (direct child of the root window) that contains the specified window.
/// </summary>
static xcb_window_t get_toplevel_window(xcb_connection_t *connection, xcb_window_t window)
{
	for (int depth = 0; depth < 64 && window != XCB_NONE && window != s_x11.root; ++depth)
	{
		const xcb_reply<xcb_query_tree_reply_t> tree(xcb_query_tree_reply(connection, xcb_query_tree(connection, window), nullptr));
		if (!tree)
			return XCB_NONE;
		if (tree->parent == tree->root || tree->parent == XCB_NONE)
			return window;
		window = tree->parent;
	}
	return XCB_NONE;
}

static bool is_x11_window_focused(xcb_connection_t *connection, xcb_window_t window)
{
	const xcb_reply<xcb_get_input_focus_reply_t> focus_reply(xcb_get_input_focus_reply(connection, xcb_get_input_focus(connection), nullptr));
	if (!focus_reply)
		return false;
	const xcb_window_t focus = focus_reply->focus;
	if (focus == XCB_NONE || focus == XCB_INPUT_FOCUS_POINTER_ROOT)
		return false;
	if (focus == window)
		return true;

	// Wine and other toolkits usually focus the top-level window (or a focus proxy inside it), while rendering happens to a child window
	const xcb_window_t toplevel = get_toplevel_window(connection, window);
	return toplevel != XCB_NONE && toplevel == get_toplevel_window(connection, focus);
}

/// <summary>
/// Updates the active pointer and keyboard grabs, so that input does not reach the application while ReShade is blocking it.
/// </summary>
static void update_x11_grabs(xcb_connection_t *connection, xcb_window_t window, bool focused, bool block_mouse, bool block_keyboard)
{
	if (!s_x11.grab_enabled)
		return;

	const xcb_window_t desired_pointer_window = (focused && block_mouse) ? window : XCB_NONE;
	if (desired_pointer_window != s_x11.grabbed_pointer_window)
	{
		if (s_x11.grabbed_pointer_window != XCB_NONE)
			xcb_ungrab_pointer(connection, XCB_CURRENT_TIME);
		s_x11.grabbed_pointer_window = XCB_NONE;

		if (desired_pointer_window != XCB_NONE)
		{
			const xcb_reply<xcb_grab_pointer_reply_t> grab(xcb_grab_pointer_reply(connection,
				xcb_grab_pointer(connection, 0, desired_pointer_window, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE, XCB_CURRENT_TIME), nullptr));
			if (grab && grab->status == XCB_GRAB_STATUS_SUCCESS)
				s_x11.grabbed_pointer_window = desired_pointer_window;
		}
	}

	const xcb_window_t desired_keyboard_window = (focused && block_keyboard) ? window : XCB_NONE;
	if (desired_keyboard_window != s_x11.grabbed_keyboard_window)
	{
		if (s_x11.grabbed_keyboard_window != XCB_NONE)
			xcb_ungrab_keyboard(connection, XCB_CURRENT_TIME);
		s_x11.grabbed_keyboard_window = XCB_NONE;

		if (desired_keyboard_window != XCB_NONE)
		{
			const xcb_reply<xcb_grab_keyboard_reply_t> grab(xcb_grab_keyboard_reply(connection,
				xcb_grab_keyboard(connection, 0, desired_keyboard_window, XCB_CURRENT_TIME, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC), nullptr));
			if (grab && grab->status == XCB_GRAB_STATUS_SUCCESS)
				s_x11.grabbed_keyboard_window = desired_keyboard_window;
		}
	}

	xcb_flush(connection);
}

/// <summary>
/// Polls the X server for input to the specified window. Returns <see langword="false"/> if the connection is not available.
/// </summary>
static bool poll_x11_input(xcb_window_t window, std::vector<input_event> &events, unsigned int mouse_position[2], uint32_t &button_mask, bool &focused, bool block_mouse, bool block_keyboard)
{
	const std::lock_guard<std::mutex> x11_lock(s_x11.mutex);
	xcb_connection_t *const connection = s_x11.get();
	if (connection == nullptr)
		return false;

	focused = is_x11_window_focused(connection, window);

	// Query pointer position and modifier state first, which is needed to translate key presses to text
	uint16_t state = 0;
	if (const xcb_reply<xcb_query_pointer_reply_t> pointer(xcb_query_pointer_reply(connection, xcb_query_pointer(connection, window), nullptr)); pointer)
	{
		mouse_position[0] = static_cast<unsigned int>(std::max<int>(pointer->win_x, 0));
		mouse_position[1] = static_cast<unsigned int>(std::max<int>(pointer->win_y, 0));
		state = pointer->mask;
		button_mask =
			((state & XCB_KEY_BUT_MASK_BUTTON_1) != 0 ? 0x1 : 0) |
			((state & XCB_KEY_BUT_MASK_BUTTON_3) != 0 ? 0x2 : 0) |
			((state & XCB_KEY_BUT_MASK_BUTTON_2) != 0 ? 0x4 : 0);
	}

	// Raw events are read for all windows at once, so do not hand events that are older than a few frames to a window that gets focused later
	const unsigned int time = get_time_ms();
	if (!s_x11.pending_events.empty() && (time - s_x11.pending_events_time) > 100)
		s_x11.pending_events.clear();
	if (s_x11.pending_events.empty())
		s_x11.pending_events_time = time;

	std::vector<input_event> &pending = s_x11.pending_events;

	// Drain pending events (core events are only received while grabbing and are ignored, since raw events are received in addition)
	while (xcb_generic_event_t *const event = xcb_poll_for_event(connection))
	{
		const uint8_t response_type = event->response_type & 0x7F;
		if (response_type == XCB_MAPPING_NOTIFY)
		{
			s_x11.update_keyboard_mapping();
		}
		else if (response_type == XCB_GE_GENERIC && reinterpret_cast<const xcb_ge_generic_event_t *>(event)->extension == s_x11.xi_opcode)
		{
			const uint16_t event_type = reinterpret_cast<const xcb_ge_generic_event_t *>(event)->event_type;
			const uint32_t detail = reinterpret_cast<const xcb_input_raw_key_press_event_t *>(event)->detail;

			switch (event_type)
			{
			case XCB_INPUT_RAW_KEY_PRESS:
			case XCB_INPUT_RAW_KEY_RELEASE:
			{
				const bool down = event_type == XCB_INPUT_RAW_KEY_PRESS;
				const xcb_keycode_t keycode = static_cast<xcb_keycode_t>(detail);

				unsigned int vk = keysym_to_vk(s_x11.lookup_keysym(keycode, state, false));
				if (vk == 0)
					vk = keycode_position_to_vk(keycode);
				pending.push_back({ input_event::key, down, vk });

				if (down && (state & XCB_MOD_MASK_CONTROL) == 0)
					if (const uint32_t c = xkb_keysym_to_utf32(s_x11.lookup_keysym(keycode, state, true)); c != 0)
						pending.push_back({ input_event::text, true, c });
				break;
			}
			case XCB_INPUT_RAW_BUTTON_PRESS:
			case XCB_INPUT_RAW_BUTTON_RELEASE:
			{
				const bool down = event_type == XCB_INPUT_RAW_BUTTON_PRESS;
				switch (detail)
				{
				case 1: pending.push_back({ input_event::button, down, 0 }); break;
				case 2: pending.push_back({ input_event::button, down, 2 }); break;
				case 3: pending.push_back({ input_event::button, down, 1 }); break;
				case 4: if (down) pending.push_back({ input_event::wheel, true, static_cast<uint32_t>(+1) }); break;
				case 5: if (down) pending.push_back({ input_event::wheel, true, static_cast<uint32_t>(-1) }); break;
				case 8: pending.push_back({ input_event::button, down, 3 }); break;
				case 9: pending.push_back({ input_event::button, down, 4 }); break;
				}
				break;
			}
			}
		}
		std::free(event);
	}

	if (xcb_connection_has_error(connection))
	{
		reshade::log::message(reshade::log::level::warning, "Lost connection to X11 display, input is no longer received.");
		s_x11.shutdown();
		return false;
	}

	bool &was_focused = s_x11.was_focused[window];
	if (focused)
	{
		events = std::move(s_x11.pending_events);
		s_x11.pending_events.clear();
	}
	else if (was_focused)
	{
		// Release all keys, so that they do not get stuck
		events.push_back({ input_event::focus_lost, false, 0 });
	}
	was_focused = focused;

	update_x11_grabs(connection, window, focused, block_mouse, block_keyboard);
	return true;
}

#pragma endregion

#pragma region Wayland

namespace
{
	struct wayland_connection
	{
		wl_display *display = nullptr;
		wl_event_queue *queue = nullptr;
		wl_display *display_wrapper = nullptr;
		wl_registry *registry = nullptr;
		wl_seat *seat = nullptr;
		uint32_t seat_version = 0;
		wl_pointer *pointer = nullptr;
		wl_keyboard *keyboard = nullptr;

		xkb_context *xkb_ctx = nullptr;
		xkb_keymap *xkb_map = nullptr;
		xkb_state *xkb_kb_state = nullptr;

		unsigned int surface_count = 0;
		bool keyboard_focused = false;
		bool pointer_inside = false;
		double pointer_x = 0.0, pointer_y = 0.0;
		uint32_t button_mask = 0;
		double wheel_accumulator = 0.0;
		std::vector<input_event> events;

		void release_pointer()
		{
			if (pointer != nullptr)
				(wl_proxy_get_version(reinterpret_cast<wl_proxy *>(pointer)) >= WL_POINTER_RELEASE_SINCE_VERSION) ? wl_pointer_release(pointer) : wl_pointer_destroy(pointer);
			pointer = nullptr;
		}
		void release_keyboard()
		{
			if (keyboard != nullptr)
				(wl_proxy_get_version(reinterpret_cast<wl_proxy *>(keyboard)) >= WL_KEYBOARD_RELEASE_SINCE_VERSION) ? wl_keyboard_release(keyboard) : wl_keyboard_destroy(keyboard);
			keyboard = nullptr;
		}

		void destroy()
		{
			release_pointer();
			release_keyboard();
			if (seat != nullptr)
				(seat_version >= WL_SEAT_RELEASE_SINCE_VERSION) ? wl_seat_release(seat) : wl_seat_destroy(seat);
			if (registry != nullptr)
				wl_registry_destroy(registry);
			if (display_wrapper != nullptr)
				wl_proxy_wrapper_destroy(display_wrapper);
			if (display != nullptr)
				wl_display_flush(display);
			if (queue != nullptr)
				wl_event_queue_destroy(queue);

			if (xkb_kb_state != nullptr)
				xkb_state_unref(xkb_kb_state);
			if (xkb_map != nullptr)
				xkb_keymap_unref(xkb_map);
			if (xkb_ctx != nullptr)
				xkb_context_unref(xkb_ctx);

			*this = wayland_connection();
		}
	};

	std::mutex s_wayland_mutex;
	std::unordered_map<wl_display *, wayland_connection> s_wayland_connections;
	std::unordered_map<window_handle, wl_display *> s_wayland_surfaces;
	// Number of Vulkan surfaces created for each Wayland surface
	std::unordered_map<window_handle, unsigned int> s_wayland_surface_references;

	// Pointer events

	void on_pointer_enter(void *data, wl_pointer *, uint32_t, wl_surface *, wl_fixed_t x, wl_fixed_t y)
	{
		auto &c = *static_cast<wayland_connection *>(data);
		c.pointer_inside = true;
		c.pointer_x = wl_fixed_to_double(x);
		c.pointer_y = wl_fixed_to_double(y);
	}
	void on_pointer_leave(void *data, wl_pointer *, uint32_t, wl_surface *)
	{
		auto &c = *static_cast<wayland_connection *>(data);
		c.pointer_inside = false;
		// Buttons are released implicitly when the pointer leaves
		for (uint32_t i = 0; i < 5; ++i)
			if ((c.button_mask & (1u << i)) != 0)
				c.events.push_back({ input_event::button, false, i });
		c.button_mask = 0;
	}
	void on_pointer_motion(void *data, wl_pointer *, uint32_t, wl_fixed_t x, wl_fixed_t y)
	{
		auto &c = *static_cast<wayland_connection *>(data);
		c.pointer_x = wl_fixed_to_double(x);
		c.pointer_y = wl_fixed_to_double(y);
	}
	void on_pointer_button(void *data, wl_pointer *, uint32_t, uint32_t, uint32_t button, uint32_t state)
	{
		auto &c = *static_cast<wayland_connection *>(data);
		uint32_t index;
		switch (button)
		{
		case BTN_LEFT: index = 0; break;
		case BTN_RIGHT: index = 1; break;
		case BTN_MIDDLE: index = 2; break;
		case BTN_SIDE: index = 3; break;
		case BTN_EXTRA: index = 4; break;
		default: return;
		}
		const bool down = state == WL_POINTER_BUTTON_STATE_PRESSED;
		c.button_mask = down ? (c.button_mask | (1u << index)) : (c.button_mask & ~(1u << index));
		c.events.push_back({ input_event::button, down, index });
	}
	void add_wheel(wayland_connection &c, double notches)
	{
		c.wheel_accumulator += notches;
		const int whole = static_cast<int>(c.wheel_accumulator);
		if (whole != 0)
		{
			c.wheel_accumulator -= whole;
			c.events.push_back({ input_event::wheel, true, static_cast<uint32_t>(whole) });
		}
	}
	void on_pointer_axis(void *data, wl_pointer *, uint32_t, uint32_t axis, wl_fixed_t value)
	{
		auto &c = *static_cast<wayland_connection *>(data);
		// Only use the continuous axis value when the seat is too old for discrete steps (10 units are one step, positive is down)
		if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL && c.seat_version < WL_POINTER_AXIS_DISCRETE_SINCE_VERSION)
			add_wheel(c, -wl_fixed_to_double(value) / 10.0);
	}
	void on_pointer_axis_discrete(void *data, wl_pointer *, uint32_t axis, int32_t discrete)
	{
		auto &c = *static_cast<wayland_connection *>(data);
		if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL && c.seat_version < WL_POINTER_AXIS_VALUE120_SINCE_VERSION)
			add_wheel(c, -static_cast<double>(discrete));
	}
	void on_pointer_axis_value120(void *data, wl_pointer *, uint32_t axis, int32_t value120)
	{
		auto &c = *static_cast<wayland_connection *>(data);
		if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
			add_wheel(c, -value120 / 120.0);
	}
	void on_pointer_frame(void *, wl_pointer *) {}
	void on_pointer_axis_source(void *, wl_pointer *, uint32_t) {}
	void on_pointer_axis_stop(void *, wl_pointer *, uint32_t, uint32_t) {}
	void on_pointer_axis_relative_direction(void *, wl_pointer *, uint32_t, uint32_t) {}

	// Keyboard events

	void on_keyboard_keymap(void *data, wl_keyboard *, uint32_t format, int32_t fd, uint32_t size)
	{
		auto &c = *static_cast<wayland_connection *>(data);
		if (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 && c.xkb_ctx != nullptr)
		{
			if (void *const mapping = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0); mapping != MAP_FAILED)
			{
				if (xkb_keymap *const keymap = xkb_keymap_new_from_buffer(c.xkb_ctx, static_cast<const char *>(mapping), strnlen(static_cast<const char *>(mapping), size), XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS))
				{
					if (c.xkb_kb_state != nullptr)
						xkb_state_unref(c.xkb_kb_state);
					if (c.xkb_map != nullptr)
						xkb_keymap_unref(c.xkb_map);
					c.xkb_map = keymap;
					c.xkb_kb_state = xkb_state_new(keymap);
				}
				munmap(mapping, size);
			}
		}
		close(fd);
	}
	void on_keyboard_enter(void *data, wl_keyboard *, uint32_t, wl_surface *, wl_array *)
	{
		static_cast<wayland_connection *>(data)->keyboard_focused = true;
	}
	void on_keyboard_leave(void *data, wl_keyboard *, uint32_t, wl_surface *)
	{
		auto &c = *static_cast<wayland_connection *>(data);
		c.keyboard_focused = false;
		c.events.push_back({ input_event::focus_lost, false, 0 });
	}
	void on_keyboard_key(void *data, wl_keyboard *, uint32_t, uint32_t, uint32_t key, uint32_t state)
	{
		auto &c = *static_cast<wayland_connection *>(data);
		const xkb_keycode_t keycode = key + 8;
		const bool down = state == WL_KEYBOARD_KEY_STATE_PRESSED;

		unsigned int vk = 0;
		if (c.xkb_kb_state != nullptr)
		{
			const xkb_layout_index_t layout = xkb_state_key_get_layout(c.xkb_kb_state, keycode);
			const xkb_keysym_t *syms = nullptr;
			if (layout != XKB_LAYOUT_INVALID && xkb_keymap_key_get_syms_by_level(c.xkb_map, keycode, layout, 0, &syms) > 0)
				vk = keysym_to_vk(syms[0]);
		}
		if (vk == 0)
			vk = keycode_position_to_vk(keycode);
		c.events.push_back({ input_event::key, down, vk });

		if (down && c.xkb_kb_state != nullptr && xkb_state_mod_name_is_active(c.xkb_kb_state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) <= 0)
			if (const uint32_t ch = xkb_state_key_get_utf32(c.xkb_kb_state, keycode); ch != 0)
				c.events.push_back({ input_event::text, true, ch });
	}
	void on_keyboard_modifiers(void *data, wl_keyboard *, uint32_t, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group)
	{
		auto &c = *static_cast<wayland_connection *>(data);
		if (c.xkb_kb_state != nullptr)
			xkb_state_update_mask(c.xkb_kb_state, depressed, latched, locked, 0, 0, group);
	}
	void on_keyboard_repeat_info(void *, wl_keyboard *, int32_t, int32_t) {}

	// Seat and registry events

	void on_seat_capabilities(void *data, wl_seat *seat, uint32_t capabilities)
	{
		auto &c = *static_cast<wayland_connection *>(data);

		static wl_pointer_listener pointer_listener = {};
		pointer_listener.enter = on_pointer_enter;
		pointer_listener.leave = on_pointer_leave;
		pointer_listener.motion = on_pointer_motion;
		pointer_listener.button = on_pointer_button;
		pointer_listener.axis = on_pointer_axis;
		pointer_listener.frame = on_pointer_frame;
		pointer_listener.axis_source = on_pointer_axis_source;
		pointer_listener.axis_stop = on_pointer_axis_stop;
		pointer_listener.axis_discrete = on_pointer_axis_discrete;
		pointer_listener.axis_value120 = on_pointer_axis_value120;
		pointer_listener.axis_relative_direction = on_pointer_axis_relative_direction;

		static wl_keyboard_listener keyboard_listener = {};
		keyboard_listener.keymap = on_keyboard_keymap;
		keyboard_listener.enter = on_keyboard_enter;
		keyboard_listener.leave = on_keyboard_leave;
		keyboard_listener.key = on_keyboard_key;
		keyboard_listener.modifiers = on_keyboard_modifiers;
		keyboard_listener.repeat_info = on_keyboard_repeat_info;

		if ((capabilities & WL_SEAT_CAPABILITY_POINTER) != 0 && c.pointer == nullptr)
		{
			c.pointer = wl_seat_get_pointer(seat);
			wl_pointer_add_listener(c.pointer, &pointer_listener, &c);
		}
		else if ((capabilities & WL_SEAT_CAPABILITY_POINTER) == 0 && c.pointer != nullptr)
		{
			// The device was removed (e.g. the mouse was unplugged)
			c.release_pointer();
			on_pointer_leave(&c, nullptr, 0, nullptr);
		}
		if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0 && c.keyboard == nullptr)
		{
			c.keyboard = wl_seat_get_keyboard(seat);
			wl_keyboard_add_listener(c.keyboard, &keyboard_listener, &c);
		}
		else if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) == 0 && c.keyboard != nullptr)
		{
			c.release_keyboard();
			on_keyboard_leave(&c, nullptr, 0, nullptr);
		}
	}
	void on_seat_name(void *, wl_seat *, const char *) {}

	void on_registry_global(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
	{
		auto &c = *static_cast<wayland_connection *>(data);
		if (c.seat != nullptr || std::strcmp(interface, wl_seat_interface.name) != 0)
			return;

		static const wl_seat_listener seat_listener = { on_seat_capabilities, on_seat_name };

		// Version 9 is the last one without new events that would need handling (version 10 adds repeated key events)
		c.seat_version = std::min<uint32_t>(version, 9);
		c.seat = static_cast<wl_seat *>(wl_registry_bind(registry, name, &wl_seat_interface, c.seat_version));
		wl_seat_add_listener(c.seat, &seat_listener, &c);
	}
	void on_registry_global_remove(void *, wl_registry *, uint32_t) {}

	/// <summary>
	/// Reads and dispatches pending events of the private event queue without blocking.
	/// </summary>
	void dispatch_wayland_events(wayland_connection &c)
	{
		while (wl_display_prepare_read_queue(c.display, c.queue) != 0)
			wl_display_dispatch_queue_pending(c.display, c.queue);
		wl_display_flush(c.display);

		pollfd fd = { wl_display_get_fd(c.display), POLLIN, 0 };
		if (poll(&fd, 1, 0) > 0)
			wl_display_read_events(c.display);
		else
			wl_display_cancel_read(c.display);

		wl_display_dispatch_queue_pending(c.display, c.queue);
	}

	/// <summary>
	/// Binds a seat on the display of the application with a private event queue, so that it does not interfere with the event processing of the application.
	/// </summary>
	bool create_wayland_connection(wayland_connection &c, wl_display *display)
	{
		c.display = display;
		c.queue = wl_display_create_queue(display);
		c.display_wrapper = static_cast<wl_display *>(wl_proxy_create_wrapper(display));
		if (c.queue == nullptr || c.display_wrapper == nullptr)
			return false;
		wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(c.display_wrapper), c.queue);

		c.xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

		static const wl_registry_listener registry_listener = { on_registry_global, on_registry_global_remove };
		c.registry = wl_display_get_registry(c.display_wrapper);
		wl_registry_add_listener(c.registry, &registry_listener, &c);

		// Receive the globals, then the seat capabilities and the keymap
		for (int i = 0; i < 2; ++i)
			if (wl_display_roundtrip_queue(display, c.queue) < 0)
				return false;

		return c.seat != nullptr;
	}
}

#pragma endregion

void reshade::linux_platform::register_wayland_surface(void *surface, void *display)
{
	const std::lock_guard<std::mutex> lock(s_wayland_mutex);
	if (s_wayland_surface_references[surface]++ == 0)
	{
		s_wayland_surfaces.emplace(surface, static_cast<wl_display *>(display));
		s_wayland_connections[static_cast<wl_display *>(display)].surface_count++;
	}
}

void reshade::linux_platform::unregister_window(void *window)
{
	{
		const std::unique_lock<std::shared_mutex> lock(s_windows_mutex);
		s_window_sizes.erase(window);
	}

	const std::lock_guard<std::mutex> lock(s_wayland_mutex);
	const auto it = s_wayland_surfaces.find(window);
	if (it == s_wayland_surfaces.end())
		return;
	if (const auto ref_it = s_wayland_surface_references.find(window); ref_it != s_wayland_surface_references.end() && --ref_it->second != 0)
		return;
	s_wayland_surface_references.erase(window);

	// The application has to destroy the Vulkan surface before it disconnects from the display, so this is the last chance to release the objects bound on it
	if (const auto connection_it = s_wayland_connections.find(it->second);
		connection_it != s_wayland_connections.end() && --connection_it->second.surface_count == 0)
	{
		if (connection_it->second.display != nullptr)
			reshade::log::message(reshade::log::level::info, "Releasing Wayland seat used for input.");
		connection_it->second.destroy();
		s_wayland_connections.erase(connection_it);
	}

	s_wayland_surfaces.erase(it);
}

void reshade::linux_platform::set_window_size(void *window, uint32_t width, uint32_t height)
{
	const std::unique_lock<std::shared_mutex> lock(s_windows_mutex);
	s_window_sizes[window] = { width, height };
}

static bool is_wayland_window(window_handle window)
{
	const std::lock_guard<std::mutex> lock(s_wayland_mutex);
	return s_wayland_surfaces.find(window) != s_wayland_surfaces.end();
}

bool reshade::input::is_keyboard_layout_german()
{
	// Only used to adjust key names, which are always shown for the US layout here
	return false;
}

void reshade::input::register_window_with_raw_input(window_handle, bool, bool)
{
	// Raw input is a Windows concept, input is always received through XInput2 or the Wayland seat here
}

std::shared_ptr<reshade::input> reshade::input::register_window(window_handle window)
{
	assert(window != nullptr);

	if (std::unique_lock<std::mutex> wayland_lock(s_wayland_mutex); s_wayland_surfaces.find(window) != s_wayland_surfaces.end())
	{
		wl_display *const display = s_wayland_surfaces.at(window);
		wayland_connection &c = s_wayland_connections[display];
		if (c.display == nullptr)
		{
			if (!create_wayland_connection(c, display))
			{
				reshade::log::message(reshade::log::level::warning, "Failed to bind a Wayland seat, so ReShade cannot receive keyboard and mouse input. Global shortcuts can still be used to open the overlay.");
				const unsigned int surface_count = c.surface_count;
				c.destroy();
				c.surface_count = surface_count;
				return nullptr;
			}
			reshade::log::message(reshade::log::level::info, "Bound Wayland seat (version %u) for input.", c.seat_version);
		}
	}
	else
	{
		wayland_lock.unlock();

		const std::lock_guard<std::mutex> x11_lock(s_x11.mutex);
		if (s_x11.get() == nullptr)
			return nullptr;
	}

	const std::unique_lock<std::shared_mutex> lock(s_windows_mutex);

	const auto insert = s_windows.emplace(window, std::weak_ptr<input>());

	if (insert.second || insert.first->second.expired())
	{
		reshade::log::message(reshade::log::level::info, "Starting input capture for window %p.", window);

		const auto instance = std::make_shared<input>(window);
		insert.first->second = instance;

		return instance;
	}
	else
	{
		return insert.first->second.lock();
	}
}

bool reshade::input::handle_window_message(const void *)
{
	// There are no window messages to handle, input is polled in 'next_frame' instead
	return false;
}

void reshade::input::max_mouse_position(unsigned int position[2]) const
{
	position[0] = 0;
	position[1] = 0;

	if (!is_wayland_window(_window))
	{
		const std::lock_guard<std::mutex> x11_lock(s_x11.mutex);
		if (xcb_connection_t *const connection = s_x11.get(); connection != nullptr)
		{
			if (const xcb_reply<xcb_get_geometry_reply_t> geometry(xcb_get_geometry_reply(connection, xcb_get_geometry(connection, to_x11_window(_window)), nullptr)); geometry)
			{
				position[0] = geometry->width;
				position[1] = geometry->height;
			}
		}
	}

	// Wayland surface coordinates are assumed to match the swap chain size (there is no way to query the logical size of a surface owned by the application)
	if (position[0] == 0 || position[1] == 0)
	{
		const std::shared_lock<std::shared_mutex> lock(s_windows_mutex);
		if (const auto it = s_window_sizes.find(_window); it != s_window_sizes.end())
		{
			position[0] = it->second.first;
			position[1] = it->second.second;
		}
	}

	// Avoid division by zero when scaling the mouse position
	position[0] = std::max(position[0], 1u);
	position[1] = std::max(position[1], 1u);
}

void reshade::input::next_frame()
{
	_frame_count++;

	// Backup key states from the last processed frame so that state transitions can be identified
	std::copy_n(_keys, 256, _last_keys);

	for (uint8_t &state : _keys)
		state &= ~0x08;

	_text_input.clear();
	_mouse_wheel_delta = 0;
	_last_mouse_position[0] = _mouse_position[0];
	_last_mouse_position[1] = _mouse_position[1];

	std::vector<input_event> events;
	uint32_t button_mask = 0;
	bool focused = false;
	bool has_button_mask = false;

	if (is_wayland_window(_window))
	{
		const std::lock_guard<std::mutex> lock(s_wayland_mutex);
		const auto display_it = s_wayland_surfaces.find(_window);
		if (display_it != s_wayland_surfaces.end())
		{
			wayland_connection &c = s_wayland_connections[display_it->second];
			if (c.display != nullptr)
			{
				dispatch_wayland_events(c);

				events = std::move(c.events);
				c.events.clear();
				focused = c.keyboard_focused || c.pointer_inside;

				if (c.pointer_inside)
				{
					_mouse_position[0] = static_cast<unsigned int>(std::max(c.pointer_x, 0.0));
					_mouse_position[1] = static_cast<unsigned int>(std::max(c.pointer_y, 0.0));
				}

				if (c.xkb_kb_state != nullptr)
					_keys[key_caps_lock] = (_keys[key_caps_lock] & ~0x1) | (xkb_state_mod_name_is_active(c.xkb_kb_state, XKB_MOD_NAME_CAPS, XKB_STATE_MODS_LOCKED) > 0 ? 0x1 : 0x0);
			}
		}
	}
	else
	{
		has_button_mask = poll_x11_input(to_x11_window(_window), events, _mouse_position, button_mask, focused, _block_mouse, _block_keyboard);
	}

	apply_events(_keys, _keys_time, _mouse_wheel_delta, _text_input, events);

	if (focused)
	{
		// Reset any pressed down key states that have not been updated for more than 5 seconds, in case a release was missed
		const unsigned int time = get_time_ms();
		for (unsigned int i = 8; i < 256; ++i)
			if ((_keys[i] & 0x80) != 0 && (time - _keys_time[i]) > 5000 && i != key_shift && i != key_ctrl && i != key_alt && (i < 0xA0 || i > 0xA5))
				_keys[i] = 0x08;
	}

	// Fix up mouse button state in case a release event was missed (e.g. because focus changed while a button was held down)
	if (has_button_mask)
	{
		const unsigned int button_keys[3] = { key_button_left, key_button_right, key_button_middle };
		for (unsigned int i = 0; i < 3; ++i)
			if ((button_mask & (1u << i)) == 0 && (_keys[button_keys[i]] & 0x80) != 0 && (_last_keys[button_keys[i]] & 0x80) != 0)
				_keys[button_keys[i]] = 0x08;
	}

	// Run through all forms of input blocking for all windows and establish whether any of them are blocking input
	{
		const std::shared_lock<std::shared_mutex> lock(s_windows_mutex);

		const auto any_window = [](bool(input::*is_blocking)() const) {
			return std::any_of(s_windows.cbegin(), s_windows.cend(),
				[is_blocking](const std::pair<const window_handle, std::weak_ptr<input>> &input_window) {
					const auto instance = input_window.second.lock();
					return instance != nullptr && (instance.get()->*is_blocking)();
				});
		};

		s_block_mouse.store(any_window(&input::is_blocking_mouse_input));
		s_block_keyboard.store(any_window(&input::is_blocking_keyboard_input));
		s_block_cursor_warping.store(any_window(&input::is_blocking_mouse_cursor_warping));
	}
}

void reshade::input::block_mouse_cursor_warping(bool enable)
{
	_block_cursor_warping = enable;
}

bool reshade::input::is_blocking_any_mouse_input(window_handle target)
{
	if (target != nullptr)
	{
		const std::shared_lock<std::shared_mutex> lock(s_windows_mutex);

		return std::any_of(s_windows.cbegin(), s_windows.cend(),
			[target](const std::pair<const window_handle, std::weak_ptr<input>> &input_window) {
				const auto instance = input_window.second.lock();
				return instance != nullptr && input_window.first == target && instance->is_blocking_mouse_input();
			});
	}

	return s_block_mouse.load();
}
bool reshade::input::is_blocking_any_keyboard_input(window_handle target)
{
	if (target != nullptr)
	{
		const std::shared_lock<std::shared_mutex> lock(s_windows_mutex);

		return std::any_of(s_windows.cbegin(), s_windows.cend(),
			[target](const std::pair<const window_handle, std::weak_ptr<input>> &input_window) {
				const auto instance = input_window.second.lock();
				return instance != nullptr && input_window.first == target && instance->is_blocking_keyboard_input();
			});
	}

	return s_block_keyboard.load();
}
bool reshade::input::is_blocking_any_mouse_cursor_warping()
{
	return s_block_cursor_warping.load();
}

std::shared_ptr<reshade::input_gamepad> reshade::input_gamepad::load()
{
	// Gamepad input is not supported on Linux yet
	return nullptr;
}

void reshade::input_gamepad::next_frame()
{
}
