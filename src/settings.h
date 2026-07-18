#pragma once

#include "types.h"

#include <string>

namespace agent_latch {

struct Settings {
    bool keep_display_on{false};
    bool notifications{true};
    bool codex_enabled{true};
    bool claude_enabled{true};
    bool cursor_enabled{true};
    bool opencode_enabled{true};
    bool gemini_enabled{true};
    DWORD activity_grace_seconds{180};

    bool Load();
    bool Save() const;
    bool IsProviderEnabled(Provider provider) const;
    void SetProviderEnabled(Provider provider, bool enabled);
};

bool IsStartWithWindowsEnabled();
bool SetStartWithWindows(bool enabled, const std::wstring& executable_path);

}  // namespace agent_latch
