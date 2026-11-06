#pragma once

#include <windows.h>

namespace agent_latch {

// Injectable only for deterministic self-tests. Production uses the Windows
// functions below; no global API replacement or live power state is involved.
struct PowerRequestApi {
    decltype(&::PowerCreateRequest) create_request{::PowerCreateRequest};
    decltype(&::PowerSetRequest) set_request{::PowerSetRequest};
    decltype(&::PowerClearRequest) clear_request{::PowerClearRequest};
    decltype(&::CloseHandle) close_handle{::CloseHandle};
    decltype(&::GetLastError) last_error{::GetLastError};
    decltype(&::GetTickCount64) tick_count{::GetTickCount64};
};

class PowerRequest {
public:
    static constexpr ULONGLONG kCreationRetryMilliseconds = 2000;
    PowerRequest();
    explicit PowerRequest(const PowerRequestApi& api);
    ~PowerRequest();

    PowerRequest(const PowerRequest&) = delete;
    PowerRequest& operator=(const PowerRequest&) = delete;

    bool Apply(bool system_required, bool display_required);
    bool IsSystemRequired() const;
    bool IsDisplayRequired() const;
    DWORD LastError() const;
    DWORD LastSystemError() const;
    DWORD LastDisplayError() const;
    bool IsAvailable() const;

private:
    bool CreateHandle();
    PowerRequestApi api_;
    HANDLE handle_{INVALID_HANDLE_VALUE};
    ULONGLONG last_creation_attempt_{0};
    bool system_required_{false};
    bool display_required_{false};
    DWORD creation_error_{ERROR_SUCCESS};
    DWORD system_error_{ERROR_SUCCESS};
    DWORD display_error_{ERROR_SUCCESS};
};

bool RunPowerRequestSelfTests();

}  // namespace agent_latch
