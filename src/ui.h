#pragma once

#include "settings.h"
#include "types.h"

#include <windows.h>

#include <vector>

namespace agent_latch {

enum class UiAction {
    None,
    ToggleCodex,
    ToggleClaude,
    ToggleCursor,
    ToggleOpenCode,
    ToggleGemini,
    ToggleDisplay,
    ToggleStartup,
    ToggleNotifications,
    SetupHooks,
};

struct DashboardState {
    bool active{false};
    bool power_request_available{true};
    bool keep_display_on{false};
    bool start_with_windows{false};
    ULONGLONG now{0};
    std::vector<Latch> latches;
    Settings settings;
};

class DashboardRenderer {
public:
    DashboardRenderer() = default;
    ~DashboardRenderer();

    DashboardRenderer(const DashboardRenderer&) = delete;
    DashboardRenderer& operator=(const DashboardRenderer&) = delete;

    void Initialize(UINT dpi);
    void Paint(HDC target, const RECT& client, const DashboardState& state);
    UiAction HitTest(POINT point) const;
    SIZE PreferredClientSize() const;

private:
    struct HitTarget {
        RECT rectangle{};
        UiAction action{UiAction::None};
    };

    int Scale(int value) const;
    void ResetFonts();
    void AddHitTarget(const RECT& rectangle, UiAction action);

    UINT dpi_{96};
    HFONT title_font_{nullptr};
    HFONT heading_font_{nullptr};
    HFONT body_font_{nullptr};
    HFONT body_semibold_font_{nullptr};
    HFONT small_font_{nullptr};
    HFONT tiny_semibold_font_{nullptr};
    HICON brand_icon_{nullptr};
    std::vector<HitTarget> hit_targets_;
};

COLORREF ProviderColor(Provider provider);

}  // namespace agent_latch
