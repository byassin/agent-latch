#pragma once

#include <windows.h>

#include <string>

namespace agent_latch {

enum class OpenAISurface {
    Unknown,
    ChatGPT,
    Codex,
};

enum class OpenAIResponseState {
    Unknown,
    Inactive,
    Responding,
};

struct OpenAIComposerObservation {
    std::wstring name;
    std::wstring class_name;
    std::wstring document_name;
    bool is_enabled{false};
    bool is_offscreen{false};
};

struct OpenAIActivitySnapshot {
    OpenAIResponseState state{OpenAIResponseState::Unknown};
    OpenAISurface surface{OpenAISurface::Unknown};
    ULONGLONG observed_at{0};
};

bool IsOpenAIComposerClass(const std::wstring& class_name);

class OpenAIComposerClassifier {
public:
    OpenAIActivitySnapshot Observe(
        const OpenAIComposerObservation& observation,
        ULONGLONG observed_at);

private:
    std::wstring localized_idle_label_;
    std::wstring disabled_candidate_label_;
    unsigned int disabled_candidate_samples_{0};
};

}  // namespace agent_latch
