#pragma once

#ifndef AGENTLATCH_VERSION
#define AGENTLATCH_VERSION "0.2.1"
#endif

#define AGENTLATCH_WIDEN_IMPL(value) L##value
#define AGENTLATCH_WIDEN(value) AGENTLATCH_WIDEN_IMPL(value)

namespace agent_latch {

inline constexpr wchar_t kAgentLatchVersion[] = AGENTLATCH_WIDEN(AGENTLATCH_VERSION);

}  // namespace agent_latch
