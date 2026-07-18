#include "latch_registry.h"

#include <algorithm>

namespace agent_latch {

bool LatchRegistry::Upsert(
    const std::wstring& id,
    Provider provider,
    LatchKind kind,
    const std::wstring& label,
    const std::wstring& detail,
    ULONGLONG now,
    ULONGLONG ttl_milliseconds,
    unsigned int instance_count) {
    if (id.empty()) {
        return false;
    }

    const auto existing = latches_.find(id);
    if (existing == latches_.end()) {
        Latch latch;
        latch.id = id;
        latch.provider = provider;
        latch.kind = kind;
        latch.label = label;
        latch.detail = detail;
        latch.created_at = now;
        latch.renewed_at = now;
        latch.expires_at = ttl_milliseconds == 0 ? 0 : now + ttl_milliseconds;
        latch.instance_count = std::max(1u, instance_count);
        latches_.emplace(id, std::move(latch));
        return true;
    }

    Latch& latch = existing->second;
    const ULONGLONG expires_at = ttl_milliseconds == 0 ? 0 : now + ttl_milliseconds;
    const bool changed = latch.provider != provider || latch.kind != kind || latch.label != label ||
                         latch.detail != detail || latch.expires_at != expires_at ||
                         latch.instance_count != std::max(1u, instance_count);
    latch.provider = provider;
    latch.kind = kind;
    latch.label = label;
    latch.detail = detail;
    latch.renewed_at = now;
    latch.expires_at = expires_at;
    latch.instance_count = std::max(1u, instance_count);
    return changed;
}

bool LatchRegistry::Remove(const std::wstring& id) {
    return latches_.erase(id) != 0;
}

bool LatchRegistry::RemoveByKind(LatchKind kind) {
    bool changed = false;
    for (auto iterator = latches_.begin(); iterator != latches_.end();) {
        if (iterator->second.kind == kind) {
            iterator = latches_.erase(iterator);
            changed = true;
        } else {
            ++iterator;
        }
    }
    return changed;
}

bool LatchRegistry::RemoveByProvider(Provider provider) {
    bool changed = false;
    for (auto iterator = latches_.begin(); iterator != latches_.end();) {
        if (iterator->second.provider == provider) {
            iterator = latches_.erase(iterator);
            changed = true;
        } else {
            ++iterator;
        }
    }
    return changed;
}

bool LatchRegistry::Expire(ULONGLONG now) {
    bool changed = false;
    for (auto iterator = latches_.begin(); iterator != latches_.end();) {
        const ULONGLONG expires_at = iterator->second.expires_at;
        if (expires_at != 0 && expires_at <= now) {
            iterator = latches_.erase(iterator);
            changed = true;
        } else {
            ++iterator;
        }
    }
    return changed;
}

void LatchRegistry::Clear() {
    latches_.clear();
}

bool LatchRegistry::IsActive() const {
    return !latches_.empty();
}

std::size_t LatchRegistry::Size() const {
    return latches_.size();
}

bool LatchRegistry::HasKind(LatchKind kind) const {
    return std::any_of(latches_.begin(), latches_.end(), [kind](const auto& entry) {
        return entry.second.kind == kind;
    });
}

const Latch* LatchRegistry::Find(const std::wstring& id) const {
    const auto iterator = latches_.find(id);
    return iterator == latches_.end() ? nullptr : &iterator->second;
}

std::vector<Latch> LatchRegistry::Snapshot() const {
    std::vector<Latch> result;
    result.reserve(latches_.size());
    for (const auto& entry : latches_) {
        result.push_back(entry.second);
    }

    std::sort(result.begin(), result.end(), [](const Latch& left, const Latch& right) {
        if (left.kind != right.kind) {
            return static_cast<int>(left.kind) < static_cast<int>(right.kind);
        }
        if (left.created_at != right.created_at) {
            return left.created_at < right.created_at;
        }
        return left.id < right.id;
    });
    return result;
}

}  // namespace agent_latch
