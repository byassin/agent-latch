#include "status_report.h"
#include "version.h"
#include <array>
#include <filesystem>
#include <cwctype>
#include <locale>
#include <sstream>

namespace agent_latch {
namespace {
const char* Boolean(bool value) { return value ? "true" : "false"; }
const char* Mode(DetectionMode mode) {
    return mode == DetectionMode::Tasks ? "tasks" : mode == DetectionMode::Open ? "open" : "off";
}
std::string JsonString(const wchar_t* value) {
    std::ostringstream out; out << '"';
    for (; *value; ++value) {
        const auto c = static_cast<unsigned int>(*value);
        if (c == '"' || c == '\\') { out << '\\' << static_cast<char>(c); }
        else if (c >= 32 && c < 127) { out << static_cast<char>(c); }
        else {
            constexpr char digits[] = "0123456789abcdef";
            out << "\\u" << digits[(c >> 12) & 15] << digits[(c >> 8) & 15] << digits[(c >> 4) & 15] << digits[c & 15];
        }
    }
    out << '"'; return out.str();
}
}

std::string BuildStatusReport(const StatusReportState& report) {
    const auto& state = report.dashboard;
    const ULONGLONG remaining = state.manual_deadline > state.now ? state.manual_deadline - state.now : 0;
    const bool protected_now = state.active && report.request_available && report.system_request_set &&
        state.system_request_accepted && state.system_request_error == ERROR_SUCCESS;
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\n  \"schema_version\": 1,\n  \"app_version\": " << JsonString(kAgentLatchVersion)
        << ",\n  \"captured_utc_ms\": " << report.captured_utc_ms
        << ",\n  \"protection_requested\": " << Boolean(state.active)
        << ",\n  \"idle_sleep_request_confirmed\": " << Boolean(protected_now)
        << ",\n  \"power\": {\"handle_available\": " << Boolean(report.request_available)
        << ", \"system_request_set\": " << Boolean(report.system_request_set)
        << ", \"display_request_set\": " << Boolean(report.display_request_set)
        << ", \"system_error\": " << state.system_request_error
        << ", \"display_error\": " << state.display_request_error << "},\n"
        << "  \"preferences\": {\"keep_screen_on\": " << Boolean(state.keep_display_on)
        << ", \"launch_at_sign_in\": " << Boolean(state.start_with_windows)
        << ", \"notifications\": " << Boolean(state.settings.notifications)
        << ", \"release_delay_seconds\": " << state.settings.activity_grace_seconds << "},\n"
        << "  \"manual\": {\"active\": " << Boolean(state.manual_active)
        << ", \"until_windows_restarts\": " << Boolean(state.manual_active && state.manual_deadline == 0)
        << ", \"remaining_seconds\": " << (state.manual_active && state.manual_deadline != 0 ? remaining / 1000 + (remaining % 1000 != 0) : 0) << "},\n"
        << "  \"providers\": [\n";
    constexpr std::array<Provider, 7> providers = {Provider::Manual, Provider::Codex, Provider::ClaudeCode,
        Provider::Cursor, Provider::OpenCode, Provider::GeminiCli, Provider::External};
    for (std::size_t index = 0; index < providers.size(); ++index) {
        const auto provider = providers[index];
        unsigned int sources = 0, grace = 0;
        ULONGLONG instances = 0;
        for (const auto& latch : state.latches) {
            if (latch.provider == provider) { ++sources; instances += latch.instance_count; grace += latch.in_grace_period ? 1 : 0; }
        }
        out << "    {\"key\": " << JsonString(ProviderKey(provider)) << ", \"mode\": ";
        if (provider == Provider::Manual || provider == Provider::External) { out << "null"; }
        else { out << '"' << Mode(state.settings.ProviderMode(provider)) << '"'; }
        out << ", \"sources\": " << sources << ", \"instances\": " << instances << ", \"grace_sources\": " << grace;
        if (provider == Provider::Codex || provider == Provider::ClaudeCode) {
            const bool codex = provider == Provider::Codex;
            out << ", \"hook_expected\": " << Boolean(codex ? state.settings.codex_integration_expected : state.settings.claude_integration_expected)
                << ", \"hook_seen\": " << Boolean(codex ? state.settings.codex_hook_seen : state.settings.claude_hook_seen);
        }
        out << '}' << (index + 1 == providers.size() ? "\n" : ",\n");
    }
    out << "  ],\n  \"limitations\": \"Snapshot only; does not prove effective Windows policy or prevent explicit sleep, restart, shutdown or power loss.\"\n}\n";
    return out.str();
}

bool IsStatusReportPath(const std::wstring& path) {
    if (path.size() < 6 || path.size() > 2000 || path.find_first_of(L"\r\n\t\0", 0, 4) != std::wstring::npos) { return false; }
    if (path.starts_with(L"\\\\?\\") || path.starts_with(L"\\\\.\\")) { return false; }
    // Ordinary absolute filesystem paths only, not alternate data streams/devices.
    const auto colon = path.find(L':');
    if (colon != std::wstring::npos && (colon != 1 || path.find(L':', colon + 1) != std::wstring::npos)) { return false; }
    const std::filesystem::path parsed(path);
    auto stem = parsed.stem().wstring();
    for (auto& c : stem) { c = static_cast<wchar_t>(towupper(c)); }
    if (stem == L"CON" || stem == L"PRN" || stem == L"AUX" || stem == L"NUL" ||
        (stem.size() == 4 && (stem.starts_with(L"COM") || stem.starts_with(L"LPT")) && stem[3] >= L'1' && stem[3] <= L'9')) { return false; }
    auto extension = parsed.extension().wstring();
    for (auto& c : extension) { c = static_cast<wchar_t>(towlower(c)); }
    return parsed.is_absolute() && extension == L".json";
}

bool WriteStatusReport(const StatusReportState& state, const std::wstring& path, bool overwrite) {
    if (!IsStatusReportPath(path)) { SetLastError(ERROR_INVALID_NAME); return false; }
    const std::string json = BuildStatusReport(state);
    // Write beside the destination, then rename. A failed write must not
    // truncate an existing report or leave a partial report at the chosen name.
    std::wstring temporary;
    HANDLE file = INVALID_HANDLE_VALUE;
    for (unsigned int attempt = 0; attempt < 8; ++attempt) {
        temporary = path + L".agentlatch-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt) + L".tmp";
        file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE || GetLastError() != ERROR_FILE_EXISTS) { break; }
    }
    if (file == INVALID_HANDLE_VALUE) { return false; }
    DWORD written = 0;
    bool success = GetFileType(file) == FILE_TYPE_DISK &&
        WriteFile(file, json.data(), static_cast<DWORD>(json.size()), &written, nullptr) && written == json.size() && FlushFileBuffers(file);
    DWORD error = GetLastError();
    if (!success && error == ERROR_SUCCESS) { error = ERROR_WRITE_FAULT; }
    CloseHandle(file);
    if (success) {
        success = MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH |
            (overwrite ? MOVEFILE_REPLACE_EXISTING : 0)) != FALSE;
        if (!success) { error = GetLastError(); }
    }
    if (!success) {
        DeleteFileW(temporary.c_str()); // Only our CREATE_NEW temporary file.
        SetLastError(!overwrite && error == ERROR_ALREADY_EXISTS ? ERROR_FILE_EXISTS : error);
    }
    return success;
}
} // namespace agent_latch
