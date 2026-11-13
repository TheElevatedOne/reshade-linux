/*
 * Copyright (C) 2014 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause OR MIT
 */

#include "dll_log.hpp"
#include <cstdarg>
#ifdef _WIN32
#include <Windows.h>
#else
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <sys/time.h>
#include <unistd.h>
#include <sys/syscall.h>
#include "linux/platform_linux.hpp"
#endif

#ifdef _WIN32
struct scoped_file_handle
{
	scoped_file_handle(HANDLE handle = INVALID_HANDLE_VALUE) : handle(handle) {}
	~scoped_file_handle()
	{
		if (handle != INVALID_HANDLE_VALUE)
			CloseHandle(handle);
	}

	operator HANDLE() const { return handle; }

	void operator=(HANDLE new_handle)
	{
		handle = new_handle;
	}

private:
	HANDLE handle;
};

static scoped_file_handle s_log_file_handle;
#else
static std::mutex s_log_file_mutex;
static FILE *s_log_file_handle = nullptr;
#endif

bool reshade::log::open_log_file(const std::filesystem::path &path, std::error_code &ec)
{
#ifndef _WIN32
	const std::lock_guard<std::mutex> lock(s_log_file_mutex);
	if (s_log_file_handle != nullptr)
		std::fclose(s_log_file_handle);
	s_log_file_handle = std::fopen(path.c_str(), "w");
	if (s_log_file_handle != nullptr)
	{
		ec.clear();
		return true;
	}
	ec.assign(errno, std::generic_category());
	return false;
#else
	// Close the previous file first
	// Do this here, instead of in 'scoped_file_handle::operator=', so that the old handle is closed before the new handle is created
	if (s_log_file_handle != INVALID_HANDLE_VALUE)
		CloseHandle(s_log_file_handle);

	// Open the log file for writing (and flush on each write) and clear previous contents
	s_log_file_handle = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);

	if (s_log_file_handle != INVALID_HANDLE_VALUE)
	{
		// Last error may be ERROR_ALREADY_EXISTS if an existing file was overwritten, which can be ignored
		ec.clear();
		return true;
	}
	else
	{
		ec.assign(GetLastError(), std::system_category());
		return false;
	}
#endif
}

void reshade::log::message(level level, const char *format, ...)
{
	static constexpr char level_names[][6] = { "ERROR", "WARN ", "INFO ", "DEBUG" };

	if (static_cast<size_t>(level) == 0)
		level = level::error;
	if (static_cast<size_t>(level) > std::size(level_names))
		level = level::debug;

#ifdef _WIN32
	SYSTEMTIME time;
	GetLocalTime(&time);
	const unsigned long thread_id = GetCurrentThreadId();
#else
	struct
	{
		short wYear, wMonth, wDay, wHour, wMinute, wSecond, wMilliseconds;
	} time;
	{
		timeval tv = {};
		gettimeofday(&tv, nullptr);
		tm local_tm = {};
		localtime_r(&tv.tv_sec, &local_tm);
		time.wYear = static_cast<short>(local_tm.tm_year + 1900);
		time.wMonth = static_cast<short>(local_tm.tm_mon + 1);
		time.wDay = static_cast<short>(local_tm.tm_mday);
		time.wHour = static_cast<short>(local_tm.tm_hour);
		time.wMinute = static_cast<short>(local_tm.tm_min);
		time.wSecond = static_cast<short>(local_tm.tm_sec);
		time.wMilliseconds = static_cast<short>(tv.tv_usec / 1000);
	}
	const unsigned long thread_id = static_cast<unsigned long>(syscall(SYS_gettid));
#endif

	std::string line_string(256, '\0');

	// Start a new line
	const auto meta_length = std::snprintf(line_string.data(), line_string.size(),
#if RESHADE_VERBOSE_LOG
		"%04hd-%02hd-%02hdT"
#endif
		"%02hd:%02hd:%02hd:%03hd [%5lu] | %.5s | ",
#if RESHADE_VERBOSE_LOG
		time.wYear, time.wMonth, time.wDay,
#endif
		time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, thread_id, level_names[static_cast<size_t>(level) - 1]);

	va_list args;
	va_start(args, format);
	const auto content_length = std::vsnprintf(line_string.data() + meta_length, line_string.size() + 1 - meta_length, format, args);
	va_end(args);

	const bool remaining_content = static_cast<size_t>(meta_length) + static_cast<size_t>(content_length) > line_string.size();
	line_string.resize(static_cast<size_t>(meta_length) + static_cast<size_t>(content_length));

	if (remaining_content)
	{
		va_start(args, format);
		std::vsnprintf(line_string.data() + meta_length, line_string.size() + 1 - meta_length, format, args);
		va_end(args);
	}

	line_string += '\n'; // Terminate line with line feed

#ifndef _WIN32
	{
		const std::lock_guard<std::mutex> lock(s_log_file_mutex);
		if (s_log_file_handle != nullptr)
		{
			std::fwrite(line_string.data(), 1, line_string.size(), s_log_file_handle);
			std::fflush(s_log_file_handle);
		}
	}

	// Mirror log messages to standard error when requested (e.g. "RESHADE_LOG_STDERR=1 RESHADE=1 vkcube")
	static const bool log_to_stderr = reshade::linux_platform::is_env_enabled("RESHADE_LOG_STDERR");
	if (log_to_stderr)
		std::fwrite(line_string.data(), 1, line_string.size(), stderr);
#else
	// Replace all LF with CRLF
	for (size_t offset = 0; (offset = line_string.find('\n', offset)) != std::string::npos; offset += 2)
		line_string.replace(offset, 1, "\r\n", 2);

	// Write line to the log file
	if (s_log_file_handle != INVALID_HANDLE_VALUE)
	{
		DWORD written = 0;
		WriteFile(s_log_file_handle, line_string.data(), static_cast<DWORD>(line_string.size()), &written, nullptr);
		assert(written == line_string.size());
	}

#ifndef NDEBUG
	// Write line to the debug output
	OutputDebugStringA(line_string.c_str());
#endif
#endif
}
