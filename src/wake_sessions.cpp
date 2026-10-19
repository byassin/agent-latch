#include "wake_sessions.h"
#include "latch_registry.h"
#include "power_request.h"

namespace agent_latch {

bool HoldFinalHookRelease(LatchRegistry& registry, Provider provider, ULONGLONG now) {
    for (const Latch& latch : registry.Snapshot()) {
        if (latch.provider == provider && latch.kind == LatchKind::Hook) { return false; }
    }
    registry.Upsert(L"hook-settle:" + std::wstring(ProviderKey(provider)), provider,
        LatchKind::Detector, std::wstring(ProviderShortName(provider)) + L" · release grace",
        L"Settling the final lifecycle event", now, 5000, 1, true);
    return true;
}

ULONGLONG AgentRestartGrace::Update(Provider provider, DetectionMode mode, bool present,
                                  bool protected_work, ULONGLONG now) {
    State& state = states_[static_cast<int>(provider)];
    if (mode != state.mode || mode == DetectionMode::Off) {
        state = State{};
        state.mode = mode;
    }
    if (mode == DetectionMode::Off) {
        return 0;
    }
    if (present) {
        state.deadline = 0;
        if (protected_work) { state.last_protected_at = now; }
    } else if (state.present && state.protected_work) {
        state.deadline = now + kDuration;
    }
    state.present = present;
    // SessionEnd may arrive just before the updater removes the process. Keep
    // one short observation tolerance, not a sliding grace for an idle app.
    const bool just_protected = state.last_protected_at != 0 &&
        now >= state.last_protected_at && now - state.last_protected_at <= 5000;
    state.protected_work = present && (mode == DetectionMode::Open || protected_work || just_protected);
    if (state.deadline != 0 && now >= state.deadline) {
        state.deadline = 0;
    }
    return state.deadline;
}

bool ManualKeepAwake::Start(unsigned int hours, ULONGLONG now, ULONGLONG utc_now) {
    if (hours > kMaximumHours) {
        return false;
    }
    const ULONGLONG duration = static_cast<ULONGLONG>(hours) * 60 * 60 * 1000;
    active_ = true;
    deadline_ = hours == 0 ? 0 : now + duration;
    utc_end_ = hours == 0 ? 0 : utc_now + duration;
    return true;
}

void ManualKeepAwake::Restore(bool enabled, ULONGLONG utc_end, ULONGLONG now, ULONGLONG utc_now) {
    Stop();
    constexpr ULONGLONG maximum = static_cast<ULONGLONG>(kMaximumHours) * 60 * 60 * 1000;
    if (!enabled || (utc_end != 0 && (utc_end <= utc_now || utc_end - utc_now > maximum))) {
        return;
    }
    active_ = true;
    utc_end_ = utc_end;
    deadline_ = utc_end == 0 ? 0 : now + (utc_end - utc_now);
}

void ManualKeepAwake::Stop() {
    active_ = false;
    deadline_ = 0;
    utc_end_ = 0;
}

bool ManualKeepAwake::Expire(ULONGLONG now) {
    if (active_ && deadline_ != 0 && now >= deadline_) {
        Stop();
        return true;
    }
    return false;
}

ULONGLONG UtcMilliseconds() {
    FILETIME time{};
    GetSystemTimeAsFileTime(&time);
    ULARGE_INTEGER value{};
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return value.QuadPart / 10000;
}

bool RunWakeSessionSelfTests() {
    AgentRestartGrace grace;
    constexpr Provider provider = Provider::ClaudeCode;
    constexpr DetectionMode open = DetectionMode::Open;
    if (grace.Update(provider, open, false, false, 1000) != 0 ||
        grace.Update(provider, open, true, true, 2000) != 0 ||
        grace.Update(provider, open, false, false, 3000) != 603000 ||
        grace.Update(provider, open, false, false, 5000) != 603000 ||
        grace.Update(provider, open, false, false, 602999) != 603000 ||
        grace.Update(provider, open, false, false, 603000) != 0 ||
        grace.Update(provider, open, false, false, 700000) != 0) {
        return false;
    }
    // Return clears grace; a later disappearance gets a fresh, bounded interval.
    grace.Update(provider, open, true, true, 800000);
    if (grace.Update(provider, open, false, false, 801000) != 1401000 ||
        grace.Update(provider, open, true, true, 802000) != 0 ||
        grace.Update(provider, open, false, false, 803000) != 1403000 ||
        grace.Update(provider, DetectionMode::Off, false, false, 804000) != 0 ||
        grace.Update(provider, open, false, false, 805000) != 0) {
        return false;
    }
    // Idle Tasks mode never arms, and a completed task disarms while app is open.
    constexpr DetectionMode tasks = DetectionMode::Tasks;
    grace.Update(provider, tasks, true, false, 900000);
    if (grace.Update(provider, tasks, false, false, 901000) != 0) { return false; }
    grace.Update(provider, tasks, true, true, 902000);
    if (grace.Update(provider, tasks, false, false, 903000) != 1503000) { return false; }
    grace.Update(provider, tasks, true, true, 904000);
    grace.Update(provider, tasks, true, false, 910000);
    if (grace.Update(provider, tasks, false, false, 911000) != 0 ||
        grace.Update(Provider::Codex, open, false, false, 911000) != 0) { return false; }
    grace.Update(provider, tasks, true, true, 920000);
    grace.Update(provider, tasks, true, false, 921000);
    if (grace.Update(provider, tasks, false, false, 922000) != 1522000) { return false; }
    grace.Update(Provider::Codex, open, true, true, 1000);
    if (grace.Update(Provider::Codex, open, false, false, 2000) != 602000 ||
        grace.Update(provider, tasks, false, false, 923000) != 1522000) { return false; }

    ManualKeepAwake manual;
    ManualKeepAwake maximum;
    if (!maximum.Start(168, 1000, 100000) || maximum.Start(169, 1000, 100000)) { return false; }
    if (!manual.Start(8, 1000, 100000) || manual.Deadline() != 28801000 ||
        manual.UtcEnd() != 28900000 || manual.Expire(28800999)) { return false; }
    ManualKeepAwake restored;
    restored.Restore(manual.Active(), manual.UtcEnd(), 5000, 3700000);
    if (!restored.Active() || restored.Deadline() != 25205000 ||
        !restored.Expire(25205000) || restored.Active()) { return false; }
    restored.Restore(true, 100000, 5000, 100000);
    if (restored.Active() || !manual.Expire(28801000) || manual.Active()) { return false; }
    if (!manual.Start(0, 1000, 100000) || manual.Expire(999999999) ||
        manual.Deadline() != 0) { return false; }
    restored.Restore(true, 0, 5000, 999999999);
    if (!restored.Active() || restored.Deadline() != 0 || manual.Start(169, 1, 1)) { return false; }
    manual.Stop();
    if (manual.Active()) { return false; }
    restored.Restore(true, 10000000000ULL, 1, 1);
    if (restored.Active()) { return false; }

    // Exercise the actual registry/power boundary with concurrent reasons:
    // stopping manual must not clear agent protection, nor may one expired
    // restart guard clear another provider or a manual session.
    LatchRegistry registry;
    PowerRequest power;
    manual.Start(0, 1000, 100000);
    registry.Upsert(L"manual:keep-awake", Provider::Manual, LatchKind::Manual,
                    L"Manual keep-awake", L"", 1000, 0);
    registry.Upsert(L"agent", Provider::Codex, LatchKind::Hook, L"Codex", L"", 1000, 0);
    if (!power.Apply(registry.IsActive(), false) || !power.IsSystemRequired()) { return false; }
    manual.Stop();
    registry.Remove(L"manual:keep-awake");
    if (registry.ActiveInstanceCount() != 1 ||
        !power.Apply(registry.IsActive(), false) || !power.IsSystemRequired()) { return false; }
    registry.Upsert(L"restart:claude", provider, LatchKind::RestartGrace,
                    L"Claude restart grace", L"", 1000, 600000, 1, true);
    registry.Expire(601000);
    if (registry.ActiveInstanceCount() != 1 ||
        !power.Apply(registry.IsActive(), false) || !power.IsSystemRequired()) { return false; }
    registry.Remove(L"agent");
    if (!power.Apply(registry.IsActive(), false) || power.IsSystemRequired()) { return false; }

    registry.Upsert(L"hook", provider, LatchKind::Hook, L"Claude", L"", 1000, 0);
    if (HoldFinalHookRelease(registry, provider, 2000)) { return false; }
    AgentRestartGrace ordering;
    ordering.Update(provider, tasks, true, true, 1000);
    registry.Remove(L"hook");
    if (!HoldFinalHookRelease(registry, provider, 2000) ||
        !power.Apply(registry.IsActive(), false) || !power.IsSystemRequired()) { return false; }
    ordering.Update(provider, tasks, true, true, 2000);
    const ULONGLONG restart_deadline = ordering.Update(provider, tasks, false, false, 4000);
    if (restart_deadline != 604000) { return false; }
    registry.Upsert(L"restart:claude", provider, LatchKind::RestartGrace, L"Restart grace", L"",
                    4000, restart_deadline - 4000, 1, true);
    registry.Expire(7000);
    if (registry.ActiveInstanceCount() != 1 ||
        !power.Apply(registry.IsActive(), false) || !power.IsSystemRequired()) { return false; }
    registry.Expire(604000);
    return power.Apply(registry.IsActive(), false) && !power.IsSystemRequired();
}

}  // namespace agent_latch
