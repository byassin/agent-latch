#pragma once

#include "settings.h"
#include "latch_registry.h"

#include <unordered_map>

namespace agent_latch {

// A single fixed deadline per disappearance, never renewed by missing scans.
class AgentRestartGrace {
public:
    static constexpr ULONGLONG kDuration = 10ULL * 60 * 1000;
    ULONGLONG Update(Provider provider, DetectionMode mode, bool present,
                     bool protected_work, ULONGLONG now);

private:
    struct State {
        DetectionMode mode{DetectionMode::Off};
        bool present{false};
        bool protected_work{false};
        ULONGLONG last_protected_at{0};
        ULONGLONG deadline{0};
    };
    std::unordered_map<int, State> states_;
};

class ManualKeepAwake {
public:
    static constexpr unsigned int kMaximumHours = 168;
    bool Start(unsigned int hours, ULONGLONG now, ULONGLONG utc_now);
    void Restore(bool enabled, ULONGLONG utc_end, ULONGLONG now, ULONGLONG utc_now);
    void Stop();
    bool Expire(ULONGLONG now);
    bool Active() const { return active_; }
    ULONGLONG Deadline() const { return deadline_; }
    ULONGLONG UtcEnd() const { return utc_end_; }

private:
    bool active_{false};
    ULONGLONG deadline_{0};
    ULONGLONG utc_end_{0};
};

ULONGLONG UtcMilliseconds();
bool HoldFinalHookRelease(LatchRegistry& registry, Provider provider, ULONGLONG now);
bool RunWakeSessionSelfTests();

}  // namespace agent_latch
