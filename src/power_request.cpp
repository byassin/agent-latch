#include "power_request.h"

namespace agent_latch {

PowerRequest::PowerRequest() : PowerRequest(PowerRequestApi{}) {}

PowerRequest::PowerRequest(const PowerRequestApi& api) : api_(api) {
    CreateHandle();
}

bool PowerRequest::CreateHandle() {
    last_creation_attempt_ = api_.tick_count();
    REASON_CONTEXT reason{};
    reason.Version = POWER_REQUEST_CONTEXT_VERSION;
    reason.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
    reason.Reason.SimpleReasonString =
        const_cast<PWSTR>(L"AgentLatch is keeping Windows awake while active work is latched.");
    handle_ = api_.create_request(&reason);
    if (handle_ == INVALID_HANDLE_VALUE || handle_ == nullptr) {
        handle_ = INVALID_HANDLE_VALUE;
        creation_error_ = api_.last_error();
        if (creation_error_ == ERROR_SUCCESS) { creation_error_ = ERROR_GEN_FAILURE; }
        system_error_ = creation_error_;
        display_error_ = creation_error_;
        return false;
    }
    creation_error_ = system_error_ = display_error_ = ERROR_SUCCESS;
    return true;
}

PowerRequest::~PowerRequest() {
    if (handle_ != INVALID_HANDLE_VALUE) {
        Apply(false, false);
        api_.close_handle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
}

bool PowerRequest::Apply(bool system_required, bool display_required) {
    if (handle_ == INVALID_HANDLE_VALUE) {
        // Hook traffic can reconcile much faster than the normal two-second
        // timer. Bound retries even when every event asks for protection.
        if (api_.tick_count() - last_creation_attempt_ < kCreationRetryMilliseconds || !CreateHandle()) {
            return false;
        }
    }

    bool success = true;
    if (system_required != system_required_) {
        const BOOL result = system_required ? api_.set_request(handle_, PowerRequestSystemRequired)
                                            : api_.clear_request(handle_, PowerRequestSystemRequired);
        if (result) {
            system_required_ = system_required;
            system_error_ = ERROR_SUCCESS;
        } else {
            system_error_ = api_.last_error();
            if (system_error_ == ERROR_SUCCESS) { system_error_ = ERROR_GEN_FAILURE; }
            success = false;
        }
    } else {
        // A failed transition may no longer be wanted. The held request already
        // matches the new desired state, so its old transition error is stale.
        system_error_ = ERROR_SUCCESS;
    }

    if (display_required != display_required_) {
        const BOOL result = display_required ? api_.set_request(handle_, PowerRequestDisplayRequired)
                                             : api_.clear_request(handle_, PowerRequestDisplayRequired);
        if (result) {
            display_required_ = display_required;
            display_error_ = ERROR_SUCCESS;
        } else {
            display_error_ = api_.last_error();
            if (display_error_ == ERROR_SUCCESS) { display_error_ = ERROR_GEN_FAILURE; }
            success = false;
        }
    } else {
        display_error_ = ERROR_SUCCESS;
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
