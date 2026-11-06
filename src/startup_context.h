#pragma once

#include <windows.h>
#include <string>

namespace agent_latch {

enum class StartupIdentity { Unpackaged, Packaged, Unknown };

// This narrow seam lets self-tests model process inheritance/failure without
// starting a monitor, changing a registry value, or sending production IPC.
struct StartupContextApi {
    StartupIdentity (*current_identity)();
    StartupIdentity (*process_identity)(HANDLE);
    bool (*create_suspended)(const std::wstring&, bool hidden_window, PROCESS_INFORMATION*);
    decltype(&::ResumeThread) resume_thread;
    decltype(&::WaitForSingleObject) wait;
    decltype(&::GetExitCodeProcess) exit_code;
    decltype(&::TerminateProcess) terminate;
    decltype(&::CloseHandle) close_handle;
    decltype(&::GetLastError) last_error;
};

StartupIdentity CurrentStartupIdentity();
StartupContextApi NativeStartupContextApi();

// Launch only an unpackaged actual child. A packaged caller uses one bounded
// relay generation; the relay does not initialize settings, UI or monitoring.
bool LaunchCanonicalProcess(const std::wstring& arguments);
bool LaunchCanonicalProcess(const std::wstring& arguments, const StartupContextApi& api);
int RunCanonicalStartupRelay(const std::wstring& arguments);
int RunCanonicalStartupRelay(const std::wstring& arguments, const StartupContextApi& api);

bool RunStartupContextSelfTests();

}  // namespace agent_latch
