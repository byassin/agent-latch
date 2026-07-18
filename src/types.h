#pragma once

#include <windows.h>

#include <string>

namespace agent_latch {

enum class Provider {
    Manual,
    Codex,
    ClaudeCode,
    Cursor,
    OpenCode,
    GeminiCli,
    External,
};

enum class LatchKind {
    Manual,
    Timer,
    Hook,
    Detector,
    External,
};

struct Latch {
    std::wstring id;
    Provider provider{Provider::External};
    LatchKind kind{LatchKind::External};
    std::wstring label;
    std::wstring detail;
    ULONGLONG created_at{0};
    ULONGLONG renewed_at{0};
    ULONGLONG expires_at{0};
    unsigned int instance_count{1};
};

const wchar_t* ProviderName(Provider provider);
const wchar_t* ProviderShortName(Provider provider);
const wchar_t* ProviderKey(Provider provider);
Provider ProviderFromString(const std::wstring& value);

}  // namespace agent_latch
