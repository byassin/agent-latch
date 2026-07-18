#pragma once

#include "types.h"

#include <string>

namespace agent_latch {

enum class DetectionMode : DWORD {
    Off = 0,
    Tasks = 1,
    Open = 2,
};

struct Settings {
    bool keep_display_on{false};
    bool notifications{true};
    DetectionMode codex_mode{DetectionMode::Tasks};
    DetectionMode claude_mode{DetectionMode::Tasks};
    DetectionMode cursor_mode{DetectionMode::Tasks};
    DetectionMode opencode_mode{DetectionMode::Tasks};
    DetectionMode gemini_mode{DetectionMode::Tasks};
    DWORD activity_grace_seconds{180};
    bool codex_integration_expected{false};
    bool codex_hook_seen{false};

    bool Load();
    bool Save() const;
    bool RefreshIntegrationStatus();
    bool MarkHookSeen(Provider provider);
    bool CodexIntegrationPending() const;
    bool IsProviderEnabled(Provider provider) const;
    DetectionMode ProviderMode(Provider provider) const;
    void SetProviderMode(Provider provider, DetectionMode mode);
    void CycleProviderMode(Provider provider);
};

DetectionMode NextDetectionMode(DetectionMode mode);
const wchar_t* DetectionModeLabel(DetectionMode mode);

bool IsStartWithWindowsEnabled();
bool SetStartWithWindows(bool enabled, const std::wstring& executable_path);

}  // namespace agent_latch
