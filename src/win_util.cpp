#include "win_util.h"

#include <algorithm>
#include <sstream>
#include <vector>

namespace agent_latch {

std::wstring GetExecutablePath() {
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size() - 1) {
            return std::wstring(buffer.data(), length);
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring GetExecutableDirectory() {
    std::wstring path = GetExecutablePath();
    const std::size_t separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? std::wstring{} : path.substr(0, separator);
}

std::wstring GetLocalAppDataDirectory() {
    DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (required == 0) {
        return {};
    }
    std::vector<wchar_t> buffer(required);
    const DWORD written = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(), required);
    return written == 0 || written >= required ? std::wstring{} : std::wstring(buffer.data(), written);
}

std::wstring JoinPath(const std::wstring& left, const std::wstring& right) {
    if (left.empty()) {
        return right;
    }
    if (right.empty()) {
        return left;
    }
    if (left.back() == L'\\' || left.back() == L'/') {
        return left + right;
    }
    return left + L"\\" + right;
}

std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty()) {
        return {};
    }
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            required) != required) {
        return {};
    }
    return result;
}

std::wstring FormatDuration(ULONGLONG milliseconds) {
    const ULONGLONG total_seconds = milliseconds / 1000;
    const ULONGLONG hours = total_seconds / 3600;
    const ULONGLONG minutes = (total_seconds % 3600) / 60;
    const ULONGLONG seconds = total_seconds % 60;
    std::wostringstream stream;
    if (hours > 0) {
        stream << hours << L"h ";
        if (minutes > 0) {
            stream << minutes << L"m";
        }
    } else if (minutes > 0) {
        stream << minutes << L"m";
    } else {
        stream << std::max<ULONGLONG>(1, seconds) << L"s";
    }
    return stream.str();
}

std::wstring FormatCountdown(ULONGLONG expires_at, ULONGLONG now) {
    if (expires_at == 0) {
        return L"until released";
    }
    if (expires_at <= now) {
        return L"ending now";
    }
    return FormatDuration(expires_at - now) + L" remaining";
}

std::wstring SanitizeMessageField(const std::wstring& value, std::size_t maximum_length) {
    std::wstring result;
    result.reserve(std::min(value.size(), maximum_length));
    for (wchar_t character : value) {
        if (result.size() >= maximum_length) {
            break;
        }
        if (character == L'\t' || character == L'\r' || character == L'\n' || character < 0x20) {
            result.push_back(L' ');
        } else {
            result.push_back(character);
        }
    }
    while (!result.empty() && result.back() == L' ') {
        result.pop_back();
    }
    return result;
}

bool FileExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

}  // namespace agent_latch
