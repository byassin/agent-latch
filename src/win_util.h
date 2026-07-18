#pragma once

#include <windows.h>

#include <string>
#include <string_view>

namespace agent_latch {

std::wstring GetExecutablePath();
std::wstring GetExecutableDirectory();
std::wstring GetLocalAppDataDirectory();
std::wstring JoinPath(const std::wstring& left, const std::wstring& right);
std::wstring Utf8ToWide(std::string_view value);
std::wstring FormatDuration(ULONGLONG milliseconds);
std::wstring FormatCountdown(ULONGLONG expires_at, ULONGLONG now);
std::wstring SanitizeMessageField(const std::wstring& value, std::size_t maximum_length = 160);
bool FileExists(const std::wstring& path);

}  // namespace agent_latch
