#pragma once

#include "settings.h"
#include "types.h"

#include <windows.h>
#include <commctrl.h>

#include <vector>

namespace agent_latch {

inline constexpr DWORD kDashboardWindowStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
struct DashboardPalette {
    bool dark;
    COLORREF background, page, surface, text, muted, border, selected, accent, warning;
};
bool WindowsAppsUseDarkMode();
bool WindowsHighContrast();
DashboardPalette ResolveDashboardPalette(Appearance appearance, bool high_contrast);

enum class UiAction {
    None,
    ToggleDisplay,
    ToggleStartup,
    ToggleNotifications,
    SetupHooks,
    ManualMenu,
    ApplyManual,
    StopManual,
    OpenDiagnostics,
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
    SIZE MinimumClientSize() const;

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
    bool InitializeDialog(HWND parent, UINT dpi, Appearance appearance);
    void SetDpi(UINT dpi);
    void Layout();
    void Update(const DashboardState& state);
    DashboardPage Page() const { return page_; }
    void SelectPage(DashboardPage page);
    bool HandleNotify(const NMHDR* notification, LRESULT* result = nullptr);
    UiAction ActionForCommand(WPARAM command) const;
    bool ReadProviderChange(WPARAM command, Provider* provider, DetectionMode* mode) const;
    bool ReadAppearanceChange(WPARAM command, Appearance* appearance) const;
    bool ReadManualCommand(WPARAM command, unsigned int* hours);
    bool DrawItem(const DRAWITEMSTRUCT* item) const;
    bool MeasureItem(MEASUREITEMSTRUCT* item) const;
    HBRUSH ControlColor(HDC dc, HWND control) const;
    bool TranslateKey(const MSG& message);
    HWND ActionWindow(UiAction action) const;
    bool ValidateLayout() const;
    HWND TabWindow() const { return tabs_; }
    void RefreshTheme();
    bool IsDark() const { return palette_.dark; }

private:
    struct Control { HWND window; int id; int page; };
    HWND Add(const wchar_t* class_name, const wchar_t* text, DWORD style, int id,
             int page);
    HWND Text(const wchar_t* text, int id, int page);
    void TextValue(int id, const std::wstring& value) const;
    HWND Find(int id) const;
    void ApplyVisibility();
    void LayoutReasonColumns();
    void UpdateReasonRows();
    void UpdateActivityDetails();
    void Place(int id, int x, int y, int width, int height);
    void PaintDarkControl(HWND window, HDC dc) const;
    void PaintDarkScrollbars(HWND window, HDC dc) const;
    static LRESULT CALLBACK ThemeProcedure(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    int Scale(int value) const;

    HWND parent_{nullptr};
    HWND tabs_{nullptr};
    HWND reasons_{nullptr};
    HWND footer_{nullptr};
    UINT dpi_{96};
    DashboardPage page_{DashboardPage::Overview};
    HFONT body_font_{nullptr};
    HFONT heading_font_{nullptr};
    HFONT small_font_{nullptr};
    HFONT title_font_{nullptr};
    HBRUSH background_{nullptr};
    HBRUSH page_brush_{nullptr};
    COLORREF page_color_{RGB(255, 255, 255)};
    DashboardPalette palette_{};
    DashboardState state_;
    std::vector<Control> controls_;
    std::vector<Latch> activity_;
    std::vector<std::wstring> row_ids_;
    bool updating_rows_{false};
    bool manual_validation_error_{false};
};

COLORREF ProviderColor(Provider provider);
std::wstring LatchSecondaryText(const Latch& latch, ULONGLONG now);
const wchar_t* ProviderTrackingStatus(
    Provider provider, DetectionMode mode, const std::vector<Latch>& latches);
bool RunDashboardContractTests(const std::wstring& output_directory = L"");

}  // namespace agent_latch
