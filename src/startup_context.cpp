#include "startup_context.h"
#include "win_util.h"

#include <appmodel.h>
#include <vector>

namespace agent_latch {
namespace {

constexpr DWORD kRelayTimeoutMilliseconds = 5000;
constexpr wchar_t kCheckArguments[] = L"--startup-check";

StartupIdentity IdentityFromResult(LONG result) {
    if (result == APPMODEL_ERROR_NO_PACKAGE) { return StartupIdentity::Unpackaged; }
    if (result == ERROR_INSUFFICIENT_BUFFER) { return StartupIdentity::Packaged; }
    SetLastError(result == ERROR_SUCCESS ? ERROR_GEN_FAILURE : static_cast<DWORD>(result));
    return StartupIdentity::Unknown;
}

StartupIdentity ProcessIdentity(HANDLE process) {
    UINT32 length = 0;
    return IdentityFromResult(GetPackageFullName(process, &length, nullptr));
}

bool CreatePolicySuspended(const std::wstring& arguments, bool hidden_window, PROCESS_INFORMATION* process) {
    const auto executable = GetExecutablePath();
    if (executable.empty()) { SetLastError(ERROR_FILE_NOT_FOUND); return false; }
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    if (bytes == 0) { return false; }
    std::vector<BYTE> storage(bytes);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &bytes)) { return false; }
    DWORD policy = PROCESS_CREATION_DESKTOP_APP_BREAKAWAY_ENABLE_PROCESS_TREE;
    bool result = false;
    DWORD error = ERROR_SUCCESS;
    if (UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_DESKTOP_APP_POLICY,
            &policy, sizeof(policy), nullptr, nullptr)) {
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        if (hidden_window) {
            startup.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
            startup.StartupInfo.wShowWindow = SW_HIDE;
        }
        startup.lpAttributeList = attributes;
        std::wstring command = L"\"" + executable + L"\" " + arguments;
        std::vector<wchar_t> writable(command.begin(), command.end());
        writable.push_back(L'\0');
        const auto directory = GetExecutableDirectory();
        result = CreateProcessW(executable.c_str(), writable.data(), nullptr, nullptr, FALSE,
            CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr,
            directory.empty() ? nullptr : directory.c_str(), &startup.StartupInfo, process) != FALSE;
        if (!result) { error = GetLastError(); }
    } else { error = GetLastError(); }
    DeleteProcThreadAttributeList(attributes);
    if (!result) { SetLastError(error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error); }
    return result;
}

struct OwnedChild {
    const StartupContextApi& api;
    PROCESS_INFORMATION process{};
    ~OwnedChild() {
        if (process.hThread != nullptr) { api.close_handle(process.hThread); }
        if (process.hProcess != nullptr) { api.close_handle(process.hProcess); }
    }
};

DWORD FailureError(const StartupContextApi& api) {
    const DWORD error = api.last_error();
    return error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error;
}

bool ChildArgumentsValid(const std::wstring& arguments) {
    // A relay must never relay itself recursively, even if invoked directly.
    return arguments.size() <= 4096 && arguments.find_first_of(L"\r\n\0", 0, 3) == std::wstring::npos &&
        arguments.find(L"--startup-relay") == std::wstring::npos;
}

bool HiddenStartupRole(const std::wstring& arguments) {
    // Only our fixed startup role switches influence launch visibility. In
    // particular a foreground restart-attempt argument is not a hidden role.
    std::size_t start = 0;
    while ((start = arguments.find_first_not_of(L" \t", start)) != std::wstring::npos) {
        const auto end = arguments.find_first_of(L" \t", start);
        const auto token = arguments.substr(start, end == std::wstring::npos ? end : end - start);
        if (token == L"--background" || token == L"--watchdog" || token == L"--startup-relay" ||
            token == kCheckArguments) { return true; }
        if (end == std::wstring::npos) { break; }
        start = end;
    }
    return false;
}

bool WaitForSuccessfulExit(OwnedChild& child, DWORD* error) {
    const DWORD wait = child.api.wait(child.process.hProcess, kRelayTimeoutMilliseconds);
    if (wait != WAIT_OBJECT_0) {
        *error = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : FailureError(child.api);
        child.api.terminate(child.process.hProcess, *error);
        return false;
    }
    DWORD exit_code = ERROR_GEN_FAILURE;
    if (!child.api.exit_code(child.process.hProcess, &exit_code)) {
        *error = FailureError(child.api);
        return false;
    }
    if (exit_code != ERROR_SUCCESS) { *error = exit_code; return false; }
    return true;
}

bool LaunchDirect(const std::wstring& arguments, const StartupContextApi& api, bool wait_for_exit) {
    DWORD error = ERROR_SUCCESS;
    bool result = false;
    {
        OwnedChild child{api, {}};
        if (!api.create_suspended(arguments, HiddenStartupRole(arguments), &child.process)) { error = FailureError(api); }
        else {
            const auto identity = api.process_identity(child.process.hProcess);
            if (identity != StartupIdentity::Unpackaged) {
                error = identity == StartupIdentity::Unknown ? FailureError(api) : ERROR_BAD_ENVIRONMENT;
                // Only this newly created, still-suspended child is terminated.
                api.terminate(child.process.hProcess, error);
            } else if (api.resume_thread(child.process.hThread) == static_cast<DWORD>(-1)) {
                error = FailureError(api);
                api.terminate(child.process.hProcess, error);
            } else { result = !wait_for_exit || WaitForSuccessfulExit(child, &error); }
        }
    }
    if (!result) { SetLastError(error); }
    return result;
}

}  // namespace

StartupIdentity CurrentStartupIdentity() {
    UINT32 length = 0;
    return IdentityFromResult(GetCurrentPackageFullName(&length, nullptr));
}

StartupContextApi NativeStartupContextApi() {
    return {CurrentStartupIdentity, ProcessIdentity, CreatePolicySuspended, ResumeThread,
        WaitForSingleObject, GetExitCodeProcess, TerminateProcess, CloseHandle, GetLastError};
}

bool LaunchCanonicalProcess(const std::wstring& arguments) {
    return LaunchCanonicalProcess(arguments, NativeStartupContextApi());
}

bool LaunchCanonicalProcess(const std::wstring& arguments, const StartupContextApi& api) {
    if (!ChildArgumentsValid(arguments)) { SetLastError(ERROR_INVALID_PARAMETER); return false; }
    const auto identity = api.current_identity();
    if (identity == StartupIdentity::Unknown) { SetLastError(FailureError(api)); return false; }
    if (identity == StartupIdentity::Unpackaged) { return LaunchDirect(arguments, api, false); }

    // ENABLE_PROCESS_TREE governs a new process's descendants, not a promise
    // about that process itself. Therefore use one relay generation, then
    // verify the actual child while suspended before its first instruction.
    DWORD error = ERROR_SUCCESS;
    bool result = false;
    {
        OwnedChild relay{api, {}};
        if (!api.create_suspended(L"--startup-relay " + arguments, true, &relay.process)) { error = FailureError(api); }
        else if (api.resume_thread(relay.process.hThread) == static_cast<DWORD>(-1)) {
            error = FailureError(api);
            api.terminate(relay.process.hProcess, error);
        } else { result = WaitForSuccessfulExit(relay, &error); }
    }
    if (!result) { SetLastError(error); }
    return result;
}

int RunCanonicalStartupRelay(const std::wstring& arguments) {
    return RunCanonicalStartupRelay(arguments, NativeStartupContextApi());
}

int RunCanonicalStartupRelay(const std::wstring& arguments, const StartupContextApi& api) {
    if (!ChildArgumentsValid(arguments)) { SetLastError(ERROR_INVALID_PARAMETER); return 12; }
    // No recursive current-identity check: a relay may still be packaged. Its
    // actual child must not be. Check-only mode also proves native exit status.
    if (LaunchDirect(arguments, api, arguments == kCheckArguments)) { return 0; }
    const DWORD error = GetLastError();
    return error != ERROR_SUCCESS && error <= 0x7fffffffUL ? static_cast<int>(error) : 12;
}

bool RunStartupContextNativeSmokeTest() {
    const auto api = NativeStartupContextApi();
    const auto identity = api.current_identity();
    if (identity == StartupIdentity::Unknown) { return false; }
    if (identity == StartupIdentity::Packaged) { return LaunchCanonicalProcess(kCheckArguments, api); }
    if (!LaunchDirect(kCheckArguments, api, true)) { return false; }
    // Exercise the real relay command dispatch even when the test runner has
    // no package identity. A parser/dispatch regression must not hide behind
    // fake API coverage or require a packaged machine to become detectable.
    return LaunchDirect(L"--startup-relay --startup-check", api, true);
}

}  // namespace agent_latch
