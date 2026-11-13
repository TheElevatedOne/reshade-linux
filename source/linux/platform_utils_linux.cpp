/*
 * Copyright (C) 2022 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "platform_utils.hpp"
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <spawn.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

extern "C" char **environ;

/// <summary>
/// Builds the environment for child processes, without the variables that would load ReShade (or other injected libraries) into them again.
/// </summary>
static std::vector<std::string> make_child_environment()
{
	std::vector<std::string> env;
	for (char **it = environ; it != nullptr && *it != nullptr; ++it)
	{
		const char *const entry = *it;
		if (std::strncmp(entry, "RESHADE=", 8) == 0 ||
			std::strncmp(entry, "LD_PRELOAD=", 11) == 0 ||
			std::strncmp(entry, "ENABLE_VKBASALT=", 16) == 0 ||
			std::strncmp(entry, "MANGOHUD=", 9) == 0)
			continue;
		env.emplace_back(entry);
	}
	return env;
}

/// <summary>
/// Spawns a detached child process (double fork via 'posix_spawn' of a shell that backgrounds the command), so that no zombie is left behind.
/// </summary>
static bool spawn_detached(const std::string &command, const std::filesystem::path &working_directory, bool hide_output)
{
	std::string script;
	if (!working_directory.empty())
	{
		script += "cd '";
		for (const char c : working_directory.string())
			script += (c == '\'') ? std::string("'\\''") : std::string(1, c);
		script += "' || exit 1; ";
	}
	script += "( " + command + " )";
	if (hide_output)
		script += " >/dev/null 2>&1";
	script += " </dev/null &";

	std::vector<std::string> env_strings = make_child_environment();
	std::vector<char *> envp;
	envp.reserve(env_strings.size() + 1);
	for (std::string &entry : env_strings)
		envp.push_back(entry.data());
	envp.push_back(nullptr);

	char sh[] = "/bin/sh", c_flag[] = "-c";
	char *const argv[] = { sh, c_flag, script.data(), nullptr };

	pid_t pid = 0;
	if (posix_spawn(&pid, "/bin/sh", nullptr, nullptr, argv, envp.data()) != 0)
		return false;

	// The shell exits right after starting the command in the background, so this does not block for long
	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
		continue;
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static std::string quote_argument(const std::string &arg)
{
	std::string quoted = "'";
	for (const char c : arg)
		quoted += (c == '\'') ? std::string("'\\''") : std::string(1, c);
	quoted += '\'';
	return quoted;
}

bool reshade::utils::open_explorer(const std::filesystem::path &path)
{
	// Ask the file manager to select the file through the freedesktop D-Bus interface first, and fall back to opening the parent directory
	// Percent-encode the URI, so that it cannot contain any quotes that would break the GVariant text
	std::string uri = "file://";
	for (const unsigned char c : path.string())
	{
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '_' || c == '.' || c == '~')
		{
			uri += static_cast<char>(c);
		}
		else
		{
			static const char hex[] = "0123456789ABCDEF";
			uri += '%';
			uri += hex[c >> 4];
			uri += hex[c & 0xF];
		}
	}

	return spawn_detached(
		"gdbus call --session --dest org.freedesktop.FileManager1 --object-path /org/freedesktop/FileManager1 --method org.freedesktop.FileManager1.ShowItems " + quote_argument("['" + uri + "']") + " '' || xdg-open " + quote_argument(path.parent_path().string()),
		std::filesystem::path(), true);
}

bool reshade::utils::execute_command(const std::string &command_line, const std::filesystem::path &working_directory, bool hide_window)
{
	// Check if this is actually a website URL that should be opened in the default web browser
	if (command_line.compare(0, 4, "http") == 0 && command_line.find_first_of(" \t") == std::string::npos)
		return spawn_detached("xdg-open " + quote_argument(command_line), std::filesystem::path(), true);

	return spawn_detached(command_line, working_directory, hide_window);
}

void reshade::utils::play_sound_async(const std::filesystem::path &audio_file)
{
	const std::filesystem::path normalized_audio_file_path = audio_file.lexically_normal();
	if (normalized_audio_file_path.empty())
		return;

	const std::string file = quote_argument(normalized_audio_file_path.string());
	spawn_detached("pw-play " + file + " || paplay " + file + " || aplay -q " + file, std::filesystem::path(), true);
}
