#pragma once

#include "types.h"

#include <windows.h>

#include <cstdint>
#include <string_view>
#include <string>
#include <unordered_map>
#include <vector>

namespace agent_latch {

struct DetectionResult {
    Provider provider{Provider::External};
    unsigned int running_instances{0};
    unsigned int activity_capable_instances{0};
    unsigned int active_task_instances{0};
    bool recently_active{false};
    ULONGLONG last_activity{0};
    std::wstring open_detail;
    std::wstring activity_detail;
};

struct ProcessClassification {
    Provider provider{Provider::External};
    bool is_provider_root{false};
    bool activity_capable{false};
};

enum class CodexSessionLifecycle {
    Unknown,
    Active,
    Inactive,
};

class AgentDetector {
public:
    struct ProcessMetric {
        ULONGLONG creation_time{0};
        ULONGLONG cpu_time{0};
        ULONGLONG io_bytes{0};
        Provider provider{Provider::External};
    };

    std::vector<DetectionResult> Scan(ULONGLONG now, DWORD grace_seconds);

private:
    struct CodexSessionMetric {
        std::uintmax_t size{0};
        bool active{false};
    };

    unsigned int ScanCodexDesktopSessions();

    bool initialized_{false};
    std::unordered_map<DWORD, ProcessMetric> previous_metrics_;
    std::unordered_map<int, ULONGLONG> last_activity_;
    std::unordered_map<std::wstring, CodexSessionMetric> codex_sessions_;
};

ProcessClassification ClassifyAgentProcess(
    const std::wstring& executable,
    const std::wstring& executable_path);
CodexSessionLifecycle LatestCodexSessionLifecycle(std::string_view json_lines);

}  // namespace agent_latch
