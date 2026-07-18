#pragma once

#include "types.h"

#include <string>
#include <string_view>

namespace agent_latch {

enum class HookAction {
    None,
    Upsert,
    Remove,
};

struct HookTranslation {
    HookAction action{HookAction::None};
    std::wstring id;
    Provider provider{Provider::External};
    std::wstring label;
    std::wstring detail;
    ULONGLONG ttl_milliseconds{0};
};

bool ExtractJsonString(std::string_view json, std::string_view field, std::wstring* value);
HookTranslation TranslateHookEvent(
    Provider provider,
    std::string_view json,
    std::wstring_view event_override = {});
int HandleHookInvocation(Provider provider, std::wstring_view event_override = {});

}  // namespace agent_latch
