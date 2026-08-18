#include "app.h"
#include "hook_bridge.h"
#include "ipc.h"
#include "latch_registry.h"
#include "openai_ui_activity.h"
#include "power_request.h"
#include "settings.h"
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

bool RunOpenAIUiActivityContractTests() {
    const std::wstring composer_class =
        L"inline-flex size-token-button-composer rounded-full bg-primary-solid";
    if (!IsOpenAIComposerClass(composer_class) ||
        IsOpenAIComposerClass(L"size-token-button-composer-ish bg-primary-solid") ||
        IsOpenAIComposerClass(L"size-token-button-composer bg-primary-solid-ish")) {
        return false;
    }

    OpenAIComposerClassifier english;
    const OpenAIActivitySnapshot chat_stop = english.Observe(
        OpenAIComposerObservation{L"Stop", composer_class, L"ChatGPT", true, false},
        1000);
    const OpenAIActivitySnapshot chat_send = english.Observe(
        OpenAIComposerObservation{L"Send", composer_class, L"ChatGPT", true, false},
        1001);
    const OpenAIActivitySnapshot minimized_stop = english.Observe(
        OpenAIComposerObservation{L"Stop", composer_class, L"ChatGPT", true, true},
        1002);
    const OpenAIActivitySnapshot disabled_stop = english.Observe(
        OpenAIComposerObservation{L"Stop", composer_class, L"ChatGPT", false, false},
        1003);
    if (chat_stop.state != OpenAIResponseState::Responding ||
        chat_stop.surface != OpenAISurface::ChatGPT || chat_stop.observed_at != 1000 ||
        chat_send.state != OpenAIResponseState::Inactive ||
        minimized_stop.state != OpenAIResponseState::Responding ||
        disabled_stop.state != OpenAIResponseState::Inactive) {
        return false;
    }

    OpenAIComposerClassifier codex;
    const OpenAIActivitySnapshot codex_stop = codex.Observe(
        OpenAIComposerObservation{L"Stop", composer_class, L"Codex", true, false},
        2000);
    const OpenAIActivitySnapshot unknown_document = codex.Observe(
        OpenAIComposerObservation{L"Stop", composer_class, L"Projects", true, false},
        2001);
    const OpenAIActivitySnapshot empty_name = codex.Observe(
        OpenAIComposerObservation{L"", composer_class, L"Codex", true, false},
        2002);
    const OpenAIActivitySnapshot unrelated = codex.Observe(
        OpenAIComposerObservation{L"Stop", L"ordinary-button", L"Codex", true, false},
        2003);
    if (codex_stop.state != OpenAIResponseState::Responding ||
        codex_stop.surface != OpenAISurface::Codex ||
        unknown_document.state != OpenAIResponseState::Unknown ||
        empty_name.state != OpenAIResponseState::Unknown ||
        unrelated.state != OpenAIResponseState::Unknown) {
        return false;
    }

    OpenAIComposerClassifier localized;
    const OpenAIComposerObservation localized_idle{
        L"Envoyer", composer_class, L"ChatGPT", false, false};
    if (localized.Observe(
            OpenAIComposerObservation{L"Envoyer", composer_class, L"ChatGPT", true, false},
            3000).state != OpenAIResponseState::Unknown ||
        localized.Observe(localized_idle, 3001).state != OpenAIResponseState::Inactive ||
        localized.Observe(localized_idle, 3002).state != OpenAIResponseState::Inactive ||
        localized.Observe(localized_idle, 3003).state != OpenAIResponseState::Inactive ||
        localized.Observe(
            OpenAIComposerObservation{L"Envoyer", composer_class, L"ChatGPT", true, false},
            3004).state != OpenAIResponseState::Inactive ||
        localized.Observe(
            OpenAIComposerObservation{L"Arreter", composer_class, L"ChatGPT", true, false},
            3005).state != OpenAIResponseState::Responding) {
        return false;
    }

    OpenAIComposerClassifier transient;
    transient.Observe(
        OpenAIComposerObservation{L"Arreter", composer_class, L"ChatGPT", false, false},
        4000);
    if (transient.Observe(
            OpenAIComposerObservation{L"Arreter", composer_class, L"ChatGPT", true, false},
            4001).state != OpenAIResponseState::Unknown) {
        return false;
    }

    const OpenAIActivitySnapshot chat_response{
        OpenAIResponseState::Responding, OpenAISurface::ChatGPT, 9000};
    const OpenAIMergedActivity fresh_chat =
        MergeOpenAIActivity(0, chat_response, 10000, 4000);
    const OpenAIMergedActivity codex_fallback = MergeOpenAIActivity(
        0,
        OpenAIActivitySnapshot{OpenAIResponseState::Responding, OpenAISurface::Codex, 9000},
        10000,
        4000);
    const OpenAIMergedActivity one_codex =
        MergeOpenAIActivity(1, chat_response, 10000, 4000);
    const OpenAIMergedActivity two_codex =
        MergeOpenAIActivity(2, chat_response, 10000, 4000);
    if (fresh_chat.active_instances != 1 || fresh_chat.detail != L"ChatGPT is responding" ||
        codex_fallback.active_instances != 1 || codex_fallback.detail != L"Codex is responding" ||
        one_codex.active_instances != 1 || one_codex.detail != L"1 Codex task is running" ||
        two_codex.active_instances != 2 || two_codex.detail != L"2 Codex tasks are running") {
        return false;
    }

    const OpenAIMergedActivity inactive = MergeOpenAIActivity(
        0,
        OpenAIActivitySnapshot{OpenAIResponseState::Inactive, OpenAISurface::ChatGPT, 9000},
        10000,
        4000);
    const OpenAIMergedActivity stale =
        MergeOpenAIActivity(0, chat_response, 13001, 4000);
    const OpenAIMergedActivity future =
        MergeOpenAIActivity(0, chat_response, 8999, 4000);
    const OpenAIMergedActivity unknown_surface = MergeOpenAIActivity(
        0,
        OpenAIActivitySnapshot{OpenAIResponseState::Responding, OpenAISurface::Unknown, 9000},
        10000,
        4000);
    if (inactive.active_instances != 0 || !inactive.detail.empty() ||
        stale.active_instances != 0 || !stale.detail.empty() ||
        future.active_instances != 0 || !future.detail.empty() ||
        unknown_surface.active_instances != 0 || !unknown_surface.detail.empty()) {
        return false;
    }
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
    const ProcessClassification chatgpt_codex = ClassifyAgentProcess(
        L"ChatGPT.exe", L"C:\\Program Files\\WindowsApps\\OpenAI.Codex_1.0.0.0_x64__test\\app\\ChatGPT.exe");
    const ProcessClassification unrelated_chatgpt = ClassifyAgentProcess(
        L"ChatGPT.exe", L"C:\\Program Files\\WindowsApps\\OpenAI.ChatGPT_1.0.0.0_x64__test\\app\\ChatGPT.exe");
    if (cursor_app.provider != Provider::Cursor || cursor_app.activity_capable ||
        antigravity_app.provider != Provider::GeminiCli || antigravity_app.activity_capable ||
        claude_desktop.provider != Provider::ClaudeCode || claude_desktop.activity_capable ||
        claude_cli.provider != Provider::ClaudeCode || !claude_cli.activity_capable ||
        claude_unknown.provider != Provider::ClaudeCode || claude_unknown.activity_capable ||
        codex_desktop.provider != Provider::Codex || codex_desktop.activity_capable ||
        chatgpt_codex.provider != Provider::Codex || !chatgpt_codex.is_provider_root ||
        chatgpt_codex.activity_capable || unrelated_chatgpt.provider != Provider::External) {
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
    if (!RunAgentDetectorSelfTests()) {
        return 53;
    }

    Settings managed_claude;
    managed_claude.claude_mode = DetectionMode::Tasks;
    managed_claude.claude_integration_expected = true;
    if (managed_claude.UseProcessActivityFallback(Provider::ClaudeCode) ||
        !managed_claude.UseProcessActivityFallback(Provider::Codex)) {
        return 54;
    }
    managed_claude.claude_integration_expected = false;
    if (!managed_claude.UseProcessActivityFallback(Provider::ClaudeCode)) {
        return 55;
    }
    managed_claude.claude_integration_expected = true;
    managed_claude.claude_mode = DetectionMode::Open;
    if (!managed_claude.UseProcessActivityFallback(Provider::ClaudeCode)) {
        return 56;
    }

    if (!RunOpenAIUiActivityContractTests()) {
        return 57;
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
