/*
 * Copyright (C) 2014 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Global shortcuts through the XDG desktop portal, see https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.GlobalShortcuts.html
 *
 * Uses a private session bus connection on its own thread, so that it does not interfere with any D-Bus usage of the application.
 */

#include "global_shortcuts_linux.hpp"
#include "dll_log.hpp"
#include "platform_linux.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <dbus/dbus.h>
#include <unistd.h>

using namespace reshade::global_shortcuts;

namespace
{
	struct shortcut_info
	{
		const char *id;
		const char *description;
	};

	// Order matches the 'action' enumeration
	constexpr shortcut_info s_shortcuts[] = {
		{ "toggle-overlay", "ReShade: Toggle overlay" },
		{ "toggle-effects", "ReShade: Toggle effects" },
		{ "screenshot", "ReShade: Take screenshot" },
		{ "reload-effects", "ReShade: Reload effects" },
		{ "next-preset", "ReShade: Switch to next preset" },
		{ "previous-preset", "ReShade: Switch to previous preset" },
		{ "toggle-fps", "ReShade: Toggle FPS counter" },
	};
	static_assert(std::size(s_shortcuts) == static_cast<size_t>(action::count));

	// Application ID announced to the portal (matches the installed "me.reshade.ReShade.desktop" file)
	constexpr const char s_app_id[] = "me.reshade.ReShade";

	constexpr const char s_portal_bus_name[] = "org.freedesktop.portal.Desktop";
	constexpr const char s_portal_object_path[] = "/org/freedesktop/portal/desktop";
	constexpr const char s_shortcuts_interface[] = "org.freedesktop.portal.GlobalShortcuts";

	std::atomic<bool> s_started = false;
	std::atomic<bool> s_stop = false;
	std::atomic<unsigned int> s_pending[static_cast<size_t>(action::count)] = {};

	struct scoped_message
	{
		explicit scoped_message(DBusMessage *message) : message(message) {}
		~scoped_message() { if (message != nullptr) dbus_message_unref(message); }
		DBusMessage *message;
	};

	void append_string_variant(DBusMessageIter *dict, const char *key, const char *value)
	{
		DBusMessageIter entry, variant;
		dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
		dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
		dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant);
		dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &value);
		dbus_message_iter_close_container(&entry, &variant);
		dbus_message_iter_close_container(dict, &entry);
	}

	/// <summary>
	/// Builds the object path of the request object the portal creates for a call with the specified handle token.
	/// </summary>
	std::string make_request_path(DBusConnection *connection, const std::string &token)
	{
		// The unique name of the connection is ":1.42", which becomes "1_42" in the path
		std::string sender = dbus_bus_get_unique_name(connection) + 1;
		for (char &c : sender)
			if (c == '.')
				c = '_';
		return std::string(s_portal_object_path) + "/request/" + sender + '/' + token;
	}

	/// <summary>
	/// Sends a method call and waits for the 'Response' signal of the request object it creates.
	/// Returns the response message (or nullptr on failure or timeout), which the caller has to unreference.
	/// </summary>
	DBusMessage *call_and_wait_for_response(DBusConnection *connection, DBusMessage *call, const std::string &request_path)
	{
		const std::string match_rule = "type='signal',interface='org.freedesktop.portal.Request',member='Response',path='" + request_path + '\'';
		dbus_bus_add_match(connection, match_rule.c_str(), nullptr);

		DBusError error;
		dbus_error_init(&error);
		const scoped_message reply(dbus_connection_send_with_reply_and_block(connection, call, 5000, &error));
		if (reply.message == nullptr)
		{
			reshade::log::message(reshade::log::level::warning, "Global shortcuts portal call '%s' failed: %s", dbus_message_get_member(call), error.message != nullptr ? error.message : "unknown error");
			dbus_error_free(&error);
			dbus_bus_remove_match(connection, match_rule.c_str(), nullptr);
			return nullptr;
		}

		DBusMessage *response = nullptr;

		// The portal may show a dialog to the user (e.g. KDE asks to confirm new shortcuts), so wait long enough for that
		const auto timeout = std::chrono::steady_clock::now() + std::chrono::minutes(5);
		while (response == nullptr && !s_stop && std::chrono::steady_clock::now() < timeout && dbus_connection_read_write(connection, 200))
		{
			while (DBusMessage *const message = dbus_connection_pop_message(connection))
			{
				if (response == nullptr && dbus_message_is_signal(message, "org.freedesktop.portal.Request", "Response") && request_path == dbus_message_get_path(message))
					response = message;
				else
					dbus_message_unref(message);
			}
		}

		dbus_bus_remove_match(connection, match_rule.c_str(), nullptr);
		return response;
	}

	/// <summary>
	/// Reads the response code and optionally a string value from the results of a 'Response' signal.
	/// </summary>
	uint32_t parse_response(DBusMessage *response, const char *result_key = nullptr, std::string *result_value = nullptr)
	{
		DBusMessageIter args;
		if (!dbus_message_iter_init(response, &args) || dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_UINT32)
			return 2;

		uint32_t code = 2;
		dbus_message_iter_get_basic(&args, &code);

		if (result_key != nullptr && dbus_message_iter_next(&args) && dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_ARRAY)
		{
			DBusMessageIter dict;
			for (dbus_message_iter_recurse(&args, &dict); dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY; dbus_message_iter_next(&dict))
			{
				DBusMessageIter entry, variant;
				dbus_message_iter_recurse(&dict, &entry);
				const char *key = nullptr;
				dbus_message_iter_get_basic(&entry, &key);
				if (!dbus_message_iter_next(&entry) || std::strcmp(key, result_key) != 0)
					continue;
				dbus_message_iter_recurse(&entry, &variant);
				if (const int type = dbus_message_iter_get_arg_type(&variant); type == DBUS_TYPE_STRING || type == DBUS_TYPE_OBJECT_PATH)
				{
					const char *value = nullptr;
					dbus_message_iter_get_basic(&variant, &value);
					*result_value = value;
				}
			}
		}

		return code;
	}

	/// <summary>
	/// Announces the application ID to the portal, since it cannot be derived for applications that are not sandboxed (only supported by newer portal versions).
	/// </summary>
	void register_app_id(DBusConnection *connection)
	{
		const scoped_message call(dbus_message_new_method_call(s_portal_bus_name, s_portal_object_path, "org.freedesktop.host.portal.Registry", "Register"));
		DBusMessageIter args, options;
		dbus_message_iter_init_append(call.message, &args);
		const char *app_id = s_app_id;
		dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &app_id);
		dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &options);
		dbus_message_iter_close_container(&args, &options);

		DBusError error;
		dbus_error_init(&error);
		const scoped_message reply(dbus_connection_send_with_reply_and_block(connection, call.message, 2000, &error));
		if (reply.message == nullptr)
		{
			reshade::log::message(reshade::log::level::info, "Could not register application ID with the desktop portal (%s), continuing without.", error.message != nullptr ? error.message : "unknown error");
			dbus_error_free(&error);
		}
	}

	void portal_thread_main()
	{
		// The application may use libdbus on other threads at the same time
		dbus_threads_init_default();

		DBusError error;
		dbus_error_init(&error);

		DBusConnection *const connection = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
		if (connection == nullptr)
		{
			reshade::log::message(reshade::log::level::info, "Global shortcuts are not available, because there is no D-Bus session bus (%s).", error.message != nullptr ? error.message : "unknown error");
			dbus_error_free(&error);
			return;
		}
		dbus_connection_set_exit_on_disconnect(connection, false);

		register_app_id(connection);

		const std::string token_prefix = "reshade_" + std::to_string(getpid()) + '_';

		// Create a global shortcuts session
		std::string session_handle;
		{
			const std::string handle_token = token_prefix + "session_request";
			const std::string session_token = token_prefix + "session";

			const scoped_message call(dbus_message_new_method_call(s_portal_bus_name, s_portal_object_path, s_shortcuts_interface, "CreateSession"));
			DBusMessageIter args, options;
			dbus_message_iter_init_append(call.message, &args);
			dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &options);
			append_string_variant(&options, "handle_token", handle_token.c_str());
			append_string_variant(&options, "session_handle_token", session_token.c_str());
			dbus_message_iter_close_container(&args, &options);

			const scoped_message response(call_and_wait_for_response(connection, call.message, make_request_path(connection, handle_token)));
			if (response.message == nullptr || parse_response(response.message, "session_handle", &session_handle) != 0 || session_handle.empty())
			{
				reshade::log::message(reshade::log::level::info, "Global shortcuts are not available, because the desktop portal did not create a session (the desktop may not support org.freedesktop.portal.GlobalShortcuts).");
				dbus_connection_close(connection);
				dbus_connection_unref(connection);
				return;
			}
		}

		// Listen for activations before binding, so that none are missed
		const std::string activated_match_rule = "type='signal',interface='" + std::string(s_shortcuts_interface) + "',path='" + s_portal_object_path + '\'';
		dbus_bus_add_match(connection, activated_match_rule.c_str(), nullptr);

		// Announce the shortcuts (without preferred triggers, so that the user picks keys that do not collide with anything else)
		{
			const std::string handle_token = token_prefix + "bind_request";

			const scoped_message call(dbus_message_new_method_call(s_portal_bus_name, s_portal_object_path, s_shortcuts_interface, "BindShortcuts"));
			DBusMessageIter args, shortcuts, options;
			dbus_message_iter_init_append(call.message, &args);
			const char *session_path = session_handle.c_str();
			dbus_message_iter_append_basic(&args, DBUS_TYPE_OBJECT_PATH, &session_path);
			dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "(sa{sv})", &shortcuts);
			for (const shortcut_info &shortcut : s_shortcuts)
			{
				DBusMessageIter shortcut_struct, shortcut_options;
				dbus_message_iter_open_container(&shortcuts, DBUS_TYPE_STRUCT, nullptr, &shortcut_struct);
				dbus_message_iter_append_basic(&shortcut_struct, DBUS_TYPE_STRING, &shortcut.id);
				dbus_message_iter_open_container(&shortcut_struct, DBUS_TYPE_ARRAY, "{sv}", &shortcut_options);
				append_string_variant(&shortcut_options, "description", shortcut.description);
				dbus_message_iter_close_container(&shortcut_struct, &shortcut_options);
				dbus_message_iter_close_container(&shortcuts, &shortcut_struct);
			}
			dbus_message_iter_close_container(&args, &shortcuts);
			const char *parent_window = "";
			dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &parent_window);
			dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &options);
			append_string_variant(&options, "handle_token", handle_token.c_str());
			dbus_message_iter_close_container(&args, &options);

			const scoped_message response(call_and_wait_for_response(connection, call.message, make_request_path(connection, handle_token)));
			if (response.message == nullptr || parse_response(response.message) != 0)
				reshade::log::message(reshade::log::level::warning, "Desktop portal did not bind the ReShade global shortcuts (the request was cancelled or failed).");
			else
				reshade::log::message(reshade::log::level::info, "Registered global shortcuts with the desktop portal (application ID '%s'). Assign keys to them in the desktop shortcut settings.", s_app_id);
		}

		// Process activations until ReShade is unloaded
		while (!s_stop && dbus_connection_read_write(connection, 200))
		{
			while (DBusMessage *const message = dbus_connection_pop_message(connection))
			{
				if (dbus_message_is_signal(message, s_shortcuts_interface, "Activated"))
				{
					DBusMessageIter args;
					const char *message_session = nullptr, *shortcut_id = nullptr;
					if (dbus_message_iter_init(message, &args) && dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_OBJECT_PATH)
					{
						dbus_message_iter_get_basic(&args, &message_session);
						if (dbus_message_iter_next(&args) && dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_STRING)
							dbus_message_iter_get_basic(&args, &shortcut_id);
					}

					if (message_session != nullptr && shortcut_id != nullptr && session_handle == message_session)
					{
						for (size_t i = 0; i < std::size(s_shortcuts); ++i)
							if (std::strcmp(shortcut_id, s_shortcuts[i].id) == 0)
								s_pending[i]++;
					}
				}
				dbus_message_unref(message);
			}
		}

		dbus_connection_close(connection);
		dbus_connection_unref(connection);
	}
}

void reshade::global_shortcuts::start()
{
	if (s_started.exchange(true))
		return;

	if (reshade::linux_platform::is_env_enabled("RESHADE_DISABLE_GLOBAL_SHORTCUTS"))
	{
		reshade::log::message(reshade::log::level::info, "Global shortcuts are disabled through RESHADE_DISABLE_GLOBAL_SHORTCUTS.");
		return;
	}

	// The thread keeps running for the lifetime of the process (ReShade is never unloaded, see "-z nodelete" in linux/CMakeLists.txt)
	std::thread(portal_thread_main).detach();
}

bool reshade::global_shortcuts::consume(action action)
{
	std::atomic<unsigned int> &pending = s_pending[static_cast<size_t>(action)];
	unsigned int value = pending.load();
	while (value != 0)
		if (pending.compare_exchange_weak(value, value - 1))
			return true;
	return false;
}
