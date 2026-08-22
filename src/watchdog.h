#pragma once

#include <windows.h>

namespace agent_latch {

struct RestartDecision {
    bool restart{false};
    unsigned int next_attempt{0};
};

RestartDecision EvaluateRestart(
    DWORD exit_code,
    ULONGLONG runtime_milliseconds,
    unsigned int prior_attempts);
bool LaunchWatchdogForCurrentProcess(unsigned int restart_attempt);
int RunWatchdog(DWORD process_id, unsigned int restart_attempt);

}  // namespace agent_latch
