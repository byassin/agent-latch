#pragma once

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

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

struct OpenAIMergedActivity {
    unsigned int active_instances{0};
    std::wstring detail;
};

OpenAIMergedActivity MergeOpenAIActivity(
    unsigned int active_codex_tasks,
    const OpenAIActivitySnapshot& snapshot,
    ULONGLONG now,
    ULONGLONG maximum_age);

bool IsOpenAIComposerClass(const std::wstring& class_name);
std::vector<DWORD> NormalizeOpenAITargets(std::vector<DWORD> process_ids);

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

class OpenAIUnifiedActivityProbe {
public:
    OpenAIUnifiedActivityProbe();
    ~OpenAIUnifiedActivityProbe();

    OpenAIUnifiedActivityProbe(const OpenAIUnifiedActivityProbe&) = delete;
    OpenAIUnifiedActivityProbe& operator=(const OpenAIUnifiedActivityProbe&) = delete;

    void SetTargets(std::vector<DWORD> process_ids);
    OpenAIActivitySnapshot Snapshot() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace agent_latch
