#pragma once

#include <windows.h>

namespace agent_latch {

class PowerRequest {
public:
    PowerRequest();
    ~PowerRequest();

    PowerRequest(const PowerRequest&) = delete;
    PowerRequest& operator=(const PowerRequest&) = delete;

    bool Apply(bool system_required, bool display_required);
    bool IsSystemRequired() const;
    bool IsDisplayRequired() const;
    DWORD LastError() const;
    bool IsAvailable() const;

private:
    HANDLE handle_{INVALID_HANDLE_VALUE};
    bool system_required_{false};
    bool display_required_{false};
    DWORD last_error_{ERROR_SUCCESS};
};

}  // namespace agent_latch
