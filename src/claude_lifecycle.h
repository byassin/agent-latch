#pragma once

#include "latch_registry.h"

#include <functional>
#include <map>
#include <string_view>
#include <vector>

namespace agent_latch {

struct ClaudeOwner {
    DWORD pid{0};
    ULONGLONG created{0};
};
enum class OwnerStatus { Alive, Exited, Unknown };
struct ClaudeReleases {
    unsigned int exited{0};
    unsigned int expired{0};
};
struct ClaudeWorker {
    std::wstring id;
    std::wstring type;
};
struct ClaudeEvent {
    std::wstring name;
    std::wstring session;
    std::wstring agent;
    std::wstring task;
    std::wstring agent_type;
    ClaudeOwner owner;
    bool has_background_snapshot{false};
    std::vector<ClaudeWorker> background;
};

bool ParseClaudeEvent(std::string_view json, ClaudeEvent* event, std::wstring_view event_override = {});
std::wstring BuildClaudeEventMessage(const ClaudeEvent& event);
bool ParseClaudeEventMessage(const std::vector<std::wstring>& fields, ClaudeEvent* event);
ClaudeOwner FindClaudeHookOwner();
OwnerStatus QueryClaudeOwner(ClaudeOwner owner);

// State belongs to a session, not a provider-wide activity timer. No prompts,
// commands, transcript contents, or project paths are retained here.
class ClaudeLifecycle {
public:
    static constexpr ULONGLONG kUnverifiedLease = 30ULL * 60 * 1000;
    bool Apply(const ClaudeEvent& event, ULONGLONG now);
    ClaudeReleases Refresh(LatchRegistry& registry, ULONGLONG now,
                           const std::function<OwnerStatus(ClaudeOwner)>& query = QueryClaudeOwner);
    void Clear(LatchRegistry& registry);
    std::size_t SessionCount() const { return sessions_.size(); }

private:
    struct Session {
        bool foreground{false};
        ULONGLONG last_event{0};
        ClaudeOwner owner;
        std::map<std::wstring, ClaudeWorker> workers;
    };
    std::map<std::wstring, Session> sessions_;
};

bool RunClaudeLifecycleSelfTests();

}  // namespace agent_latch
