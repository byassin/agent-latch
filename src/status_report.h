#pragma once

#include "ui.h"
#include <string>

namespace agent_latch {

struct StatusReportState {
    DashboardState dashboard;
    bool request_available{false};
    bool system_request_set{false};
    bool display_request_set{false};
    ULONGLONG captured_utc_ms{0};
};

// Deliberately excludes user labels, IDs, paths, prompts, logs and transcripts.
std::string BuildStatusReport(const StatusReportState& state);
bool WriteStatusReport(const StatusReportState& state, const std::wstring& path, bool overwrite = false);
bool IsStatusReportPath(const std::wstring& path);
bool RunStatusReportSelfTests(const std::wstring& output_directory = L"");

} // namespace agent_latch
