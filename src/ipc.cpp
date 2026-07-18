#include "ipc.h"

#include "win_util.h"

#include <algorithm>
#include <sstream>
#include <vector>

namespace agent_latch {

std::wstring BuildUpsertMessage(
    const std::wstring& id,
    Provider provider,
    LatchKind kind,
    const std::wstring& label,
    const std::wstring& detail,
    ULONGLONG ttl_milliseconds,
    unsigned int instance_count) {
    std::wostringstream stream;
    stream << L"UPSERT\t" << SanitizeMessageField(id, 200) << L'\t' << ProviderKey(provider) << L'\t'
           << static_cast<int>(kind) << L'\t' << SanitizeMessageField(label) << L'\t'
           << SanitizeMessageField(detail, 240) << L'\t' << ttl_milliseconds << L'\t'
           << std::max(1u, instance_count);
    return stream.str();
}

std::wstring BuildRemoveMessage(const std::wstring& id) {
    return L"REMOVE\t" + SanitizeMessageField(id, 200);
}

bool SendIpcMessage(const std::wstring& message, DWORD timeout_milliseconds) {
    HWND window = FindWindowW(kWindowClassName, nullptr);
    if (window == nullptr || message.empty() || message.size() > 4096) {
        return false;
    }

    COPYDATASTRUCT data{};
    data.dwData = kCopyDataSignature;
    data.cbData = static_cast<DWORD>((message.size() + 1) * sizeof(wchar_t));
    data.lpData = const_cast<wchar_t*>(message.c_str());
    DWORD_PTR response = 0;
    return SendMessageTimeoutW(
               window,
               WM_COPYDATA,
               0,
               reinterpret_cast<LPARAM>(&data),
               SMTO_ABORTIFHUNG | SMTO_BLOCK,
               timeout_milliseconds,
               &response) != 0 &&
           response != 0;
}

bool EnsureBackgroundInstance() {
    if (FindWindowW(kWindowClassName, nullptr) != nullptr) {
        return true;
    }

    const std::wstring executable = GetExecutablePath();
    if (executable.empty()) {
        return false;
    }

    std::wstring command_line = L"\"" + executable + L"\" --background";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> writable(command_line.begin(), command_line.end());
    writable.push_back(L'\0');
    if (!CreateProcessW(
            executable.c_str(),
            writable.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup,
            &process)) {
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    for (unsigned int attempt = 0; attempt < 40; ++attempt) {
        if (FindWindowW(kWindowClassName, nullptr) != nullptr) {
            return true;
        }
        Sleep(50);
    }
    return false;
}

}  // namespace agent_latch
