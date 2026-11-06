#include "startup_context.h"

#include <cstdio>
#include <vector>

namespace agent_latch {
bool RunStartupContextNativeSmokeTest();
namespace {

struct FakeStartup {
    StartupIdentity creator{StartupIdentity::Unpackaged};
    StartupIdentity child{StartupIdentity::Unpackaged};
    bool create_success{true}, resume_success{true}, exit_query_success{true};
    DWORD wait_result{WAIT_OBJECT_0}, exit{0}, error{ERROR_ACCESS_DENIED};
    unsigned int creates{0}, resumes{0}, waits{0}, identity_queries{0}, terminations{0}, closes{0};
    std::vector<std::wstring> arguments;
    std::vector<bool> hidden_windows;
};
thread_local FakeStartup* current = nullptr;
StartupIdentity Creator() { return current->creator; }
StartupIdentity Child(HANDLE) { ++current->identity_queries; return current->child; }
bool Create(const std::wstring& arguments, bool hidden_window, PROCESS_INFORMATION* process) {
    ++current->creates; current->arguments.push_back(arguments);
    current->hidden_windows.push_back(hidden_window);
    if (!current->create_success) { return false; }
    process->hProcess = reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(101));
    process->hThread = reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(102));
    return true;
}
DWORD WINAPI Resume(HANDLE) { ++current->resumes; return current->resume_success ? 1 : static_cast<DWORD>(-1); }
DWORD WINAPI Wait(HANDLE, DWORD milliseconds) {
    ++current->waits;
    return milliseconds == 5000 ? current->wait_result : WAIT_FAILED;
}
BOOL WINAPI Exit(HANDLE, DWORD* code) { *code = current->exit; return current->exit_query_success; }
BOOL WINAPI Terminate(HANDLE, UINT) { ++current->terminations; return TRUE; }
BOOL WINAPI Close(HANDLE) { ++current->closes; return TRUE; }
DWORD WINAPI Error() { return current->error; }
StartupContextApi Api() { return {Creator, Child, Create, Resume, Wait, Exit, Terminate, Close, Error}; }

bool FakeTests() {
#define CHECK_STARTUP(condition) do { if (!(condition)) { std::fprintf(stderr, "Startup context test failed at %d\n", __LINE__); return false; } } while (false)
    FakeStartup fake; current = &fake;
    struct Reset { ~Reset() { current = nullptr; } } reset;
    CHECK_STARTUP(LaunchCanonicalProcess(L"--background", Api()));
    CHECK_STARTUP(fake.creates == 1 && fake.resumes == 1 && fake.identity_queries == 1 &&
        fake.waits == 0 && fake.terminations == 0 && fake.closes == 2 && fake.arguments[0] == L"--background" &&
        fake.hidden_windows[0]);
    for (const auto* foreground : {L"", L"--restart-attempt 2", L"--background-not-a-role"}) {
        fake = {};
        CHECK_STARTUP(LaunchCanonicalProcess(foreground, Api()));
        CHECK_STARTUP(!fake.hidden_windows[0]);
    }
    for (const auto* background : {L"--background", L"--watchdog 123 --restart-attempt 1", L"--startup-check",
            L"--restart-attempt 2 --background"}) {
        fake = {};
        CHECK_STARTUP(LaunchCanonicalProcess(background, Api()));
        CHECK_STARTUP(fake.hidden_windows[0]);
    }
    fake = {}; fake.creator = StartupIdentity::Packaged;
    CHECK_STARTUP(LaunchCanonicalProcess(L"--background --restart-attempt 2", Api()));
    CHECK_STARTUP(fake.arguments.size() == 1 && fake.arguments[0] == L"--startup-relay --background --restart-attempt 2" &&
        fake.identity_queries == 0 && fake.resumes == 1 && fake.waits == 1 && fake.closes == 2 && fake.hidden_windows[0]);
    fake = {}; fake.creator = StartupIdentity::Packaged;
    CHECK_STARTUP(LaunchCanonicalProcess(L"", Api()) && fake.hidden_windows[0]);
    CHECK_STARTUP(fake.arguments[0] == L"--startup-relay ");
    fake = {}; fake.creator = StartupIdentity::Packaged;
    CHECK_STARTUP(RunCanonicalStartupRelay(L"", Api()) == 0 && !fake.hidden_windows[0]);
    // Relay executes exactly one direct launch even when still packaged itself.
    fake = {}; fake.creator = StartupIdentity::Packaged;
    CHECK_STARTUP(RunCanonicalStartupRelay(L"--background", Api()) == 0);
    CHECK_STARTUP(fake.creates == 1 && fake.arguments[0] == L"--background" && fake.identity_queries == 1 && fake.waits == 0);
    for (auto identity : {StartupIdentity::Packaged, StartupIdentity::Unknown}) {
        fake = {}; fake.child = identity;
        CHECK_STARTUP(!LaunchCanonicalProcess(L"--background", Api()));
        CHECK_STARTUP(fake.creates == 1 && fake.resumes == 0 && fake.terminations == 1 && fake.closes == 2);
        fake = {}; fake.creator = StartupIdentity::Packaged; fake.child = identity;
        CHECK_STARTUP(RunCanonicalStartupRelay(L"--background", Api()) != 0);
        CHECK_STARTUP(fake.creates == 1 && fake.resumes == 0 && fake.terminations == 1 && fake.closes == 2);
    }
    fake = {}; fake.creator = StartupIdentity::Unknown;
    CHECK_STARTUP(!LaunchCanonicalProcess(L"--background", Api()) && fake.creates == 0);
    for (auto identity : {StartupIdentity::Unpackaged, StartupIdentity::Packaged}) {
        fake = {}; fake.creator = identity; fake.create_success = false;
        CHECK_STARTUP(!LaunchCanonicalProcess(L"--background", Api()));
        CHECK_STARTUP(fake.resumes == 0 && fake.terminations == 0 && fake.closes == 0);
        fake = {}; fake.creator = identity; fake.resume_success = false;
        CHECK_STARTUP(!LaunchCanonicalProcess(L"--background", Api()));
        CHECK_STARTUP(fake.resumes == 1 && fake.terminations == 1 && fake.closes == 2);
    }
    for (DWORD wait : {static_cast<DWORD>(WAIT_TIMEOUT), static_cast<DWORD>(WAIT_FAILED)}) {
        fake = {}; fake.creator = StartupIdentity::Packaged; fake.wait_result = wait;
        CHECK_STARTUP(!LaunchCanonicalProcess(L"--background", Api()));
        CHECK_STARTUP(fake.waits == 1 && fake.terminations == 1 && fake.closes == 2);
    }
    fake = {}; fake.creator = StartupIdentity::Packaged; fake.exit = 12;
    CHECK_STARTUP(!LaunchCanonicalProcess(L"--background", Api()) && GetLastError() == 12);
    CHECK_STARTUP(fake.terminations == 0 && fake.closes == 2);
    fake = {}; fake.creator = StartupIdentity::Packaged; fake.exit_query_success = false;
    CHECK_STARTUP(!LaunchCanonicalProcess(L"--background", Api()) && fake.closes == 2);
    fake = {};
    CHECK_STARTUP(RunCanonicalStartupRelay(L"--startup-check", Api()) == 0 && fake.waits == 1);
    fake = {}; fake.exit = 12;
    CHECK_STARTUP(RunCanonicalStartupRelay(L"--startup-check", Api()) != 0 && fake.closes == 2);
    for (const auto* arguments : {L"--startup-relay --background", L"--background\r\n--startup-relay"}) {
        fake = {};
        CHECK_STARTUP(!LaunchCanonicalProcess(arguments, Api()) && fake.creates == 0);
        CHECK_STARTUP(RunCanonicalStartupRelay(arguments, Api()) != 0 && fake.creates == 0);
    }
#undef CHECK_STARTUP
    return true;
}

}  // namespace

bool RunStartupContextSelfTests() {
    return FakeTests() && RunStartupContextNativeSmokeTest();
}

}  // namespace agent_latch
