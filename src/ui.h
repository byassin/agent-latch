#pragma once

#include "settings.h"
#include "types.h"

#include <windows.h>
#include <commctrl.h>

#include <vector>

namespace agent_latch {

enum class UiAction {
    None,
    ToggleDisplay,
    ToggleStartup,
    ToggleNotifications,
    SetupHooks,
    ManualMenu,
    StopManual,
    OpenDiagnostics,
    MoreMenu,
    PreviousSources,
    NextSources,
};

enum class DashboardPage { Overview, Agents, Settings };

struct DashboardState {
    DashboardPage page{DashboardPage::Overview};
    bool active{false};
    bool system_request_accepted{true};
    bool display_request_accepted{true};
    DWORD system_request_error{ERROR_SUCCESS};
    DWORD display_request_error{ERROR_SUCCESS};
    bool keep_display_on{false};
    bool start_with_windows{false};
    bool manual_active{false};
    ULONGLONG manual_deadline{0};
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
    SIZE PreferredClientSize() const;

private:
    int Scale(int value) const;
    void ResetResources();

    UINT dpi_{96};
    HICON brand_icon_{nullptr};
};

// Real HWND controls supply keyboard navigation, accessible names, selection,
// focus and Windows UI Automation; the renderer paints only the visual shell.
class DashboardControls {
public:
    ~DashboardControls();
    bool Initialize(HWND parent, UINT dpi);
    void SetDpi(UINT dpi);
    void Layout();
    void Update(const DashboardState& state);
    DashboardPage Page() const { return page_; }
    void SelectPage(DashboardPage page);
    bool HandleNotify(const NMHDR* notification);
    UiAction ActionForCommand(WPARAM command) const;
    bool ReadProviderChange(WPARAM command, Provider* provider, DetectionMode* mode) const;
    bool DrawItem(const DRAWITEMSTRUCT* item) const;
    bool MeasureItem(MEASUREITEMSTRUCT* item) const;
    HBRUSH ControlColor(HDC dc, HWND control) const;
    bool TranslateKey(const MSG& message);
    HWND ActionWindow(UiAction action) const;
    bool ValidateLayout() const;
    HWND TabWindow() const { return tabs_; }
    void ChangeReasonPage(int direction);

private:
    struct Control { HWND window; int id; int page; RECT bounds; bool right_anchor; };
    HWND Add(const wchar_t* class_name, const wchar_t* text, DWORD style, int id,
             int page, RECT bounds, bool right_anchor = false);
    HWND Text(const wchar_t* text, int id, int page, RECT bounds, bool right_anchor = false);
    void TextValue(int id, const std::wstring& value) const;
    HWND Find(int id) const;
    void ApplyVisibility();
    void PaintTabs(HDC dc) const;
    static LRESULT CALLBACK TabProcedure(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    static LRESULT CALLBACK ComboProcedure(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    void PaintCombo(HWND window, HDC dc) const;
    void LayoutReasonColumns();
    void UpdateReasonRows();
    void PaintToggle(HWND window, HDC dc) const;
    static LRESULT CALLBACK ToggleProcedure(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    static LRESULT CALLBACK ReasonsProcedure(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    static LRESULT CALLBACK ButtonProcedure(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    int Scale(int value) const;

    HWND parent_{nullptr};
    HWND tabs_{nullptr};
    HWND reasons_{nullptr};
    UINT dpi_{96};
    DashboardPage page_{DashboardPage::Overview};
    HFONT body_font_{nullptr};
    HFONT heading_font_{nullptr};
    HFONT small_font_{nullptr};
    HFONT title_font_{nullptr};
    HBRUSH background_{nullptr};
    HBRUSH surface_{nullptr};
    HBRUSH status_brush_{nullptr};
    HBRUSH badge_brush_{nullptr};
    HIMAGELIST row_images_{nullptr};
    DashboardState state_;
    std::vector<Control> controls_;
    std::vector<Latch> activity_;
    std::size_t reason_page_{0};
};

COLORREF ProviderColor(Provider provider);
std::wstring LatchSecondaryText(const Latch& latch, ULONGLONG now);
const wchar_t* ProviderTrackingStatus(
    Provider provider, DetectionMode mode, const std::vector<Latch>& latches);
bool RunDashboardContractTests(const std::wstring& output_directory = L"");

}  // namespace agent_latch
