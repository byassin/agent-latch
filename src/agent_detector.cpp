#include "agent_detector.h"

#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace agent_latch {
namespace {

constexpr ULONGLONG kMinimumCpuDelta = 250000;  // 25 ms in 100-nanosecond units.
constexpr ULONGLONG kMinimumIoDelta = 16 * 1024;
constexpr std::size_t kMaximumRecentCodexSessions = 64;
constexpr ULONGLONG kCodexDiscoveryIntervalMilliseconds = 5000;
constexpr ULONGLONG kFileTimeTicksPerSecond = 10'000'000;
constexpr ULONGLONG kCodexProcessStartTolerance = 5 * kFileTimeTicksPerSecond;
constexpr std::streamoff kCodexReadBlockBytes = 64 * 1024;
constexpr std::streamoff kCodexReadOverlapBytes = 256;
constexpr std::string_view kCodexTaskStarted = R"("type":"event_msg","payload":{"type":"task_started")";
constexpr std::string_view kCodexTaskComplete = R"("type":"event_msg","payload":{"type":"task_complete")";

struct ProcessEntry {
    DWORD pid{0};
    DWORD parent_pid{0};
    std::wstring executable;
    std::wstring executable_path;
    Provider provider{Provider::External};
    bool is_provider_root{false};
    bool activity_capable{false};
    ULONGLONG creation_time{0};
};

struct CodexSessionCandidate {
    std::filesystem::path path;
    ULONGLONG write_time{0};
};

ULONGLONG FileTimeValue(const FILETIME& value) {
    ULARGE_INTEGER integer{};
    integer.LowPart = value.dwLowDateTime;
    integer.HighPart = value.dwHighDateTime;
    return integer.QuadPart;
}

std::wstring Lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return value;
}

bool ContainsCaseInsensitive(const std::wstring& value, const std::wstring& needle) {
    return Lowercase(value).find(Lowercase(needle)) != std::wstring::npos;
}

std::wstring ReadProcessPath(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) {
        return {};
    }
    std::wstring path(32768, L'\0');
    DWORD length = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process, 0, path.data(), &length)) {
        CloseHandle(process);
        return {};
    }
    CloseHandle(process);
    path.resize(length);
    return path;
}

bool ReadMetric(DWORD pid, Provider provider, AgentDetector::ProcessMetric* metric) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) {
        return false;
    }

    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    IO_COUNTERS io{};
    const bool times_ok = GetProcessTimes(process, &creation, &exit, &kernel, &user) != FALSE;
    const bool io_ok = GetProcessIoCounters(process, &io) != FALSE;
    CloseHandle(process);
    if (!times_ok) {
        return false;
    }

    metric->creation_time = FileTimeValue(creation);
    metric->cpu_time = FileTimeValue(kernel) + FileTimeValue(user);
    metric->io_bytes = io_ok ? io.ReadTransferCount + io.WriteTransferCount + io.OtherTransferCount : 0;
    metric->provider = provider;
    return true;
}

ULONGLONG ReadProcessCreationTime(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) {
        return 0;
    }
    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    const bool result = GetProcessTimes(process, &creation, &exit, &kernel, &user) != FALSE;
    CloseHandle(process);
    return result ? FileTimeValue(creation) : 0;
}

std::vector<ProcessEntry> SnapshotProcesses() {
    std::vector<ProcessEntry> processes;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return processes;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            ProcessEntry process;
            process.pid = entry.th32ProcessID;
            process.parent_pid = entry.th32ParentProcessID;
            process.executable = entry.szExeFile;
            process.executable_path = ReadProcessPath(process.pid);
            const ProcessClassification classification =
                ClassifyAgentProcess(process.executable, process.executable_path);
            process.provider = classification.provider;
            process.is_provider_root = classification.is_provider_root;
            process.activity_capable = classification.activity_capable;
            if (process.provider != Provider::External) {
                process.creation_time = ReadProcessCreationTime(process.pid);
            }
            processes.push_back(std::move(process));
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return processes;
}

int ProviderIndex(Provider provider) {
    return static_cast<int>(provider);
}

std::wstring EnvironmentValue(const wchar_t* name) {
    const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
    if (length == 0) {
        return {};
    }
    std::wstring value(length, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), length);
    if (written == 0 || written >= length) {
        return {};
    }
    value.resize(written);
    return value;
}

std::filesystem::path CodexSessionsRoot() {
    std::wstring codex_home = EnvironmentValue(L"CODEX_HOME");
    if (codex_home.empty()) {
        const std::wstring profile = EnvironmentValue(L"USERPROFILE");
        if (profile.empty()) {
            return {};
        }
        codex_home = (std::filesystem::path(profile) / L".codex").wstring();
    }
    return std::filesystem::path(codex_home) / L"sessions";
}

bool ReadFileLastWriteTime(const std::filesystem::path& path, ULONGLONG* write_time) {
    if (write_time == nullptr) {
        return false;
    }
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes)) {
        return false;
    }
    *write_time = FileTimeValue(attributes.ftLastWriteTime);
    return true;
}

std::vector<CodexSessionCandidate> DiscoverRecentCodexSessions(const std::filesystem::path& root) {
    std::vector<CodexSessionCandidate> candidates;
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(
        root,
        std::filesystem::directory_options::skip_permission_denied,
        error);
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
        const std::filesystem::directory_entry& entry = *iterator;
        if (entry.is_regular_file(error) && !error && entry.path().extension() == L".jsonl") {
            ULONGLONG write_time = 0;
            if (ReadFileLastWriteTime(entry.path(), &write_time)) {
                candidates.push_back(CodexSessionCandidate{entry.path(), write_time});
            }
        }
        error.clear();
        iterator.increment(error);
    }
    std::sort(candidates.begin(), candidates.end(), [](const CodexSessionCandidate& left, const CodexSessionCandidate& right) {
        return left.write_time > right.write_time;
    });
    if (candidates.size() > kMaximumRecentCodexSessions) {
        candidates.resize(kMaximumRecentCodexSessions);
    }
    return candidates;
}

bool IsCodexSessionEligible(ULONGLONG write_time, ULONGLONG desktop_creation_time) {
    if (write_time == 0 || desktop_creation_time == 0) {
        return false;
    }
    return write_time >= desktop_creation_time ||
           desktop_creation_time - write_time <= kCodexProcessStartTolerance;
}

CodexSessionLifecycle ReadLatestCodexSessionLifecycle(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return CodexSessionLifecycle::Unknown;
    }
    stream.seekg(0, std::ios::end);
    const std::streamoff file_size = stream.tellg();
    if (file_size <= 0) {
        return CodexSessionLifecycle::Unknown;
    }

    std::streamoff block_end = file_size;
    while (block_end > 0) {
        const std::streamoff block_start = std::max<std::streamoff>(0, block_end - kCodexReadBlockBytes);
        const std::streamoff read_end = std::min(file_size, block_end + kCodexReadOverlapBytes);
        const std::streamoff read_length = read_end - block_start;
        std::string block(static_cast<std::size_t>(read_length), '\0');
        stream.clear();
        stream.seekg(block_start, std::ios::beg);
        stream.read(block.data(), read_length);
        block.resize(static_cast<std::size_t>(stream.gcount()));
        const CodexSessionLifecycle lifecycle = LatestCodexSessionLifecycle(block);
        if (lifecycle != CodexSessionLifecycle::Unknown) {
            return lifecycle;
        }
        block_end = block_start;
    }
    return CodexSessionLifecycle::Unknown;
}

}  // namespace

CodexSessionLifecycle LatestCodexSessionLifecycle(std::string_view json_lines) {
    const std::size_t started = json_lines.rfind(kCodexTaskStarted);
    const std::size_t completed = json_lines.rfind(kCodexTaskComplete);
    if (started == std::string_view::npos && completed == std::string_view::npos) {
        return CodexSessionLifecycle::Unknown;
    }
    if (completed == std::string_view::npos ||
        (started != std::string_view::npos && started > completed)) {
        return CodexSessionLifecycle::Active;
    }
    return CodexSessionLifecycle::Inactive;
}

ProcessClassification ClassifyAgentProcess(
    const std::wstring& executable,
    const std::wstring& executable_path) {
    ProcessClassification result;
    if (_wcsicmp(executable.c_str(), L"codex.exe") == 0) {
        result.provider = Provider::Codex;
        result.is_provider_root = true;
        result.activity_capable = !executable_path.empty() &&
                                  !ContainsCaseInsensitive(executable_path, L"\\WindowsApps\\OpenAI.Codex_");
        return result;
    }
    if (_wcsicmp(executable.c_str(), L"chatgpt.exe") == 0 &&
        ContainsCaseInsensitive(executable_path, L"\\WindowsApps\\OpenAI.Codex_")) {
        return ProcessClassification{Provider::Codex, true, false};
    }
    if (_wcsicmp(executable.c_str(), L"claude.exe") == 0) {
        result.provider = Provider::ClaudeCode;
        result.is_provider_root = true;
        result.activity_capable = !executable_path.empty() &&
                                  !ContainsCaseInsensitive(executable_path, L"\\WindowsApps\\Claude_");
        return result;
    }
    if (_wcsicmp(executable.c_str(), L"cursor.exe") == 0) {
        return ProcessClassification{Provider::Cursor, true, false};
    }
    if (_wcsicmp(executable.c_str(), L"cursor-agent.exe") == 0 ||
        _wcsicmp(executable.c_str(), L"cursoragent.exe") == 0) {
        return ProcessClassification{Provider::Cursor, true, true};
    }
    if (_wcsicmp(executable.c_str(), L"opencode.exe") == 0) {
        return ProcessClassification{Provider::OpenCode, true, true};
    }
    if (_wcsicmp(executable.c_str(), L"antigravity.exe") == 0) {
        return ProcessClassification{Provider::GeminiCli, true, false};
    }
    if (_wcsicmp(executable.c_str(), L"gemini.exe") == 0 ||
        _wcsicmp(executable.c_str(), L"gemini-cli.exe") == 0) {
        return ProcessClassification{Provider::GeminiCli, true, true};
    }
    return result;
}

unsigned int AgentDetector::ScanCodexDesktopSessions(ULONGLONG now, ULONGLONG desktop_creation_time) {
    if (desktop_creation_time == 0) {
        codex_sessions_.clear();
        codex_candidate_paths_.clear();
        next_codex_discovery_at_ = 0;
        return 0;
    }
    const std::filesystem::path root = CodexSessionsRoot();
    if (root.empty()) {
        return 0;
    }

    if (codex_candidate_paths_.empty() || now >= next_codex_discovery_at_) {
        codex_candidate_paths_.clear();
        for (const CodexSessionCandidate& candidate : DiscoverRecentCodexSessions(root)) {
            codex_candidate_paths_.push_back(candidate.path.wstring());
        }
        next_codex_discovery_at_ = now + kCodexDiscoveryIntervalMilliseconds;
    }

    std::unordered_set<std::wstring> scanned;
    unsigned int active_count = 0;
    const auto scan_path = [&](const std::filesystem::path& path) {
        const std::wstring key = path.wstring();
        if (!scanned.emplace(key).second) {
            return;
        }
        std::error_code error;
        const std::uintmax_t size = std::filesystem::file_size(path, error);
        if (error) {
            codex_sessions_.erase(key);
            return;
        }
        ULONGLONG write_time = 0;
        if (!ReadFileLastWriteTime(path, &write_time) ||
            !IsCodexSessionEligible(write_time, desktop_creation_time)) {
            codex_sessions_.erase(key);
            return;
        }
        auto metric = codex_sessions_.find(key);
        if (metric == codex_sessions_.end() || metric->second.size != size ||
            metric->second.write_time != write_time) {
            const CodexSessionLifecycle lifecycle = ReadLatestCodexSessionLifecycle(path);
            CodexSessionMetric updated;
            updated.size = size;
            updated.write_time = write_time;
            updated.active = lifecycle == CodexSessionLifecycle::Active;
            metric = codex_sessions_.insert_or_assign(key, updated).first;
        }
        if (metric->second.active) {
            ++active_count;
        }
    };

    for (const std::wstring& path : codex_candidate_paths_) {
        scan_path(path);
    }

    std::vector<std::filesystem::path> previously_active;
    for (const auto& [path, metric] : codex_sessions_) {
        if (metric.active && scanned.find(path) == scanned.end()) {
            previously_active.emplace_back(path);
        }
    }
    for (const std::filesystem::path& path : previously_active) {
        scan_path(path);
    }

    for (auto iterator = codex_sessions_.begin(); iterator != codex_sessions_.end();) {
        if (scanned.find(iterator->first) == scanned.end()) {
            iterator = codex_sessions_.erase(iterator);
        } else {
            ++iterator;
        }
    }
    return active_count;
}

std::vector<DetectionResult> AgentDetector::Scan(ULONGLONG now, DWORD grace_seconds) {
    std::vector<ProcessEntry> processes = SnapshotProcesses();
    std::unordered_map<DWORD, std::size_t> index_by_pid;
    index_by_pid.reserve(processes.size());
    for (std::size_t index = 0; index < processes.size(); ++index) {
        index_by_pid.emplace(processes[index].pid, index);
    }

    // Electron applications create several processes with the same executable.
    // Count only the top-most provider process as an open application instance.
    for (ProcessEntry& process : processes) {
        if (!process.is_provider_root || process.activity_capable) {
            continue;
        }
        const auto parent = index_by_pid.find(process.parent_pid);
        if (parent != index_by_pid.end() && processes[parent->second].provider == process.provider) {
            process.is_provider_root = false;
        }
    }

    ULONGLONG codex_desktop_creation_time = 0;
    for (const ProcessEntry& process : processes) {
        if (process.provider != Provider::Codex || !process.is_provider_root ||
            process.activity_capable || process.creation_time == 0) {
            continue;
        }
        codex_desktop_creation_time = codex_desktop_creation_time == 0
                                          ? process.creation_time
                                          : std::min(codex_desktop_creation_time, process.creation_time);
    }
    const unsigned int active_codex_tasks =
        ScanCodexDesktopSessions(now, codex_desktop_creation_time);

    // Propagate a provider from each known agent root to its child process tree.
    for (std::size_t pass = 0; pass < processes.size(); ++pass) {
        bool changed = false;
        for (ProcessEntry& process : processes) {
            if (process.provider != Provider::External) {
                continue;
            }
            const auto parent = index_by_pid.find(process.parent_pid);
            if (parent != index_by_pid.end() && processes[parent->second].provider != Provider::External) {
                process.provider = processes[parent->second].provider;
                process.activity_capable = processes[parent->second].activity_capable;
                changed = true;
            }
        }
        if (!changed) {
            break;
        }
    }

    std::unordered_map<int, unsigned int> root_counts;
    std::unordered_map<int, unsigned int> activity_root_counts;
    std::unordered_map<DWORD, ProcessMetric> current_metrics;
    current_metrics.reserve(processes.size());

    for (const ProcessEntry& process : processes) {
        if (process.provider == Provider::External) {
            continue;
        }
        if (process.is_provider_root) {
            ++root_counts[ProviderIndex(process.provider)];
            if (process.activity_capable) {
                ++activity_root_counts[ProviderIndex(process.provider)];
            }
        }
        if (!process.activity_capable) {
            continue;
        }

        ProcessMetric metric;
        if (!ReadMetric(process.pid, process.provider, &metric)) {
            continue;
        }
        current_metrics.emplace(process.pid, metric);

        bool active = false;
        const auto previous = previous_metrics_.find(process.pid);
        if (previous != previous_metrics_.end() &&
            previous->second.creation_time == metric.creation_time &&
            previous->second.provider == metric.provider) {
            const ULONGLONG cpu_delta = metric.cpu_time >= previous->second.cpu_time
                                            ? metric.cpu_time - previous->second.cpu_time
                                            : 0;
            const ULONGLONG io_delta = metric.io_bytes >= previous->second.io_bytes
                                           ? metric.io_bytes - previous->second.io_bytes
                                           : 0;
            active = cpu_delta >= kMinimumCpuDelta || io_delta >= kMinimumIoDelta;
        } else if (initialized_) {
            active = true;
        }

        if (active) {
            last_activity_[ProviderIndex(metric.provider)] = now;
        }
    }

    previous_metrics_ = std::move(current_metrics);
    initialized_ = true;

    static constexpr std::array<Provider, 5> providers = {
        Provider::Codex,
        Provider::ClaudeCode,
        Provider::Cursor,
        Provider::OpenCode,
        Provider::GeminiCli,
    };

    std::vector<DetectionResult> results;
    results.reserve(providers.size());
    const ULONGLONG grace_milliseconds = static_cast<ULONGLONG>(grace_seconds) * 1000;
    for (Provider provider : providers) {
        DetectionResult result;
        result.provider = provider;
        result.running_instances = root_counts[ProviderIndex(provider)];
        result.activity_capable_instances = activity_root_counts[ProviderIndex(provider)];
        result.active_task_instances = provider == Provider::Codex ? active_codex_tasks : 0;
        const auto activity = last_activity_.find(ProviderIndex(provider));
        result.last_activity = activity == last_activity_.end() ? 0 : activity->second;
        result.recently_active = result.running_instances > 0 && result.last_activity != 0 &&
                                 now - result.last_activity <= grace_milliseconds;
        if (result.active_task_instances > 0) {
            result.recently_active = true;
            result.last_activity = now;
        }

        std::wostringstream open_detail;
        if (result.running_instances == 1) {
            open_detail << L"1 app or CLI instance open";
        } else {
            open_detail << result.running_instances << L" app or CLI instances open";
        }
        result.open_detail = open_detail.str();

        std::wostringstream activity_detail;
        if (result.active_task_instances == 1) {
            activity_detail << L"1 Codex task is running";
        } else if (result.active_task_instances > 1) {
            activity_detail << result.active_task_instances << L" Codex tasks are running";
        } else if (result.activity_capable_instances > 0) {
            activity_detail << L"Agent process tree recently active";
        } else if (result.running_instances > 0) {
            activity_detail << L"App open; waiting for a task hook";
        } else {
            activity_detail << L"No running agent process";
        }
        result.activity_detail = activity_detail.str();
        results.push_back(std::move(result));
    }
    return results;
}

bool RunAgentDetectorSelfTests() {
    wchar_t temporary_directory[MAX_PATH]{};
    if (GetTempPathW(static_cast<DWORD>(std::size(temporary_directory)), temporary_directory) == 0) {
        return false;
    }
    const std::filesystem::path root =
        std::filesystem::path(temporary_directory) /
        (L"AgentLatchDetectorSelfTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()));
    std::error_code error;
    const std::filesystem::path old_session = root / L"2020" / L"01" / L"01" / L"resumed.jsonl";
    try {
        std::filesystem::create_directories(old_session.parent_path());
        {
            std::ofstream stream(old_session, std::ios::binary);
            stream << R"({"type":"event_msg","payload":{"type":"task_started"}})" << '\n';
        }
        const std::vector<CodexSessionCandidate> candidates = DiscoverRecentCodexSessions(root);
        if (candidates.size() != 1 || candidates.front().path != old_session ||
            ReadLatestCodexSessionLifecycle(old_session) != CodexSessionLifecycle::Active) {
            std::filesystem::remove_all(root, error);
            return false;
        }
        const ULONGLONG write_time = candidates.front().write_time;
        if (!IsCodexSessionEligible(write_time, write_time) ||
            !IsCodexSessionEligible(write_time, write_time + kCodexProcessStartTolerance) ||
            IsCodexSessionEligible(write_time, write_time + kCodexProcessStartTolerance + 1) ||
            IsCodexSessionEligible(write_time, 0)) {
            std::filesystem::remove_all(root, error);
            return false;
        }
    } catch (...) {
        std::filesystem::remove_all(root, error);
        return false;
    }
    std::filesystem::remove_all(root, error);
    return !error;
}

}  // namespace agent_latch
