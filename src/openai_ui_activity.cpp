#include "openai_ui_activity.h"

#include <objbase.h>
#include <uiautomation.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cwchar>
#include <cwctype>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

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

std::wstring TakeBstr(BSTR value) {
    if (value == nullptr) {
        return {};
    }
    std::wstring result(value, SysStringLen(value));
    SysFreeString(value);
    return result;
}

std::wstring ReadElementName(IUIAutomationElement* element) {
    BSTR value = nullptr;
    if (FAILED(element->get_CurrentName(&value))) {
        SysFreeString(value);
        return {};
    }
    return TakeBstr(value);
}

std::wstring ReadElementClassName(IUIAutomationElement* element) {
    BSTR value = nullptr;
    if (FAILED(element->get_CurrentClassName(&value))) {
        SysFreeString(value);
        return {};
    }
    return TakeBstr(value);
}

std::wstring ReadNearestDocumentName(
    IUIAutomationElement* element,
    IUIAutomationTreeWalker* walker) {
    IUIAutomationElement* current = element;
    current->AddRef();
    for (unsigned int depth = 0; depth < 64; ++depth) {
        IUIAutomationElement* parent = nullptr;
        const HRESULT parent_result = walker->GetParentElement(current, &parent);
        current->Release();
        current = parent;
        if (FAILED(parent_result) || current == nullptr) {
            return {};
        }
        CONTROLTYPEID control_type = 0;
        if (SUCCEEDED(current->get_CurrentControlType(&control_type)) &&
            control_type == UIA_DocumentControlTypeId) {
            const std::wstring name = ReadElementName(current);
            current->Release();
            return name;
        }
    }
    current->Release();
    return {};
}

bool ReadComposerObservation(
    IUIAutomationElement* element,
    IUIAutomationTreeWalker* walker,
    OpenAIComposerObservation* observation) {
    if (observation == nullptr) {
        return false;
    }
    observation->class_name = ReadElementClassName(element);
    if (!IsOpenAIComposerClass(observation->class_name)) {
        return false;
    }
    observation->name = ReadElementName(element);
    observation->document_name = ReadNearestDocumentName(element, walker);
    BOOL enabled = FALSE;
    BOOL offscreen = FALSE;
    if (FAILED(element->get_CurrentIsEnabled(&enabled))) {
        return false;
    }
    element->get_CurrentIsOffscreen(&offscreen);
    observation->is_enabled = enabled != FALSE;
    observation->is_offscreen = offscreen != FALSE;
    return true;
}

struct WindowEnumeration {
    const std::unordered_set<DWORD>* process_ids{nullptr};
    std::vector<HWND>* windows{nullptr};
};

BOOL CALLBACK CollectTargetWindow(HWND window, LPARAM parameter) {
    auto* enumeration = reinterpret_cast<WindowEnumeration*>(parameter);
    DWORD process_id = 0;
    GetWindowThreadProcessId(window, &process_id);
    if (process_id != 0 && enumeration->process_ids->contains(process_id)) {
        enumeration->windows->push_back(window);
    }
    return TRUE;
}

std::vector<HWND> FindTargetWindows(const std::vector<DWORD>& process_ids) {
    const std::unordered_set<DWORD> targets(process_ids.begin(), process_ids.end());
    std::vector<HWND> windows;
    WindowEnumeration enumeration{&targets, &windows};
    EnumWindows(CollectTargetWindow, reinterpret_cast<LPARAM>(&enumeration));
    return windows;
}

}  // namespace

bool IsOpenAIComposerClass(const std::wstring& class_name) {
    return HasClassToken(class_name, L"size-token-button-composer") &&
           HasClassToken(class_name, L"bg-primary-solid");
}

std::vector<DWORD> NormalizeOpenAITargets(std::vector<DWORD> process_ids) {
    std::erase(process_ids, DWORD{0});
    std::sort(process_ids.begin(), process_ids.end());
    process_ids.erase(std::unique(process_ids.begin(), process_ids.end()), process_ids.end());
    return process_ids;
}

OpenAIMergedActivity MergeOpenAIActivity(
    unsigned int active_codex_tasks,
    const OpenAIActivitySnapshot& snapshot,
    ULONGLONG now,
    ULONGLONG maximum_age) {
    const bool fresh_response =
        snapshot.state == OpenAIResponseState::Responding &&
        snapshot.surface != OpenAISurface::Unknown && snapshot.observed_at != 0 &&
        snapshot.observed_at <= now && now - snapshot.observed_at <= maximum_age;

    if (active_codex_tasks > 0 && fresh_response && snapshot.surface == OpenAISurface::ChatGPT) {
        const std::wstring codex_detail = active_codex_tasks == 1
                                              ? L"1 Codex task"
                                              : std::to_wstring(active_codex_tasks) + L" Codex tasks";
        return OpenAIMergedActivity{
            active_codex_tasks + 1,
            codex_detail + L" + ChatGPT response"};
    }
    if (active_codex_tasks == 1) {
        return OpenAIMergedActivity{1, L"1 Codex task is running"};
    }
    if (active_codex_tasks > 1) {
        return OpenAIMergedActivity{
            active_codex_tasks,
            std::to_wstring(active_codex_tasks) + L" Codex tasks are running"};
    }
    if (!fresh_response) {
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

class OpenAIUnifiedActivityProbe::Impl {
public:
    Impl() : worker_([this](std::stop_token stop_token) { Run(stop_token); }) {}

    ~Impl() {
        worker_.request_stop();
        condition_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    void SetTargets(std::vector<DWORD> process_ids) {
        process_ids = NormalizeOpenAITargets(std::move(process_ids));
        {
            std::lock_guard lock(mutex_);
            if (process_ids == process_ids_) {
                return;
            }
            process_ids_ = std::move(process_ids);
            snapshot_ = {};
            ++target_generation_;
        }
        condition_.notify_all();
    }

    OpenAIActivitySnapshot Snapshot() const {
        std::lock_guard lock(mutex_);
        return snapshot_;
    }

private:
    OpenAIActivitySnapshot Query(
        IUIAutomation* automation,
        IUIAutomationCondition* button_condition,
        IUIAutomationTreeWalker* walker,
        const std::vector<DWORD>& process_ids) {
        const ULONGLONG observed_at = GetTickCount64();
        OpenAIActivitySnapshot best{
            OpenAIResponseState::Unknown, OpenAISurface::Unknown, observed_at};
        for (HWND window : FindTargetWindows(process_ids)) {
            IUIAutomationElement* root = nullptr;
            if (FAILED(automation->ElementFromHandle(window, &root)) || root == nullptr) {
                continue;
            }
            IUIAutomationElementArray* buttons = nullptr;
            const HRESULT find_result =
                root->FindAll(TreeScope_Descendants, button_condition, &buttons);
            root->Release();
            if (FAILED(find_result) || buttons == nullptr) {
                continue;
            }
            int button_count = 0;
            if (FAILED(buttons->get_Length(&button_count))) {
                buttons->Release();
                continue;
            }
            for (int index = 0; index < button_count; ++index) {
                IUIAutomationElement* button = nullptr;
                if (FAILED(buttons->GetElement(index, &button)) || button == nullptr) {
                    continue;
                }
                OpenAIComposerObservation observation;
                const bool found = ReadComposerObservation(button, walker, &observation);
                button->Release();
                if (!found) {
                    continue;
                }
                OpenAIActivitySnapshot candidate =
                    classifiers_[window].Observe(observation, observed_at);
                if (candidate.state == OpenAIResponseState::Responding) {
                    buttons->Release();
                    return candidate;
                }
                if (candidate.state == OpenAIResponseState::Inactive) {
                    best = candidate;
                }
                break;
            }
            buttons->Release();
        }
        return best;
    }

    void Run(std::stop_token stop_token) {
        const HRESULT initialize_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool com_initialized = SUCCEEDED(initialize_result);
        IUIAutomation* automation = nullptr;
        IUIAutomationCondition* button_condition = nullptr;
        IUIAutomationTreeWalker* walker = nullptr;
        if (com_initialized &&
            SUCCEEDED(CoCreateInstance(
                CLSID_CUIAutomation,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_IUIAutomation,
                reinterpret_cast<void**>(&automation))) &&
            automation != nullptr) {
            VARIANT button_type;
            VariantInit(&button_type);
            button_type.vt = VT_I4;
            button_type.lVal = UIA_ButtonControlTypeId;
            automation->CreatePropertyCondition(
                UIA_ControlTypePropertyId,
                button_type,
                &button_condition);
            automation->get_ControlViewWalker(&walker);
            VariantClear(&button_type);
        }

        ULONGLONG observed_generation = 0;
        while (!stop_token.stop_requested()) {
            std::vector<DWORD> process_ids;
            {
                std::lock_guard lock(mutex_);
                process_ids = process_ids_;
                observed_generation = target_generation_;
            }

            OpenAIActivitySnapshot next;
            if (!process_ids.empty() && automation != nullptr && button_condition != nullptr &&
                walker != nullptr) {
                next = Query(automation, button_condition, walker, process_ids);
            }
            {
                std::lock_guard lock(mutex_);
                if (observed_generation == target_generation_) {
                    snapshot_ = next;
                }
            }

            std::unique_lock lock(mutex_);
            condition_.wait_for(
                lock,
                stop_token,
                std::chrono::seconds(2),
                [this, observed_generation] { return target_generation_ != observed_generation; });
        }

        if (walker != nullptr) {
            walker->Release();
        }
        if (button_condition != nullptr) {
            button_condition->Release();
        }
        if (automation != nullptr) {
            automation->Release();
        }
        if (com_initialized) {
            CoUninitialize();
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable_any condition_;
    std::vector<DWORD> process_ids_;
    OpenAIActivitySnapshot snapshot_;
    ULONGLONG target_generation_{0};
    std::unordered_map<HWND, OpenAIComposerClassifier> classifiers_;
    std::jthread worker_;
};

OpenAIUnifiedActivityProbe::OpenAIUnifiedActivityProbe() : impl_(std::make_unique<Impl>()) {}

OpenAIUnifiedActivityProbe::~OpenAIUnifiedActivityProbe() = default;

void OpenAIUnifiedActivityProbe::SetTargets(std::vector<DWORD> process_ids) {
    impl_->SetTargets(std::move(process_ids));
}

OpenAIActivitySnapshot OpenAIUnifiedActivityProbe::Snapshot() const {
    return impl_->Snapshot();
}

}  // namespace agent_latch
