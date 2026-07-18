#include "settings.h"

#include <windows.h>

#include <algorithm>

namespace agent_latch {
namespace {

constexpr wchar_t kSettingsKey[] = L"Software\\AgentLatch";
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
    codex_mode = ReadDetectionMode(key, L"ModeCodex", L"DetectCodex", codex_mode);
    claude_mode = ReadDetectionMode(key, L"ModeClaude", L"DetectClaude", claude_mode);
    cursor_mode = ReadDetectionMode(key, L"ModeCursor", L"DetectCursor", cursor_mode);
    opencode_mode = ReadDetectionMode(key, L"ModeOpenCode", L"DetectOpenCode", opencode_mode);
    gemini_mode = ReadDetectionMode(key, L"ModeGemini", L"DetectGemini", gemini_mode);
    activity_grace_seconds = std::clamp<DWORD>(ReadDword(key, L"ActivityGraceSeconds", 180), 30, 1800);
    codex_integration_expected = ReadDword(key, L"IntegrationExpectedCodex", 0) != 0;
    codex_hook_seen = ReadDword(key, L"HookSeenCodex", 0) != 0;
    RegCloseKey(key);
    return true;
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
            KEY_SET_VALUE,
            nullptr,
            &key,
            &disposition) != ERROR_SUCCESS) {
        return false;
    }

    bool result = true;
    result = WriteDword(key, L"KeepDisplayOn", keep_display_on ? 1u : 0u) && result;
    result = WriteDword(key, L"Notifications", notifications ? 1u : 0u) && result;
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
        return true;
    }
    if (result != ERROR_SUCCESS) {
        return false;
    }
    codex_integration_expected = ReadDword(key, L"IntegrationExpectedCodex", 0) != 0;
    codex_hook_seen = ReadDword(key, L"HookSeenCodex", 0) != 0;
    RegCloseKey(key);
    return true;
}

bool Settings::MarkHookSeen(Provider provider) {
    if (provider != Provider::Codex) {
        return true;
    }
    codex_hook_seen = true;
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
    const bool result = WriteDword(key, L"HookSeenCodex", 1);
    RegCloseKey(key);
    return result;
}

bool Settings::CodexIntegrationPending() const {
    return codex_mode == DetectionMode::Tasks && codex_integration_expected && !codex_hook_seen;
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
