#include "claude_lifecycle.h"
#include "agent_detector.h"
#include "app.h"
#include "ui.h"

#include <cstdio>

namespace agent_latch {
namespace {

std::vector<std::wstring> Split(const std::wstring& text) {
    std::vector<std::wstring> fields;
    std::size_t start = 0;
    for (;;) {
        const auto at = text.find(L'\t', start);
        fields.push_back(text.substr(start, at == std::wstring::npos ? at : at - start));
        if (at == std::wstring::npos) { return fields; }
        start = at + 1;
    }
}

}  // namespace

bool RunClaudeLifecycleSelfTests() {
    const auto unknown = [](ClaudeOwner) { return OwnerStatus::Unknown; };
    const auto alive = [](ClaudeOwner) { return OwnerStatus::Alive; };
    const auto exited = [](ClaudeOwner) { return OwnerStatus::Exited; };
    ClaudeLifecycle tracker;
    LatchRegistry registry;
    ULONGLONG now = 1000;
    auto send = [&](std::string_view json) {
        ClaudeEvent parsed;
        if (!ParseClaudeEvent(json, &parsed)) { return false; }
        ClaudeEvent round_trip;
        if (!ParseClaudeEventMessage(Split(BuildClaudeEventMessage(parsed)), &round_trip) ||
            round_trip.name != parsed.name || round_trip.session != parsed.session ||
            round_trip.agent != parsed.agent || round_trip.task != parsed.task ||
            round_trip.has_background_snapshot != parsed.has_background_snapshot ||
            round_trip.background.size() != parsed.background.size()) { return false; }
        tracker.Apply(round_trip, now++);
        tracker.Refresh(registry, now, unknown);
        return true;
    };
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "Claude lifecycle check failed at line %d\n", __LINE__); return false; } } while (false)
    CHECK(send(R"({"hook_event_name":"UserPromptSubmit","session_id":"one"})"));
    CHECK(registry.Size() == 1);
    CHECK(send(R"({"hook_event_name":"SubagentStart","session_id":"one","agent_id":"worker","agent_type":"Explore"})"));
    CHECK(registry.Size() == 2);
    // A parent Stop snapshot replaces, not duplicates, existing worker latches.
    CHECK(send(R"({"hook_event_name":"Stop","session_id":"one","background_tasks":[{"id":"worker","type":"subagent","status":"running"}]})"));
    CHECK(registry.Size() == 1 && registry.Find(L"claude-session:one:foreground") == nullptr);
    CHECK(send(R"({"hook_event_name":"SubagentStop","session_id":"one","agent_id":"worker","background_tasks":[]})"));
    CHECK(registry.Size() == 0 && tracker.SessionCount() == 0);
    // Child tool events never create/renew a phantom foreground latch.
    CHECK(send(R"({"hook_event_name":"PreToolUse","session_id":"one","agent_id":"worker"})"));
    CHECK(registry.Size() == 1 && registry.Find(L"claude-session:one:worker:worker") != nullptr);
    CHECK(send(R"({"hook_event_name":"SubagentStop","session_id":"one","agent_id":"worker"})"));
    CHECK(registry.Size() == 0);
    CHECK(send(R"({"hook_event_name":"PostToolUse","session_id":"one","agent_id":"worker"})"));
    CHECK(send(R"({"hook_event_name":"PostToolBatch","session_id":"one"})"));
    CHECK(registry.Size() == 0); // Late completion cannot resurrect ended work.
    // Multiple sessions are isolated; SessionEnd removes all of its descendants.
    CHECK(send(R"({"hook_event_name":"UserPromptSubmit","session_id":"one"})"));
    CHECK(send(R"({"hook_event_name":"SubagentStart","session_id":"one","agent_id":"worker"})"));
    CHECK(send(R"({"hook_event_name":"UserPromptSubmit","session_id":"one-other"})"));
    CHECK(send(R"({"hook_event_name":"SessionEnd","session_id":"one"})"));
    CHECK(registry.Size() == 1 && registry.Find(L"claude-session:one-other:foreground") != nullptr);
    CHECK(send(R"({"hook_event_name":"StopFailure","session_id":"one-other"})"));
    CHECK(registry.Size() == 0);
    // Planning tasks are not evidence of execution.
    CHECK(send(R"({"hook_event_name":"TaskCreated","session_id":"one","task_id":"todo"})"));
    CHECK(registry.Size() == 0 && tracker.SessionCount() == 0);
    CHECK(send(R"({"hook_event_name":"TaskCompleted","session_id":"one","task_id":"todo"})"));
    CHECK(registry.Size() == 0);
    // Completed/cancelled entries never hold a background latch; punctuation and
    // nested identity fields in private text do not alter session identity.
    CHECK(send(R"({"tool_input":{"session_id":"evil","background_tasks":[{"id":"wrong"}]},"hook_event_name":"Stop","session_id":"one","background_tasks":[{"id":"shell","type":"shell","status":"running","description":"private ] , \"session_id\":\"evil\""},{"id":"done","status":"completed"},{"id":"cancelled","status":"cancelled"}]})"));
    CHECK(registry.Size() == 1 && registry.Find(L"claude-session:one:worker:shell") != nullptr);
    CHECK(send(R"({"hook_event_name":"TaskCompleted","session_id":"one","task_id":"shell"})"));
    CHECK(registry.Size() == 0);
    // Foreground work is not ended by a worker finishing; remaining workers are
    // taken from the parent snapshot, including shell tasks without agent hooks.
    CHECK(send(R"({"hook_event_name":"UserPromptSubmit","session_id":"one"})"));
    CHECK(send(R"({"hook_event_name":"SubagentStop","session_id":"one","agent_id":"done","background_tasks":[{"id":"done","type":"subagent"},{"id":"shell","type":"shell"}]})"));
    CHECK(registry.Size() == 2);
    CHECK(send(R"({"hook_event_name":"Notification","session_id":"one","notification_type":"idle_prompt"})"));
    CHECK(registry.Size() == 1); // Idle foreground does not discard live background work.
    CHECK(send(R"({"hook_event_name":"Stop","session_id":"one","background_tasks":[]})"));
    CHECK(registry.Size() == 0);
    CHECK(send(R"({"hook_event_name":"Stop","session_id":"one","background_tasks":[{"id":"a","type":"shell"},{"id":"a","type":"shell"}]})"));
    CHECK(registry.Size() == 1); // Duplicate IDs count once.
    CHECK(send(R"({"hook_event_name":"SessionEnd","session_id":"one"})"));
    // Unknown ownership has a fixed 30-minute cap; liveness-verified work does
    // not silently expire while a long tool/model call is still running.
    CHECK(send(R"({"hook_event_name":"UserPromptSubmit","session_id":"one"})"));
    const ULONGLONG deadline = registry.Snapshot().front().expires_at;
    tracker.Refresh(registry, now + 1200000, unknown);
    CHECK(registry.Size() == 1 && registry.Snapshot().front().expires_at == deadline);
    tracker.Refresh(registry, deadline, unknown);
    CHECK(registry.Size() == 0 && tracker.SessionCount() == 0);
    CHECK(send(R"({"hook_event_name":"UserPromptSubmit","session_id":"one"})"));
    tracker.Refresh(registry, now + 7200000, alive);
    CHECK(registry.Size() == 1 && registry.Snapshot().front().expires_at == 0);
    tracker.Refresh(registry, now + 7200001, exited);
    CHECK(registry.Size() == 0 && tracker.SessionCount() == 0);
    // Unrelated external latches survive both reconciliation and provider Off.
    registry.Upsert(L"custom", Provider::ClaudeCode, LatchKind::External, L"External", L"", now, 0);
    CHECK(send(R"({"hook_event_name":"UserPromptSubmit","session_id":"one"})"));
    tracker.Clear(registry);
    CHECK(registry.Size() == 1 && registry.Find(L"custom") != nullptr);
    ClaudeEvent bad;
    CHECK(ParseClaudeEvent(R"({"session_id":"override"})", &bad, L"UserPromptSubmit") && bad.name == L"userpromptsubmit");
    CHECK(!ParseClaudeEvent(R"({"hook_event_name":"Stop","session_id":"one","background_tasks":null})", &bad));
    CHECK(!ParseClaudeEvent(R"({"hook_event_name":"Stop","session_id":"one","background_tasks":[{"type":"shell"}]})", &bad));
    CHECK(!ParseClaudeEvent(R"({"hook_event_name":"Stop","session_id":"one"} trailing)", &bad));
    CHECK(!ParseClaudeEvent(R"({"hook_event_name":"Stop","session_id":"bad\tidentity"})", &bad));
    CHECK(!ParseClaudeEvent(R"({"hook_event_name":"Notification","session_id":"one","notification_type":"permission_prompt"})", &bad));
    CHECK(!ParseClaudeEventMessage({L"CLAUDE2"}, &bad));
    auto fields = Split(L"CLAUDE2\tstop\tone\t\t\t\t4294967296\t1\t0");
    CHECK(!ParseClaudeEventMessage(fields, &bad));
    // Twenty simulated minutes of ongoing process activity is activity, never
    // a continuously-reset 'Releasing in' promise. Quiet begins one fixed grace.
    DetectionResult detection;
    detection.provider = Provider::Codex;
    detection.running_instances = detection.activity_capable_instances = 1;
    detection.process_activity_observed = true;
    for (ULONGLONG tick = 1000; tick <= 1201000; tick += 2000) {
        detection.last_activity = tick;
        ApplyDetectionGrace(&detection, tick, 180);
        CHECK(detection.recently_active && !detection.in_grace_period && detection.grace_remaining_milliseconds == 0);
        CHECK(DetectorLatchLabel(detection, DetectionMode::Tasks) == L"Codex · process activity");
    }
    detection.process_activity_observed = false;
    ApplyDetectionGrace(&detection, detection.last_activity + 2000, 180);
    CHECK(detection.in_grace_period && detection.grace_remaining_milliseconds == 178000);
    ApplyDetectionGrace(&detection, detection.last_activity + 179999, 180);
    CHECK(detection.in_grace_period && detection.grace_remaining_milliseconds == 1);
    ApplyDetectionGrace(&detection, detection.last_activity + 180000, 180);
    CHECK(!detection.recently_active && !detection.in_grace_period);
    Latch process_latch;
    process_latch.kind = LatchKind::Detector;
    process_latch.detail = L"Process activity detected";
    CHECK(LatchSecondaryText(process_latch, now) == L"Process activity");
    HANDLE self = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    CHECK(self != nullptr);
    FILETIME created{}, end{}, kernel{}, user{};
    CHECK(GetProcessTimes(self, &created, &end, &kernel, &user));
    CloseHandle(self);
    ULARGE_INTEGER created_value{};
    created_value.LowPart = created.dwLowDateTime; created_value.HighPart = created.dwHighDateTime;
    CHECK(QueryClaudeOwner({GetCurrentProcessId(), created_value.QuadPart}) == OwnerStatus::Alive);
    CHECK(QueryClaudeOwner({GetCurrentProcessId(), created_value.QuadPart + 1}) == OwnerStatus::Exited);
    CHECK(QueryClaudeOwner({}) == OwnerStatus::Unknown);
#undef CHECK
    return true;
}

}  // namespace agent_latch
