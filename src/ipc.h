#pragma once

#include "types.h"

#include <windows.h>

#include <string>

namespace agent_latch {

inline constexpr wchar_t kWindowClassName[] = L"AgentLatch.ControlWindow.v1";
inline constexpr wchar_t kInstanceMutexName[] = L"Local\\AgentLatch.19F17B56-3F58-48D1-AB09-7CBE086EE9B6";
inline constexpr ULONG_PTR kCopyDataSignature = 0x414C4154;  // "ALAT"

std::wstring BuildUpsertMessage(
    const std::wstring& id,
    Provider provider,
    LatchKind kind,
    const std::wstring& label,
    const std::wstring& detail,
    ULONGLONG ttl_milliseconds,
    unsigned int instance_count = 1);
std::wstring BuildRemoveMessage(const std::wstring& id);
bool SendIpcMessage(const std::wstring& message, DWORD timeout_milliseconds = 1500);
bool EnsureBackgroundInstance();

}  // namespace agent_latch
