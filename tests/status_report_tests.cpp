#include "status_report.h"
#include "win_util.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <filesystem>

namespace agent_latch {
bool RunStatusReportSelfTests(const std::wstring& output_directory) {
#define CHECK_REPORT(value) do { if (!(value)) { std::fprintf(stderr, "Status report failure at %d\n", __LINE__); return false; } } while (false)
    StatusReportState report;
    report.captured_utc_ms = 100000;
    report.dashboard.active = report.dashboard.manual_active = true;
    report.dashboard.now = 1000; report.dashboard.manual_deadline = 2001;
    report.request_available = report.system_request_set = true;
    Latch private_latch;
    private_latch.provider = Provider::Codex; private_latch.instance_count = 3; private_latch.in_grace_period = true;
    private_latch.id = L"SECRET_SESSION_ID"; private_latch.label = L"SECRET_PROMPT"; private_latch.detail = L"SECRET_PATH";
    report.dashboard.latches.push_back(private_latch);
    auto json = BuildStatusReport(report);
    CHECK_REPORT(json.find("SECRET") == std::string::npos);
    CHECK_REPORT(json.find("\"remaining_seconds\": 2") != std::string::npos);
    CHECK_REPORT(json.find("\"instances\": 3, \"grace_sources\": 1") != std::string::npos);
    CHECK_REPORT(json.find("\"idle_sleep_request_confirmed\": true") != std::string::npos);
    report.system_request_set = false;
    CHECK_REPORT(BuildStatusReport(report).find("\"idle_sleep_request_confirmed\": false") != std::string::npos);
    report.system_request_set = true; report.dashboard.system_request_error = ERROR_ACCESS_DENIED;
    CHECK_REPORT(BuildStatusReport(report).find("\"idle_sleep_request_confirmed\": false") != std::string::npos);
    report.dashboard.system_request_error = 0;
    report.dashboard.manual_deadline = 0;
    CHECK_REPORT(BuildStatusReport(report).find("\"until_windows_restarts\": true") != std::string::npos);
    for (const auto* invalid : {L"relative.json", L"C:\\test.txt", L"C:\\x:ads.json", L"\\\\.\\pipe\\x.json", L"C:\\bad\nfile.json", L"C:\\NUL.json"}) {
        CHECK_REPORT(!IsStatusReportPath(invalid));
    }
    wchar_t temporary[MAX_PATH]{}; const DWORD length = GetTempPathW(MAX_PATH, temporary);
    CHECK_REPORT(length > 0 && length < MAX_PATH);
    const auto path = JoinPath(temporary, L"AgentLatchReport-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L".json");
    CHECK_REPORT(WriteStatusReport(report, path));
    const auto read = [&]() { std::ifstream file(std::filesystem::path(path), std::ios::binary); return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()); };
    const auto original = read();
    CHECK_REPORT(original == BuildStatusReport(report));
    report.captured_utc_ms = 200000;
    const bool rejected = !WriteStatusReport(report, path) && GetLastError() == ERROR_FILE_EXISTS;
    CHECK_REPORT(read() == original);
    CHECK_REPORT(WriteStatusReport(report, path, true) && read() == BuildStatusReport(report));
    report.captured_utc_ms = 100000;
    const bool removed = DeleteFileW(path.c_str()) != FALSE;
    CHECK_REPORT(rejected && removed);
    if (!output_directory.empty()) { CHECK_REPORT(WriteStatusReport(report, JoinPath(output_directory, L"status-report.json"), true)); }
#undef CHECK_REPORT
    return true;
}
}
