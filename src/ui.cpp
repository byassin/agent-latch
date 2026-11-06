#include "ui.h"

#include "resource.h"
#include "version.h"
#include "win_util.h"

#include <algorithm>
#include <array>
#include <uxtheme.h>
#include <initguid.h>
#include <oleacc.h>
#include <vsstyle.h>
#include <dwmapi.h>

namespace agent_latch {
namespace {

constexpr int kTabs = 1000;
constexpr int kStatusTitle = 40, kStatusDetail = 41, kFooter = 12;
constexpr int kManualGroup = 69, kManualStatus = 71, kManualHint = 72;
constexpr int kManualStart = 73, kManualStop = 74;
constexpr int kManualDuration = 75, kManualHours = 76, kManualHoursLabel = 77;
constexpr int kReasonsTitle = 90, kReasonsCount = 91, kReasons = 92;
constexpr int kEmptyTitle = 95, kEmptyHint = 96;
constexpr int kAgentsTitle = 100, kAgentsHint = 101;
constexpr int kModeFirst = 300, kProviderName = 310, kProviderDetail = 320;
constexpr int kScreen = 400, kStartup = 401, kNotifications = 402;
constexpr int kSettingHint = 410, kDiagnostics = 420, kRepair = 421, kStatusReport = 422, kSettingsTitle = 430;
constexpr int kAppearance = 450, kAppearanceLabel = 451;
constexpr int kReleaseDelay = 460, kReleaseLabel = 461, kReleaseHint = 462;
constexpr std::array<DWORD, 6> kReleaseSeconds = {30, 60, 180, 300, 600, 1800};
constexpr std::array<const wchar_t*, 6> kReleaseLabels = {
    L"30 seconds", L"1 minute", L"3 minutes (default)", L"5 minutes", L"10 minutes", L"30 minutes"};
constexpr std::array<Provider, 5> kProviders = {
    Provider::Codex, Provider::ClaudeCode, Provider::Cursor, Provider::OpenCode, Provider::GeminiCli};
constexpr std::array<const wchar_t*, 5> kProviderLabels = {
    L"Codex / ChatGPT", L"Claude", L"Cursor", L"OpenCode", L"Antigravity / Gemini"};
constexpr std::array<const wchar_t*, 3> kModeLabels = {L"Tasks only", L"While open", L"Off"};

HFONT Font(UINT dpi, int points, int weight) {
    return CreateFontW(-MulDiv(points, static_cast<int>(dpi), 72), 0, 0, 0, weight,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

COLORREF StatusColor(const DashboardState& state) {
    const auto palette = ResolveDashboardPalette(state.settings.appearance, WindowsHighContrast());
    return !state.system_request_accepted || (!state.active && !state.display_request_accepted) ?
        palette.warning : state.active ? palette.accent : palette.text;
}

int ModeIndex(DetectionMode mode) {
    return mode == DetectionMode::Tasks ? 0 : mode == DetectionMode::Open ? 1 : 2;
}
DetectionMode IndexMode(int index) {
    return index == 0 ? DetectionMode::Tasks : index == 1 ? DetectionMode::Open : DetectionMode::Off;
}

std::wstring ProviderDetail(Provider provider, const DashboardState& state) {
    const auto mode = state.settings.ProviderMode(provider);
    if (mode == DetectionMode::Off) { return L"Not monitored"; }
    const std::wstring status = ProviderTrackingStatus(provider, mode, state.latches);
    if (status == L"OPEN") { return L"App open · keeping awake"; }
    if (status == L"RUNNING") { return L"Activity detected"; }
    if (status == L"GRACE") {
        for (const auto& latch : state.latches) {
            if (latch.provider == provider && latch.kind == LatchKind::RestartGrace) {
                return L"Waiting for app restart";
            }
        }
        return L"Finishing protection";
    }
    if (mode == DetectionMode::Tasks && provider == Provider::ClaudeCode &&
        state.settings.claude_integration_expected && !state.settings.claude_hook_seen) {
        return L"Awaiting first hook · restart agent session";
    }
    return mode == DetectionMode::Open ? L"Waiting for app to open" : L"Watching for active tasks";
}

const wchar_t* Signal(const Latch& latch) {
    if (latch.kind == LatchKind::RestartGrace) { return L"Restart grace"; }
    if (latch.in_grace_period) { return L"Release grace"; }
    if (latch.kind == LatchKind::External) { return L"External request"; }
    if (latch.label.ends_with(L" open")) { return L"App presence"; }
    if (latch.detail == L"Process activity detected") { return L"Process activity"; }
    return latch.kind == LatchKind::Hook ? L"Lifecycle event" : L"Task detected";
}

void SetCell(HWND list, int row, int column, const std::wstring& text) {
    wchar_t previous[1024]{};
    ListView_GetItemText(list, row, column, previous, static_cast<int>(std::size(previous)));
    if (text != previous) { ListView_SetItemText(list, row, column, const_cast<wchar_t*>(text.c_str())); }
}

void AccessibleName(HWND window, const std::wstring& name) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IAccPropServices* properties = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_AccPropServices, nullptr, CLSCTX_INPROC_SERVER,
        IID_IAccPropServices, reinterpret_cast<void**>(&properties)))) {
        properties->SetHwndPropStr(window, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF,
                                  PROPID_ACC_NAME, name.c_str());
        properties->Release();
    }
    if (SUCCEEDED(initialized)) { CoUninitialize(); }
}

}  // namespace

COLORREF ProviderColor(Provider provider) {
    switch (provider) {
        case Provider::Manual: return RGB(37, 99, 235);
        case Provider::Codex: return RGB(5, 150, 105);
        case Provider::ClaudeCode: return RGB(217, 119, 6);
        case Provider::Cursor: return RGB(124, 58, 237);
        case Provider::OpenCode: return RGB(13, 148, 136);
        case Provider::GeminiCli: return RGB(37, 99, 235);
        case Provider::External: return GetSysColor(COLOR_GRAYTEXT);
    }
    return GetSysColor(COLOR_WINDOWTEXT);
}

std::wstring LatchSecondaryText(const Latch& latch, ULONGLONG now) {
    if (latch.in_grace_period) {
        return L"Releasing in " + FormatDuration(latch.expires_at > now ? latch.expires_at - now : 0);
    }
    if (latch.kind == LatchKind::Manual) { return L"Until Windows restarts"; }
    if (latch.kind == LatchKind::Hook) {
        return latch.instance_count > 1 ? std::to_wstring(latch.instance_count) + L" active" : L"Active";
    }
    if (latch.kind == LatchKind::Detector) {
        if (latch.detail == L"Process activity detected") { return L"Process activity"; }
        if (latch.label.ends_with(L" open")) { return L"App open"; }
        if (latch.detail.find(L"ChatGPT response") != std::wstring::npos &&
            latch.detail.find(L"Codex task") != std::wstring::npos) { return L"Codex + ChatGPT"; }
        return latch.instance_count > 1 ? std::to_wstring(latch.instance_count) + L" tasks running" : L"1 task running";
    }
    return FormatCountdown(latch.expires_at, now);
}

const wchar_t* ProviderTrackingStatus(Provider provider, DetectionMode mode, const std::vector<Latch>& latches) {
    bool grace = false;
    for (const auto& latch : latches) {
        if (latch.provider != provider) { continue; }
        if (!latch.in_grace_period) { return mode == DetectionMode::Open ? L"OPEN" : L"RUNNING"; }
        grace = true;
    }
    return grace ? L"GRACE" : L"";
}

DashboardRenderer::~DashboardRenderer() { ResetResources(); }
void DashboardRenderer::ResetResources() {
    if (brand_icon_ != nullptr) { DestroyIcon(brand_icon_); brand_icon_ = nullptr; }
}
int DashboardRenderer::Scale(int value) const { return MulDiv(value, static_cast<int>(dpi_), 96); }
void DashboardRenderer::Initialize(UINT dpi) {
    ResetResources(); dpi_ = dpi == 0 ? 96 : dpi;
    brand_icon_ = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_AGENTLATCH),
        IMAGE_ICON, Scale(32), Scale(32), LR_DEFAULTCOLOR));
}
SIZE DashboardRenderer::PreferredClientSize() const { return {Scale(680), Scale(520)}; }
SIZE DashboardRenderer::MinimumClientSize() const { return {Scale(600), Scale(460)}; }
void DashboardRenderer::Paint(HDC target, const RECT& client, const DashboardState& state) {
    const auto palette = ResolveDashboardPalette(state.settings.appearance, WindowsHighContrast());
    HBRUSH background = CreateSolidBrush(palette.background); FillRect(target, &client, background); DeleteObject(background);
    if (brand_icon_ != nullptr) {
        DrawIconEx(target, Scale(24), Scale(23), brand_icon_, Scale(32), Scale(32), 0, nullptr, DI_NORMAL);
    }
}

DashboardControls::~DashboardControls() {
    for (const auto& control : controls_) {
        if (IsWindow(control.window)) { RemoveWindowSubclass(control.window, ThemeProcedure, 2); }
    }
    if (IsWindow(reasons_)) { RemoveWindowSubclass(ListView_GetHeader(reasons_), ThemeProcedure, 2); }
    for (HFONT font : {body_font_, heading_font_, small_font_, title_font_}) {
        if (font != nullptr) { DeleteObject(font); }
    }
    if (background_ != nullptr) { DeleteObject(background_); }
    if (page_brush_ != nullptr) { DeleteObject(page_brush_); }
}
int DashboardControls::Scale(int value) const { return MulDiv(value, static_cast<int>(dpi_), 96); }
HWND DashboardControls::Find(int id) const {
    for (const auto& control : controls_) { if (control.id == id) { return control.window; } }
    return nullptr;
}
HWND DashboardControls::Add(const wchar_t* class_name, const wchar_t* text, DWORD style, int id, int page) {
    HWND child = CreateWindowExW(0, class_name, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1,
        parent_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    if (child != nullptr) {
        controls_.push_back({child, id, page});
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(body_font_), FALSE);
    }
    return child;
}
HWND DashboardControls::Text(const wchar_t* text, int id, int page) {
    return Add(L"STATIC", text, SS_LEFT | SS_NOPREFIX, id, page);
}
void DashboardControls::TextValue(int id, const std::wstring& text) const {
    HWND window = Find(id);
    if (window == nullptr) { return; }
    std::wstring old(static_cast<std::size_t>(GetWindowTextLengthW(window)) + 1, L'\0');
    GetWindowTextW(window, old.data(), static_cast<int>(old.size())); old.resize(old.size() - 1);
    if (old != text) { SetWindowTextW(window, text.c_str()); }
}

bool DashboardControls::Initialize(HWND parent, UINT dpi) {
    parent_ = parent;
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES};
    if (!InitCommonControlsEx(&common)) { return false; }
    SetDpi(dpi);
    Text(L"", kStatusTitle, -1); Text(L"", kStatusDetail, -1);
    tabs_ = Add(WC_TABCONTROLW, L"AgentLatch pages", WS_TABSTOP | WS_CLIPSIBLINGS, kTabs, -1);
    if (tabs_ == nullptr) { return false; }
    for (const auto* label : {L"Overview", L"Agents", L"Settings"}) {
        TCITEMW item{}; item.mask = TCIF_TEXT; item.pszText = const_cast<wchar_t*>(label);
        TabCtrl_InsertItem(tabs_, TabCtrl_GetItemCount(tabs_), &item);
    }
    // A native tab owns the page border; sibling controls sit above its pane.
    SetWindowPos(tabs_, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    footer_ = Add(STATUSCLASSNAMEW, L"", SBARS_SIZEGRIP, kFooter, -1);
    const int overview = static_cast<int>(DashboardPage::Overview);
    Text(L"Activity", kReasonsTitle, overview);
    HWND count = Text(L"", kReasonsCount, overview);
    SetWindowLongPtrW(count, GWL_STYLE, GetWindowLongPtrW(count, GWL_STYLE) | SS_RIGHT);
    reasons_ = Add(WC_LISTVIEWW, L"Live agent activity, read only", LVS_REPORT |
        LVS_NOSORTHEADER | LVS_SHAREIMAGELISTS, kReasons, overview);
    if (reasons_ == nullptr) { return false; }
    ListView_SetExtendedListViewStyle(reasons_, LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    for (const auto* label : {L"Agent or source", L"Signal", L"Status"}) {
        LVCOLUMNW column{}; column.mask = LVCF_TEXT | LVCF_WIDTH; column.cx = Scale(160);
        column.pszText = const_cast<wchar_t*>(label);
        ListView_InsertColumn(reasons_, Header_GetItemCount(ListView_GetHeader(reasons_)), &column);
    }
    Text(L"No active agents", kEmptyTitle, overview);
    Text(L"New work appears here automatically.", kEmptyHint, overview);
    // This frame and its fields are siblings. Clip the frame's real paint DC
    // against fields above it so a late group repaint cannot erase them.
    Add(L"BUTTON", L"Manual override", BS_GROUPBOX | WS_CLIPSIBLINGS, kManualGroup, overview);
    Text(L"Off", kManualStatus, overview);
    Text(L"Independent of agent tracking.", kManualHint, overview);
    HWND duration = Add(WC_COMBOBOXW, L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
        kManualDuration, overview);
    for (const auto* label : {L"Until Windows restarts", L"1 hour", L"2 hours", L"4 hours",
                             L"8 hours", L"12 hours", L"24 hours", L"Custom hours"}) {
        SendMessageW(duration, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    }
    SendMessageW(duration, CB_SETCURSEL, 0, 0);
    AccessibleName(duration, L"Manual override duration");
    HWND hours = Add(L"EDIT", L"8", WS_TABSTOP | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL,
        kManualHours, overview);
    SendMessageW(hours, EM_SETLIMITTEXT, 3, 0);
    AccessibleName(hours, L"Custom manual duration in hours, 1 to 168");
    EnableWindow(hours, FALSE);
    Text(L"hours", kManualHoursLabel, overview);
    Add(L"BUTTON", L"Start", WS_TABSTOP | BS_PUSHBUTTON, kManualStart, overview);
    Add(L"BUTTON", L"Stop", WS_TABSTOP | BS_PUSHBUTTON, kManualStop, overview);
    const int agents = static_cast<int>(DashboardPage::Agents);
    Text(L"Agent tracking", kAgentsTitle, agents);
    Text(L"Choose when each app can keep your PC awake.", kAgentsHint, agents);
    for (std::size_t index = 0; index < kProviders.size(); ++index) {
        const int offset = static_cast<int>(index);
        Text(kProviderLabels[index], kProviderName + offset, agents);
        Text(L"", kProviderDetail + offset, agents);
        HWND combo = Add(WC_COMBOBOXW, L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
            kModeFirst + offset, agents);
        if (combo == nullptr) { return false; }
        for (const auto* label : kModeLabels) { SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label)); }
        SendMessageW(combo, CB_SETMINVISIBLE, 3, 0);
        AccessibleName(combo, std::wstring(kProviderLabels[index]) + L" tracking mode");
    }
    Text(L"Release delay", kReleaseLabel, agents);
    Text(L"Activity only; hooks/manual timers unchanged.", kReleaseHint, agents);
    HWND release = Add(WC_COMBOBOXW, L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
        kReleaseDelay, agents);
    if (release == nullptr) { return false; }
    for (std::size_t index = 0; index < kReleaseSeconds.size(); ++index) {
        const LRESULT item = SendMessageW(release, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kReleaseLabels[index]));
        SendMessageW(release, CB_SETITEMDATA, static_cast<WPARAM>(item), kReleaseSeconds[index]);
    }
    SendMessageW(release, CB_SETMINVISIBLE, 7, 0);
    AccessibleName(release, L"Release delay after detected task activity");
    const int settings = static_cast<int>(DashboardPage::Settings);
    Text(L"Preferences", kSettingsTitle, settings);
    Text(L"Appearance", kAppearanceLabel, settings);
    HWND appearance = Add(WC_COMBOBOXW, L"", WS_TABSTOP | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
        kAppearance, settings);
    for (const auto* label : {L"Use Windows setting", L"Light", L"Dark"}) {
        SendMessageW(appearance, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    }
    AccessibleName(appearance, L"Appearance");
    const std::array<const wchar_t*, 3> labels = {
        L"Keep the screen on while protected", L"Start AgentLatch when I sign in", L"Show status notifications"};
    const std::array<const wchar_t*, 3> hints = {
        L"When unchecked, only the display may turn off; your PC stays awake.",
        L"Start monitoring automatically at Windows sign-in.",
        L"Notify when protection starts or stops. Failures always notify."};
    for (int index = 0; index < 3; ++index) {
        Add(L"BUTTON", labels[static_cast<std::size_t>(index)], WS_TABSTOP | BS_AUTOCHECKBOX,
            kScreen + index, settings);
        Text(hints[static_cast<std::size_t>(index)], kSettingHint + index, settings);
    }
    Add(L"BUTTON", L"Diagnostic history", WS_TABSTOP | BS_PUSHBUTTON, kDiagnostics, settings);
    Add(L"BUTTON", L"Repair integrations…", WS_TABSTOP | BS_PUSHBUTTON, kRepair, settings);
    Add(L"BUTTON", L"Save status report…", WS_TABSTOP | BS_PUSHBUTTON, kStatusReport, settings);
    for (const auto& control : controls_) {
        wchar_t name[64]{}; GetClassNameW(control.window, name, 64);
        if (control.window == tabs_ || control.window == reasons_ || control.window == footer_ ||
            _wcsicmp(name, L"BUTTON") == 0 || _wcsicmp(name, WC_COMBOBOXW) == 0) {
            SetWindowSubclass(control.window, ThemeProcedure, 2, reinterpret_cast<DWORD_PTR>(this));
        }
    }
    SetWindowSubclass(ListView_GetHeader(reasons_), ThemeProcedure, 2, reinterpret_cast<DWORD_PTR>(this));
    SetDpi(dpi); RefreshTheme(); Layout(); Update(state_);
    return footer_ != nullptr;
}
bool DashboardControls::InitializeDialog(HWND parent, UINT dpi, Appearance appearance) {
    parent_ = parent; state_.settings.appearance = appearance;
    for (HWND child = GetWindow(parent, GW_CHILD); child != nullptr; child = GetWindow(child, GW_HWNDNEXT)) {
        controls_.push_back({child, GetDlgCtrlID(child), 0});
        wchar_t name[64]{}; GetClassNameW(child, name, 64);
        if (_wcsicmp(name, L"BUTTON") == 0) {
            SetWindowSubclass(child, ThemeProcedure, 2, reinterpret_cast<DWORD_PTR>(this));
        }
    }
    SetDpi(dpi); RefreshTheme(); return true;
}

void DashboardControls::SetDpi(UINT dpi) {
    dpi_ = dpi == 0 ? 96 : dpi;
    for (HFONT font : {body_font_, heading_font_, small_font_, title_font_}) {
        if (font != nullptr) { DeleteObject(font); }
    }
    body_font_ = Font(dpi_, 10, FW_NORMAL);
    heading_font_ = Font(dpi_, 10, FW_SEMIBOLD);
    small_font_ = Font(dpi_, 9, FW_NORMAL);
    title_font_ = Font(dpi_, 13, FW_SEMIBOLD);
    for (const auto& control : controls_) {
        HFONT font = body_font_;
        if (control.id == kStatusTitle) { font = title_font_; }
        else if (control.id == kReasonsTitle || control.id == kAgentsTitle || control.id == kSettingsTitle ||
                 control.id == kManualStatus || (control.id >= kProviderName && control.id < kProviderName + 5)) {
            font = heading_font_;
        } else if (control.id == kStatusDetail || control.id == kReasonsCount || control.id == kManualHint ||
                   control.id == kAgentsHint || control.id == kEmptyHint || control.id == kReleaseHint ||
                   (control.id >= kProviderDetail && control.id < kProviderDetail + 5) ||
                   (control.id >= kSettingHint && control.id < kSettingHint + 3)) { font = small_font_; }
        SendMessageW(control.window, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        if ((control.id >= kModeFirst && control.id < kModeFirst + 5) || control.id == kAppearance ||
            control.id == kManualDuration || control.id == kReleaseDelay) {
            SendMessageW(control.window, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), Scale(22));
            SendMessageW(control.window, CB_SETITEMHEIGHT, 0, Scale(22));
        }
    }
    Layout();
}

void DashboardControls::RefreshTheme() {
    palette_ = ResolveDashboardPalette(state_.settings.appearance, WindowsHighContrast());
    if (background_ != nullptr) { DeleteObject(background_); }
    background_ = CreateSolidBrush(palette_.background);
    for (const auto& control : controls_) {
        SetWindowTheme(control.window, palette_.dark ? L"" : control.id == kReasons ? L"Explorer" : nullptr,
            palette_.dark ? L"" : nullptr);
    }
    page_color_ = palette_.page;
    // Ask the active Windows tab theme to render its own pane, then use that
    // exact interior color behind standard labels and checkboxes. No white bars
    // over a gray theme pane, and no hard-coded approximation of the theme.
    HTHEME theme = OpenThemeData(tabs_, L"TAB");
    if (theme != nullptr && !palette_.dark && !WindowsHighContrast()) {
        HDC dc = CreateCompatibleDC(nullptr);
        HBITMAP bitmap = CreateBitmap(32, 32, 1, 32, nullptr);
        if (dc != nullptr && bitmap != nullptr) {
            HGDIOBJ old = SelectObject(dc, bitmap);
            RECT pane{0, 0, 32, 32};
            if (SUCCEEDED(DrawThemeBackground(theme, dc, TABP_PANE, 0, &pane, nullptr))) {
                const COLORREF sampled = GetPixel(dc, 16, 16);
                if (sampled != CLR_INVALID) { page_color_ = sampled; }
            }
            SelectObject(dc, old);
        }
        if (bitmap != nullptr) { DeleteObject(bitmap); }
        if (dc != nullptr) { DeleteDC(dc); }
    }
    if (theme != nullptr) { CloseThemeData(theme); }
    if (page_brush_ != nullptr) { DeleteObject(page_brush_); }
    page_brush_ = CreateSolidBrush(page_color_);
    if (reasons_ != nullptr) {
        ListView_SetBkColor(reasons_, palette_.background);
        ListView_SetTextBkColor(reasons_, palette_.background);
        ListView_SetTextColor(reasons_, palette_.text);
    }
    const BOOL dark_caption = palette_.dark ? TRUE : FALSE;
    DwmSetWindowAttribute(parent_, 20, &dark_caption, sizeof(dark_caption));
    RedrawWindow(parent_, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
    if (parent_ != nullptr) { InvalidateRect(parent_, nullptr, TRUE); }
}

void DashboardControls::Place(int id, int x, int y, int width, int height) {
    MoveWindow(Find(id), x, y, std::max(1, width), std::max(1, height), TRUE);
}
void DashboardControls::Layout() {
    if (parent_ == nullptr || tabs_ == nullptr) { return; }
    RECT client{}; GetClientRect(parent_, &client);
    SendMessageW(footer_, WM_SIZE, 0, 0);
    RECT footer{}; GetWindowRect(footer_, &footer);
    const int footer_height = footer.bottom - footer.top;
    const int parts[] = {std::max(Scale(280), static_cast<int>(client.right) - Scale(112)), -1};
    SendMessageW(footer_, SB_SETPARTS, 2, reinterpret_cast<LPARAM>(parts));
    SendMessageW(footer_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(L"Closing this window keeps monitoring in the tray."));
    const std::wstring version = L"v" + std::wstring(kAgentLatchVersion);
    SendMessageW(footer_, SB_SETTEXTW, 1, reinterpret_cast<LPARAM>(version.c_str()));
    Place(kStatusTitle, Scale(72), Scale(18), client.right - Scale(96), Scale(26));
    Place(kStatusDetail, Scale(72), Scale(48), client.right - Scale(96), Scale(22));
    const int tab_x = Scale(16), tab_y = Scale(88);
    const int tab_width = client.right - Scale(32);
    const int tab_height = client.bottom - footer_height - tab_y - Scale(12);
    Place(kTabs, tab_x, tab_y, tab_width, tab_height);
    RECT pane{0, 0, tab_width, tab_height}; TabCtrl_AdjustRect(tabs_, FALSE, &pane);
    const int left = tab_x + pane.left + Scale(16), top = tab_y + pane.top + Scale(16);
    const int right = tab_x + pane.right - Scale(16), bottom = tab_y + pane.bottom - Scale(16);
    const int width = right - left;
    Place(kReasonsTitle, left, top, width - Scale(140), Scale(24));
    Place(kReasonsCount, right - Scale(140), top + Scale(2), Scale(140), Scale(22));
    const int list_top = top + Scale(32), manual_top = bottom - Scale(116);
    const int list_bottom = manual_top - Scale(14);
    Place(kReasons, left, list_top, width, list_bottom - list_top);
    const int empty_top = list_top + std::max(Scale(26), (list_bottom - list_top) / 2 - Scale(15));
    Place(kEmptyTitle, left + Scale(24), empty_top, width - Scale(48), Scale(22));
    Place(kEmptyHint, left + Scale(24), empty_top + Scale(26), width - Scale(48), Scale(20));
    Place(kManualGroup, left, manual_top, width, Scale(116));
    Place(kManualStatus, left + Scale(16), manual_top + Scale(23), width - Scale(32), Scale(23));
    const bool custom = SendMessageW(Find(kManualDuration), CB_GETCURSEL, 0, 0) == 7;
    Place(kManualDuration, left + Scale(16), manual_top + Scale(51), width - Scale(custom ? 340 : 238), Scale(240));
    Place(kManualHours, right - Scale(316), manual_top + Scale(52), Scale(43), Scale(26));
    Place(kManualHoursLabel, right - Scale(267), manual_top + Scale(55), Scale(38), Scale(22));
    Place(kManualStart, right - Scale(214), manual_top + Scale(50), Scale(126), Scale(30));
    Place(kManualStop, right - Scale(78), manual_top + Scale(50), Scale(62), Scale(30));
    Place(kManualHint, left + Scale(16), manual_top + Scale(87), width - Scale(32), Scale(22));
    Place(kAgentsTitle, left, top, width - Scale(188), Scale(24));
    Place(kAgentsHint, left, top + Scale(24), width - Scale(188), Scale(17));
    Place(kReleaseLabel, right - Scale(176), top, Scale(176), Scale(19));
    Place(kReleaseDelay, right - Scale(176), top + Scale(22), Scale(176), Scale(220));
    Place(kReleaseHint, left, top + Scale(42), width - Scale(188), Scale(17));
    for (int index = 0; index < 5; ++index) {
        const int row = top + Scale(66 + index * 37);
        Place(kProviderName + index, left, row, width - Scale(180), Scale(19));
        Place(kProviderDetail + index, left, row + Scale(19), width - Scale(180), Scale(17));
        Place(kModeFirst + index, right - Scale(156), row + Scale(3), Scale(156), Scale(150));
    }
    Place(kSettingsTitle, left, top, width, Scale(24));
    Place(kAppearanceLabel, left, top + Scale(36), Scale(160), Scale(26));
    Place(kAppearance, right - Scale(200), top + Scale(33), Scale(200), Scale(150));
    for (int index = 0; index < 3; ++index) {
        const int row = top + Scale(76 + index * 52);
        Place(kScreen + index, left, row, width, Scale(24));
        Place(kSettingHint + index, left + Scale(24), row + Scale(27), width - Scale(24), Scale(26));
    }
    Place(kDiagnostics, left, bottom - Scale(31), Scale(144), Scale(30));
    Place(kRepair, left + Scale(156), bottom - Scale(31), Scale(164), Scale(30));
    Place(kStatusReport, left + Scale(332), bottom - Scale(31), Scale(160), Scale(30));
    SetWindowPos(Find(kManualGroup), HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(tabs_, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    for (int id : {kEmptyTitle, kEmptyHint}) {
        SetWindowPos(Find(id), HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    LayoutReasonColumns();
}
void DashboardControls::LayoutReasonColumns() {
    if (reasons_ == nullptr) { return; }
    RECT area{}; GetClientRect(reasons_, &area);
    const int status = Scale(142), signal = Scale(124);
    ListView_SetColumnWidth(reasons_, 0, std::max(Scale(150), static_cast<int>(area.right) - signal - status - 1));
    ListView_SetColumnWidth(reasons_, 1, signal); ListView_SetColumnWidth(reasons_, 2, status);
}
void DashboardControls::ApplyVisibility() {
    for (const auto& control : controls_) {
        const bool visible = (control.page < 0 || control.page == static_cast<int>(page_)) &&
            ((control.id != kEmptyTitle && control.id != kEmptyHint) || activity_.empty()) &&
            ((control.id != kManualHours && control.id != kManualHoursLabel) ||
             SendMessageW(Find(kManualDuration), CB_GETCURSEL, 0, 0) == 7);
        ShowWindow(control.window, visible ? SW_SHOWNA : SW_HIDE);
    }
}
void DashboardControls::SelectPage(DashboardPage page) {
    page_ = page; state_.page = page;
    TabCtrl_SetCurSel(tabs_, static_cast<int>(page)); ApplyVisibility();
    InvalidateRect(parent_, nullptr, FALSE);
}
void DashboardControls::Update(const DashboardState& state) {
    const auto old_appearance = state_.settings.appearance;
    const bool manual_changed = state.manual_active &&
        (!state_.manual_active || state.manual_deadline != state_.manual_deadline);
    state_ = state; state_.page = page_;
    if (manual_changed) {
        manual_syncing_ = true;
        const ULONGLONG remaining = state_.manual_deadline > state_.now ? state_.manual_deadline - state_.now : 0;
        const unsigned int hours = state_.manual_deadline == 0 ? 0 :
            static_cast<unsigned int>(std::min<ULONGLONG>(168, std::max<ULONGLONG>(1, (remaining + 3599999) / 3600000)));
        constexpr unsigned int presets[] = {0, 1, 2, 4, 8, 12, 24};
        int selection = 7;
        for (int index = 0; index < 7; ++index) { if (presets[index] == hours) { selection = index; } }
        SendMessageW(Find(kManualDuration), CB_SETCURSEL, selection, 0);
        if (hours != 0) { TextValue(kManualHours, std::to_wstring(hours)); }
        EnableWindow(Find(kManualHours), selection == 7);
        manual_applied_hours_ = hours;
        manual_feedback_ = manual_pending_;
        manual_pending_ = ManualFeedback::None;
        manual_syncing_ = false;
        Layout();
    }
    if (!state_.manual_active) {
        manual_feedback_ = manual_pending_ = ManualFeedback::None;
    }
    const auto wanted = ResolveDashboardPalette(state_.settings.appearance, WindowsHighContrast());
    if (old_appearance != state_.settings.appearance || wanted.dark != palette_.dark) { RefreshTheme(); }
    if (!SendMessageW(Find(kAppearance), CB_GETDROPPEDSTATE, 0, 0)) {
        SendMessageW(Find(kAppearance), CB_SETCURSEL, static_cast<WPARAM>(state_.settings.appearance), 0);
    }
    const bool grace_only = !state_.latches.empty() && std::all_of(state_.latches.begin(), state_.latches.end(),
        [](const auto& latch) { return latch.in_grace_period; });
    TextValue(kStatusTitle, !state_.system_request_accepted || (!state_.active && !state_.display_request_accepted) ? L"Sleep protection needs attention" :
        state_.active ? (grace_only ? L"Finishing protection" : L"Sleep protection is on") : L"Ready when your agents are");
    std::wstring detail;
    if (!state_.system_request_accepted) {
        detail = L"Windows rejected the request (error " + std::to_wstring(state_.system_request_error) + L"). Retrying.";
    } else if (!state_.active && !state_.display_request_accepted) {
        detail = L"Windows could not release the display request (error " + std::to_wstring(state_.display_request_error) + L"). Retrying.";
    } else if (state_.active) {
        detail = state_.keep_display_on ? L"Your PC and display are staying awake." : L"Your PC stays awake. The display may turn off.";
        if (!state_.display_request_accepted) { detail += L" Display request failed."; }
    } else { detail = L"Normal sleep settings apply. Agent monitoring is on."; }
    TextValue(kStatusDetail, detail);
    UpdateManualControls();
    UpdateReleaseDelay();
    TextValue(kEmptyHint, state_.manual_active ? L"Manual override is keeping your PC awake." : L"New work appears here automatically.");
    for (std::size_t index = 0; index < kProviders.size(); ++index) {
        HWND combo = Find(kModeFirst + static_cast<int>(index));
        const int selection = ModeIndex(state_.settings.ProviderMode(kProviders[index]));
        if (!SendMessageW(combo, CB_GETDROPPEDSTATE, 0, 0) && SendMessageW(combo, CB_GETCURSEL, 0, 0) != selection) {
            SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(selection), 0);
        }
        TextValue(kProviderDetail + static_cast<int>(index), ProviderDetail(kProviders[index], state_));
    }
    SendMessageW(Find(kScreen), BM_SETCHECK, state_.keep_display_on ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(Find(kStartup), BM_SETCHECK, state_.start_with_windows ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(Find(kNotifications), BM_SETCHECK, state_.settings.notifications ? BST_CHECKED : BST_UNCHECKED, 0);
    activity_.clear();
    for (const auto& latch : state_.latches) { if (latch.id != L"manual:keep-awake") { activity_.push_back(latch); } }
    UpdateReasonRows(); LayoutReasonColumns(); ApplyVisibility();
}
void DashboardControls::UpdateReasonRows() {
    std::vector<std::wstring> ids;
    for (const auto& latch : activity_) { ids.push_back(latch.id); }
    const bool changed = ids != row_ids_;
    const int first = ListView_GetTopIndex(reasons_);
    const bool visible = (GetWindowLongPtrW(reasons_, GWL_STYLE) & WS_VISIBLE) != 0;
    if (visible) { SendMessageW(reasons_, WM_SETREDRAW, FALSE, 0); }
    if (changed) { ListView_DeleteAllItems(reasons_); }
    for (std::size_t index = 0; index < activity_.size(); ++index) {
        const auto& latch = activity_[index]; const int row = static_cast<int>(index);
        if (changed) {
            LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = row;
            item.pszText = const_cast<wchar_t*>(latch.label.c_str());
            ListView_InsertItem(reasons_, &item);
        } else {
            SetCell(reasons_, row, 0, latch.label);
        }
        SetCell(reasons_, row, 1, Signal(latch)); SetCell(reasons_, row, 2, LatchSecondaryText(latch, state_.now));
    }
    row_ids_ = std::move(ids);
    if (changed && !activity_.empty()) { ListView_EnsureVisible(reasons_, std::min(first, static_cast<int>(activity_.size()) - 1), FALSE); }
    if (visible) { SendMessageW(reasons_, WM_SETREDRAW, TRUE, 0); }
    InvalidateRect(reasons_, nullptr, FALSE);
    TextValue(kReasonsCount, std::to_wstring(activity_.size()) + (activity_.size() == 1 ? L" source" : L" sources"));
}

void DashboardControls::UpdateReleaseDelay() {
    HWND combo = Find(kReleaseDelay);
    if (SendMessageW(combo, CB_GETDROPPEDSTATE, 0, 0)) { return; }
    const DWORD seconds = std::clamp<DWORD>(state_.settings.activity_grace_seconds, 30, 1800);
    int selected = -1;
    for (std::size_t index = 0; index < kReleaseSeconds.size(); ++index) {
        if (kReleaseSeconds[index] == seconds) { selected = static_cast<int>(index); break; }
    }
    const int custom = static_cast<int>(kReleaseSeconds.size());
    if (selected >= 0) {
        if (SendMessageW(combo, CB_GETCOUNT, 0, 0) > custom) { SendMessageW(combo, CB_DELETESTRING, custom, 0); }
    } else {
        if (SendMessageW(combo, CB_GETCOUNT, 0, 0) > custom &&
            SendMessageW(combo, CB_GETITEMDATA, custom, 0) != seconds) {
            SendMessageW(combo, CB_DELETESTRING, custom, 0);
        }
        if (SendMessageW(combo, CB_GETCOUNT, 0, 0) == custom) {
            const std::wstring label = std::to_wstring(seconds) + L" sec (custom)";
            const LRESULT item = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            SendMessageW(combo, CB_SETITEMDATA, static_cast<WPARAM>(item), seconds);
        }
        selected = custom;
    }
    if (SendMessageW(combo, CB_GETCURSEL, 0, 0) != selected) { SendMessageW(combo, CB_SETCURSEL, selected, 0); }
}

bool DashboardControls::HandleNotify(const NMHDR* notification, LRESULT* result) {
    if (result != nullptr) { *result = 0; }
    if (notification != nullptr && notification->hwndFrom == reasons_ &&
        notification->code == LVN_ITEMCHANGING && result != nullptr) {
        const auto* item = reinterpret_cast<const NMLISTVIEW*>(notification);
        // Also reject accessibility/programmatic selection, not only clicks.
        if ((item->uChanged & LVIF_STATE) && (item->uNewState & (LVIS_SELECTED | LVIS_FOCUSED))) {
            *result = TRUE; return true;
        }
    }
    if (notification != nullptr && notification->hwndFrom == tabs_ && notification->code == TCN_SELCHANGE) {
        SelectPage(static_cast<DashboardPage>(TabCtrl_GetCurSel(tabs_))); return true;
    }
    return false;
}
UiAction DashboardControls::ActionForCommand(WPARAM command) const {
    if (HIWORD(command) != BN_CLICKED) { return UiAction::None; }
    switch (LOWORD(command)) {
        case kManualStart: return UiAction::ApplyManual;
        case kManualStop: return UiAction::StopManual;
        case kScreen: return UiAction::ToggleDisplay;
        case kStartup: return UiAction::ToggleStartup;
        case kNotifications: return UiAction::ToggleNotifications;
        case kDiagnostics: return UiAction::OpenDiagnostics;
        case kRepair: return UiAction::SetupHooks;
        case kStatusReport: return UiAction::SaveStatusReport;
        default: return UiAction::None;
    }
}
bool DashboardControls::ReadManualDuration(unsigned int* hours) const {
    if (hours == nullptr) { return false; }
    constexpr unsigned int presets[] = {0, 1, 2, 4, 8, 12, 24};
    const auto selection = SendMessageW(Find(kManualDuration), CB_GETCURSEL, 0, 0);
    if (selection >= 0 && selection < 7) { *hours = presets[selection]; }
    else if (selection == 7) {
        wchar_t text[4]{};
        if (GetWindowTextLengthW(Find(kManualHours)) > 3) { return false; }
        GetWindowTextW(Find(kManualHours), text, 4);
        unsigned int value = 0;
        bool valid = text[0] != L'\0';
        for (const wchar_t c : std::wstring(text)) {
            if (c < L'0' || c > L'9') { valid = false; break; }
            value = value * 10 + static_cast<unsigned int>(c - L'0');
        }
        if (!valid || value < 1 || value > 168) {
            return false;
        }
        *hours = value;
    } else { return false; }
    return true;
}

void DashboardControls::UpdateManualControls() {
    unsigned int hours = 0;
    const bool valid = ReadManualDuration(&hours);
    const bool changed = valid && hours != manual_applied_hours_;
    TextValue(kManualStatus, !state_.manual_active ? L"Off" : L"On · " +
        (state_.manual_deadline == 0 ? std::wstring(L"Until Windows restarts") : FormatCountdown(state_.manual_deadline, state_.now)));
    TextValue(kManualStart, state_.manual_active ? L"Update duration" : L"Start");
    EnableWindow(Find(kManualStart), valid && (!state_.manual_active || changed));
    EnableWindow(Find(kManualStop), state_.manual_active);
    std::wstring hint;
    if (!valid) { hint = L"Enter a whole number from 1 to 168 hours."; }
    else if (!state_.manual_active) { hint = L"Choose a duration, then Start. Resets when Windows restarts."; }
    else if (changed) { hint = L"Update duration replaces this session starting now. Stop stays available."; }
    else if (manual_feedback_ == ManualFeedback::Started) { hint = L"Started. Manual override is on; no further action is needed."; }
    else if (manual_feedback_ == ManualFeedback::Updated) { hint = L"Duration updated. Manual override is on."; }
    else { hint = L"Manual override is on. Change the duration only if needed."; }
    TextValue(kManualHint, hint);
}

bool DashboardControls::ReadManualCommand(WPARAM command, unsigned int* hours) {
    if (manual_syncing_) { return false; }
    const int id = LOWORD(command);
    if ((id == kManualDuration && HIWORD(command) == CBN_SELCHANGE) ||
        (id == kManualHours && HIWORD(command) == EN_CHANGE)) {
        manual_feedback_ = manual_pending_ = ManualFeedback::None;
        EnableWindow(Find(kManualHours), SendMessageW(Find(kManualDuration), CB_GETCURSEL, 0, 0) == 7);
        UpdateManualControls(); Layout(); ApplyVisibility();
        return false;
    }
    if (id != kManualStart || HIWORD(command) != BN_CLICKED || !ReadManualDuration(hours)) {
        return false;
    }
    // Guard the command too: a disabled/stale/programmatic repeat must not
    // silently restart a running timer or reapply an unlimited session.
    if (state_.manual_active && *hours == manual_applied_hours_) { return false; }
    manual_pending_ = state_.manual_active ? ManualFeedback::Updated : ManualFeedback::Started;
    return true;
}
bool DashboardControls::ReadProviderChange(WPARAM command, Provider* provider, DetectionMode* mode) const {
    const unsigned int id = LOWORD(command);
    if (HIWORD(command) != CBN_SELCHANGE || id < kModeFirst || id >= kModeFirst + 5 || provider == nullptr || mode == nullptr) { return false; }
    const auto selection = SendMessageW(Find(static_cast<int>(id)), CB_GETCURSEL, 0, 0);
    if (selection < 0 || selection > 2) { return false; }
    *provider = kProviders[id - kModeFirst]; *mode = IndexMode(static_cast<int>(selection)); return true;
}
// Windows draws the controls, including hover/pressed/focus/disabled states.
bool DashboardControls::DrawItem(const DRAWITEMSTRUCT* item) const {
    if (item == nullptr || item->CtlType != ODT_COMBOBOX || item->itemID == static_cast<UINT>(-1)) { return false; }
    HBRUSH brush = CreateSolidBrush((item->itemState & ODS_SELECTED) ? palette_.selected : palette_.background);
    FillRect(item->hDC, &item->rcItem, brush); DeleteObject(brush);
    wchar_t label[128]{}; SendMessageW(item->hwndItem, CB_GETLBTEXT, item->itemID, reinterpret_cast<LPARAM>(label));
    HGDIOBJ old = SelectObject(item->hDC, body_font_); RECT rect = item->rcItem; rect.left += Scale(4);
    SetTextColor(item->hDC, (item->itemState & ODS_SELECTED) && !palette_.dark ? GetSysColor(COLOR_HIGHLIGHTTEXT) : palette_.text);
    SetBkMode(item->hDC, TRANSPARENT); DrawTextW(item->hDC, label, -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (item->itemState & ODS_FOCUS) { DrawFocusRect(item->hDC, &rect); }
    SelectObject(item->hDC, old); return true;
}
bool DashboardControls::MeasureItem(MEASUREITEMSTRUCT* item) const {
    if (item != nullptr && item->CtlType == ODT_COMBOBOX) { item->itemHeight = static_cast<UINT>(Scale(22)); return true; }
    return false;
}
HBRUSH DashboardControls::ControlColor(HDC dc, HWND control) const {
    const int id = GetDlgCtrlID(control);
    bool page = false;
    for (const auto& entry : controls_) { if (entry.window == control) { page = entry.page >= 0; break; } }
    if (id == kEmptyTitle || id == kEmptyHint || (id >= kModeFirst && id < kModeFirst + 5)) { page = false; }
    SetTextColor(dc, id == kStatusTitle ? StatusColor(state_) :
        id == kStatusDetail || id == kManualHint || id == kReasonsCount || id == kAgentsHint || id == kEmptyHint || id == kReleaseHint ||
        (id >= kProviderDetail && id < kProviderDetail + 5) || (id >= kSettingHint && id < kSettingHint + 3) ?
        palette_.muted : palette_.text);
    SetBkColor(dc, page ? page_color_ : palette_.background);
    return page ? page_brush_ : background_;
}
bool DashboardControls::ReadAppearanceChange(WPARAM command, Appearance* appearance) const {
    if (LOWORD(command) != kAppearance || HIWORD(command) != CBN_SELCHANGE || appearance == nullptr) { return false; }
    const auto selected = SendMessageW(Find(kAppearance), CB_GETCURSEL, 0, 0);
    if (selected < 0 || selected > 2) { return false; }
    *appearance = static_cast<Appearance>(selected); return true;
}
bool DashboardControls::ReadReleaseDelayChange(WPARAM command, DWORD* seconds) const {
    if (LOWORD(command) != kReleaseDelay || HIWORD(command) != CBN_SELCHANGE || seconds == nullptr) { return false; }
    const LRESULT selection = SendMessageW(Find(kReleaseDelay), CB_GETCURSEL, 0, 0);
    if (selection == CB_ERR) { return false; }
    const LRESULT value = SendMessageW(Find(kReleaseDelay), CB_GETITEMDATA, static_cast<WPARAM>(selection), 0);
    if (value < 30 || value > 1800) { return false; }
    *seconds = static_cast<DWORD>(value); return true;
}
bool DashboardControls::TranslateKey(const MSG& message) {
    if (message.message == WM_KEYDOWN && message.wParam == VK_TAB && (GetKeyState(VK_CONTROL) & 0x8000)) {
        const int direction = (GetKeyState(VK_SHIFT) & 0x8000) ? 2 : 1;
        SelectPage(static_cast<DashboardPage>((static_cast<int>(page_) + direction) % 3)); SetFocus(tabs_); return true;
    }
    return false;
}
HWND DashboardControls::ActionWindow(UiAction action) const {
    switch (action) {
        case UiAction::ManualMenu: return Find(kManualStart);
        case UiAction::StopManual: return Find(kManualStop);
        case UiAction::SetupHooks: return Find(kRepair);
        case UiAction::OpenDiagnostics: return Find(kDiagnostics);
        case UiAction::SaveStatusReport: return Find(kStatusReport);
        default: return nullptr;
    }
}
bool DashboardControls::ValidateLayout() const {
    RECT client{}; GetClientRect(parent_, &client);
    for (const auto& control : controls_) {
        RECT bounds{}; GetWindowRect(control.window, &bounds);
        MapWindowPoints(nullptr, parent_, reinterpret_cast<POINT*>(&bounds), 2);
        if (bounds.left < 0 || bounds.top < 0 || bounds.right > client.right ||
            bounds.bottom > client.bottom || bounds.right <= bounds.left || bounds.bottom <= bounds.top) { return false; }
        const bool visible = (control.page < 0 || control.page == static_cast<int>(page_)) &&
            ((control.id != kEmptyTitle && control.id != kEmptyHint) || activity_.empty()) &&
            ((control.id != kManualHours && control.id != kManualHoursLabel) ||
             SendMessageW(Find(kManualDuration), CB_GETCURSEL, 0, 0) == 7);
        if (((GetWindowLongPtrW(control.window, GWL_STYLE) & WS_VISIBLE) != 0) != visible) { return false; }
    }
    return true;
}

}  // namespace agent_latch
