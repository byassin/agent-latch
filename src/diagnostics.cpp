#include "diagnostics.h"

#include "win_util.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <vector>

namespace agent_latch {
namespace {

constexpr LONGLONG kMaximumDiagnosticBytes = 512LL * 1024LL;

std::string WideToUtf8(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }
    const int required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    const int written = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        required,
        nullptr,
        nullptr);
    return written == required ? result : std::string{};
}

std::wstring UtcTimestamp() {
    SYSTEMTIME time{};
    GetSystemTime(&time);
    wchar_t value[32]{};
    std::swprintf(
        value,
        std::size(value),
        L"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        time.wYear,
        time.wMonth,
        time.wDay,
        time.wHour,
        time.wMinute,
        time.wSecond,
        time.wMilliseconds);
    return value;
}

}  // namespace

std::wstring FormatDiagnosticRecord(
    std::wstring_view timestamp,
    std::wstring_view event_name,
    std::wstring_view detail) {
    std::wstring result = SanitizeMessageField(std::wstring(timestamp), 40);
    result += L'\t';
    result += SanitizeMessageField(std::wstring(event_name), 64);
    const std::wstring safe_detail = SanitizeMessageField(std::wstring(detail), 512);
    if (!safe_detail.empty()) {
        result += L'\t';
        result += safe_detail;
    }
    result += L"\r\n";
    return result;
}

DiagnosticsLog::DiagnosticsLog() {
    const std::wstring local_app_data = GetLocalAppDataDirectory();
    if (local_app_data.empty()) {
        return;
    }
    directory_ = JoinPath(local_app_data, L"AgentLatch");
    const DWORD attributes = GetFileAttributesW(directory_.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        if (!CreateDirectoryW(directory_.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
            directory_.clear();
            return;
        }
    } else if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        directory_.clear();
        return;
    }
    path_ = JoinPath(directory_, L"diagnostics.log");
    previous_path_ = JoinPath(directory_, L"diagnostics.previous.log");
}

bool DiagnosticsLog::Write(std::wstring_view event_name, std::wstring_view detail) {
    if (path_.empty() || event_name.empty()) {
        return false;
    }
    const std::string record = WideToUtf8(FormatDiagnosticRecord(UtcTimestamp(), event_name, detail));
    if (record.empty() || !RotateIfNeeded(record.size())) {
        return false;
    }

    HANDLE file = CreateFileW(
        path_.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const BOOL success = WriteFile(
        file,
        record.data(),
        static_cast<DWORD>(record.size()),
        &written,
        nullptr);
    CloseHandle(file);
    return success && written == static_cast<DWORD>(record.size());
}

const std::wstring& DiagnosticsLog::Path() const {
    return path_;
}

bool DiagnosticsLog::RotateIfNeeded(std::size_t incoming_bytes) const {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path_.c_str(), GetFileExInfoStandard, &data)) {
        return GetLastError() == ERROR_FILE_NOT_FOUND;
    }
    ULARGE_INTEGER size{};
    size.HighPart = data.nFileSizeHigh;
    size.LowPart = data.nFileSizeLow;
    if (size.QuadPart + incoming_bytes <= static_cast<ULONGLONG>(kMaximumDiagnosticBytes)) {
        return true;
    }
    DeleteFileW(previous_path_.c_str());
    return MoveFileExW(
               path_.c_str(),
               previous_path_.c_str(),
               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

}  // namespace agent_latch
