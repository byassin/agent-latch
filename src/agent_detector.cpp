#include "agent_detector.h"

#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <cwchar>
#include <sstream>
#include <unordered_set>

namespace agent_latch {
namespace {

constexpr ULONGLONG kMinimumCpuDelta = 100000;  // 10 ms in 100-nanosecond units.
constexpr ULONGLONG kMinimumIoDelta = 1024;

struct ProcessEntry {
    DWORD pid{0};
    DWORD parent_pid{0};
    std::wstring executable;
    Provider provider{Provider::External};
    bool is_provider_root{false};
};

ULONGLONG FileTimeValue(const FILETIME& value) {
    ULARGE_INTEGER integer{};
    integer.LowPart = value.dwLowDateTime;
    integer.HighPart = value.dwHighDateTime;
    return integer.QuadPart;
}

Provider RootProvider(const std::wstring& executable) {
    if (_wcsicmp(executable.c_str(), L"codex.exe") == 0) {
        return Provider::Codex;
    }
    if (_wcsicmp(executable.c_str(), L"claude.exe") == 0) {
        return Provider::ClaudeCode;
    }
    if (_wcsicmp(executable.c_str(), L"cursor-agent.exe") == 0 ||
        _wcsicmp(executable.c_str(), L"cursoragent.exe") == 0) {
        return Provider::Cursor;
    }
    if (_wcsicmp(executable.c_str(), L"opencode.exe") == 0) {
        return Provider::OpenCode;
    }
    if (_wcsicmp(executable.c_str(), L"gemini.exe") == 0 ||
        _wcsicmp(executable.c_str(), L"gemini-cli.exe") == 0) {
        return Provider::GeminiCli;
    }
    return Provider::External;
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
            process.provider = RootProvider(process.executable);
            process.is_provider_root = process.provider != Provider::External;
            processes.push_back(std::move(process));
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return processes;
}

int ProviderIndex(Provider provider) {
    return static_cast<int>(provider);
}

}  // namespace

std::vector<DetectionResult> AgentDetector::Scan(ULONGLONG now, DWORD grace_seconds) {
    std::vector<ProcessEntry> processes = SnapshotProcesses();
    std::unordered_map<DWORD, std::size_t> index_by_pid;
    index_by_pid.reserve(processes.size());
    for (std::size_t index = 0; index < processes.size(); ++index) {
        index_by_pid.emplace(processes[index].pid, index);
    }

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
                changed = true;
            }
        }
        if (!changed) {
            break;
        }
    }

    std::unordered_map<int, unsigned int> root_counts;
    std::unordered_map<DWORD, ProcessMetric> current_metrics;
    current_metrics.reserve(processes.size());

    for (const ProcessEntry& process : processes) {
        if (process.provider == Provider::External) {
            continue;
        }
        if (process.is_provider_root) {
            ++root_counts[ProviderIndex(process.provider)];
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
        const auto activity = last_activity_.find(ProviderIndex(provider));
        result.last_activity = activity == last_activity_.end() ? 0 : activity->second;
        result.recently_active = result.running_instances > 0 && result.last_activity != 0 &&
                                 now - result.last_activity <= grace_milliseconds;

        std::wostringstream detail;
        if (result.running_instances == 1) {
            detail << L"1 related process active";
        } else {
            detail << result.running_instances << L" related processes active";
        }
        result.detail = detail.str();
        results.push_back(std::move(result));
    }
    return results;
}

}  // namespace agent_latch
