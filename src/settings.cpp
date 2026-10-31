#include "settings.h"

#include <windows.h>

#include <algorithm>

namespace agent_latch {
namespace {

constexpr wchar_t kSettingsKey[] = L"Software\\AgentLatch";
constexpr wchar_t kManualSessionKey[] = L"ManualSession";
constexpr wchar_t kManualSessionValue[] = L"UntilUtc";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"AgentLatch";

DWORD ReadDword(HKEY key, const wchar_t* name, DWORD fallback) {
    DWORD value = 0;
    DWORD type = 0;
    DWORD size = sizeof(value);
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &size) != ERROR_SUCCESS ||
        type != REG_DWORD || size != sizeof(value)) {
        return fallback;
    }
    return value;
}

bool TryReadDword(HKEY key, const wchar_t* name, DWORD* value) {
    if (value == nullptr) {
        return false;
    }
    DWORD type = 0;
    DWORD size = sizeof(*value);
    return RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(value), &size) == ERROR_SUCCESS &&
           type == REG_DWORD && size == sizeof(*value);
}

DetectionMode ReadDetectionMode(
    HKEY key,
    const wchar_t* mode_name,
    const wchar_t* legacy_enabled_name,
    DetectionMode fallback) {
    DWORD value = 0;
    if (TryReadDword(key, mode_name, &value) && value <= static_cast<DWORD>(DetectionMode::Open)) {
        return static_cast<DetectionMode>(value);
    }
    if (TryReadDword(key, legacy_enabled_name, &value)) {
        return value == 0 ? DetectionMode::Off : DetectionMode::Tasks;
    }
    return fallback;
}

bool WriteDword(HKEY key, const wchar_t* name, DWORD value) {
    return RegSetValueExW(
               key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS;
}

}  // namespace

bool Settings::Load() {
    manual_keep_awake = false;
    manual_end_utc_milliseconds = 0;
    HKEY key = nullptr;
    const LSTATUS result = RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_QUERY_VALUE, &key);
    if (result == ERROR_FILE_NOT_FOUND) {
        return true;
    }
    if (result != ERROR_SUCCESS) {
        return false;
    }

    keep_display_on = ReadDword(key, L"KeepDisplayOn", keep_display_on ? 1u : 0u) != 0;
    notifications = ReadDword(key, L"Notifications", notifications ? 1u : 0u) != 0;
    ULONGLONG manual_value = 0;
    DWORD end_type = 0;
    DWORD end_size = sizeof(manual_value);
    // Never restore the legacy permanent value: its boot cannot be established.
    // A volatile child survives app restarts, but not a Windows restart/hive unload.
    HKEY manual_key = nullptr;
    const LSTATUS manual_result = RegOpenKeyExW(key, kManualSessionKey, 0, KEY_QUERY_VALUE, &manual_key);
    if (manual_result == ERROR_SUCCESS &&
        (RegQueryValueExW(manual_key, kManualSessionValue, nullptr, &end_type,
                        reinterpret_cast<BYTE*>(&manual_value), &end_size) != ERROR_SUCCESS ||
        end_type != REG_QWORD || end_size != sizeof(manual_value))) {
        manual_value = 0;
    }
    if (manual_key != nullptr) { RegCloseKey(manual_key); }
    manual_keep_awake = manual_value != 0;
    manual_end_utc_milliseconds = manual_value > 1 ? manual_value : 0;
    codex_mode = ReadDetectionMode(key, L"ModeCodex", L"DetectCodex", codex_mode);
    claude_mode = ReadDetectionMode(key, L"ModeClaude", L"DetectClaude", claude_mode);
    cursor_mode = ReadDetectionMode(key, L"ModeCursor", L"DetectCursor", cursor_mode);
    opencode_mode = ReadDetectionMode(key, L"ModeOpenCode", L"DetectOpenCode", opencode_mode);
    gemini_mode = ReadDetectionMode(key, L"ModeGemini", L"DetectGemini", gemini_mode);
    activity_grace_seconds = std::clamp<DWORD>(ReadDword(key, L"ActivityGraceSeconds", 180), 30, 1800);
    codex_integration_expected = ReadDword(key, L"IntegrationExpectedCodex", 0) != 0;
    codex_hook_seen = ReadDword(key, L"HookSeenCodex", 0) != 0;
    claude_integration_expected = ReadDword(key, L"IntegrationExpectedClaude", 0) != 0;
    claude_hook_seen = ReadDword(key, L"HookSeenClaude", 0) != 0;
    RegCloseKey(key);
    return manual_result == ERROR_SUCCESS || manual_result == ERROR_FILE_NOT_FOUND;
}

bool Settings::Save() const {
    HKEY key = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER,
            kSettingsKey,
            0,
            nullptr,
            REG_OPTION_NON_VOLATILE,
            KEY_SET_VALUE | KEY_CREATE_SUB_KEY,
            nullptr,
            &key,
            &disposition) != ERROR_SUCCESS) {
        return false;
    }

    bool result = true;
    result = WriteDword(key, L"KeepDisplayOn", keep_display_on ? 1u : 0u) && result;
    result = WriteDword(key, L"Notifications", notifications ? 1u : 0u) && result;
    // Keep the old permanent slot disabled, including for older app versions.
    const ULONGLONG disabled = 0;
    result = RegSetValueExW(key, L"ManualKeepAwakeUntilUtc", 0, REG_QWORD,
        reinterpret_cast<const BYTE*>(&disabled), sizeof(disabled)) == ERROR_SUCCESS && result;
    // One value avoids separate enabled/deadline writes. The volatile key, not a
    // wall-clock/uptime heuristic or a shutdown callback, supplies restart reset.
    const ULONGLONG manual_value = !manual_keep_awake ? 0 :
        manual_end_utc_milliseconds == 0 ? 1 : manual_end_utc_milliseconds;
    HKEY manual_key = nullptr;
    if (RegCreateKeyExW(key, kManualSessionKey, 0, nullptr, REG_OPTION_VOLATILE,
                        KEY_SET_VALUE, nullptr, &manual_key, nullptr) == ERROR_SUCCESS) {
        result = RegSetValueExW(manual_key, kManualSessionValue, 0, REG_QWORD,
            reinterpret_cast<const BYTE*>(&manual_value), sizeof(manual_value)) == ERROR_SUCCESS && result;
        RegCloseKey(manual_key);
    } else { result = false; }
    result = WriteDword(key, L"ModeCodex", static_cast<DWORD>(codex_mode)) && result;
    result = WriteDword(key, L"ModeClaude", static_cast<DWORD>(claude_mode)) && result;
    result = WriteDword(key, L"ModeCursor", static_cast<DWORD>(cursor_mode)) && result;
    result = WriteDword(key, L"ModeOpenCode", static_cast<DWORD>(opencode_mode)) && result;
    result = WriteDword(key, L"ModeGemini", static_cast<DWORD>(gemini_mode)) && result;
    // Keep the v0.1 boolean values in sync so rolling back does not unexpectedly
    // re-enable a provider the user turned off.
    result = WriteDword(key, L"DetectCodex", codex_mode == DetectionMode::Off ? 0u : 1u) && result;
    result = WriteDword(key, L"DetectClaude", claude_mode == DetectionMode::Off ? 0u : 1u) && result;
    result = WriteDword(key, L"DetectCursor", cursor_mode == DetectionMode::Off ? 0u : 1u) && result;
    result = WriteDword(key, L"DetectOpenCode", opencode_mode == DetectionMode::Off ? 0u : 1u) && result;
    result = WriteDword(key, L"DetectGemini", gemini_mode == DetectionMode::Off ? 0u : 1u) && result;
    result = WriteDword(key, L"ActivityGraceSeconds", activity_grace_seconds) && result;
    RegCloseKey(key);
    return result;
}

bool Settings::RefreshIntegrationStatus() {
    HKEY key = nullptr;
    const LSTATUS result = RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_QUERY_VALUE, &key);
    if (result == ERROR_FILE_NOT_FOUND) {
        codex_integration_expected = false;
        codex_hook_seen = false;
        claude_integration_expected = false;
        claude_hook_seen = false;
        return true;
    }
    if (result != ERROR_SUCCESS) {
        return false;
    }
    codex_integration_expected = ReadDword(key, L"IntegrationExpectedCodex", 0) != 0;
    codex_hook_seen = ReadDword(key, L"HookSeenCodex", 0) != 0;
    claude_integration_expected = ReadDword(key, L"IntegrationExpectedClaude", 0) != 0;
    claude_hook_seen = ReadDword(key, L"HookSeenClaude", 0) != 0;
    RegCloseKey(key);
    return true;
}

bool RunSettingsSelfTests() {
    // Redirect HKCU only in this self-test process. Never touch live app settings.
    const std::wstring test_path = L"Software\\AgentLatch.SettingsTest." +
        std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64());
    HKEY test_root = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, test_path.c_str(), 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &test_root, &disposition) != ERROR_SUCCESS) { return false; }
    if (disposition != REG_CREATED_NEW_KEY) { RegCloseKey(test_root); return false; }
    if (RegOverridePredefKey(HKEY_CURRENT_USER, test_root) != ERROR_SUCCESS) {
        RegCloseKey(test_root); RegDeleteTreeW(HKEY_CURRENT_USER, test_path.c_str()); return false;
    }
    const bool valid = []() {
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
            KEY_ALL_ACCESS, nullptr, &key, nullptr) != ERROR_SUCCESS) { return false; }
        const ULONGLONG legacy = 1;
        bool success = RegSetValueExW(key, L"ManualKeepAwakeUntilUtc", 0, REG_QWORD,
            reinterpret_cast<const BYTE*>(&legacy), sizeof(legacy)) == ERROR_SUCCESS;
        RegCloseKey(key);
        Settings original;
        success = original.Load() && !original.manual_keep_awake && success;
        original.manual_keep_awake = true;
        original.claude_mode = DetectionMode::Off;
        original.notifications = false;
        success = original.Save() && success;
        Settings app_restart;
        success = app_restart.Load() && app_restart.manual_keep_awake &&
            app_restart.manual_end_utc_milliseconds == 0 &&
            app_restart.claude_mode == DetectionMode::Off && !app_restart.notifications && success;

        HKEY manual_key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\AgentLatch\\ManualSession", 0,
            KEY_ALL_ACCESS, &manual_key) != ERROR_SUCCESS) { return false; }
        HKEY probe = nullptr;
        // Windows rejects a nonvolatile child under a genuinely volatile key.
        const LSTATUS probe_result = RegCreateKeyExW(manual_key, L"NonVolatileProbe", 0, nullptr,
            REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &probe, nullptr);
        if (probe != nullptr) { RegCloseKey(probe); }
        success = probe_result == ERROR_CHILD_MUST_BE_VOLATILE && success;
        const DWORD malformed = 1;
        success = RegSetValueExW(manual_key, kManualSessionValue, 0, REG_DWORD,
            reinterpret_cast<const BYTE*>(&malformed), sizeof(malformed)) == ERROR_SUCCESS && success;
        RegCloseKey(manual_key);
        success = app_restart.Load() && !app_restart.manual_keep_awake && success;

        original.manual_end_utc_milliseconds = 123456789;
        success = original.Save() && app_restart.Load() && app_restart.manual_keep_awake &&
            app_restart.manual_end_utc_milliseconds == original.manual_end_utc_milliseconds && success;
        // Model Windows discarding only volatile session data, not preferences.
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_ALL_ACCESS, &key) != ERROR_SUCCESS) { return false; }
        success = RegDeleteTreeW(key, kManualSessionKey) == ERROR_SUCCESS && success;
        RegCloseKey(key);
        success = app_restart.Load() && !app_restart.manual_keep_awake &&
            app_restart.manual_end_utc_milliseconds == 0 &&
            app_restart.claude_mode == DetectionMode::Off && !app_restart.notifications && success;
        original.manual_end_utc_milliseconds = 0;
        success = original.Save() && app_restart.Load() && app_restart.manual_keep_awake && success;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_ALL_ACCESS, &key) != ERROR_SUCCESS) { return false; }
        ULONGLONG legacy_disabled = 99;
        DWORD legacy_size = sizeof(legacy_disabled);
        success = RegGetValueW(key, nullptr, L"ManualKeepAwakeUntilUtc", RRF_RT_REG_QWORD, nullptr,
            &legacy_disabled, &legacy_size) == ERROR_SUCCESS && legacy_disabled == 0 && success;
        success = RegDeleteTreeW(key, kManualSessionKey) == ERROR_SUCCESS && success;
        success = RegSetValueExW(key, L"ManualKeepAwakeUntilUtc", 0, REG_QWORD,
            reinterpret_cast<const BYTE*>(&legacy), sizeof(legacy)) == ERROR_SUCCESS && success;
        RegCloseKey(key);
        success = app_restart.Load() && !app_restart.manual_keep_awake && success;
        original.manual_keep_awake = false;
        success = original.Save() && app_restart.Load() && !app_restart.manual_keep_awake && success;
        return success;
    }();
    const bool restored = RegOverridePredefKey(HKEY_CURRENT_USER, nullptr) == ERROR_SUCCESS;
    RegCloseKey(test_root);
    const bool cleaned = restored && RegDeleteTreeW(HKEY_CURRENT_USER, test_path.c_str()) == ERROR_SUCCESS;
    return valid && restored && cleaned;
}

bool Settings::MarkHookSeen(Provider provider) {
    const wchar_t* value_name = nullptr;
    if (provider == Provider::Codex) {
        codex_hook_seen = true;
        value_name = L"HookSeenCodex";
    } else if (provider == Provider::ClaudeCode) {
        claude_hook_seen = true;
        value_name = L"HookSeenClaude";
    } else {
        return true;
    }
    HKEY key = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER,
            kSettingsKey,
            0,
            nullptr,
            REG_OPTION_NON_VOLATILE,
            KEY_SET_VALUE,
            nullptr,
            &key,
            &disposition) != ERROR_SUCCESS) {
        return false;
    }
    const bool result = WriteDword(key, value_name, 1);
    RegCloseKey(key);
    return result;
}

bool Settings::CodexIntegrationPending() const {
    return codex_mode == DetectionMode::Tasks && codex_integration_expected && !codex_hook_seen;
}

bool Settings::UseProcessActivityFallback(Provider provider) const {
    return provider != Provider::ClaudeCode || claude_mode != DetectionMode::Tasks ||
           !claude_integration_expected;
}

bool Settings::IsProviderEnabled(Provider provider) const {
    return ProviderMode(provider) != DetectionMode::Off;
}

DetectionMode Settings::ProviderMode(Provider provider) const {
    switch (provider) {
        case Provider::Codex:
            return codex_mode;
        case Provider::ClaudeCode:
            return claude_mode;
        case Provider::Cursor:
            return cursor_mode;
        case Provider::OpenCode:
            return opencode_mode;
        case Provider::GeminiCli:
            return gemini_mode;
        case Provider::Manual:
        case Provider::External:
            return DetectionMode::Open;
    }
    return DetectionMode::Open;
}

void Settings::SetProviderMode(Provider provider, DetectionMode mode) {
    switch (provider) {
        case Provider::Codex:
            codex_mode = mode;
            break;
        case Provider::ClaudeCode:
            claude_mode = mode;
            break;
        case Provider::Cursor:
            cursor_mode = mode;
            break;
        case Provider::OpenCode:
            opencode_mode = mode;
            break;
        case Provider::GeminiCli:
            gemini_mode = mode;
            break;
        case Provider::Manual:
        case Provider::External:
            break;
    }
}

void Settings::CycleProviderMode(Provider provider) {
    SetProviderMode(provider, NextDetectionMode(ProviderMode(provider)));
}

DetectionMode NextDetectionMode(DetectionMode mode) {
    switch (mode) {
        case DetectionMode::Tasks:
            return DetectionMode::Open;
        case DetectionMode::Open:
            return DetectionMode::Off;
        case DetectionMode::Off:
            return DetectionMode::Tasks;
    }
    return DetectionMode::Tasks;
}

const wchar_t* DetectionModeLabel(DetectionMode mode) {
    switch (mode) {
        case DetectionMode::Tasks:
            return L"TASKS";
        case DetectionMode::Open:
            return L"OPEN";
        case DetectionMode::Off:
            return L"OFF";
    }
    return L"TASKS";
}

bool IsStartWithWindowsEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }
    const LSTATUS result = RegQueryValueExW(key, kRunValue, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

bool SetStartWithWindows(bool enabled, const std::wstring& executable_path) {
    HKEY key = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER,
            kRunKey,
            0,
            nullptr,
            REG_OPTION_NON_VOLATILE,
            KEY_QUERY_VALUE | KEY_SET_VALUE,
            nullptr,
            &key,
            &disposition) != ERROR_SUCCESS) {
        return false;
    }

    LSTATUS result = ERROR_SUCCESS;
    if (enabled) {
        const std::wstring command = L"\"" + executable_path + L"\" --background";
        result = RegSetValueExW(
            key,
            kRunValue,
            0,
            REG_SZ,
            reinterpret_cast<const BYTE*>(command.c_str()),
            static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        result = RegDeleteValueW(key, kRunValue);
        if (result == ERROR_FILE_NOT_FOUND) {
            result = ERROR_SUCCESS;
        }
    }
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

}  // namespace agent_latch
