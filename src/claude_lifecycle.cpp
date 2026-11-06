#include "claude_lifecycle.h"

#include "hook_bridge.h"
#include "win_util.h"

#include <tlhelp32.h>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <sstream>
#include <set>

namespace agent_latch {
namespace {

constexpr std::size_t kMaximumWorkers = 128;
constexpr std::wstring_view kLatchPrefix = L"claude-session:";

void Space(std::string_view json, std::size_t* at) {
    while (*at < json.size() && std::isspace(static_cast<unsigned char>(json[*at]))) { ++*at; }
}

bool SkipValue(std::string_view json, std::size_t* at, unsigned int depth = 0) {
    Space(json, at);
    if (*at >= json.size() || depth > 64) { return false; }
    const char first = json[(*at)++];
    if (first == '"') {
        while (*at < json.size()) {
            const char ch = json[(*at)++];
            if (ch == '"') { return true; }
            if (static_cast<unsigned char>(ch) < 32) { return false; }
            if (ch == '\\') {
                if (*at >= json.size()) { return false; }
                ++*at;
            }
        }
        return false;
    }
    if (first == '{' || first == '[') {
        const char end = first == '{' ? '}' : ']';
        Space(json, at);
        if (*at < json.size() && json[*at] == end) { ++*at; return true; }
        for (;;) {
            if (first == '{') {
                Space(json, at);
                if (*at >= json.size() || json[*at] != '"' || !SkipValue(json, at, depth + 1)) { return false; }
                Space(json, at);
                if (*at >= json.size() || json[(*at)++] != ':') { return false; }
            }
            if (!SkipValue(json, at, depth + 1)) { return false; }
            Space(json, at);
            if (*at >= json.size()) { return false; }
            const char next = json[(*at)++];
            if (next == end) { return true; }
            if (next != ',') { return false; }
        }
    }
    const std::size_t start = *at - 1;
    while (*at < json.size() && json[*at] != ',' && json[*at] != ']' && json[*at] != '}' &&
           !std::isspace(static_cast<unsigned char>(json[*at]))) { ++*at; }
    const auto token = json.substr(start, *at - start);
    return token == "true" || token == "false" || token == "null" ||
        (!token.empty() && (token.front() == '-' || std::isdigit(static_cast<unsigned char>(token.front()))));
}

// Only direct object members may supply identity/state. A task description or
// nested tool_input containing the same field names must never impersonate one.
std::string_view Field(std::string_view json, std::string_view name) {
    std::size_t at = 0;
    Space(json, &at);
    if (at >= json.size() || json[at++] != '{') { return {}; }
    for (;;) {
        Space(json, &at);
        if (at >= json.size() || json[at] == '}') { return {}; }
        const std::size_t key_start = at;
        if (json[at] != '"' || !SkipValue(json, &at)) { return {}; }
        const auto key = json.substr(key_start, at - key_start);
        Space(json, &at);
        if (at >= json.size() || json[at++] != ':') { return {}; }
        Space(json, &at);
        const std::size_t start = at;
        if (!SkipValue(json, &at)) { return {}; }
        if (key.size() == name.size() + 2 && key.substr(1, name.size()) == name) {
            return json.substr(start, at - start);
        }
        Space(json, &at);
        if (at >= json.size() || json[at++] != ',') { return {}; }
    }
}

std::wstring String(std::string_view json, std::string_view field) {
    const auto value = Field(json, field);
    if (value.empty() || value.front() != '"') { return {}; }
    std::wstring result;
    std::string wrapped = "{\"v\":";
    wrapped.append(value);
    wrapped.push_back('}');
    ExtractJsonString(wrapped, "v", &result);
    return result;
}

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

bool SafeId(const std::wstring& value, bool empty_ok = false) {
    return (empty_ok || !value.empty()) && value.size() <= 120 &&
        std::all_of(value.begin(), value.end(), [](wchar_t ch) {
            return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
                   (ch >= L'0' && ch <= L'9') || ch == L'-' || ch == L'_' || ch == L'.' || ch == L':';
        });
}

bool Number(const std::wstring& value, ULONGLONG* number) {
    if (value.empty() || !std::all_of(value.begin(), value.end(), [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; })) {
        return false;
    }
    ULONGLONG result = 0;
    for (wchar_t ch : value) {
        const auto digit = static_cast<ULONGLONG>(ch - L'0');
        if (result > (MAXULONGLONG - digit) / 10) { return false; }
        result = result * 10 + digit;
    }
    *number = result;
    return true;
}

ULONGLONG CreationTime(HANDLE process) {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) { return 0; }
    ULARGE_INTEGER result{};
    result.LowPart = created.dwLowDateTime; result.HighPart = created.dwHighDateTime;
    return result.QuadPart;
}

bool Activity(const std::wstring& name) {
    return name == L"userpromptsubmit" || name == L"pretooluse" || name == L"posttooluse" ||
           name == L"posttoolusefailure" || name == L"posttoolbatch";
}

}  // namespace

bool ParseClaudeEvent(std::string_view json, ClaudeEvent* event, std::wstring_view event_override) {
    if (event == nullptr || json.size() > 1024 * 1024) { return false; }
    std::size_t end = 0;
    if (!SkipValue(json, &end)) { return false; }
    Space(json, &end);
    if (end != json.size()) { return false; }
    ClaudeEvent parsed;
    parsed.name = Lower(event_override.empty() ? String(json, "hook_event_name") : std::wstring(event_override));
    parsed.session = String(json, "session_id");
    parsed.agent = String(json, "agent_id");
    parsed.task = String(json, "task_id");
    parsed.agent_type = SanitizeMessageField(String(json, "agent_type"), 60);
    if (!SafeId(parsed.name) || !SafeId(parsed.session) || !SafeId(parsed.agent, true) || !SafeId(parsed.task, true)) {
        return false;
    }
    if (parsed.name == L"notification") {
        if (String(json, "notification_type") != L"idle_prompt") { return false; }
        parsed.name = L"idle";
    }
    const auto background = Field(json, "background_tasks");
    if (!background.empty()) {
        if (background.front() != '[') { return false; }
        parsed.has_background_snapshot = true;
        std::size_t at = 1;
        Space(background, &at);
        while (at < background.size() && background[at] != ']') {
            const std::size_t start = at;
            if (background[at] != '{' || !SkipValue(background, &at)) { return false; }
            const auto item = background.substr(start, at - start);
            ClaudeWorker worker{String(item, "id"), SanitizeMessageField(String(item, "type"), 60)};
            if (!SafeId(worker.id)) { return false; }
            const auto status = Lower(String(item, "status"));
            if (status != L"completed" && status != L"failed" && status != L"cancelled" &&
                status != L"canceled" && status != L"stopped") {
                parsed.background.push_back(std::move(worker));
                if (parsed.background.size() > kMaximumWorkers) { return false; }
            }
            Space(background, &at);
            if (at < background.size() && background[at] == ',') { ++at; Space(background, &at); }
            else if (at >= background.size() || background[at] != ']') { return false; }
        }
    }
    *event = std::move(parsed);
    return true;
}

std::wstring BuildClaudeEventMessage(const ClaudeEvent& event) {
    std::wostringstream out;
    out << L"CLAUDE2\t" << event.name << L'\t' << event.session << L'\t' << event.agent << L'\t'
        << event.task << L'\t' << SanitizeMessageField(event.agent_type, 60) << L'\t' << event.owner.pid
        << L'\t' << event.owner.created << L'\t' << (event.has_background_snapshot ? 1 : 0);
    for (const auto& worker : event.background) {
        out << L'\t' << worker.id << L'\t' << SanitizeMessageField(worker.type, 60);
    }
    return out.str();
}

bool ParseClaudeEventMessage(const std::vector<std::wstring>& fields, ClaudeEvent* event) {
    if (event == nullptr || fields.size() < 9 || fields.size() > 9 + kMaximumWorkers * 2 ||
        fields[0] != L"CLAUDE2" || (fields.size() - 9) % 2 != 0) { return false; }
    ClaudeEvent parsed;
    parsed.name = fields[1]; parsed.session = fields[2]; parsed.agent = fields[3]; parsed.task = fields[4];
    parsed.agent_type = SanitizeMessageField(fields[5], 60);
    ULONGLONG pid = 0;
    if (!SafeId(parsed.name) || !SafeId(parsed.session) || !SafeId(parsed.agent, true) ||
        !SafeId(parsed.task, true) || !Number(fields[6], &pid) || pid > MAXDWORD ||
        !Number(fields[7], &parsed.owner.created) || (fields[8] != L"0" && fields[8] != L"1")) { return false; }
    parsed.owner.pid = static_cast<DWORD>(pid);
    parsed.has_background_snapshot = fields[8] == L"1";
    if (!parsed.has_background_snapshot && fields.size() != 9) { return false; }
    for (std::size_t at = 9; at < fields.size(); at += 2) {
        if (!SafeId(fields[at])) { return false; }
        parsed.background.push_back({fields[at], SanitizeMessageField(fields[at + 1], 60)});
    }
    *event = std::move(parsed);
    return true;
}

ClaudeOwner FindClaudeHookOwner() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) { return {}; }
    std::map<DWORD, PROCESSENTRY32W> processes;
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do { processes.emplace(entry.th32ProcessID, entry); } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    DWORD pid = GetCurrentProcessId();
    for (unsigned int depth = 0; depth < 32; ++depth) {
        const auto found = processes.find(pid);
        if (found == processes.end()) { break; }
        if (_wcsicmp(found->second.szExeFile, L"claude.exe") == 0) {
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (process == nullptr) { return {}; }
            std::wstring path(32768, L'\0'); DWORD length = static_cast<DWORD>(path.size());
            const bool native = QueryFullProcessImageNameW(process, 0, path.data(), &length) &&
                Lower(path.substr(0, length)).find(L"\\windowsapps\\") == std::wstring::npos;
            const ULONGLONG created = native ? CreationTime(process) : 0;
            CloseHandle(process);
            return created == 0 ? ClaudeOwner{} : ClaudeOwner{pid, created};
        }
        if (pid == found->second.th32ParentProcessID) { break; }
        pid = found->second.th32ParentProcessID;
    }
    return {};
}

OwnerStatus QueryClaudeOwner(ClaudeOwner owner) {
    if (owner.pid == 0 || owner.created == 0) { return OwnerStatus::Unknown; }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, owner.pid);
    if (process == nullptr) { return GetLastError() == ERROR_INVALID_PARAMETER ? OwnerStatus::Exited : OwnerStatus::Unknown; }
    const ULONGLONG created = CreationTime(process);
    const DWORD state = WaitForSingleObject(process, 0);
    CloseHandle(process);
    if ((created != 0 && created != owner.created) || state == WAIT_OBJECT_0) { return OwnerStatus::Exited; }
    return created != 0 && state == WAIT_TIMEOUT ? OwnerStatus::Alive : OwnerStatus::Unknown;
}

bool ClaudeLifecycle::Apply(const ClaudeEvent& event, ULONGLONG now) {
    if (!SafeId(event.session)) { return false; }
    if (event.name == L"sessionend") { sessions_.erase(event.session); return true; }
    const bool stop = event.name == L"stop" || event.name == L"stopfailure" || event.name == L"idle";
    const bool worker_stop = event.name == L"subagentstop" || event.name == L"teammateidle";
    if (!Activity(event.name) && !stop && !worker_stop && event.name != L"subagentstart" &&
        event.name != L"taskcompleted") { return false; }
    const bool passive = event.name == L"posttooluse" || event.name == L"posttoolusefailure" ||
                         event.name == L"posttoolbatch";
    if (passive) {
        const auto found = sessions_.find(event.session);
        if (found == sessions_.end() || (event.agent.empty() ? !found->second.foreground :
            !found->second.workers.contains(event.agent))) { return false; }
    }
    // A planning TaskCreate entry is not executing work. No independent latch.
    auto& session = sessions_[event.session];
    session.last_event = now;
    if (event.owner.pid != 0 && event.owner.created != 0) { session.owner = event.owner; }
    if (Activity(event.name)) {
        if (event.agent.empty()) { session.foreground = true; }
        else { session.workers[event.agent] = {event.agent, event.agent_type}; }
    } else if (event.name == L"subagentstart" && !event.agent.empty()) {
        session.workers[event.agent] = {event.agent, event.agent_type};
    } else if (stop) {
        if (event.agent.empty()) { session.foreground = false; }
        else { session.workers.erase(event.agent); }
    } else if (worker_stop) {
        session.workers.erase(event.agent);
    } else if (event.name == L"taskcompleted") {
        session.workers.erase(event.task);
    }
    // Stop/SubagentStop snapshots describe the parent's in-flight work. Replace
    // the old set rather than adding a second aggregate 30-minute parent lease.
    if (event.has_background_snapshot && (stop || worker_stop || event.name == L"taskcompleted")) {
        session.workers.clear();
        for (const auto& worker : event.background) {
            if ((worker_stop && worker.id == event.agent) ||
                (event.name == L"taskcompleted" && worker.id == event.task)) { continue; }
            session.workers[worker.id] = worker;
        }
    }
    if (!session.foreground && session.workers.empty()) { sessions_.erase(event.session); }
    return true;
}

void ClaudeLifecycle::Clear(LatchRegistry& registry) {
    sessions_.clear();
    for (const auto& latch : registry.Snapshot()) {
        if (latch.id.starts_with(kLatchPrefix)) { registry.Remove(latch.id); }
    }
}

ClaudeReleases ClaudeLifecycle::Refresh(LatchRegistry& registry, ULONGLONG now,
                                       const std::function<OwnerStatus(ClaudeOwner)>& query) {
    // Reconcile only this reducer's namespace; preserve creation order for
    // unchanged rows. Unknown ownership retains a fixed, bounded deadline.
    ClaudeReleases released;
    std::set<std::wstring> desired;
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        const auto& session = it->second;
        const auto status = query(session.owner);
        if (status == OwnerStatus::Exited || (status == OwnerStatus::Unknown &&
            now >= session.last_event && now - session.last_event >= kUnverifiedLease)) {
            if (status == OwnerStatus::Exited) { ++released.exited; }
            else { ++released.expired; }
            it = sessions_.erase(it);
            continue;
        }
        const ULONGLONG ttl = status == OwnerStatus::Alive ? 0 :
            kUnverifiedLease - (now >= session.last_event ? now - session.last_event : 0);
        const std::wstring base = std::wstring(kLatchPrefix) + it->first;
        if (session.foreground) {
            desired.insert(base + L":foreground");
            registry.Upsert(base + L":foreground", Provider::ClaudeCode, LatchKind::Hook,
                L"Claude Code task", L"Session lifecycle", now, ttl);
        }
        for (const auto& [id, worker] : session.workers) {
            desired.insert(base + L":worker:" + id);
            registry.Upsert(base + L":worker:" + id, Provider::ClaudeCode, LatchKind::Hook,
                worker.type.empty() ? L"Claude Code background work" : L"Claude Code · " + worker.type,
                L"Worker lifecycle", now, ttl);
        }
        ++it;
    }
    for (const auto& latch : registry.Snapshot()) {
        if (latch.id.starts_with(kLatchPrefix) && !desired.contains(latch.id)) { registry.Remove(latch.id); }
    }
    return released;
}

}  // namespace agent_latch
