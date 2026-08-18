#include "openai_ui_activity.h"

#include <cwchar>
#include <cwctype>

namespace agent_latch {
namespace {

constexpr unsigned int kLocalizedIdleSamplesRequired = 3;

bool EqualsInsensitive(const std::wstring& left, const wchar_t* right) {
    return _wcsicmp(left.c_str(), right) == 0;
}

bool HasClassToken(const std::wstring& class_name, const std::wstring& token) {
    std::size_t position = 0;
    while (position < class_name.size()) {
        while (position < class_name.size() && std::iswspace(class_name[position])) {
            ++position;
        }
        const std::size_t start = position;
        while (position < class_name.size() && !std::iswspace(class_name[position])) {
            ++position;
        }
        if (class_name.compare(start, position - start, token) == 0) {
            return true;
        }
    }
    return false;
}

OpenAISurface SurfaceFromDocumentName(const std::wstring& document_name) {
    if (EqualsInsensitive(document_name, L"ChatGPT")) {
        return OpenAISurface::ChatGPT;
    }
    if (EqualsInsensitive(document_name, L"Codex")) {
        return OpenAISurface::Codex;
    }
    return OpenAISurface::Unknown;
}

OpenAIActivitySnapshot MakeSnapshot(
    OpenAIResponseState state,
    OpenAISurface surface,
    ULONGLONG observed_at) {
    return OpenAIActivitySnapshot{state, surface, observed_at};
}

}  // namespace

bool IsOpenAIComposerClass(const std::wstring& class_name) {
    return HasClassToken(class_name, L"size-token-button-composer") &&
           HasClassToken(class_name, L"bg-primary-solid");
}

OpenAIMergedActivity MergeOpenAIActivity(
    unsigned int active_codex_tasks,
    const OpenAIActivitySnapshot& snapshot,
    ULONGLONG now,
    ULONGLONG maximum_age) {
    if (active_codex_tasks == 1) {
        return OpenAIMergedActivity{1, L"1 Codex task is running"};
    }
    if (active_codex_tasks > 1) {
        return OpenAIMergedActivity{
            active_codex_tasks,
            std::to_wstring(active_codex_tasks) + L" Codex tasks are running"};
    }
    if (snapshot.state != OpenAIResponseState::Responding ||
        snapshot.surface == OpenAISurface::Unknown || snapshot.observed_at == 0 ||
        snapshot.observed_at > now || now - snapshot.observed_at > maximum_age) {
        return {};
    }
    if (snapshot.surface == OpenAISurface::ChatGPT) {
        return OpenAIMergedActivity{1, L"ChatGPT is responding"};
    }
    return OpenAIMergedActivity{1, L"Codex is responding"};
}

OpenAIActivitySnapshot OpenAIComposerClassifier::Observe(
    const OpenAIComposerObservation& observation,
    ULONGLONG observed_at) {
    if (!IsOpenAIComposerClass(observation.class_name) || observation.name.empty()) {
        return MakeSnapshot(OpenAIResponseState::Unknown, OpenAISurface::Unknown, observed_at);
    }

    const OpenAISurface surface = SurfaceFromDocumentName(observation.document_name);
    if (surface == OpenAISurface::Unknown) {
        return MakeSnapshot(OpenAIResponseState::Unknown, surface, observed_at);
    }

    if (!observation.is_enabled) {
        if (!EqualsInsensitive(observation.name, L"Stop")) {
            if (observation.name == disabled_candidate_label_) {
                ++disabled_candidate_samples_;
            } else {
                disabled_candidate_label_ = observation.name;
                disabled_candidate_samples_ = 1;
            }
            if (disabled_candidate_samples_ >= kLocalizedIdleSamplesRequired) {
                localized_idle_label_ = disabled_candidate_label_;
            }
        }
        return MakeSnapshot(OpenAIResponseState::Inactive, surface, observed_at);
    }

    disabled_candidate_label_.clear();
    disabled_candidate_samples_ = 0;
    if (EqualsInsensitive(observation.name, L"Send") ||
        (!localized_idle_label_.empty() && observation.name == localized_idle_label_)) {
        return MakeSnapshot(OpenAIResponseState::Inactive, surface, observed_at);
    }
    if (EqualsInsensitive(observation.name, L"Stop") || !localized_idle_label_.empty()) {
        return MakeSnapshot(OpenAIResponseState::Responding, surface, observed_at);
    }
    return MakeSnapshot(OpenAIResponseState::Unknown, surface, observed_at);
}

}  // namespace agent_latch
