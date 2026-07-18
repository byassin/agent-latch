#pragma once

#include "types.h"

#include <windows.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace agent_latch {

struct DetectionResult {
    Provider provider{Provider::External};
    unsigned int running_instances{0};
    bool recently_active{false};
    ULONGLONG last_activity{0};
    std::wstring detail;
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
    bool initialized_{false};
    std::unordered_map<DWORD, ProcessMetric> previous_metrics_;
    std::unordered_map<int, ULONGLONG> last_activity_;
};

}  // namespace agent_latch
