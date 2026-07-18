#include "hook_bridge.h"

#include "ipc.h"
#include "win_util.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cwctype>
#include <string>
#include <string_view>
#include <vector>

namespace agent_latch {
namespace {

constexpr ULONGLONG kHookLeaseTtlMilliseconds = 30ULL * 60ULL * 1000ULL;
constexpr DWORD kMaximumHookInputBytes = 1024 * 1024;

std::wstring Lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return value;
}

int HexValue(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return 10 + character - 'a';
    }
    if (character >= 'A' && character <= 'F') {
        return 10 + character - 'A';
    }
    return -1;
}

void AppendCodePoint(std::wstring* result, unsigned int code_point) {
#if WCHAR_MAX <= 0xFFFF
    if (code_point > 0xFFFF) {
        code_point -= 0x10000;
        result->push_back(static_cast<wchar_t>(0xD800 + (code_point >> 10)));
        result->push_back(static_cast<wchar_t>(0xDC00 + (code_point & 0x3FF)));
    } else {
        result->push_back(static_cast<wchar_t>(code_point));
    }
#else
    result->push_back(static_cast<wchar_t>(code_point));
#endif
}

std::wstring PathLeaf(const std::wstring& path) {
    if (path.empty()) {
        return {};
    }
    std::size_t end = path.size();
    while (end > 0 && (path[end - 1] == L'\\' || path[end - 1] == L'/')) {
        --end;
    }
    const std::size_t separator = path.find_last_of(L"\\/", end == 0 ? 0 : end - 1);
    const std::size_t start = separator == std::wstring::npos ? 0 : separator + 1;
    return path.substr(start, end - start);
}

std::vector<char> ReadStandardInput() {
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    if (input == nullptr || input == INVALID_HANDLE_VALUE) {
        return {};
    }
    std::vector<char> result;
    std::vector<char> buffer(8192);
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(input, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) || read == 0) {
            break;
        }
        if (result.size() + read > kMaximumHookInputBytes) {
            return {};
        }
        result.insert(result.end(), buffer.begin(), buffer.begin() + read);
    }
    return result;
}

bool IsReleaseEvent(const std::wstring& event_name) {
    return event_name == L"stop" || event_name == L"sessionend" || event_name == L"stopfailure" ||
           event_name == L"afteragentresponse";
}

bool IsSubagentStopEvent(const std::wstring& event_name) {
    return event_name == L"subagentstop" || event_name == L"teammateidle";
}

bool IsActivityEvent(const std::wstring& event_name) {
    return event_name == L"userpromptsubmit" || event_name == L"beforesubmitprompt" ||
           event_name == L"pretooluse" || event_name == L"posttooluse" ||
           event_name == L"posttoolbatch" || event_name == L"subagentstart" ||
           event_name == L"taskcreated" || event_name == L"preinvocation" ||
           event_name == L"postinvocation" || event_name == L"afteragentthought";
}

bool ExtractJsonBoolean(std::string_view json, std::string_view field, bool* value) {
    if (value == nullptr) {
        return false;
    }
    std::size_t cursor = json.find(field);
    while (cursor != std::string_view::npos &&
           (cursor == 0 || cursor + field.size() >= json.size() || json[cursor - 1] != '"' ||
            json[cursor + field.size()] != '"')) {
        cursor = json.find(field, cursor + 1);
    }
    if (cursor == std::string_view::npos) {
        return false;
    }
    cursor += field.size() + 1;
    while (cursor < json.size() && std::isspace(static_cast<unsigned char>(json[cursor]))) {
        ++cursor;
    }
    if (cursor >= json.size() || json[cursor++] != ':') {
        return false;
    }
    while (cursor < json.size() && std::isspace(static_cast<unsigned char>(json[cursor]))) {
        ++cursor;
    }
    if (json.substr(cursor, 4) == "true") {
        *value = true;
        return true;
    }
    if (json.substr(cursor, 5) == "false") {
        *value = false;
        return true;
    }
    return false;
}

void WriteHookResponse(Provider provider, const std::wstring& event_name) {
    const char* response = provider == Provider::GeminiCli && event_name == L"stop"
                               ? "{\"decision\":\"allow\"}\n"
                               : "{}\n";
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output == nullptr || output == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(output, response, static_cast<DWORD>(std::char_traits<char>::length(response)), &written, nullptr);
}

}  // namespace

bool ExtractJsonString(std::string_view json, std::string_view field, std::wstring* value) {
    if (value == nullptr) {
        return false;
    }
    std::size_t cursor = json.find(field);
    while (cursor != std::string_view::npos &&
           (cursor == 0 || cursor + field.size() >= json.size() || json[cursor - 1] != '"' ||
            json[cursor + field.size()] != '"')) {
        cursor = json.find(field, cursor + 1);
    }
    if (cursor == std::string_view::npos) {
        return false;
    }
    cursor += field.size() + 1;
    while (cursor < json.size() && std::isspace(static_cast<unsigned char>(json[cursor]))) {
        ++cursor;
    }
    if (cursor >= json.size() || json[cursor++] != ':') {
        return false;
    }
    while (cursor < json.size() && std::isspace(static_cast<unsigned char>(json[cursor]))) {
        ++cursor;
    }
    if (cursor >= json.size() || json[cursor++] != '"') {
        return false;
    }

    std::string utf8_chunk;
    std::wstring result;
    const auto flush_utf8 = [&]() {
        if (!utf8_chunk.empty()) {
            result += Utf8ToWide(utf8_chunk);
            utf8_chunk.clear();
        }
    };

    while (cursor < json.size()) {
        const char character = json[cursor++];
        if (character == '"') {
            flush_utf8();
            *value = std::move(result);
            return true;
        }
        if (character != '\\') {
            utf8_chunk.push_back(character);
            continue;
        }
        if (cursor >= json.size()) {
            return false;
        }
        flush_utf8();
        const char escape = json[cursor++];
        switch (escape) {
            case '"':
                result.push_back(L'"');
                break;
            case '\\':
                result.push_back(L'\\');
                break;
            case '/':
                result.push_back(L'/');
                break;
            case 'b':
                result.push_back(L'\b');
                break;
            case 'f':
                result.push_back(L'\f');
                break;
            case 'n':
                result.push_back(L'\n');
                break;
            case 'r':
                result.push_back(L'\r');
                break;
            case 't':
                result.push_back(L'\t');
                break;
            case 'u': {
                if (cursor + 4 > json.size()) {
                    return false;
                }
                unsigned int code_point = 0;
                for (unsigned int index = 0; index < 4; ++index) {
                    const int digit = HexValue(json[cursor + index]);
                    if (digit < 0) {
                        return false;
                    }
                    code_point = code_point * 16 + static_cast<unsigned int>(digit);
                }
                cursor += 4;
                AppendCodePoint(&result, code_point);
                break;
            }
            default:
                return false;
        }
    }
    return false;
}

HookTranslation TranslateHookEvent(
    Provider provider,
    std::string_view json,
    std::wstring_view event_override) {
    HookTranslation translation;
    translation.provider = provider;

    std::wstring event_name;
    std::wstring session_id;
    std::wstring agent_id;
    std::wstring agent_type;
    std::wstring cwd;
    event_name.assign(event_override);
    if (event_name.empty()) {
        ExtractJsonString(json, "hook_event_name", &event_name);
    }
    if (!ExtractJsonString(json, "session_id", &session_id)) {
        if (!ExtractJsonString(json, "conversation_id", &session_id)) {
            ExtractJsonString(json, "conversationId", &session_id);
        }
    }
    ExtractJsonString(json, "agent_id", &agent_id);
    if (agent_id.empty()) {
        if (!ExtractJsonString(json, "generation_id", &agent_id)) {
            if (!ExtractJsonString(json, "agentId", &agent_id)) {
                ExtractJsonString(json, "generationId", &agent_id);
            }
        }
    }
    ExtractJsonString(json, "agent_type", &agent_type);
    ExtractJsonString(json, "cwd", &cwd);
    if (cwd.empty()) {
        std::wstring workspace;
        if (ExtractJsonString(json, "workspace_root", &workspace)) {
            cwd = workspace;
        }
    }

    event_name = Lowercase(event_name);
    session_id = SanitizeMessageField(session_id, 120);
    agent_id = SanitizeMessageField(agent_id, 120);
    if (event_name.empty() || session_id.empty()) {
        return translation;
    }

    const bool subagent_event = event_name.find(L"subagent") != std::wstring::npos ||
                                event_name == L"teammateidle";
    const std::wstring base_id = std::wstring(ProviderKey(provider)) + L":" + session_id;
    translation.id = subagent_event && !agent_id.empty() ? base_id + L":" + agent_id : base_id;
    translation.label = ProviderName(provider);
    if (subagent_event) {
        translation.label += agent_type.empty() ? L" subagent" : L" · " + SanitizeMessageField(agent_type, 60);
    } else {
        translation.label += L" task";
    }
    const std::wstring project = PathLeaf(cwd);
    translation.detail = project.empty() ? L"Lifecycle hook" : project;

    bool fully_idle = true;
    const bool has_fully_idle = ExtractJsonBoolean(json, "fullyIdle", &fully_idle);
    if ((IsReleaseEvent(event_name) || IsSubagentStopEvent(event_name)) &&
        !(event_name == L"stop" && has_fully_idle && !fully_idle)) {
        translation.action = HookAction::Remove;
        return translation;
    }

    // SessionStart alone does not mean an agent is working. Every configured
    // turn/tool event acquires or renews a bounded lease.
    const bool background_stop = event_name == L"stop" && has_fully_idle && !fully_idle;
    if (event_name == L"sessionstart" || (!IsActivityEvent(event_name) && !background_stop)) {
        return translation;
    }

    translation.action = HookAction::Upsert;
    translation.ttl_milliseconds = kHookLeaseTtlMilliseconds;
    return translation;
}

int HandleHookInvocation(Provider provider, std::wstring_view event_override) {
    const std::vector<char> input = ReadStandardInput();
    std::wstring event_name(event_override);
    if (event_name.empty()) {
        ExtractJsonString(std::string_view(input.data(), input.size()), "hook_event_name", &event_name);
    }
    event_name = Lowercase(event_name);
    const HookTranslation translation =
        TranslateHookEvent(provider, std::string_view(input.data(), input.size()), event_override);
    if (!event_name.empty()) {
        if (!EnsureBackgroundInstance()) {
            WriteHookResponse(provider, event_name);
            return 0;
        }
        SendIpcMessage(std::wstring(L"SEEN\t") + ProviderKey(provider));
    }
    if (translation.action == HookAction::None) {
        WriteHookResponse(provider, event_name);
        return 0;
    }

    const std::wstring message = translation.action == HookAction::Remove
                                     ? BuildRemoveMessage(translation.id)
                                     : BuildUpsertMessage(
                                           translation.id,
                                           translation.provider,
                                           LatchKind::Hook,
                                           translation.label,
                                           translation.detail,
                                           translation.ttl_milliseconds);
    SendIpcMessage(message);
    WriteHookResponse(provider, event_name);
    return 0;
}

}  // namespace agent_latch
