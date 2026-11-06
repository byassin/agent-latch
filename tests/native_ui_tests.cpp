#include "ui.h"
#include "dashboard_capture.h"
#include "win_util.h"

#include <array>
#include <cstdio>
#include <uxtheme.h>
#include <cmath>

namespace agent_latch {
namespace {

constexpr int kReasons = 92, kManualStart = 73, kManualStop = 74;
constexpr int kModeFirst = 300, kScreen = 400, kStartup = 401, kNotifications = 402;

bool TextFits(HWND window) {
    wchar_t label[512]{}; GetWindowTextW(window, label, static_cast<int>(std::size(label)));
    RECT bounds{}; GetClientRect(window, &bounds);
    HDC dc = GetDC(window);
    HGDIOBJ previous = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0)));
    SIZE extent{}; GetTextExtentPoint32W(dc, label, lstrlenW(label), &extent);
    SelectObject(dc, previous); ReleaseDC(window, dc);
    return extent.cx <= bounds.right && extent.cy <= bounds.bottom;
}

DashboardState Fixture(Appearance appearance) {
    DashboardState state;
    state.settings.appearance = appearance;
    state.active = state.manual_active = true;
    state.now = 1000; state.manual_deadline = 28801000;
    for (int index = 0; index < 24; ++index) {
        Latch latch;
        latch.id = L"fixture:" + std::to_wstring(index);
        latch.kind = LatchKind::Hook; latch.provider = Provider::ClaudeCode;
        latch.label = L"Agent task " + std::to_wstring(index);
        state.latches.push_back(latch);
    }
    return state;
}

double Luminance(COLORREF color) {
    const auto linear = [](int channel) { const double v = channel / 255.0; return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    return .2126 * linear(GetRValue(color)) + .7152 * linear(GetGValue(color)) + .0722 * linear(GetBValue(color));
}
bool DarkSurface(HWND window) {
    RECT area{}; GetClientRect(window, &area);
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateBitmap(area.right, area.bottom, 1, 32, nullptr);
    if (dc == nullptr || bitmap == nullptr) { if (bitmap) { DeleteObject(bitmap); } if (dc) { DeleteDC(dc); } return false; }
    HGDIOBJ old = SelectObject(dc, bitmap);
    HBRUSH seed = CreateSolidBrush(RGB(255, 0, 255)); FillRect(dc, &area, seed); DeleteObject(seed);
    SendMessageW(window, WM_PRINT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT | PRF_ERASEBKGND | PRF_CHILDREN);
    bool valid = true;
    for (POINT point : {POINT{0, 0}, POINT{area.right - 1, 0}, POINT{0, area.bottom - 1}, POINT{area.right - 1, area.bottom - 1}}) {
        const COLORREF pixel = GetPixel(dc, point.x, point.y);
        valid = pixel != CLR_INVALID && GetRValue(pixel) < 140 && GetGValue(pixel) < 140 && GetBValue(pixel) < 140 && valid;
    }
    SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc); return valid;
}

}  // namespace

bool RunDashboardContractTests(const std::wstring& output_directory) {
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "Native UI check failed at line %d, dpi=%u\n", __LINE__, dpi); return false; } } while (false)
    for (Appearance appearance : {Appearance::Light, Appearance::Dark}) {
    for (UINT dpi : {96u, 120u, 144u, 168u, 192u}) {
        const auto scale = [dpi](int value) { return MulDiv(value, static_cast<int>(dpi), 96); };
        DashboardRenderer renderer; renderer.Initialize(dpi);
        SIZE preferred = renderer.PreferredClientSize();
        HWND native_frame = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"AgentLatch native frame",
            kDashboardWindowStyle, 0, 0, preferred.cx, preferred.cy, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        CHECK(native_frame != nullptr);
        const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(native_frame, GWL_STYLE));
        CHECK((style & (WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME)) ==
            (WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME));
        DestroyWindow(native_frame);
        // Client-only hosts make the test dimensions independent of the actual
        // monitor's non-client DPI. No window is shown or moved between screens.
        HWND parent = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"AgentLatch native test",
            WS_POPUP, 0, 0, preferred.cx, preferred.cy, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        CHECK(parent != nullptr);
        {
            DashboardControls controls;
            CHECK(controls.Initialize(parent, dpi));
            CHECK(GetDlgItem(parent, 13) == nullptr && GetDlgItem(parent, 10) == nullptr && GetDlgItem(parent, 11) == nullptr);
            CHECK(TabCtrl_GetItemCount(controls.TabWindow()) == 3);
            CHECK((GetWindowLongPtrW(controls.TabWindow(), GWL_STYLE) & TCS_OWNERDRAWFIXED) == 0);
            for (int id : {kManualStart, kManualStop, 420, 421}) {
                CHECK((GetWindowLongPtrW(GetDlgItem(parent, id), GWL_STYLE) & BS_TYPEMASK) == BS_PUSHBUTTON);
            }
            for (int index = 0; index < 5; ++index) {
                HWND combo = GetDlgItem(parent, kModeFirst + index);
                CHECK((GetWindowLongPtrW(combo, GWL_STYLE) & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE)) == 0);
                CHECK(SendMessageW(combo, CB_GETCOUNT, 0, 0) == 3);
                for (int selection = 0; selection < 3; ++selection) {
                    SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(selection), 0);
                    Provider provider{}; DetectionMode mode{};
                    CHECK(controls.ReadProviderChange(MAKEWPARAM(kModeFirst + index, CBN_SELCHANGE), &provider, &mode));
                    CHECK(mode == (selection == 0 ? DetectionMode::Tasks : selection == 1 ? DetectionMode::Open : DetectionMode::Off));
                }
            }
            HWND reasons = GetDlgItem(parent, kReasons);
            CHECK((GetWindowLongPtrW(reasons, GWL_STYLE) & (LVS_OWNERDRAWFIXED | LVS_NOCOLUMNHEADER)) == 0);
            CHECK(Header_GetItemCount(ListView_GetHeader(reasons)) == 3);
            auto state = Fixture(appearance);
            controls.Update(state);
            CHECK(controls.IsDark() == (appearance == Appearance::Dark && !WindowsHighContrast()));
            const auto palette = ResolveDashboardPalette(appearance, WindowsHighContrast());
            const auto contrast = [&](COLORREF color) { return (std::max(Luminance(color), Luminance(palette.background)) + .05) /
                (std::min(Luminance(color), Luminance(palette.background)) + .05); };
            CHECK(contrast(palette.text) >= 4.5 && contrast(palette.muted) >= 4.5);
            if (appearance == Appearance::Dark && !WindowsHighContrast()) {
                for (int id : {1000, 12, kManualStart, kManualStop, kScreen, kStartup, kNotifications, 450}) {
                    CHECK(DarkSurface(GetDlgItem(parent, id)));
                }
                EnableWindow(GetDlgItem(parent, kManualStop), FALSE);
                CHECK(DarkSurface(GetDlgItem(parent, kManualStop)));
                EnableWindow(GetDlgItem(parent, kManualStop), TRUE);
                SendMessageW(GetDlgItem(parent, kManualStart), BM_SETSTATE, TRUE, 0);
                CHECK(DarkSurface(GetDlgItem(parent, kManualStart)));
                SendMessageW(GetDlgItem(parent, kManualStart), BM_SETSTATE, FALSE, 0);
                CHECK(DarkSurface(ListView_GetHeader(reasons)));
            }
            for (int selected = 0; selected < 3; ++selected) {
                SendMessageW(GetDlgItem(parent, 450), CB_SETCURSEL, static_cast<WPARAM>(selected), 0);
                Appearance read{}; CHECK(controls.ReadAppearanceChange(MAKEWPARAM(450, CBN_SELCHANGE), &read));
                CHECK(read == static_cast<Appearance>(selected));
            }
            CHECK(!ResolveDashboardPalette(Appearance::Dark, true).dark);
            CHECK(WindowsHighContrast() || ResolveDashboardPalette(Appearance::System, false).dark == WindowsAppsUseDarkMode());
            CHECK(SendMessageW(GetDlgItem(parent, kModeFirst), CB_GETCURSEL, 0, 0) == 0);
            const auto original_latches = state.latches.size();
            for (Appearance changed : {Appearance::Dark, Appearance::Light, appearance}) {
                state.settings.appearance = changed; controls.Update(state);
                CHECK(controls.IsDark() == (changed == Appearance::Dark && !WindowsHighContrast()));
                CHECK(ListView_GetItemCount(reasons) == static_cast<int>(original_latches) && state.manual_active);
                CHECK(SendMessageW(GetDlgItem(parent, kModeFirst), CB_GETCURSEL, 0, 0) == 0);
            }
            wchar_t selected_mode[64]{};
            GetWindowTextW(GetDlgItem(parent, kModeFirst), selected_mode, static_cast<int>(std::size(selected_mode)));
            CHECK(std::wstring(selected_mode) == L"Tasks only");
            CHECK(ListView_GetItemCount(reasons) == 24);
            for (SIZE size : {renderer.MinimumClientSize(), renderer.PreferredClientSize(), SIZE{scale(960), scale(700)}}) {
                CHECK(SetWindowPos(parent, nullptr, 0, 0, size.cx, size.cy,
                    SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
                controls.Layout();
                for (DashboardPage page : {DashboardPage::Overview, DashboardPage::Agents, DashboardPage::Settings}) {
                    controls.SelectPage(page); CHECK(controls.ValidateLayout());
                }
                controls.SelectPage(DashboardPage::Overview);
                CHECK((GetWindowLongPtrW(reasons, GWL_STYLE) & WS_HSCROLL) == 0);
                CHECK(TextFits(GetDlgItem(parent, 40)) && TextFits(GetDlgItem(parent, 71)));
                ListView_SetItemState(reasons, 12, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                ListView_EnsureVisible(reasons, 12, FALSE);
                const int top = ListView_GetTopIndex(reasons);
                controls.Update(state);
                CHECK(ListView_GetNextItem(reasons, -1, LVNI_SELECTED) == 12 && ListView_GetTopIndex(reasons) == top);
            }
            for (int id : {kScreen, kStartup, kNotifications}) {
                HWND checkbox = GetDlgItem(parent, id);
                CHECK((GetWindowLongPtrW(checkbox, GWL_STYLE) & BS_TYPEMASK) == BS_AUTOCHECKBOX);
                for (WPARAM checked : {static_cast<WPARAM>(BST_UNCHECKED), static_cast<WPARAM>(BST_CHECKED)}) {
                    SendMessageW(checkbox, BM_SETCHECK, checked, 0);
                    CHECK(SendMessageW(checkbox, BM_GETCHECK, 0, 0) == static_cast<LRESULT>(checked));
                }
            }
            state.manual_active = false; state.active = false; state.latches.clear(); controls.Update(state);
            CHECK(ListView_GetItemCount(reasons) == 0 && !IsWindowEnabled(GetDlgItem(parent, kManualStop)));
            CHECK((GetWindowLongPtrW(GetDlgItem(parent, 95), GWL_STYLE) & WS_VISIBLE) != 0);
            state.active = state.manual_active = true;
            Latch manual; manual.id = L"manual:keep-awake"; manual.kind = LatchKind::Manual;
            manual.provider = Provider::Manual; manual.label = L"Manual keep-awake";
            state.latches.push_back(manual); controls.Update(state);
            CHECK(ListView_GetItemCount(reasons) == 0 && IsWindowEnabled(GetDlgItem(parent, kManualStop)));
            manual.id = L"custom"; manual.kind = LatchKind::External; manual.label = L"Custom manual request";
            state.latches.push_back(manual); controls.Update(state);
            CHECK(ListView_GetItemCount(reasons) == 1);
            controls.RefreshTheme(); CHECK(controls.ValidateLayout());
            if (!output_directory.empty()) {
                state = Fixture(appearance); state.latches.clear();
                const auto root = JoinPath(JoinPath(output_directory, appearance == Appearance::Dark ? L"dark" : L"light"), std::to_wstring(dpi));
                CHECK(ExportDashboardScreenshots(state, root, dpi));
                state.manual_deadline = 0;
                CHECK(ExportDashboardScreenshots(state, JoinPath(root, L"indefinite"), dpi));
                state.system_request_accepted = false; state.system_request_error = ERROR_ACCESS_DENIED;
                CHECK(ExportDashboardScreenshots(state, JoinPath(root, L"error"), dpi));
                state.system_request_accepted = true; state.active = state.manual_active = false;
                CHECK(ExportDashboardScreenshots(state, JoinPath(root, L"idle"), dpi));
                state = Fixture(appearance);
                for (auto& latch : state.latches) { latch.label = L"A long agent label that must not overlap another column or control"; }
                CHECK(ExportDashboardScreenshots(state, JoinPath(root, L"overflow"), dpi));
                state.latches.resize(4);
                state.latches[0].label = L"Codex task"; state.latches[0].provider = Provider::Codex;
                state.latches[0].kind = LatchKind::Detector; state.latches[0].instance_count = 2;
                state.latches[1].label = L"Claude Code · general-purpose";
                state.latches[2].label = L"Cursor task"; state.latches[2].provider = Provider::Cursor;
                state.latches[3].label = L"Claude Code · release grace";
                state.latches[3].in_grace_period = true; state.latches[3].expires_at = state.now + 120000;
                CHECK(ExportDashboardScreenshots(state, JoinPath(root, L"busy"), dpi));
            }
            DestroyWindow(parent);
        }
    }
    }
#undef CHECK
    return true;
}

}  // namespace agent_latch
