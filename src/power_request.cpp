#include "power_request.h"

namespace agent_latch {

PowerRequest::PowerRequest() {
    REASON_CONTEXT reason{};
    reason.Version = POWER_REQUEST_CONTEXT_VERSION;
    reason.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
    reason.Reason.SimpleReasonString =
        const_cast<PWSTR>(L"AgentLatch is keeping Windows awake while active work is latched.");
    handle_ = PowerCreateRequest(&reason);
    if (handle_ == INVALID_HANDLE_VALUE) {
        creation_error_ = GetLastError();
        system_error_ = creation_error_;
        display_error_ = creation_error_;
    }
}

PowerRequest::~PowerRequest() {
    Apply(false, false);
    if (handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
}

bool PowerRequest::Apply(bool system_required, bool display_required) {
    if (handle_ == INVALID_HANDLE_VALUE) {
        return false;
    }

    bool success = true;
    if (system_required != system_required_) {
        const BOOL result = system_required ? PowerSetRequest(handle_, PowerRequestSystemRequired)
                                            : PowerClearRequest(handle_, PowerRequestSystemRequired);
        if (result) {
            system_required_ = system_required;
            system_error_ = ERROR_SUCCESS;
        } else {
            system_error_ = GetLastError();
            success = false;
        }
    }

    if (display_required != display_required_) {
        const BOOL result = display_required ? PowerSetRequest(handle_, PowerRequestDisplayRequired)
                                             : PowerClearRequest(handle_, PowerRequestDisplayRequired);
        if (result) {
            display_required_ = display_required;
            display_error_ = ERROR_SUCCESS;
        } else {
            display_error_ = GetLastError();
            success = false;
        }
    }
    return success;
}

bool PowerRequest::IsSystemRequired() const {
    return system_required_;
}

bool PowerRequest::IsDisplayRequired() const {
    return display_required_;
}

DWORD PowerRequest::LastError() const {
    if (creation_error_ != ERROR_SUCCESS) {
        return creation_error_;
    }
    return system_error_ != ERROR_SUCCESS ? system_error_ : display_error_;
}

DWORD PowerRequest::LastSystemError() const {
    return creation_error_ != ERROR_SUCCESS ? creation_error_ : system_error_;
}

DWORD PowerRequest::LastDisplayError() const {
    return creation_error_ != ERROR_SUCCESS ? creation_error_ : display_error_;
}

bool PowerRequest::IsAvailable() const {
    return handle_ != INVALID_HANDLE_VALUE;
}

}  // namespace agent_latch
