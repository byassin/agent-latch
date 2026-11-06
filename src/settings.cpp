#include "settings.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cwchar>

namespace agent_latch {
namespace {

constexpr wchar_t kSettingsKey[] = L"Software\\AgentLatch";
constexpr wchar_t kManualSessionKey[] = L"ManualSession";
constexpr wchar_t kManualSessionValue[] = L"UntilUtc";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"AgentLatch";

using RegistryQuery = decltype(&RegQueryValueExW);

bool TryReadDword(HKEY key, const wchar_t* name, DWORD* value,
                  bool* valid = nullptr, RegistryQuery query = RegQueryValueExW) {
    if (value == nullptr) { return false; }
    DWORD type = 0;
    DWORD size = sizeof(*value);
    const LSTATUS result = query(key, name, nullptr, &type, reinterpret_cast<BYTE*>(value), &size);
    // Missing or malformed individual preferences use defaults/legacy values.
    // An access/deleted-key/I/O failure is not a valid complete snapshot.
    if (valid != nullptr && result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND && result != ERROR_MORE_DATA) {
        *valid = false;
    }
    return result == ERROR_SUCCESS && type == REG_DWORD && size == sizeof(*value);
}

DWORD ReadDword(HKEY key, const wchar_t* name, DWORD fallback,
                bool* valid = nullptr, RegistryQuery query = RegQueryValueExW) {
    DWORD value = 0;
    return TryReadDword(key, name, &value, valid, query) ? value : fallback;
}

DetectionMode ReadDetectionMode(
    HKEY key,
    const wchar_t* mode_name,
    const wchar_t* legacy_enabled_name,
    DetectionMode fallback,
    bool* valid = nullptr,
    RegistryQuery query = RegQueryValueExW) {
    DWORD value = 0;
    if (TryReadDword(key, mode_name, &value, valid, query) && value <= static_cast<DWORD>(DetectionMode::Open)) {
        return static_cast<DetectionMode>(value);
    }
    if (TryReadDword(key, legacy_enabled_name, &value, valid, query)) {
        return value == 0 ? DetectionMode::Off : DetectionMode::Tasks;
    }
    return fallback;
}

bool WriteDword(HKEY key, const wchar_t* name, DWORD value) {
    return RegSetValueExW(
               key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS;
}

bool LoadSettings(Settings& target, RegistryQuery query = RegQueryValueExW) {
    // Always start from defaults: a missing value must not retain an old mode.
    // Commit once, only after the entire registry snapshot was read successfully.
    Settings snapshot;
    HKEY key = nullptr;
    const LSTATUS result = RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_QUERY_VALUE, &key);
    if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND) {
        target = snapshot;
        return true;
    }
    if (result != ERROR_SUCCESS) {
        return false;
    }

    bool valid = true;
    snapshot.keep_display_on = ReadDword(key, L"KeepDisplayOn", snapshot.keep_display_on ? 1u : 0u, &valid, query) != 0;
    snapshot.notifications = ReadDword(key, L"Notifications", snapshot.notifications ? 1u : 0u, &valid, query) != 0;
    ULONGLONG manual_value = 0;
    DWORD end_type = 0;
    DWORD end_size = sizeof(manual_value);
    // Never restore the legacy permanent value: its boot cannot be established.
    // A volatile child survives app restarts, but not a Windows restart/hive unload.
    HKEY manual_key = nullptr;
    const LSTATUS manual_result = RegOpenKeyExW(key, kManualSessionKey, 0, KEY_QUERY_VALUE, &manual_key);
    if (manual_result == ERROR_SUCCESS) {
        const LSTATUS end_result = query(manual_key, kManualSessionValue, nullptr, &end_type,
            reinterpret_cast<BYTE*>(&manual_value), &end_size);
        if (end_result != ERROR_SUCCESS || end_type != REG_QWORD || end_size != sizeof(manual_value)) {
            manual_value = 0;
        }
        valid = (end_result == ERROR_SUCCESS || end_result == ERROR_FILE_NOT_FOUND || end_result == ERROR_MORE_DATA) && valid;
    } else if (manual_result != ERROR_FILE_NOT_FOUND && manual_result != ERROR_PATH_NOT_FOUND) {
        valid = false;
    }
    if (manual_key != nullptr) { RegCloseKey(manual_key); }
    snapshot.manual_keep_awake = manual_value != 0;
    snapshot.manual_end_utc_milliseconds = manual_value > 1 ? manual_value : 0;
    snapshot.codex_mode = ReadDetectionMode(key, L"ModeCodex", L"DetectCodex", snapshot.codex_mode, &valid, query);
    const DWORD appearance_value = ReadDword(key, L"Appearance", 0, &valid, query);
    snapshot.appearance = appearance_value <= 2 ? static_cast<Appearance>(appearance_value) : Appearance::System;
    snapshot.claude_mode = ReadDetectionMode(key, L"ModeClaude", L"DetectClaude", snapshot.claude_mode, &valid, query);
    snapshot.cursor_mode = ReadDetectionMode(key, L"ModeCursor", L"DetectCursor", snapshot.cursor_mode, &valid, query);
    snapshot.opencode_mode = ReadDetectionMode(key, L"ModeOpenCode", L"DetectOpenCode", snapshot.opencode_mode, &valid, query);
    snapshot.gemini_mode = ReadDetectionMode(key, L"ModeGemini", L"DetectGemini", snapshot.gemini_mode, &valid, query);
    snapshot.activity_grace_seconds = std::clamp<DWORD>(ReadDword(key, L"ActivityGraceSeconds", 180, &valid, query), 30, 1800);
    snapshot.codex_integration_expected = ReadDword(key, L"IntegrationExpectedCodex", 0, &valid, query) != 0;
    snapshot.codex_hook_seen = ReadDword(key, L"HookSeenCodex", 0, &valid, query) != 0;
    snapshot.claude_integration_expected = ReadDword(key, L"IntegrationExpectedClaude", 0, &valid, query) != 0;
    snapshot.claude_hook_seen = ReadDword(key, L"HookSeenClaude", 0, &valid, query) != 0;
    RegCloseKey(key);
    if (!valid) { return false; }
    target = snapshot;
    return true;
}

bool SameSettings(const Settings& left, const Settings& right) {
    return left.appearance == right.appearance && left.keep_display_on == right.keep_display_on &&
        left.notifications == right.notifications && left.manual_keep_awake == right.manual_keep_awake &&
        left.manual_end_utc_milliseconds == right.manual_end_utc_milliseconds &&
        left.codex_mode == right.codex_mode && left.claude_mode == right.claude_mode &&
        left.cursor_mode == right.cursor_mode && left.opencode_mode == right.opencode_mode &&
        left.gemini_mode == right.gemini_mode && left.activity_grace_seconds == right.activity_grace_seconds &&
        left.codex_integration_expected == right.codex_integration_expected && left.codex_hook_seen == right.codex_hook_seen &&
        left.claude_integration_expected == right.claude_integration_expected && left.claude_hook_seen == right.claude_hook_seen;
}

LSTATUS WINAPI QueryWithInjectedFailure(HKEY key, LPCWSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size) {
    // Used only by the isolated settings regression below, never by Load().
    if (name != nullptr && std::wcscmp(name, L"ModeGemini") == 0) { return ERROR_ACCESS_DENIED; }
    return RegQueryValueExW(key, name, reserved, type, data, size);
}

bool RunTrackingModeSettingsTests(HKEY test_root) {
    // This is the newly-created unique test subtree's real handle, not HKCU.
    // All fixture edits/deletes below stay relative to it even if redirection
    // ever changes; only the production Load/Save calls exercise redirected HKCU.
    if (test_root == nullptr || test_root == HKEY_CURRENT_USER) { return false; }
    struct Preference { Provider provider; const wchar_t* mode; const wchar_t* legacy; };
    constexpr std::array preferences{
        Preference{Provider::Codex, L"ModeCodex", L"DetectCodex"},
        Preference{Provider::ClaudeCode, L"ModeClaude", L"DetectClaude"},
        Preference{Provider::Cursor, L"ModeCursor", L"DetectCursor"},
        Preference{Provider::OpenCode, L"ModeOpenCode", L"DetectOpenCode"},
        Preference{Provider::GeminiCli, L"ModeGemini", L"DetectGemini"}};
    bool success = true;
    // Rotate mixed values so every provider round-trips Off, Tasks and Open.
    for (std::size_t rotation = 0; rotation < 3; ++rotation) {
        Settings saved;
        saved.keep_display_on = true; saved.notifications = false;
        saved.manual_keep_awake = true; saved.appearance = Appearance::Dark;
        saved.activity_grace_seconds = 75;
        for (std::size_t index = 0; index < preferences.size(); ++index) {
            saved.SetProviderMode(preferences[index].provider, static_cast<DetectionMode>((index + rotation) % 3));
        }
        Settings loaded;
        success = saved.Save() && loaded.Load() && SameSettings(saved, loaded) && success;

        HKEY key = nullptr;
        if (RegOpenKeyExW(test_root, kSettingsKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) { return false; }
        for (const auto& preference : preferences) {
            const DWORD contrary = saved.ProviderMode(preference.provider) == DetectionMode::Off ? 1u : 0u;
            success = WriteDword(key, preference.legacy, contrary) && success;
        }
        RegCloseKey(key);
        // Explicit modes, including Off and Open, take precedence over legacy flags.
        success = loaded.Load() && SameSettings(saved, loaded) && success;
    }

    HKEY key = nullptr;
    if (RegOpenKeyExW(test_root, kSettingsKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) { return false; }
    for (std::size_t index = 0; index < preferences.size(); ++index) {
        const auto& preference = preferences[index];
        const DWORD legacy = static_cast<DWORD>(index % 2);
        const wchar_t malformed[] = L"0";
        success = WriteDword(key, preference.legacy, legacy) && success;
        success = (index % 2 == 0 ?
            RegSetValueExW(key, preference.mode, 0, REG_SZ, reinterpret_cast<const BYTE*>(malformed), sizeof(malformed)) == ERROR_SUCCESS :
            WriteDword(key, preference.mode, 99)) && success;
    }
    RegCloseKey(key);
    Settings legacy_fallback;
    success = legacy_fallback.Load() && success;
    for (std::size_t index = 0; index < preferences.size(); ++index) {
        success = legacy_fallback.ProviderMode(preferences[index].provider) ==
            (index % 2 == 0 ? DetectionMode::Off : DetectionMode::Tasks) && success;
    }

    Settings existing;
    existing.keep_display_on = false; existing.notifications = true;
    existing.manual_keep_awake = true; existing.manual_end_utc_milliseconds = 123456789;
    existing.appearance = Appearance::Light; existing.activity_grace_seconds = 600;
    existing.codex_integration_expected = existing.codex_hook_seen = true;
    existing.claude_integration_expected = existing.claude_hook_seen = true;
    for (const auto& preference : preferences) { existing.SetProviderMode(preference.provider, DetectionMode::Open); }
    const Settings before_failure = existing;
    // Fail near the end of the read, after different values have been observed.
    // A real read failure must not partially replace the caller's settings.
    success = !LoadSettings(existing, QueryWithInjectedFailure) && SameSettings(existing, before_failure) && success;

    // These keys belong to the process-local redirected test hive only.
    success = RegDeleteTreeW(test_root, kSettingsKey) == ERROR_SUCCESS && success;
    success = existing.Load() && SameSettings(existing, Settings{}) && success;
    if (RegCreateKeyExW(test_root, kSettingsKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
        KEY_ALL_ACCESS, nullptr, &key, nullptr) != ERROR_SUCCESS) { return false; }
    const wchar_t malformed[] = L"not-a-dword";
    success = RegSetValueExW(key, L"Notifications", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(malformed), sizeof(malformed)) == ERROR_SUCCESS && success;
    RegCloseKey(key);
    existing = before_failure;
    success = existing.Load() && SameSettings(existing, Settings{}) && success;
    success = RegDeleteTreeW(test_root, kSettingsKey) == ERROR_SUCCESS && success;
    return success;
}

}  // namespace

bool Settings::Load() { return LoadSettings(*this); }

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
    result = WriteDword(key, L"Appearance", static_cast<DWORD>(appearance)) && result;
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
    const bool valid = [test_root]() {
        if (!RunTrackingModeSettingsTests(test_root)) { return false; }
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
        original.appearance = Appearance::Dark;
        success = original.Save() && success;
        Settings app_restart;
        success = app_restart.Load() && app_restart.manual_keep_awake &&
            app_restart.manual_end_utc_milliseconds == 0 &&
            app_restart.claude_mode == DetectionMode::Off && !app_restart.notifications && success;
        success = app_restart.appearance == Appearance::Dark && success;

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
        for (Appearance appearance : {Appearance::System, Appearance::Light, Appearance::Dark}) {
            original.appearance = appearance;
            success = original.Save() && app_restart.Load() && app_restart.appearance == appearance && success;
        }
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) { return false; }
        const DWORD invalid_appearance = 99;
        success = RegSetValueExW(key, L"Appearance", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&invalid_appearance),
            sizeof(invalid_appearance)) == ERROR_SUCCESS && success;
        RegCloseKey(key);
        success = app_restart.Load() && app_restart.appearance == Appearance::System && success;
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
