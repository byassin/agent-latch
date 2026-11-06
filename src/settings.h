#pragma once

#include "types.h"

#include <string>

namespace agent_latch {

enum class DetectionMode : DWORD {
    Off = 0,
    Tasks = 1,
    Open = 2,
};
enum class Appearance : DWORD { System = 0, Light = 1, Dark = 2 };

struct Settings {
    Appearance appearance{Appearance::System};
    bool keep_display_on{false};
    bool notifications{true};
    bool manual_keep_awake{false};
    ULONGLONG manual_end_utc_milliseconds{0};
    DetectionMode codex_mode{DetectionMode::Tasks};
    DetectionMode claude_mode{DetectionMode::Tasks};
    DetectionMode cursor_mode{DetectionMode::Tasks};
    DetectionMode opencode_mode{DetectionMode::Tasks};
    DetectionMode gemini_mode{DetectionMode::Tasks};
    DWORD activity_grace_seconds{180};
    bool codex_integration_expected{false};
    bool codex_hook_seen{false};
    bool claude_integration_expected{false};
    bool claude_hook_seen{false};

    // A successful load replaces the whole snapshot, using defaults for missing
    // or malformed preferences. Read failures leave this object unchanged.
    bool Load();
    bool Save() const;
    bool RefreshIntegrationStatus();
    bool MarkHookSeen(Provider provider);
    bool CodexIntegrationPending() const;
    bool UseProcessActivityFallback(Provider provider) const;
    bool IsProviderEnabled(Provider provider) const;
    DetectionMode ProviderMode(Provider provider) const;
    void SetProviderMode(Provider provider, DetectionMode mode);
    void CycleProviderMode(Provider provider);
};

DetectionMode NextDetectionMode(DetectionMode mode);
const wchar_t* DetectionModeLabel(DetectionMode mode);

bool IsStartWithWindowsEnabled();
bool SetStartWithWindows(bool enabled, const std::wstring& executable_path);
bool RunSettingsSelfTests();

}  // namespace agent_latch
