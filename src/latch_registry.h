#pragma once

#include "types.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace agent_latch {

class LatchRegistry {
public:
    bool Upsert(
        const std::wstring& id,
        Provider provider,
        LatchKind kind,
        const std::wstring& label,
        const std::wstring& detail,
        ULONGLONG now,
        ULONGLONG ttl_milliseconds,
        unsigned int instance_count = 1);

    bool Remove(const std::wstring& id);
    bool RemoveByKind(LatchKind kind);
    bool RemoveByProvider(Provider provider);
    bool Expire(ULONGLONG now);
    void Clear();

    bool IsActive() const;
    std::size_t Size() const;
    bool HasKind(LatchKind kind) const;
    const Latch* Find(const std::wstring& id) const;
    std::vector<Latch> Snapshot() const;

private:
    std::unordered_map<std::wstring, Latch> latches_;
};

}  // namespace agent_latch
