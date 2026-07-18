#include "app.h"
#include "hook_bridge.h"
#include "ipc.h"
#include "latch_registry.h"
#include "power_request.h"
#include "types.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace agent_latch {
namespace {

struct CommandLine {
    std::vector<std::wstring> arguments;

    bool Has(const std::wstring& option) const {
        return std::find(arguments.begin(), arguments.end(), option) != arguments.end();
    }

    std::wstring ValueAfter(const std::wstring& option) const {
        const auto iterator = std::find(arguments.begin(), arguments.end(), option);
        return iterator != arguments.end() && std::next(iterator) != arguments.end() ? *std::next(iterator)
                                                                                     : std::wstring{};
    }
};

CommandLine ReadCommandLine() {
    CommandLine result;
    int count = 0;
    LPWSTR* values = CommandLineToArgvW(GetCommandLineW(), &count);
    if (values == nullptr) {
        return result;
    }
    for (int index = 1; index < count; ++index) {
        result.arguments.emplace_back(values[index]);
    }
    LocalFree(values);
    return result;
}

bool ParseSeconds(const std::wstring& text, ULONGLONG* milliseconds) {
    if (milliseconds == nullptr || text.empty()) {
        return false;
    }
    wchar_t* end = nullptr;
    const unsigned long long seconds = std::wcstoull(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != L'\0' || seconds > 86400) {
        return false;
    }
    *milliseconds = static_cast<ULONGLONG>(seconds) * 1000;
    return true;
}

int RunSelfTests() {
    const ULONGLONG now = GetTickCount64();
    LatchRegistry registry;
    if (!registry.Upsert(L"test", Provider::Codex, LatchKind::Hook, L"Codex task", L"test", now, 1000) ||
        !registry.IsActive() || registry.Size() != 1 || registry.Find(L"test") == nullptr) {
        return 41;
    }
    if (registry.Expire(now + 999) || !registry.Expire(now + 1000) || registry.IsActive()) {
        return 42;
    }

    const std::string_view codex_start =
        R"({"session_id":"session-1","cwd":"C:\\work\\demo","hook_event_name":"UserPromptSubmit"})";
    const HookTranslation start = TranslateHookEvent(Provider::Codex, codex_start);
    if (start.action != HookAction::Upsert || start.id != L"codex:session-1" || start.detail != L"demo") {
        return 43;
    }
    const std::string_view codex_stop = R"({"session_id":"session-1","hook_event_name":"Stop"})";
    const HookTranslation stop = TranslateHookEvent(Provider::Codex, codex_stop);
    if (stop.action != HookAction::Remove || stop.id != L"codex:session-1") {
        return 44;
    }
    const std::string_view claude_subagent =
        R"({"session_id":"session-2","agent_id":"agent-9","agent_type":"Explore","hook_event_name":"SubagentStart"})";
    const HookTranslation subagent = TranslateHookEvent(Provider::ClaudeCode, claude_subagent);
    if (subagent.action != HookAction::Upsert || subagent.id != L"claude:session-2:agent-9" ||
        subagent.label.find(L"Explore") == std::wstring::npos) {
        return 45;
    }
    const std::string_view unicode_json = R"({"value":"Agent \u2713"})";
    std::wstring unicode_value;
    if (!ExtractJsonString(unicode_json, "value", &unicode_value) || unicode_value != L"Agent ✓") {
        return 46;
    }

    const ProcessClassification cursor_app = ClassifyAgentProcess(
        L"Cursor.exe", L"C:\\Users\\test\\AppData\\Local\\Programs\\cursor\\Cursor.exe");
    const ProcessClassification antigravity_app = ClassifyAgentProcess(
        L"Antigravity.exe", L"C:\\Users\\test\\AppData\\Local\\Programs\\Antigravity\\Antigravity.exe");
    const ProcessClassification claude_desktop = ClassifyAgentProcess(
        L"Claude.exe", L"C:\\Program Files\\WindowsApps\\Claude_1.0.0.0_x64__test\\app\\Claude.exe");
    const ProcessClassification claude_cli = ClassifyAgentProcess(L"claude.exe", L"C:\\tools\\claude.exe");
    const ProcessClassification claude_unknown = ClassifyAgentProcess(L"claude.exe", L"");
    const ProcessClassification codex_desktop = ClassifyAgentProcess(
        L"codex.exe", L"C:\\Program Files\\WindowsApps\\OpenAI.Codex_1.0.0.0_x64__test\\app\\codex.exe");
    if (cursor_app.provider != Provider::Cursor || cursor_app.activity_capable ||
        antigravity_app.provider != Provider::GeminiCli || antigravity_app.activity_capable ||
        claude_desktop.provider != Provider::ClaudeCode || claude_desktop.activity_capable ||
        claude_cli.provider != Provider::ClaudeCode || !claude_cli.activity_capable ||
        claude_unknown.provider != Provider::ClaudeCode || claude_unknown.activity_capable ||
        codex_desktop.provider != Provider::Codex || codex_desktop.activity_capable) {
        return 47;
    }

    const std::string_view antigravity_start =
        R"({"conversationId":"gravity-1","fullyIdle":false,"workspacePaths":["C:\\work\\demo"]})";
    const HookTranslation gravity_start =
        TranslateHookEvent(Provider::GeminiCli, antigravity_start, L"PreInvocation");
    const HookTranslation gravity_background =
        TranslateHookEvent(Provider::GeminiCli, antigravity_start, L"Stop");
    const std::string_view antigravity_stop = R"({"conversationId":"gravity-1","fullyIdle":true})";
    const HookTranslation gravity_stop =
        TranslateHookEvent(Provider::GeminiCli, antigravity_stop, L"Stop");
    if (gravity_start.action != HookAction::Upsert || gravity_start.id != L"gemini:gravity-1" ||
        gravity_background.action != HookAction::Upsert || gravity_stop.action != HookAction::Remove) {
        return 48;
    }

    const std::string_view cursor_complete =
        R"({"conversation_id":"cursor-1","generation_id":"generation-1","hook_event_name":"afterAgentResponse"})";
    const HookTranslation cursor_stop = TranslateHookEvent(Provider::Cursor, cursor_complete);
    if (cursor_stop.action != HookAction::Remove || cursor_stop.id != L"cursor:cursor-1") {
        return 49;
    }
    if (NextDetectionMode(DetectionMode::Tasks) != DetectionMode::Open ||
        NextDetectionMode(DetectionMode::Open) != DetectionMode::Off ||
        NextDetectionMode(DetectionMode::Off) != DetectionMode::Tasks) {
        return 50;
    }

    const std::string_view codex_desktop_active =
        R"({"type":"event_msg","payload":{"type":"task_complete"}}
{"type":"response_item","payload":{"type":"message","text":"escaped \"type\":\"task_started\""}}
{"type":"event_msg","payload":{"type":"task_started"}})";
    const std::string_view codex_desktop_complete =
        R"({"type":"event_msg","payload":{"type":"task_started"}}
{"type":"event_msg","payload":{"type":"task_complete"}})";
    if (LatestCodexSessionLifecycle(codex_desktop_active) != CodexSessionLifecycle::Active ||
        LatestCodexSessionLifecycle(codex_desktop_complete) != CodexSessionLifecycle::Inactive ||
        LatestCodexSessionLifecycle(R"({"type":"event_msg","payload":{"type":"user_message"}})") !=
            CodexSessionLifecycle::Unknown) {
        return 52;
    }

    PowerRequest request;
    if (!request.IsAvailable() || !request.Apply(true, false) || !request.IsSystemRequired() ||
        !request.Apply(false, false) || request.IsSystemRequired()) {
        return 51;
    }
    return 0;
}

int HandleUtilityCommand(const CommandLine& command_line) {
    if (command_line.Has(L"--show")) {
        if (EnsureBackgroundInstance()) {
            SendIpcMessage(L"SHOW");
        }
        return 0;
    }
    if (command_line.Has(L"--quit")) {
        SendIpcMessage(L"EXIT");
        return 0;
    }
    if (command_line.Has(L"--release")) {
        const std::wstring id = command_line.ValueAfter(L"--id");
        if (!id.empty() && EnsureBackgroundInstance()) {
            SendIpcMessage(BuildRemoveMessage(L"external:" + id));
        }
        return id.empty() ? 2 : 0;
    }
    if (command_line.Has(L"--acquire")) {
        const std::wstring id = command_line.ValueAfter(L"--id");
        const Provider provider = ProviderFromString(command_line.ValueAfter(L"--source"));
        std::wstring label = command_line.ValueAfter(L"--label");
        if (label.empty()) {
            label = std::wstring(ProviderName(provider)) + L" lease";
        }
        ULONGLONG ttl = 30ULL * 60ULL * 1000ULL;
        const std::wstring ttl_text = command_line.ValueAfter(L"--ttl");
        if (!ttl_text.empty() && !ParseSeconds(ttl_text, &ttl)) {
            return 3;
        }
        if (id.empty() || !EnsureBackgroundInstance()) {
            return id.empty() ? 2 : 4;
        }
        const std::wstring message = BuildUpsertMessage(
            L"external:" + id,
            provider,
            LatchKind::External,
            label,
            command_line.ValueAfter(L"--detail"),
            ttl);
        return SendIpcMessage(message) ? 0 : 5;
    }
    return -1;
}

}  // namespace
}  // namespace agent_latch

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous_instance, PWSTR command_line_text, int show_command) {
    (void)previous_instance;
    (void)command_line_text;
    (void)show_command;

    using namespace agent_latch;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const CommandLine command_line = ReadCommandLine();

    if (command_line.Has(L"--self-test")) {
        return RunSelfTests();
    }
    if (command_line.Has(L"--hook")) {
        return HandleHookInvocation(
            ProviderFromString(command_line.ValueAfter(L"--hook")),
            command_line.ValueAfter(L"--event"));
    }
    const int utility_result = HandleUtilityCommand(command_line);
    if (utility_result >= 0) {
        return utility_result;
    }

    HANDLE mutex = CreateMutexW(nullptr, TRUE, kInstanceMutexName);
    if (mutex == nullptr) {
        return 11;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (!command_line.Has(L"--background")) {
            SendIpcMessage(L"SHOW");
        }
        CloseHandle(mutex);
        return 0;
    }

    AgentLatchApp app(instance);
    const int result = app.Run(!command_line.Has(L"--background"));
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return result;
}
