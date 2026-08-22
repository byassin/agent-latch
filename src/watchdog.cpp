#include "watchdog.h"

#include "diagnostics.h"
#include "win_util.h"

#include <sstream>
#include <string>
#include <vector>

namespace agent_latch {
namespace {

constexpr unsigned int kMaximumRapidRestartAttempts = 3;
constexpr ULONGLONG kStableRuntimeMilliseconds = 60ULL * 1000ULL;
constexpr DWORD kRestartDelayMilliseconds = 2000;

bool LaunchProcess(const std::wstring& arguments) {
    const std::wstring executable = GetExecutablePath();
    if (executable.empty()) {
        return false;
    }
    std::wstring command_line = L"\"" + executable + L"\" " + arguments;
    std::vector<wchar_t> writable(command_line.begin(), command_line.end());
    writable.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const std::wstring directory = GetExecutableDirectory();
    const BOOL created = CreateProcessW(
        executable.c_str(),
        writable.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        directory.empty() ? nullptr : directory.c_str(),
        &startup,
        &process);
    if (!created) {
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

std::wstring RestartDetail(
    DWORD process_id,
    DWORD exit_code,
    ULONGLONG runtime_milliseconds,
    unsigned int attempt) {
    std::wostringstream detail;
    detail << L"pid=" << process_id << L" exit_code=" << exit_code
           << L" runtime_ms=" << runtime_milliseconds << L" attempt=" << attempt;
    return detail.str();
}

}  // namespace

RestartDecision EvaluateRestart(
    DWORD exit_code,
    ULONGLONG runtime_milliseconds,
    unsigned int prior_attempts) {
    if (exit_code == ERROR_SUCCESS) {
        return {};
    }
    const unsigned int next_attempt = runtime_milliseconds >= kStableRuntimeMilliseconds
                                          ? 1u
                                          : prior_attempts + 1u;
    return RestartDecision{next_attempt <= kMaximumRapidRestartAttempts, next_attempt};
}

bool LaunchWatchdogForCurrentProcess(unsigned int restart_attempt) {
    std::wostringstream arguments;
    arguments << L"--watchdog " << GetCurrentProcessId()
              << L" --restart-attempt " << restart_attempt;
    return LaunchProcess(arguments.str());
}

int RunWatchdog(DWORD process_id, unsigned int restart_attempt) {
    DiagnosticsLog diagnostics;
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (process == nullptr) {
        diagnostics.Write(L"watchdog_attach_failed", L"pid=" + std::to_wstring(process_id) +
                                                        L" error=" + std::to_wstring(GetLastError()));
        return 71;
    }
    const ULONGLONG started_at = GetTickCount64();
    const DWORD wait_result = WaitForSingleObject(process, INFINITE);
    DWORD exit_code = ERROR_SUCCESS;
    const BOOL read_exit_code = GetExitCodeProcess(process, &exit_code);
    CloseHandle(process);
    if (wait_result != WAIT_OBJECT_0 || !read_exit_code || exit_code == STILL_ACTIVE) {
        diagnostics.Write(L"watchdog_wait_failed", L"pid=" + std::to_wstring(process_id));
        return 72;
    }

    const ULONGLONG runtime = GetTickCount64() - started_at;
    const RestartDecision decision = EvaluateRestart(exit_code, runtime, restart_attempt);
    if (!decision.restart) {
        if (exit_code != ERROR_SUCCESS) {
            diagnostics.Write(
                L"restart_suppressed",
                RestartDetail(process_id, exit_code, runtime, decision.next_attempt));
        }
        return 0;
    }

    Sleep(kRestartDelayMilliseconds);
    const std::wstring detail = RestartDetail(process_id, exit_code, runtime, decision.next_attempt);
    if (!LaunchProcess(
            L"--background --restart-attempt " + std::to_wstring(decision.next_attempt))) {
        diagnostics.Write(L"restart_launch_failed", detail + L" error=" + std::to_wstring(GetLastError()));
        return 73;
    }
    diagnostics.Write(L"app_restarted", detail);
    return 0;
}

}  // namespace agent_latch
