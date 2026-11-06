#include "ui.h"
#include "dashboard_capture.h"
#include "win_util.h"
#include "wake_sessions.h"

#include <array>
#include <cstdio>
#include <uxtheme.h>
#include <cmath>
#include <thread>

namespace agent_latch {
namespace {

constexpr int kReasons = 92, kManualStart = 73, kManualStop = 74;
constexpr int kModeFirst = 300, kScreen = 400, kStartup = 401, kNotifications = 402;

LRESULT CALLBACK FixtureProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* controls = reinterpret_cast<DashboardControls*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (controls != nullptr && message == WM_NOTIFY) {
        LRESULT result = 0;
        if (controls->HandleNotify(reinterpret_cast<const NMHDR*>(lparam), &result)) { return result; }
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

bool TextFits(HWND window) {
    wchar_t label[512]{}; GetWindowTextW(window, label, static_cast<int>(std::size(label)));
    RECT bounds{}; GetClientRect(window, &bounds);
    HDC dc = GetDC(window);
    HGDIOBJ previous = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0)));
    SIZE extent{}; GetTextExtentPoint32W(dc, label, lstrlenW(label), &extent);
    SelectObject(dc, previous); ReleaseDC(window, dc);
    return extent.cx <= bounds.right && extent.cy <= bounds.bottom;
}

std::wstring WindowText(HWND window) {
    wchar_t text[512]{}; GetWindowTextW(window, text, 512); return text;
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

bool RepaintClippingContract() {
    const std::wstring name = L"AgentLatchRepaintTest-" + std::to_wstring(GetCurrentProcessId());
    HDESK desktop = CreateDesktopW(name.c_str(), nullptr, nullptr, 0,
        DESKTOP_CREATEWINDOW | DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS, nullptr);
    if (desktop == nullptr) { std::fprintf(stderr, "Cannot create isolated repaint desktop: %lu\n", GetLastError()); return false; }
    bool passed = false;
    std::thread worker([&]() {
        if (!SetThreadDesktop(desktop)) { return; }
        HWND parent = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"Repaint fixture",
            WS_POPUP | WS_CLIPCHILDREN, 0, 0, 680, 620, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (parent == nullptr) { return; }
        {
            DashboardControls controls;
            passed = controls.Initialize(parent, 96);
            // This desktop is never switched to. Exercise real WM_PAINT/DC
            // clipping without showing anything on the user's input desktop.
            ShowWindow(parent, SW_SHOWNOACTIVATE);
            for (Appearance appearance : {Appearance::Dark, Appearance::Light}) {
                DashboardState state; state.settings.appearance = appearance;
                controls.Update(state);
                for (int repeat = 0; repeat < 3; ++repeat) {
                    SendMessageW(GetDlgItem(parent, 75), CB_SETCURSEL, repeat == 1 ? 7 : 0, 0);
                    unsigned int hours = 0;
                    controls.ReadManualCommand(MAKEWPARAM(75, CBN_SELCHANGE), &hours);
                    controls.SelectPage(DashboardPage::Settings);
                    controls.SelectPage(DashboardPage::Overview);
                    RedrawWindow(parent, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
                    HWND group = GetDlgItem(parent, 69);
                    HDC dc = GetDC(group);
                    if (dc == nullptr || !PtVisible(dc, 2, 20)) { passed = false; }
                    for (int id : {71, 72, 73, 74, 75, 76, 77}) {
                        HWND child = GetDlgItem(parent, id);
                        if (!(GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE)) { continue; }
                        RECT rect{}; GetWindowRect(child, &rect);
                        MapWindowPoints(nullptr, group, reinterpret_cast<POINT*>(&rect), 2);
                        const bool protected_from_group = dc != nullptr &&
                            !PtVisible(dc, (rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2);
                        if (!protected_from_group || !IsWindowVisible(child)) {
                            std::fprintf(stderr, "Manual control %d can be overpainted by its group (theme=%d)\n", id, static_cast<int>(appearance));
                            passed = false;
                        }
                    }
                    if (dc != nullptr) { ReleaseDC(group, dc); }
                    // Repainting just the container must not erase siblings.
                    RedrawWindow(group, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
                }
            }
            DestroyWindow(parent);
        }
    });
    worker.join();
    CloseDesktop(desktop);
    return passed;
}

}  // namespace

bool RunDashboardContractTests(const std::wstring& output_directory) {
    if (!RepaintClippingContract()) { return false; }
    WNDCLASSW fixture{}; fixture.lpfnWndProc = FixtureProcedure;
    fixture.hInstance = GetModuleHandleW(nullptr); fixture.lpszClassName = L"AgentLatch.NativeFixture.v1";
    if (!RegisterClassW(&fixture) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { return false; }
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
        HWND parent = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, fixture.lpszClassName, L"AgentLatch native test",
            WS_POPUP, 0, 0, preferred.cx, preferred.cy, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        CHECK(parent != nullptr);
        {
            DashboardControls controls;
            CHECK(controls.Initialize(parent, dpi));
            SetWindowLongPtrW(parent, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&controls));
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
            HWND duration = GetDlgItem(parent, 75), custom_hours = GetDlgItem(parent, 76);
            CHECK(duration != nullptr && custom_hours != nullptr && GetDlgItem(parent, 93) == nullptr);
            CHECK(GetDlgItem(parent, 94) == nullptr);
            CHECK(SendMessageW(duration, CB_GETCOUNT, 0, 0) == 8);
            CHECK((GetWindowLongPtrW(reasons, GWL_STYLE) & (WS_TABSTOP | LVS_SHOWSELALWAYS | WS_BORDER)) == 0);
            // Exercise the single-step UI with the actual in-memory session
            // engine. No production IPC, preferences, input or timers are used.
            {
            ManualKeepAwake manual;
            DashboardState manual_state; manual_state.settings.appearance = appearance; manual_state.now = 100000;
            const auto sync_manual = [&]() {
                manual_state.active = manual_state.manual_active = manual.Active();
                manual_state.manual_deadline = manual.Deadline(); controls.Update(manual_state);
            };
            unsigned int chosen = 999;
            const auto choose = [&](int index) {
                SendMessageW(duration, CB_SETCURSEL, index, 0);
                controls.ReadManualCommand(MAKEWPARAM(75, CBN_SELCHANGE), &chosen);
            };
            const auto type_hours = [&](const wchar_t* value) {
                SetWindowTextW(custom_hours, value);
                controls.ReadManualCommand(MAKEWPARAM(76, EN_CHANGE), &chosen);
            };
            const auto start_or_update = [&]() {
                if (!controls.ReadManualCommand(MAKEWPARAM(kManualStart, BN_CLICKED), &chosen)) { return false; }
                if (!manual.Start(chosen, manual_state.now, 1000000 + manual_state.now)) { return false; }
                sync_manual(); return true;
            };
            sync_manual(); choose(0);
            CHECK(IsWindowEnabled(GetDlgItem(parent, kManualStart)));
            CHECK(start_or_update() && manual.Active() && manual.Deadline() == 0);
            CHECK(WindowText(GetDlgItem(parent, 71)) == L"On · Until Windows restarts");
            CHECK(WindowText(GetDlgItem(parent, 72)).find(L"Started.") == 0);
            CHECK(WindowText(GetDlgItem(parent, kManualStart)) == L"Update duration");
            CHECK(!IsWindowEnabled(GetDlgItem(parent, kManualStart)) && IsWindowEnabled(GetDlgItem(parent, kManualStop)));
            CHECK(!start_or_update()); // No second step or duplicate unlimited command.
            manual_state.now += 60000; sync_manual();
            CHECK(!IsWindowEnabled(GetDlgItem(parent, kManualStart)));
            choose(1); CHECK(IsWindowEnabled(GetDlgItem(parent, kManualStart)) && manual.Deadline() == 0);
            choose(0); CHECK(!IsWindowEnabled(GetDlgItem(parent, kManualStart)));
            choose(7); type_hours(L"0");
            CHECK(!IsWindowEnabled(GetDlgItem(parent, kManualStart)) && !start_or_update());
            CHECK(WindowText(GetDlgItem(parent, 72)).find(L"1 to 168") != std::wstring::npos);
            type_hours(L"12"); CHECK(IsWindowEnabled(GetDlgItem(parent, kManualStart)));
            CHECK(start_or_update() && chosen == 12);
            const ULONGLONG original_deadline = manual.Deadline();
            CHECK(WindowText(GetDlgItem(parent, 71)).find(L"On · ") == 0);
            CHECK(WindowText(GetDlgItem(parent, 72)).find(L"Duration updated.") == 0);
            CHECK(!IsWindowEnabled(GetDlgItem(parent, kManualStart)));
            manual_state.now += 60000; sync_manual();
            CHECK(!start_or_update() && manual.Deadline() == original_deadline);
            choose(7); type_hours(L"012");
            CHECK(!IsWindowEnabled(GetDlgItem(parent, kManualStart))); // Same duration, different presentation.
            choose(2); manual_state.now += 60000; sync_manual();
            CHECK(SendMessageW(duration, CB_GETCURSEL, 0, 0) == 2);
            CHECK(IsWindowEnabled(GetDlgItem(parent, kManualStart)) && manual.Deadline() == original_deadline);
            CHECK(manual.Start(4, manual_state.now, 2000000)); sync_manual(); // External/tray update.
            CHECK(!IsWindowEnabled(GetDlgItem(parent, kManualStart)));
            CHECK(WindowText(GetDlgItem(parent, 72)).find(L"Duration updated.") != 0);
            manual.Stop(); sync_manual();
            CHECK(WindowText(GetDlgItem(parent, 71)) == L"Off");
            CHECK(WindowText(GetDlgItem(parent, kManualStart)) == L"Start");
            CHECK(IsWindowEnabled(GetDlgItem(parent, kManualStart)) && !IsWindowEnabled(GetDlgItem(parent, kManualStop)));
            choose(1); CHECK(start_or_update());
            manual_state.now = manual.Deadline(); CHECK(manual.Expire(manual_state.now)); sync_manual();
            CHECK(WindowText(GetDlgItem(parent, 71)) == L"Off" && IsWindowEnabled(GetDlgItem(parent, kManualStart)));
            }
            constexpr unsigned int expected_hours[] = {0, 1, 2, 4, 8, 12, 24};
            for (int preset = 0; preset < 7; ++preset) {
                SendMessageW(duration, CB_SETCURSEL, preset, 0);
                unsigned int hours = 999;
                controls.ReadManualCommand(MAKEWPARAM(75, CBN_SELCHANGE), &hours);
                CHECK(!IsWindowEnabled(custom_hours));
                CHECK(controls.ReadManualCommand(MAKEWPARAM(kManualStart, BN_CLICKED), &hours));
                CHECK(hours == expected_hours[preset]);
            }
            SendMessageW(duration, CB_SETCURSEL, 7, 0);
            unsigned int hours = 999;
            controls.ReadManualCommand(MAKEWPARAM(75, CBN_SELCHANGE), &hours);
            CHECK(IsWindowEnabled(custom_hours));
            for (const auto* invalid : {L"", L"0", L"169", L"-1", L"2x"}) {
                SetWindowTextW(custom_hours, invalid);
                CHECK(!controls.ReadManualCommand(MAKEWPARAM(kManualStart, BN_CLICKED), &hours));
                CHECK(hours == 999);
            }
            for (const auto* valid : {L"1", L"8", L"168"}) {
                SetWindowTextW(custom_hours, valid);
                CHECK(controls.ReadManualCommand(MAKEWPARAM(kManualStart, BN_CLICKED), &hours));
            }
            SetWindowTextW(custom_hours, L"8");
            SendMessageW(duration, CB_SETCURSEL, 0, 0);
            controls.ReadManualCommand(MAKEWPARAM(75, CBN_SELCHANGE), &hours);
            auto state = Fixture(appearance);
            controls.Update(state);
            CHECK(SendMessageW(duration, CB_GETCURSEL, 0, 0) == 4); // Actual eight-hour session.
            CHECK(!IsWindowEnabled(GetDlgItem(parent, kManualStart))); // No redundant second step.
            SendMessageW(duration, CB_SETCURSEL, 2, 0); // User's draft choice survives periodic refresh.
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
                CHECK(TextFits(GetDlgItem(parent, 40)) && TextFits(GetDlgItem(parent, 71)) &&
                    TextFits(GetDlgItem(parent, kManualStart)) && TextFits(GetDlgItem(parent, 72)));
                ListView_SetItemState(reasons, 12, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                ListView_EnsureVisible(reasons, 12, FALSE);
                const int top = ListView_GetTopIndex(reasons);
                controls.Update(state);
                CHECK(ListView_GetNextItem(reasons, -1, LVNI_SELECTED) == -1 && ListView_GetTopIndex(reasons) == top);
                CHECK(ListView_GetNextItem(reasons, -1, LVNI_FOCUSED) == -1);
                // Synthetic input targets only this hidden fixture, not the desktop.
                SendMessageW(reasons, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(scale(20), scale(35)));
                SendMessageW(reasons, WM_LBUTTONUP, 0, MAKELPARAM(scale(20), scale(35)));
                CHECK(ListView_GetNextItem(reasons, -1, LVNI_SELECTED) == -1);
                CHECK(ListView_GetNextItem(reasons, -1, LVNI_FOCUSED) == -1);
                CHECK(SendMessageW(duration, CB_GETCURSEL, 0, 0) == 2);
                // Hidden fixture hit-tests ensure siblings do not intercept controls.
                for (int id : {kManualStart, kManualStop, 75, 76, 92}) {
                    HWND child = GetDlgItem(parent, id); RECT rect{}; GetWindowRect(child, &rect);
                    if (!(GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE)) { continue; }
                    MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&rect), 2);
                    POINT center{(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2};
                    CHECK(ChildWindowFromPointEx(parent, center, CWP_SKIPINVISIBLE) == child);
                }
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
