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
    codex_enabled = ReadDword(key, L"DetectCodex", codex_enabled ? 1u : 0u) != 0;
    claude_enabled = ReadDword(key, L"DetectClaude", claude_enabled ? 1u : 0u) != 0;
    cursor_enabled = ReadDword(key, L"DetectCursor", cursor_enabled ? 1u : 0u) != 0;
    opencode_enabled = ReadDword(key, L"DetectOpenCode", opencode_enabled ? 1u : 0u) != 0;
    gemini_enabled = ReadDword(key, L"DetectGemini", gemini_enabled ? 1u : 0u) != 0;
    activity_grace_seconds = std::clamp<DWORD>(ReadDword(key, L"ActivityGraceSeconds", 180), 30, 1800);
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
    result = WriteDword(key, L"DetectCodex", codex_enabled ? 1u : 0u) && result;
    result = WriteDword(key, L"DetectClaude", claude_enabled ? 1u : 0u) && result;
    result = WriteDword(key, L"DetectCursor", cursor_enabled ? 1u : 0u) && result;
    result = WriteDword(key, L"DetectOpenCode", opencode_enabled ? 1u : 0u) && result;
    result = WriteDword(key, L"DetectGemini", gemini_enabled ? 1u : 0u) && result;
    result = WriteDword(key, L"ActivityGraceSeconds", activity_grace_seconds) && result;
    RegCloseKey(key);
    return result;
}

bool Settings::IsProviderEnabled(Provider provider) const {
    switch (provider) {
        case Provider::Codex:
            return codex_enabled;
        case Provider::ClaudeCode:
            return claude_enabled;
        case Provider::Cursor:
            return cursor_enabled;
        case Provider::OpenCode:
            return opencode_enabled;
        case Provider::GeminiCli:
            return gemini_enabled;
        case Provider::Manual:
        case Provider::External:
            return true;
    }
    return true;
}

void Settings::SetProviderEnabled(Provider provider, bool enabled) {
    switch (provider) {
        case Provider::Codex:
            codex_enabled = enabled;
            break;
        case Provider::ClaudeCode:
            claude_enabled = enabled;
            break;
        case Provider::Cursor:
            cursor_enabled = enabled;
            break;
        case Provider::OpenCode:
            opencode_enabled = enabled;
            break;
        case Provider::GeminiCli:
            gemini_enabled = enabled;
            break;
        case Provider::Manual:
        case Provider::External:
            break;
    }
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
