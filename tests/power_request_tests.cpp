#include "power_request.h"

#include <cstdio>

namespace agent_latch {
namespace {

struct FakePower {
    ULONGLONG now{1000};
    DWORD error{ERROR_NOT_ENOUGH_MEMORY};
    DWORD failure_error{ERROR_ACCESS_DENIED};
    unsigned int creation_failures{0}, creates{0}, closes{0};
    unsigned int system_sets{0}, display_sets{0}, system_clears{0}, display_clears{0};
    unsigned int system_set_failures{0}, system_clear_failures{0};
    unsigned int display_set_failures{0}, display_clear_failures{0};
};
thread_local FakePower* current = nullptr;

HANDLE WINAPI Create(PREASON_CONTEXT) {
    ++current->creates;
    if (current->creation_failures != 0) {
        --current->creation_failures;
        return INVALID_HANDLE_VALUE;
    }
    return reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(123));
}
BOOL WINAPI Set(HANDLE, POWER_REQUEST_TYPE type) {
    if (type == PowerRequestSystemRequired) {
        ++current->system_sets;
        if (current->system_set_failures != 0) {
            --current->system_set_failures;
            current->error = current->failure_error;
            return FALSE;
        }
    } else if (type == PowerRequestDisplayRequired) {
        ++current->display_sets;
        if (current->display_set_failures != 0) {
            --current->display_set_failures;
            current->error = current->failure_error;
            return FALSE;
        }
    }
    return TRUE;
}
BOOL WINAPI Clear(HANDLE, POWER_REQUEST_TYPE type) {
    if (type == PowerRequestSystemRequired) {
        ++current->system_clears;
        if (current->system_clear_failures != 0) {
            --current->system_clear_failures;
            current->error = current->failure_error;
            return FALSE;
        }
    } else if (type == PowerRequestDisplayRequired) {
        ++current->display_clears;
        if (current->display_clear_failures != 0) {
            --current->display_clear_failures;
            current->error = current->failure_error;
            return FALSE;
        }
    }
    return TRUE;
}
BOOL WINAPI Close(HANDLE) { ++current->closes; return TRUE; }
DWORD WINAPI Error() { return current->error; }
ULONGLONG WINAPI Tick() { return current->now; }
PowerRequestApi Api() { return {Create, Set, Clear, Close, Error, Tick}; }

}  // namespace

bool RunPowerRequestSelfTests() {
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "Power request check failed at line %d\n", __LINE__); return false; } } while (false)
    FakePower fake;
    current = &fake;
    struct Reset { ~Reset() { current = nullptr; } } reset;
    fake.creation_failures = 1;
    {
        PowerRequest request(Api());
        CHECK(!request.IsAvailable() && request.LastError() == ERROR_NOT_ENOUGH_MEMORY);
        CHECK(!request.Apply(false, false) && fake.creates == 1);
        fake.now += PowerRequest::kCreationRetryMilliseconds - 1;
        CHECK(!request.Apply(true, true) && fake.creates == 1 && fake.system_sets == 0);
        ++fake.now;
        CHECK(request.Apply(true, true) && fake.creates == 2 && request.IsAvailable());
        CHECK(request.IsSystemRequired() && request.IsDisplayRequired());
        CHECK(request.LastSystemError() == ERROR_SUCCESS && request.LastDisplayError() == ERROR_SUCCESS);
        CHECK(fake.system_sets == 1 && fake.display_sets == 1);
        CHECK(request.Apply(true, true) && fake.creates == 2 && fake.system_sets == 1 && fake.display_sets == 1);
        CHECK(request.Apply(true, false) && request.IsSystemRequired() && !request.IsDisplayRequired());
        CHECK(fake.display_clears == 1 && fake.system_clears == 0);
        CHECK(request.Apply(false, false) && !request.IsSystemRequired() && fake.system_clears == 1);
    }
    CHECK(fake.closes == 1 && fake.system_clears == 1 && fake.display_clears == 1);

    fake = FakePower{}; fake.creation_failures = 2;
    {
        PowerRequest request(Api());
        for (int event = 0; event < 100; ++event) { CHECK(!request.Apply(true, false)); }
        CHECK(fake.creates == 1);
        fake.now += PowerRequest::kCreationRetryMilliseconds;
        CHECK(!request.Apply(true, false) && fake.creates == 2);
        fake.now += PowerRequest::kCreationRetryMilliseconds - 1;
        CHECK(!request.Apply(true, false) && fake.creates == 2);
        ++fake.now;
        CHECK(request.Apply(true, false) && fake.creates == 3 && fake.system_sets == 1);
    }
    CHECK(fake.closes == 1 && fake.system_clears == 1);

    fake = FakePower{}; fake.creation_failures = 1;
    {
        PowerRequest request(Api());
        fake.now += PowerRequest::kCreationRetryMilliseconds;
        CHECK(request.Apply(false, false) && request.IsAvailable());
        CHECK(!request.IsSystemRequired() && !request.IsDisplayRequired());
        CHECK(request.LastError() == ERROR_SUCCESS && fake.creates == 2);
        CHECK(fake.system_sets == 0 && fake.display_sets == 0);
    }
    CHECK(fake.closes == 1 && fake.system_clears == 0 && fake.display_clears == 0);

    fake = FakePower{}; fake.creation_failures = 100;
    {
        PowerRequest request(Api());
        fake.now += 100000;
        // Destruction must not create a new handle just to clear/close it.
    }
    CHECK(fake.creates == 1 && fake.closes == 0);

    fake = FakePower{}; fake.system_set_failures = 1;
    {
        PowerRequest request(Api());
        CHECK(!request.Apply(true, true));
        CHECK(!request.IsSystemRequired() && request.IsDisplayRequired());
        CHECK(request.LastSystemError() == ERROR_ACCESS_DENIED && request.LastDisplayError() == ERROR_SUCCESS);
        CHECK(request.Apply(true, true) && request.IsSystemRequired() && fake.system_sets == 2 && fake.display_sets == 1);
        fake.system_clear_failures = 1;
        CHECK(!request.Apply(false, false) && request.IsSystemRequired() && !request.IsDisplayRequired());
        CHECK(request.Apply(false, false) && !request.IsSystemRequired() && request.LastError() == ERROR_SUCCESS);
        CHECK(fake.creates == 1 && fake.system_clears == 2 && fake.display_clears == 1);
    }
    CHECK(fake.closes == 1);

    fake = FakePower{}; fake.display_set_failures = 1; fake.failure_error = ERROR_SUCCESS;
    {
        PowerRequest request(Api());
        CHECK(!request.Apply(false, true) && request.LastDisplayError() == ERROR_GEN_FAILURE);
        CHECK(request.Apply(false, true) && request.LastError() == ERROR_SUCCESS);
        fake.display_clear_failures = 1;
        CHECK(!request.Apply(false, false) && request.LastDisplayError() == ERROR_GEN_FAILURE);
        CHECK(request.Apply(false, false) && request.LastError() == ERROR_SUCCESS);
    }
    CHECK(fake.closes == 1 && fake.system_sets == 0 && fake.system_clears == 0);

    // If desired state changes back after a failed transition, no native
    // transition is needed. Do not retain an error for work no longer wanted.
    fake = FakePower{}; fake.system_set_failures = fake.display_set_failures = 1;
    {
        PowerRequest request(Api());
        CHECK(!request.Apply(true, true));
        CHECK(!request.IsSystemRequired() && !request.IsDisplayRequired());
        CHECK(request.LastSystemError() == ERROR_ACCESS_DENIED && request.LastDisplayError() == ERROR_ACCESS_DENIED);
        CHECK(request.Apply(false, false) && request.LastError() == ERROR_SUCCESS);
        CHECK(fake.system_sets == 1 && fake.display_sets == 1 && fake.system_clears == 0 && fake.display_clears == 0);
        CHECK(request.Apply(true, true) && request.IsSystemRequired() && request.IsDisplayRequired());
        fake.system_clear_failures = fake.display_clear_failures = 1;
        CHECK(!request.Apply(false, false));
        CHECK(request.IsSystemRequired() && request.IsDisplayRequired());
        CHECK(request.LastSystemError() == ERROR_ACCESS_DENIED && request.LastDisplayError() == ERROR_ACCESS_DENIED);
        CHECK(request.Apply(true, true) && request.LastError() == ERROR_SUCCESS);
        CHECK(fake.system_sets == 2 && fake.display_sets == 2 && fake.system_clears == 1 && fake.display_clears == 1);
    }
    CHECK(fake.closes == 1 && fake.system_clears == 2 && fake.display_clears == 2);

    fake = FakePower{}; fake.system_set_failures = 1; fake.failure_error = ERROR_SUCCESS;
    {
        PowerRequest request(Api());
        CHECK(!request.Apply(true, false) && request.LastSystemError() == ERROR_GEN_FAILURE);
        CHECK(request.Apply(true, false) && request.LastError() == ERROR_SUCCESS);
        fake.system_clear_failures = 1;
        CHECK(!request.Apply(false, false) && request.LastSystemError() == ERROR_GEN_FAILURE);
        CHECK(request.Apply(false, false) && request.LastError() == ERROR_SUCCESS);
    }
    CHECK(fake.closes == 1);

    fake = FakePower{}; fake.creation_failures = 1; fake.error = ERROR_SUCCESS;
    {
        PowerRequest request(Api());
        CHECK(!request.IsAvailable() && request.LastError() == ERROR_GEN_FAILURE);
    }
    CHECK(fake.creates == 1 && fake.closes == 0);
    current = nullptr;
#undef CHECK
    return true;
}

}  // namespace agent_latch
