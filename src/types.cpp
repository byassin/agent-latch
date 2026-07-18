#include "types.h"

#include <algorithm>
#include <cwctype>

namespace agent_latch {
namespace {

std::wstring Lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return value;
}

}  // namespace

const wchar_t* ProviderName(Provider provider) {
    switch (provider) {
        case Provider::Manual:
            return L"Manual";
        case Provider::Codex:
            return L"Codex";
        case Provider::ClaudeCode:
            return L"Claude Code";
        case Provider::Cursor:
            return L"Cursor";
        case Provider::OpenCode:
            return L"OpenCode";
        case Provider::GeminiCli:
            return L"Gemini CLI";
        case Provider::External:
            return L"External";
    }
    return L"External";
}

const wchar_t* ProviderShortName(Provider provider) {
    switch (provider) {
        case Provider::ClaudeCode:
            return L"Claude";
        case Provider::GeminiCli:
            return L"Gemini";
        default:
            return ProviderName(provider);
    }
}

const wchar_t* ProviderKey(Provider provider) {
    switch (provider) {
        case Provider::Manual:
            return L"manual";
        case Provider::Codex:
            return L"codex";
        case Provider::ClaudeCode:
            return L"claude";
        case Provider::Cursor:
            return L"cursor";
        case Provider::OpenCode:
            return L"opencode";
        case Provider::GeminiCli:
            return L"gemini";
        case Provider::External:
            return L"external";
    }
    return L"external";
}

Provider ProviderFromString(const std::wstring& value) {
    const std::wstring normalized = Lowercase(value);
    if (normalized == L"codex" || normalized == L"openai") {
        return Provider::Codex;
    }
    if (normalized == L"claude" || normalized == L"claude-code" || normalized == L"claudecode") {
        return Provider::ClaudeCode;
    }
    if (normalized == L"cursor" || normalized == L"cursor-agent") {
        return Provider::Cursor;
    }
    if (normalized == L"opencode" || normalized == L"open-code") {
        return Provider::OpenCode;
    }
    if (normalized == L"gemini" || normalized == L"gemini-cli") {
        return Provider::GeminiCli;
    }
    if (normalized == L"manual") {
        return Provider::Manual;
    }
    return Provider::External;
}

}  // namespace agent_latch
