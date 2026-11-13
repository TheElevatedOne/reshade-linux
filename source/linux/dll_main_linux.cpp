/*
 * Copyright (C) 2014 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Entry point of the native Linux build, which is loaded as an implicit Vulkan layer when the environment variable RESHADE=1 is set.
 *
 * Mirrors what the Windows setup tool does when installing ReShade for a game:
 *   - ReShade.ini, ReShade.log and ReShadePreset.ini live next to the game executable
 *   - the default effect files are copied into a "reshade-shaders" directory next to the game executable, where effects can then be added per game
 *
 * For Wine/Proton games, the directory of the Windows executable is used (e.g. "<steamapps>/common/<game>/bin"), not the one of the Wine loader.
 */

#include "version.h"
#include "dll_log.hpp"
#include "ini_file.hpp"
#include "addon_manager.hpp"
#include "platform_linux.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <dlfcn.h>
#include <unistd.h>

#include "linux/module_linux.hpp"

// Export special symbol to identify modules as ReShade instances
extern "C" { __attribute__((visibility("default"))) const char *ReShadeVersion = VERSION_STRING_PRODUCT; }

HMODULE g_module_handle = nullptr;
std::filesystem::path g_reshade_dll_path;
std::filesystem::path g_reshade_base_path;
std::filesystem::path g_target_executable_path;

// Only set while a Vulkan swap chain is being created or presented by the application (used to identify calls from the effect runtime itself)
thread_local bool g_in_dxgi_runtime = false;

// Set to false for helper processes (Wine services, launchers, compositors, ...) in which no effect runtime should be created
bool g_reshade_runtime_enabled = true;

#ifndef RESHADE_DATA_DIR
#define RESHADE_DATA_DIR "/usr/share/reshade"
#endif

static std::string get_env(const char *name)
{
	const char *const value = std::getenv(name);
	return value != nullptr ? std::string(value) : std::string();
}

static bool equals_ignore_case(const std::string &a, const std::string &b)
{
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char l, char r) { return std::tolower(static_cast<unsigned char>(l)) == std::tolower(static_cast<unsigned char>(r)); });
}

static std::vector<std::string> read_process_command_line()
{
	std::vector<std::string> args;
	std::ifstream file("/proc/self/cmdline", std::ios::binary);
	std::string arg;
	while (std::getline(file, arg, '\0'))
		args.push_back(arg);
	return args;
}

/// <summary>
/// Resolves a path component by component, matching each component case-insensitively if it does not exist with the exact case (Windows paths are case-insensitive).
/// </summary>
static std::filesystem::path resolve_case_insensitive(const std::filesystem::path &path)
{
	std::error_code ec;
	if (std::filesystem::exists(path, ec))
		return path;

	std::filesystem::path result = path.root_path();
	for (const std::filesystem::path &component : path.relative_path())
	{
		std::filesystem::path candidate = result / component;
		if (!std::filesystem::exists(candidate, ec))
		{
			for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(result, std::filesystem::directory_options::skip_permission_denied, ec))
			{
				if (equals_ignore_case(entry.path().filename().string(), component.string()))
				{
					candidate = entry.path();
					break;
				}
			}
		}
		result = std::move(candidate);
	}
	return result;
}

/// <summary>
/// Converts a Windows path as seen by a Wine process (e.g. "C:\Games\Game.exe" or "Z:\home\user\Game.exe") to the corresponding Unix path.
/// </summary>
static std::filesystem::path convert_wine_path(std::string path)
{
	std::replace(path.begin(), path.end(), '\\', '/');

	// Strip NT path prefixes
	if (path.compare(0, 4, "//?/") == 0 || path.compare(0, 4, "/\?\?/") == 0)
		path.erase(0, 4);

	if (path.size() < 3 || path[1] != ':' || path[2] != '/')
		return path[0] == '/' ? std::filesystem::path(path) : std::filesystem::path();

	const char drive = static_cast<char>(std::tolower(static_cast<unsigned char>(path[0])));

	std::filesystem::path prefix = get_env("WINEPREFIX");
	if (prefix.empty())
		if (const std::string compat_data = get_env("STEAM_COMPAT_DATA_PATH"); !compat_data.empty())
			prefix = std::filesystem::path(compat_data) / "pfx";
	if (prefix.empty())
		prefix = std::filesystem::path(get_env("HOME")) / ".wine";

	std::error_code ec;
	std::filesystem::path drive_root = std::filesystem::canonical(prefix / "dosdevices" / std::string { drive, ':' }, ec);
	if (ec)
	{
		if (drive == 'z')
			drive_root = "/";
		else if (drive == 'c')
			drive_root = prefix / "drive_c";
		else
			return std::filesystem::path();
	}

	return resolve_case_insensitive(drive_root / path.substr(3));
}

/// <summary>
/// Finds the executable of the application ReShade was loaded into. For Wine processes, this is the Windows executable that Wine runs.
/// </summary>
static std::filesystem::path find_target_executable_path(bool &is_wine)
{
	std::error_code ec;
	const std::filesystem::path self_exe = std::filesystem::read_symlink("/proc/self/exe", ec);
	const std::string self_exe_name = self_exe.filename().string();

	is_wine = self_exe_name.compare(0, 4, "wine") == 0 || std::getenv("WINELOADERNOEXEC") != nullptr;
	if (!is_wine)
		return self_exe;

	// Wine replaces the process command-line with the Windows command-line, so look for the Windows executable in there
	for (const std::string &arg : read_process_command_line())
	{
		if (arg.size() < 5 || !equals_ignore_case(arg.substr(arg.size() - 4), ".exe"))
			continue;

		const std::filesystem::path path = convert_wine_path(arg);
		if (!path.empty() && std::filesystem::is_regular_file(path, ec))
			return path;
	}

	// Fall back to the directory Steam reports as install directory of the game
	if (const std::string install_path = get_env("STEAM_COMPAT_INSTALL_PATH"); !install_path.empty())
		return std::filesystem::path(install_path) / "game.exe";

	return self_exe;
}

/// <summary>
/// Checks whether this process is a helper process ReShade should not set itself up for (Wine system services, Steam runtime tools, compositors, ...).
/// </summary>
static bool is_ignored_process(const std::filesystem::path &exe_path, bool is_wine)
{
	static const char *const ignored_wine_processes[] = {
		"services.exe", "winedevice.exe", "plugplay.exe", "explorer.exe", "rpcss.exe", "svchost.exe", "tabtip.exe", "conhost.exe",
		"wineboot.exe", "winemenubuilder.exe", "start.exe", "cmd.exe", "rundll32.exe", "msiexec.exe", "regsvr32.exe", "steam.exe",
		"steamwebhelper.exe", "steamerrorreporter.exe", "steamerrorreporter64.exe", "crashpad_handler.exe", "unitycrashhandler64.exe", "unitycrashhandler32.exe",
		"easyanticheat_eos_setup.exe", "installscript.exe", "dxsetup.exe", "vc_redist.x64.exe", "vc_redist.x86.exe", "vcredist_x64.exe", "vcredist_x86.exe",
	};
	static const char *const ignored_native_processes[] = {
		"steam", "steamwebhelper", "gamescope", "gamescope-wl", "fossilize_replay", "pressure-vessel-wrap", "pressure-vessel-adverb", "srt-bwrap", "reaper", "Xwayland",
	};

	const std::string name = exe_path.filename().string();
	if (is_wine)
	{
		for (const char *ignored : ignored_wine_processes)
			if (equals_ignore_case(name, ignored))
				return true;
	}
	else
	{
		for (const char *ignored : ignored_native_processes)
			if (name == ignored)
				return true;
	}
	return false;
}

static bool is_directory_writable(const std::filesystem::path &path)
{
	return !path.empty() && access(path.c_str(), W_OK) == 0;
}

/// <summary>
/// Returns the user data directory of ReShade ("$XDG_DATA_HOME/reshade" or "~/.local/share/reshade").
/// </summary>
static std::filesystem::path get_user_data_path()
{
	if (const std::string xdg_data_home = get_env("XDG_DATA_HOME"); !xdg_data_home.empty() && xdg_data_home[0] == '/')
		return std::filesystem::path(xdg_data_home) / "reshade";
	return std::filesystem::path(get_env("HOME")) / ".local" / "share" / "reshade";
}

/// <summary>
/// Returns the path that should be used as base for relative paths (where ReShade.ini is stored).
/// </summary>
static std::filesystem::path get_base_path()
{
	std::error_code ec;

	// Allow overriding the base path through an environment variable (same name as on Windows, plus a shorter alias)
	for (const char *name : { "RESHADE_BASE_PATH", "RESHADE_BASE_PATH_OVERRIDE" })
		if (const std::string path_override = get_env(name); !path_override.empty() && std::filesystem::is_directory(path_override, ec))
			return std::filesystem::path(path_override);

	const std::filesystem::path target_executable_parent_path = g_target_executable_path.parent_path();

	// Same 'BasePath' override as on Windows, read from the configuration file next to the executable
	if (std::filesystem::path path_override;
		reshade::ini_file(target_executable_parent_path / "ReShade.ini").get("INSTALL", "BasePath", path_override))
	{
		if (path_override.is_relative())
			path_override = target_executable_parent_path / path_override;
		if (std::filesystem::is_directory(path_override, ec))
			return path_override;
	}

	if (is_directory_writable(target_executable_parent_path))
		return target_executable_parent_path;

	// Fall back to a per-application directory in the user data directory if the application directory is read-only (e.g. "/usr/bin")
	return get_user_data_path() / "apps" / g_target_executable_path.stem();
}

/// <summary>
/// Copies all files from <paramref name="source"/> to <paramref name="target"/> that do not exist in the target yet (never overwrites anything).
/// </summary>
static void copy_missing_files(const std::filesystem::path &source, const std::filesystem::path &target)
{
	std::error_code ec;
	if (!std::filesystem::is_directory(source, ec))
		return;

	for (auto it = std::filesystem::recursive_directory_iterator(source, std::filesystem::directory_options::follow_directory_symlink | std::filesystem::directory_options::skip_permission_denied, ec);
		!ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
	{
		const std::filesystem::path relative_path = it->path().lexically_relative(source);
		const std::filesystem::path target_path = target / relative_path;

		if (it->is_directory(ec))
		{
			std::filesystem::create_directories(target_path, ec);
		}
		else if (!std::filesystem::exists(target_path, ec))
		{
			std::filesystem::create_directories(target_path.parent_path(), ec);
			std::filesystem::copy_file(it->path(), target_path, std::filesystem::copy_options::skip_existing, ec);
		}
	}
}

/// <summary>
/// Sets up ReShade for the current application the way the Windows setup tool would: copies the default effects and creates the configuration file.
/// </summary>
static void setup_application_directory()
{
	std::error_code ec;
	std::filesystem::create_directories(g_reshade_base_path, ec);

	const std::filesystem::path system_data_path = RESHADE_DATA_DIR;
	const std::filesystem::path user_data_path = get_user_data_path();

	// Copy default effects on first launch only, so that effects removed from the game directory stay removed
	// Use 'RESHADE_UPDATE_SHADERS=1' to copy effect files that were added to the default effect directories since then
	const std::filesystem::path shaders_path = g_reshade_base_path / "reshade-shaders";
	if (!std::filesystem::exists(shaders_path, ec) || reshade::linux_platform::is_env_enabled("RESHADE_UPDATE_SHADERS"))
	{
		// User defaults take precedence over the system defaults, since files are never overwritten
		copy_missing_files(user_data_path / "reshade-shaders", shaders_path);
		copy_missing_files(system_data_path / "reshade-shaders", shaders_path);

		std::filesystem::create_directories(shaders_path / "Shaders", ec);
		std::filesystem::create_directories(shaders_path / "Textures", ec);
	}

	// Create configuration file the way the Windows setup tool does
	const std::filesystem::path config_path = g_reshade_base_path / "ReShade.ini";
	if (!std::filesystem::exists(config_path, ec))
	{
		// Start from a user or system template if one exists
		for (const std::filesystem::path &template_path : { user_data_path / "ReShade.ini", system_data_path / "ReShade.ini" })
			if (std::filesystem::copy_file(template_path, config_path, std::filesystem::copy_options::skip_existing, ec))
				break;

		reshade::ini_file config(config_path);

		// Apply the per-game depth settings from the compatibility list of the official setup tool (downloaded by "reshade --download-shaders")
		if (!config.has("GENERAL", "PreprocessorDefinitions"))
		{
			const std::string executable_name = g_target_executable_path.filename().u8string();

			std::string depth_reversed = "0", depth_upside_down = "0", depth_logarithmic = "0";
			for (const std::filesystem::path &compatibility_path : { user_data_path / "Compatibility.ini", system_data_path / "Compatibility.ini" })
			{
				if (!std::filesystem::exists(compatibility_path, ec))
					continue;

				const reshade::ini_file compatibility(compatibility_path);
				compatibility.get(executable_name, "DepthReversed", depth_reversed);
				compatibility.get(executable_name, "DepthUpsideDown", depth_upside_down);
				compatibility.get(executable_name, "DepthLogarithmic", depth_logarithmic);

				if (compatibility.has(executable_name, "DepthCopyBeforeClears") ||
					compatibility.has(executable_name, "DepthCopyAtClearIndex") ||
					compatibility.has(executable_name, "DrawStatsHeuristic") ||
					compatibility.has(executable_name, "UseAspectRatioHeuristics"))
				{
					std::string value;
					config.set("DEPTH", "DepthCopyBeforeClears", compatibility.get(executable_name, "DepthCopyBeforeClears", value) ? value : std::string("0"));
					config.set("DEPTH", "DepthCopyAtClearIndex", compatibility.get(executable_name, "DepthCopyAtClearIndex", value) ? value : std::string("0"));
					config.set("DEPTH", "DrawStatsHeuristic", compatibility.get(executable_name, "DrawStatsHeuristic", value) ? value : std::string("0"));
					config.set("DEPTH", "UseAspectRatioHeuristics", compatibility.get(executable_name, "UseAspectRatioHeuristics", value) ? value : std::string("1"));
				}
				break;
			}

			config.set("GENERAL", "PreprocessorDefinitions", std::vector<std::string> {
				"RESHADE_DEPTH_LINEARIZATION_FAR_PLANE=1000.0",
				"RESHADE_DEPTH_INPUT_IS_UPSIDE_DOWN=" + depth_upside_down,
				"RESHADE_DEPTH_INPUT_IS_REVERSED=" + depth_reversed,
				"RESHADE_DEPTH_INPUT_IS_LOGARITHMIC=" + depth_logarithmic,
			});
		}

		if (!config.has("GENERAL", "EffectSearchPaths") && !config.has("GENERAL", "TextureSearchPaths"))
		{
			config.set("GENERAL", "EffectSearchPaths", std::string("./reshade-shaders/Shaders/**"));
			config.set("GENERAL", "TextureSearchPaths", std::string("./reshade-shaders/Textures/**"));
		}
		if (!config.has("GENERAL", "PresetPath"))
			config.set("GENERAL", "PresetPath", std::string("./ReShadePreset.ini"));

		if (!config.has("INPUT", "KeyOverlay"))
		{
			const unsigned int key_overlay[4] = { 0x24 /* Home */, 0, 0, 0 };
			config.set("INPUT", "KeyOverlay", key_overlay);
			config.set("INPUT", "GamepadNavigation", std::string("0"));
		}

		config.save();
	}
}

static void log_exit()
{
	reshade::log::message(reshade::log::level::info, "Exiting ...");

#if RESHADE_ADDON >= 2
	if (reshade::has_loaded_addons())
		reshade::log::message(reshade::log::level::warning, "Add-ons are still loaded! Application may crash on exit.");
#endif

	reshade::log::message(reshade::log::level::info, "Finished exiting.");
}

static void initialize_once()
{
	g_module_handle = get_module_handle_from_address(reinterpret_cast<const void *>(&initialize_once));

	Dl_info info = {};
	if (dladdr(reinterpret_cast<const void *>(&initialize_once), &info) && info.dli_fname != nullptr)
	{
		std::error_code ec;
		g_reshade_dll_path = std::filesystem::canonical(info.dli_fname, ec);
		if (ec)
			g_reshade_dll_path = info.dli_fname;
	}

	bool is_wine = false;
	g_target_executable_path = find_target_executable_path(is_wine);

	if (is_ignored_process(g_target_executable_path, is_wine))
	{
		g_reshade_runtime_enabled = false;
		// Do not set up anything for helper processes, but keep the base path somewhere valid
		g_reshade_base_path = get_user_data_path();
		return;
	}

	g_reshade_base_path = get_base_path();

	setup_application_directory();

	const reshade::ini_file &config = reshade::global_config();

	if (config.get("INSTALL", "Logging") || (!config.has("INSTALL", "Logging") && !reshade::linux_platform::is_env_enabled("RESHADE_DISABLE_LOGGING")))
	{
		std::filesystem::path log_path = config.path();
		log_path.replace_extension(".log");

		std::error_code ec;
		if (!reshade::log::open_log_file(log_path, ec))
		{
			// Try a different file if the default failed to open
			for (int log_index = 0; log_index < 10; ++log_index)
			{
				log_path.replace_extension(".log" + std::to_string(log_index + 1));
				if (reshade::log::open_log_file(log_path, ec))
					break;
			}
		}
	}

	reshade::log::message(reshade::log::level::info,
		"Initializing crosire's ReShade version '" VERSION_STRING_FILE "' "
#if defined(__x86_64__) || defined(__aarch64__)
		"(64-bit) "
#else
		"(32-bit) "
#endif
		"loaded from '%s' into '%s'%s (0x%X) ...",
		g_reshade_dll_path.u8string().c_str(),
		g_target_executable_path.u8string().c_str(),
		is_wine ? " (Wine)" : "",
		static_cast<unsigned int>(std::hash<std::string>()(g_target_executable_path.stem().u8string()) & 0xFFFFFFFF));
	reshade::log::message(reshade::log::level::info, "Using base path '%s'.", g_reshade_base_path.u8string().c_str());

	reshade::log::message(reshade::log::level::info, "Initialized.");

	// Registered here instead of using a destructor function, so that it runs before the global variables of ReShade are destroyed
	// (exit handlers and destructors of global variables run in reverse order of their registration)
	std::atexit(log_exit);
}

/// <summary>
/// Initializes ReShade on the first call into the layer.
/// This cannot be done in a constructor function of the shared object, since those may run before the global variables of ReShade are constructed.
/// </summary>
void reshade::linux_platform::initialize()
{
	static std::once_flag once;
	std::call_once(once, initialize_once);
}
