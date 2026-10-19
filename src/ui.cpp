#include "ui.h"

#include "resource.h"
#include "dashboard_capture.h"
#include "version.h"
#include "win_util.h"

#include <algorithm>
#include <array>
#include <sstream>
#include <uxtheme.h>
#include <initguid.h>
#include <oleacc.h>

namespace agent_latch {
namespace {

constexpr COLORREF kBackground = RGB(11, 14, 18);
constexpr COLORREF kSurface = RGB(20, 24, 30);
constexpr COLORREF kSurfaceRaised = RGB(27, 32, 39);
constexpr COLORREF kBorder = RGB(43, 49, 59);
constexpr COLORREF kText = RGB(244, 247, 250);
constexpr COLORREF kMuted = RGB(157, 166, 178);
constexpr COLORREF kFaint = RGB(128, 139, 155);
constexpr COLORREF kActive = RGB(52, 211, 153);
constexpr COLORREF kActiveDark = RGB(18, 52, 44);
constexpr COLORREF kIdle = RGB(112, 122, 136);
constexpr COLORREF kBlue = RGB(104, 169, 255);
constexpr COLORREF kWarning = RGB(251, 191, 36);

HFONT CreateUiFont(UINT dpi, int points, int weight) {
    return CreateFontW(
        -MulDiv(points, static_cast<int>(dpi), 72),
        0,
        0,
        0,
        weight,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI Variable Text");
}

void FillRectangle(HDC dc, const RECT& rectangle, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rectangle, brush);
    DeleteObject(brush);
}

void FillRoundedRectangle(HDC dc, const RECT& rectangle, int radius, COLORREF fill, COLORREF border) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    RoundRect(dc, rectangle.left, rectangle.top, rectangle.right, rectangle.bottom, radius, radius);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void DrawTextBlock(
    HDC dc,
    const std::wstring& text,
    RECT rectangle,
    HFONT font,
    COLORREF color,
    UINT format = DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX) {
    HGDIOBJ old_font = SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rectangle, format);
    SelectObject(dc, old_font);
}

void DrawStatusDot(HDC dc, int center_x, int center_y, int radius, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    Ellipse(dc, center_x - radius, center_y - radius, center_x + radius, center_y + radius);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}


}  // namespace

std::wstring LatchSecondaryText(const Latch& latch, ULONGLONG now) {
    if (latch.in_grace_period) {
        return L"Releasing in " + FormatDuration(latch.expires_at > now ? latch.expires_at - now : 0);
    }
    if (latch.kind == LatchKind::Manual) {
        return L"Until stopped";
    }
    if (latch.kind == LatchKind::Hook) {
        return latch.instance_count > 1 ? std::to_wstring(latch.instance_count) + L" active" : L"Active";
    }
    if (latch.kind == LatchKind::Detector) {
        if (latch.label.ends_with(L" open")) {
            return L"App open";
        }
        if (latch.detail.find(L"ChatGPT response") != std::wstring::npos &&
            latch.detail.find(L"Codex task") != std::wstring::npos) {
            return L"Codex + ChatGPT";
        }
        if (latch.instance_count > 1) {
            return std::to_wstring(latch.instance_count) + L" tasks running";
        }
        return L"1 task running";
    }
    return FormatCountdown(latch.expires_at, now);
}

const wchar_t* ProviderTrackingStatus(
    Provider provider, DetectionMode mode, const std::vector<Latch>& latches) {
    bool grace_only = false;
    for (const Latch& latch : latches) {
        if (latch.provider != provider) {
            continue;
        }
        if (!latch.in_grace_period) {
            return mode == DetectionMode::Open ? L"OPEN" : L"RUNNING";
        }
        grace_only = true;
    }
    return grace_only ? L"GRACE" : L"";
}

COLORREF ProviderColor(Provider provider) {
    switch (provider) {
        case Provider::Manual:
            return kBlue;
        case Provider::Codex:
            return RGB(16, 185, 129);
        case Provider::ClaudeCode:
            return RGB(249, 115, 22);
        case Provider::Cursor:
            return RGB(167, 139, 250);
        case Provider::OpenCode:
            return RGB(45, 212, 191);
        case Provider::GeminiCli:
            return RGB(96, 165, 250);
        case Provider::External:
            return kMuted;
    }
    return kMuted;
}


namespace {
constexpr int kTabs = 1000;
constexpr int kTitle = 10, kVersion = 11, kFooter = 12, kMore = 13;
constexpr int kStatusTitle = 40, kStatusDetail = 41, kStatusBadge = 42;
constexpr int kManualTitle = 70, kManualStatus = 71, kManualHint = 72;
constexpr int kManualStart = 73, kManualStop = 74, kManualNote = 75;
constexpr int kReasonsTitle = 90, kReasonsCount = 91, kReasons = 92;
constexpr int kPreviousSources = 93, kNextSources = 94;
constexpr int kEmptyTitle = 95, kEmptyHint = 96;
constexpr int kAgentsTitle = 100, kAgentsHint = 101;
constexpr int kSettingsTitle = 430;
constexpr std::size_t kSourcesPerPage = 5;
constexpr int kModeFirst = 300, kProviderName = 310, kProviderDetail = 320;
constexpr int kScreen = 400, kStartup = 401, kNotifications = 402;
constexpr int kSettingHint = 410, kDiagnostics = 420, kRepair = 421;
constexpr std::array<Provider, 5> kProviders = {
    Provider::Codex, Provider::ClaudeCode, Provider::Cursor, Provider::OpenCode, Provider::GeminiCli};
constexpr std::array<const wchar_t*, 5> kProviderLabels = {
    L"Codex / ChatGPT", L"Claude", L"Cursor", L"OpenCode", L"Antigravity / Gemini"};
constexpr std::array<const wchar_t*, 3> kModeLabels = {L"Tasks only", L"While open", L"Off"};

int ModeIndex(DetectionMode mode) {
    return mode == DetectionMode::Tasks ? 0 : mode == DetectionMode::Open ? 1 : 2;
}
DetectionMode IndexMode(int index) {
    return index == 0 ? DetectionMode::Tasks : index == 1 ? DetectionMode::Open : DetectionMode::Off;
}
bool IsStatus(int id) { return id >= kStatusTitle && id <= kStatusBadge; }
bool IsSurface(int id) {
    return (id >= kManualTitle && id <= kManualNote) ||
        (id >= kProviderName && id < kProviderDetail + 5) ||
        (id >= kScreen && id <= kSettingHint + 2);
}
COLORREF StatusFill(const DashboardState& state) {
    return !state.system_request_accepted ? RGB(49, 34, 15) : state.active ? RGB(15, 38, 33) : kSurface;
}
COLORREF BadgeFill(const DashboardState& state) {
    return !state.system_request_accepted ? RGB(71, 43, 12) : state.active ? kActiveDark : kSurfaceRaised;
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
std::wstring ProviderDetail(Provider provider, const DashboardState& state) {
    const DetectionMode mode = state.settings.ProviderMode(provider);
    if (mode == DetectionMode::Off) { return L"Not monitored"; }
    const std::wstring status = ProviderTrackingStatus(provider, mode, state.latches);
    if (status == L"OPEN") { return L"App open · keeping awake"; }
    if (status == L"RUNNING") { return L"Active work detected"; }
    if (status == L"GRACE") {
        for (const auto& latch : state.latches) {
            if (latch.provider == provider && latch.kind == LatchKind::RestartGrace) {
                return L"Waiting for app restart";
            }
        }
        return L"Finishing protection";
    }
    return mode == DetectionMode::Open ? L"Waiting for app to open" : L"Watching for active tasks";
}
}  // namespace

DashboardRenderer::~DashboardRenderer() { ResetResources(); }

void DashboardRenderer::ResetResources() {
    if (brand_icon_ != nullptr) { DestroyIcon(brand_icon_); brand_icon_ = nullptr; }
}
void DashboardRenderer::Initialize(UINT dpi) {
    ResetResources();
    dpi_ = dpi == 0 ? 96 : dpi;
    brand_icon_ = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDI_AGENTLATCH), IMAGE_ICON, Scale(26), Scale(26), LR_DEFAULTCOLOR));
}
int DashboardRenderer::Scale(int value) const {
    return MulDiv(value, static_cast<int>(dpi_), 96);
}
SIZE DashboardRenderer::PreferredClientSize() const { return SIZE{Scale(620), Scale(560)}; }

void DashboardRenderer::Paint(HDC dc, const RECT& client, const DashboardState& state) {
    FillRectangle(dc, client, kBackground);
    const int right = client.right - Scale(20);
    if (brand_icon_ != nullptr) {
        DrawIconEx(dc, Scale(20), Scale(17), brand_icon_, Scale(26), Scale(26), 0, nullptr, DI_NORMAL);
    }
    RECT header_line{Scale(20), Scale(104), right, Scale(105)};
    FillRectangle(dc, header_line, kBorder);
    RECT status{Scale(20), Scale(114), right, Scale(172)};
    FillRoundedRectangle(dc, status, Scale(12), StatusFill(state),
        !state.system_request_accepted ? RGB(120, 78, 20) : state.active ? RGB(31, 91, 72) : kBorder);
    const COLORREF accent = !state.system_request_accepted ? kWarning : state.active ? kActive : kIdle;
    DrawStatusDot(dc, Scale(38), Scale(136), Scale(4), accent);
    if (state.page == DashboardPage::Overview) {
        FillRoundedRectangle(dc, RECT{Scale(20), Scale(222), right, Scale(426)}, Scale(10), kSurface, kBorder);
        FillRoundedRectangle(dc, RECT{Scale(20), Scale(444), right, Scale(516)}, Scale(10), kSurface, kBorder);
    } else if (state.page == DashboardPage::Agents) {
        FillRoundedRectangle(dc, RECT{Scale(20), Scale(234), right, Scale(504)}, Scale(10), kSurface, kBorder);
        for (int index = 1; index < 5; ++index) {
            const int y = Scale(234 + index * 54);
            FillRectangle(dc, RECT{Scale(36), y, right - Scale(16), y + 1}, kBorder);
        }
    } else {
        FillRoundedRectangle(dc, RECT{Scale(20), Scale(234), right, Scale(438)}, Scale(10), kSurface, kBorder);
        for (int index = 1; index < 3; ++index) {
            const int y = Scale(234 + index * 68);
            FillRectangle(dc, RECT{Scale(36), y, right - Scale(16), y + 1}, kBorder);
        }
    }
    FillRectangle(dc, RECT{Scale(20), Scale(530), right, Scale(531)}, kBorder);
}

DashboardControls::~DashboardControls() {
    if (tabs_ != nullptr && IsWindow(tabs_)) { RemoveWindowSubclass(tabs_, TabProcedure, 1); }
    for (HFONT font : {body_font_, heading_font_, small_font_, title_font_}) {
        if (font != nullptr) { DeleteObject(font); }
    }
    for (HBRUSH brush : {background_, surface_, status_brush_, badge_brush_}) {
        if (brush != nullptr) { DeleteObject(brush); }
    }
    if (row_images_ != nullptr) { ImageList_Destroy(row_images_); }
}
int DashboardControls::Scale(int value) const { return MulDiv(value, static_cast<int>(dpi_), 96); }
HWND DashboardControls::Find(int id) const {
    for (const auto& control : controls_) { if (control.id == id) { return control.window; } }
    return nullptr;
}
HWND DashboardControls::Add(const wchar_t* class_name, const wchar_t* text, DWORD style,
                            int id, int page, RECT bounds, bool right_anchor) {
    HWND control = CreateWindowExW(0, class_name, text, WS_CHILD | WS_VISIBLE | style,
        0, 0, 1, 1, parent_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    if (control == nullptr) { return nullptr; }
    controls_.push_back(Control{control, id, page, bounds, right_anchor});
    if (std::wstring(class_name) == L"BUTTON" && (style & BS_TYPEMASK) == BS_OWNERDRAW) {
        SetWindowSubclass(control, ButtonProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
    }
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(body_font_), FALSE);
    return control;
}
HWND DashboardControls::Text(const wchar_t* text, int id, int page, RECT bounds, bool right_anchor) {
    return Add(L"STATIC", text, SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, id, page, bounds, right_anchor);
}
void DashboardControls::TextValue(int id, const std::wstring& value) const {
    HWND control = Find(id);
    if (control == nullptr) { return; }
    const int length = GetWindowTextLengthW(control);
    std::wstring previous(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(control, previous.data(), length + 1);
    previous.resize(static_cast<std::size_t>(length));
    if (previous != value) { SetWindowTextW(control, value.c_str()); }
}
bool DashboardControls::Initialize(HWND parent, UINT dpi) {
    parent_ = parent;
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES};
    if (!InitCommonControlsEx(&common)) { return false; }
    background_ = CreateSolidBrush(kBackground);
    surface_ = CreateSolidBrush(kSurface);
    status_brush_ = CreateSolidBrush(kSurface);
    badge_brush_ = CreateSolidBrush(kSurfaceRaised);
    SetDpi(dpi);
    Text(L"AgentLatch", kTitle, -1, RECT{56, 15, 224, 44});
    Text((L"v" + std::wstring(kAgentLatchVersion)).c_str(), kVersion, -1, RECT{225, 22, 360, 40});
    Add(L"BUTTON", L"More…", WS_TABSTOP | BS_OWNERDRAW, kMore, -1, RECT{526, 18, 600, 44}, true);
    tabs_ = Add(WC_TABCONTROLW, L"AgentLatch pages", WS_TABSTOP | TCS_FIXEDWIDTH,
        kTabs, -1, RECT{20, 64, 360, 100});
    if (tabs_ == nullptr) { return false; }
    for (const wchar_t* label : {L"Overview", L"Agents", L"Settings"}) {
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(label);
        TabCtrl_InsertItem(tabs_, TabCtrl_GetItemCount(tabs_), &item);
    }
    SetWindowSubclass(tabs_, TabProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
    Text(L"", kStatusTitle, -1, RECT{52, 122, 582, 146});
    Text(L"", kStatusDetail, -1, RECT{52, 147, 582, 165});
    Text(L"Monitoring continues in the system tray.", kFooter, -1, RECT{20, 537, 600, 555});

    const int overview = static_cast<int>(DashboardPage::Overview);
    Text(L"Manual override", kManualTitle, overview, RECT{36, 454, 340, 477});
    Text(L"", kManualStatus, overview, RECT{36, 480, 390, 502});
    Add(L"BUTTON", L"Keep awake…", WS_TABSTOP | BS_OWNERDRAW, kManualStart, overview, RECT{404, 463, 504, 493}, true);
    Add(L"BUTTON", L"Stop", WS_TABSTOP | BS_OWNERDRAW, kManualStop, overview, RECT{514, 463, 584, 493}, true);
    Text(L"Agent activity", kReasonsTitle, overview, RECT{20, 190, 280, 214});
    HWND count = Text(L"", kReasonsCount, overview, RECT{318, 193, 600, 214}, true);
    SetWindowLongPtrW(count, GWL_STYLE, GetWindowLongPtrW(count, GWL_STYLE) | SS_RIGHT);
    Add(L"BUTTON", L"‹", WS_TABSTOP | BS_OWNERDRAW, kPreviousSources, overview, RECT{522, 188, 556, 216}, true);
    Add(L"BUTTON", L"›", WS_TABSTOP | BS_OWNERDRAW, kNextSources, overview, RECT{566, 188, 600, 216}, true);
    AccessibleName(Find(kPreviousSources), L"Previous activity page");
    AccessibleName(Find(kNextSources), L"Next activity page");
    reasons_ = Add(WC_LISTVIEWW, L"Active protection reasons", WS_TABSTOP | LVS_REPORT |
        LVS_NOCOLUMNHEADER | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS | LVS_OWNERDRAWFIXED,
        kReasons, overview, RECT{24, 224, 596, 424});
    if (reasons_ == nullptr) { return false; }
    SetWindowTheme(reasons_, L"", L"");
    AccessibleName(reasons_, L"Active protection reasons");
    SetWindowSubclass(reasons_, ReasonsProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
    ListView_SetBkColor(reasons_, kSurface);
    ListView_SetTextBkColor(reasons_, kSurface);
    ListView_SetTextColor(reasons_, kText);
    ListView_SetExtendedListViewStyle(reasons_, LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP);
    LVCOLUMNW column{};
    column.mask = LVCF_WIDTH | LVCF_TEXT;
    column.cx = Scale(338);
    column.pszText = const_cast<wchar_t*>(L"Reason");
    ListView_InsertColumn(reasons_, 0, &column);
    column.cx = Scale(190);
    column.pszText = const_cast<wchar_t*>(L"Status");
    column.fmt = LVCFMT_RIGHT;
    column.mask |= LVCF_FMT;
    ListView_InsertColumn(reasons_, 1, &column);
    HWND empty_title = Text(L"All quiet for now", kEmptyTitle, overview, RECT{36, 292, 584, 318});
    HWND empty_hint = Text(L"", kEmptyHint, overview, RECT{36, 320, 584, 345});
    for (HWND empty : {empty_title, empty_hint}) {
        SetWindowLongPtrW(empty, GWL_STYLE, (GetWindowLongPtrW(empty, GWL_STYLE) & ~SS_TYPEMASK) | SS_CENTER);
    }

    const int agents = static_cast<int>(DashboardPage::Agents);
    Text(L"Agent tracking", kAgentsTitle, agents, RECT{20, 190, 300, 214});
    Text(L"Choose when each app can keep your PC awake.", kAgentsHint, agents, RECT{20, 214, 600, 232});
    for (std::size_t index = 0; index < kProviders.size(); ++index) {
        const int offset = static_cast<int>(index);
        const int top = 234 + offset * 54;
        Text(kProviderLabels[index], kProviderName + offset, agents, RECT{36, top + 8, 372, top + 29});
        Text(L"", kProviderDetail + offset, agents, RECT{36, top + 30, 372, top + 49});
        HWND combo = Add(WC_COMBOBOXW, L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST |
            CBS_OWNERDRAWFIXED | CBS_HASSTRINGS, kModeFirst + offset, agents,
            RECT{410, top + 13, 584, top + 145}, true);
        if (combo == nullptr) { return false; }
        SetWindowTheme(combo, L"", L"");
        for (const wchar_t* mode : kModeLabels) { SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(mode)); }
        AccessibleName(combo, std::wstring(kProviderLabels[index]) + L" tracking mode");
        SetWindowSubclass(combo, ComboProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
    }
    const int settings = static_cast<int>(DashboardPage::Settings);
    Text(L"Preferences", kSettingsTitle, settings, RECT{20, 190, 600, 218});
    const std::array<const wchar_t*, 3> labels = {
        L"Keep the screen on while protected", L"Start AgentLatch when I sign in", L"Show status notifications"};
    const std::array<const wchar_t*, 3> hints = {
        L"Off lets the display turn off. Your PC still stays awake.",
        L"Monitor agents and restore saved manual sessions at sign-in.",
        L"Optional wake alerts. Protection failures always notify you."};
    for (int index = 0; index < 3; ++index) {
        const int top = 246 + index * 68;
        HWND checkbox = Add(L"BUTTON", labels[static_cast<std::size_t>(index)],
            WS_TABSTOP | BS_AUTOCHECKBOX, kScreen + index, settings, RECT{36, top, 584, top + 26});
        SetWindowTheme(checkbox, L"", L"");
        SetWindowSubclass(checkbox, ToggleProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
        Text(hints[static_cast<std::size_t>(index)], kSettingHint + index, settings,
            RECT{36, top + 29, 584, top + 48});
    }
    Add(L"BUTTON", L"Diagnostic history", WS_TABSTOP | BS_OWNERDRAW,
        kDiagnostics, settings, RECT{20, 458, 207, 490});
    Add(L"BUTTON", L"Repair integrations…", WS_TABSTOP | BS_OWNERDRAW,
        kRepair, settings, RECT{220, 458, 421, 490});
    SetDpi(dpi);
    Layout();
    Update(state_);
    return std::all_of(controls_.begin(), controls_.end(), [](const Control& control) {
        return control.window != nullptr;
    });
}
void DashboardControls::SetDpi(UINT dpi) {
    dpi_ = dpi == 0 ? 96 : dpi;
    for (HFONT font : {body_font_, heading_font_, small_font_, title_font_}) {
        if (font != nullptr) { DeleteObject(font); }
    }
    body_font_ = CreateUiFont(dpi_, 10, FW_NORMAL);
    heading_font_ = CreateUiFont(dpi_, 12, FW_SEMIBOLD);
    small_font_ = CreateUiFont(dpi_, 9, FW_NORMAL);
    title_font_ = CreateUiFont(dpi_, 15, FW_SEMIBOLD);
    for (const auto& control : controls_) {
        HFONT font = body_font_;
        if (control.id == kTitle) { font = title_font_; }
        else if (control.id == kStatusTitle || control.id == kManualTitle ||
                 control.id == kReasonsTitle || control.id == kAgentsTitle || control.id == kSettingsTitle || control.id == kEmptyTitle) { font = heading_font_; }
        else if (control.id == kVersion || control.id == kStatusDetail || control.id == kStatusBadge ||
                 control.id == kFooter || control.id == kManualHint || control.id == kManualNote ||
                 control.id == kReasonsCount || control.id == kAgentsHint || control.id == kEmptyHint ||
                 (control.id >= kProviderDetail && control.id < kProviderDetail + 5) ||
                 (control.id >= kSettingHint && control.id <= kSettingHint + 2)) { font = small_font_; }
        SendMessageW(control.window, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        if (control.id >= kModeFirst && control.id < kModeFirst + 5) {
            SendMessageW(control.window, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), Scale(22));
            SendMessageW(control.window, CB_SETITEMHEIGHT, 0, Scale(24));
        }
    }
    if (row_images_ != nullptr) {
        if (reasons_ != nullptr) { ListView_SetImageList(reasons_, nullptr, LVSIL_SMALL); }
        ImageList_Destroy(row_images_);
    }
    row_images_ = ImageList_Create(1, Scale(38), ILC_COLOR32, 1, 1);
    if (reasons_ != nullptr) { ListView_SetImageList(reasons_, row_images_, LVSIL_SMALL); }
    Layout();
}
void DashboardControls::Layout() {
    if (parent_ == nullptr) { return; }
    RECT client{};
    GetClientRect(parent_, &client);
    const int extra = client.right - Scale(620);
    for (const auto& control : controls_) {
        RECT rect{Scale(control.bounds.left), Scale(control.bounds.top),
                  Scale(control.bounds.right), Scale(control.bounds.bottom)};
        if (control.right_anchor) { rect.left += extra; rect.right += extra; }
        else if (control.id == kReasons || control.id == kFooter ||
                 control.id == kStatusDetail) { rect.right += extra; }
        MoveWindow(control.window, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, TRUE);
    }
    if (tabs_ != nullptr) {
        TabCtrl_SetItemSize(tabs_, Scale(110), Scale(34));
    }
    LayoutReasonColumns();
}
void DashboardControls::LayoutReasonColumns() {
    if (reasons_ == nullptr) { return; }
    RECT client{};
    GetClientRect(reasons_, &client);
    const int status_width = Scale(190);
    // The client width already excludes a visible vertical scrollbar. Leave a
    // pixel of slack so rounding cannot create an unwanted horizontal bar.
    ListView_SetColumnWidth(reasons_, 0, std::max(Scale(80), static_cast<int>(client.right) - status_width - 1));
    ListView_SetColumnWidth(reasons_, 1, status_width);
}
void DashboardControls::ApplyVisibility() {
    for (const auto& control : controls_) {
        bool visible = control.page < 0 || control.page == static_cast<int>(page_);
        if ((control.id == kManualStop || control.id == kManualNote) && !state_.manual_active) { visible = false; }
        if ((control.id == kPreviousSources || control.id == kNextSources) && activity_.size() <= kSourcesPerPage) { visible = false; }
        if (control.id == kReasons && activity_.empty()) { visible = false; }
        if ((control.id == kEmptyTitle || control.id == kEmptyHint) && !activity_.empty()) { visible = false; }
        ShowWindow(control.window, visible ? SW_SHOWNA : SW_HIDE);
    }
}
void DashboardControls::SelectPage(DashboardPage page) {
    page_ = page;
    state_.page = page;
    TabCtrl_SetCurSel(tabs_, static_cast<int>(page));
    ApplyVisibility();
    InvalidateRect(tabs_, nullptr, FALSE);
    InvalidateRect(parent_, nullptr, FALSE);
}
void DashboardControls::Update(const DashboardState& state) {
    const COLORREF old_fill = StatusFill(state_);
    const COLORREF old_badge = BadgeFill(state_);
    state_ = state;
    state_.page = page_;
    if (old_fill != StatusFill(state_)) {
        DeleteObject(status_brush_);
        status_brush_ = CreateSolidBrush(StatusFill(state_));
    }
    if (old_badge != BadgeFill(state_)) {
        DeleteObject(badge_brush_);
        badge_brush_ = CreateSolidBrush(BadgeFill(state_));
    }
    const bool error = !state_.system_request_accepted;
    const bool grace_only = !state_.latches.empty() && std::all_of(state_.latches.begin(),
        state_.latches.end(), [](const Latch& latch) { return latch.in_grace_period; });
    TextValue(kStatusTitle, error ? L"Sleep protection needs attention" :
        state_.active ? (grace_only ? L"Finishing protection" : L"Sleep protection is on") : L"Ready when your agents are");
    std::wstring detail;
    if (error) {
        detail = L"Windows rejected the request (error " + std::to_wstring(state_.system_request_error) + L"). Retrying.";
    } else if (state_.active) {
        detail = state_.keep_display_on ? L"PC and screen stay awake while protected." : L"Your PC stays awake. The screen may turn off.";
        if (!state_.display_request_accepted) { detail += L" · screen request failed"; }
    } else { detail = L"Normal sleep settings apply. Enabled agents are being monitored."; }
    TextValue(kStatusDetail, detail);
    TextValue(kManualStatus, !state_.manual_active ? L"Off · keep awake independently of agents" :
        (state_.manual_deadline == 0 ? L"Until stopped" : FormatCountdown(state_.manual_deadline, state_.now)) +
            std::wstring(L" · independent of agents"));
    TextValue(kManualStart, state_.manual_active ? L"Change…" : L"Keep awake…");
    TextValue(kEmptyHint, state_.manual_active ? L"Manual override is keeping your PC awake." : L"Agent activity will appear here when detected.");
    for (std::size_t index = 0; index < kProviders.size(); ++index) {
        HWND combo = Find(kModeFirst + static_cast<int>(index));
        const int selected = ModeIndex(state_.settings.ProviderMode(kProviders[index]));
        if (SendMessageW(combo, CB_GETDROPPEDSTATE, 0, 0) == FALSE &&
            SendMessageW(combo, CB_GETCURSEL, 0, 0) != selected) {
            SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(selected), 0);
        }
        TextValue(kProviderDetail + static_cast<int>(index), ProviderDetail(kProviders[index], state_));
    }
    SendMessageW(Find(kScreen), BM_SETCHECK, state_.keep_display_on ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(Find(kStartup), BM_SETCHECK, state_.start_with_windows ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(Find(kNotifications), BM_SETCHECK, state_.settings.notifications ? BST_CHECKED : BST_UNCHECKED, 0);
    activity_.clear();
    for (const auto& latch : state_.latches) {
        if (latch.id != L"manual:keep-awake") { activity_.push_back(latch); }
    }
    UpdateReasonRows();
    LayoutReasonColumns();
    ApplyVisibility();
}
void DashboardControls::ChangeReasonPage(int direction) {
    const std::size_t pages = (activity_.size() + kSourcesPerPage - 1) / kSourcesPerPage;
    if (direction < 0 && reason_page_ > 0) { --reason_page_; }
    if (direction > 0 && reason_page_ + 1 < pages) { ++reason_page_; }
    UpdateReasonRows();
    HWND focus = GetFocus();
    if ((focus == Find(kPreviousSources) || focus == Find(kNextSources)) && !IsWindowEnabled(focus)) { SetFocus(reasons_); }
}
void DashboardControls::UpdateReasonRows() {
    const std::size_t pages = std::max<std::size_t>(1, (activity_.size() + kSourcesPerPage - 1) / kSourcesPerPage);
    reason_page_ = std::min(reason_page_, pages - 1);
    const std::size_t begin = reason_page_ * kSourcesPerPage;
    const std::size_t end = std::min(begin + kSourcesPerPage, activity_.size());
    std::vector<std::pair<std::wstring, std::wstring>> rows;
    for (std::size_t index = begin; index < end; ++index) {
        rows.emplace_back(activity_[index].label, LatchSecondaryText(activity_[index], state_.now));
    }
    if (rows.empty()) { rows.emplace_back(L"No agent activity", state_.manual_active ? L"Manual override is keeping your PC awake." : L"New agent work will appear here automatically."); }
    if (ListView_GetItemCount(reasons_) != static_cast<int>(rows.size())) { ListView_DeleteAllItems(reasons_); }
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const int row = static_cast<int>(index);
        if (ListView_GetItemCount(reasons_) <= row) {
            LVITEMW item{};
            item.mask = LVIF_TEXT; item.iItem = row; item.pszText = rows[index].first.data();
            ListView_InsertItem(reasons_, &item);
        } else { ListView_SetItemText(reasons_, row, 0, rows[index].first.data()); }
        ListView_SetItemText(reasons_, row, 1, rows[index].second.data());
    }
    const bool paged = pages > 1;
    TextValue(kReasonsCount, std::to_wstring(activity_.size()) + (activity_.size() == 1 ? L" source" : L" sources") +
        (paged ? L" · " + std::to_wstring(begin + 1) + L"–" + std::to_wstring(end) : L""));
    RECT client{}; GetClientRect(parent_, &client);
    MoveWindow(Find(kManualStart), client.right - Scale(216), Scale(463),
        Scale(state_.manual_active ? 100 : 180), Scale(30), TRUE);
    MoveWindow(Find(kReasonsCount), Scale(300), Scale(193),
        client.right - Scale(paged ? 410 : 320), Scale(21), TRUE);
    EnableWindow(Find(kPreviousSources), reason_page_ > 0);
    EnableWindow(Find(kNextSources), reason_page_ + 1 < pages);
    // Do not reset list selection, keyboard focus or the current page on monitoring ticks.
    InvalidateRect(reasons_, nullptr, FALSE);
    ApplyVisibility();
}
bool DashboardControls::HandleNotify(const NMHDR* notification) {
    if (notification != nullptr && notification->hwndFrom == tabs_ && notification->code == TCN_SELCHANGE) {
        SelectPage(static_cast<DashboardPage>(TabCtrl_GetCurSel(tabs_)));
        return true;
    }
    return false;
}
UiAction DashboardControls::ActionForCommand(WPARAM command) const {
    if (HIWORD(command) != BN_CLICKED) { return UiAction::None; }
    switch (LOWORD(command)) {
        case kMore: return UiAction::MoreMenu;
        case kManualStart: return UiAction::ManualMenu;
        case kManualStop: return UiAction::StopManual;
        case kScreen: return UiAction::ToggleDisplay;
        case kStartup: return UiAction::ToggleStartup;
        case kNotifications: return UiAction::ToggleNotifications;
        case kDiagnostics: return UiAction::OpenDiagnostics;
        case kRepair: return UiAction::SetupHooks;
        case kPreviousSources: return UiAction::PreviousSources;
        case kNextSources: return UiAction::NextSources;
        default: return UiAction::None;
    }
}
bool DashboardControls::ReadProviderChange(WPARAM command, Provider* provider, DetectionMode* mode) const {
    const unsigned int id = LOWORD(command);
    if (HIWORD(command) != CBN_SELCHANGE || id < kModeFirst || id >= kModeFirst + 5 ||
        provider == nullptr || mode == nullptr) { return false; }
    const int selection = static_cast<int>(SendMessageW(Find(static_cast<int>(id)), CB_GETCURSEL, 0, 0));
    if (selection < 0 || selection > 2) { return false; }
    *provider = kProviders[id - kModeFirst];
    *mode = IndexMode(selection);
    return true;
}
bool DashboardControls::DrawItem(const DRAWITEMSTRUCT* item) const {
    if (item == nullptr) { return false; }
    if (item->CtlType == ODT_LISTVIEW && item->CtlID == kReasons) {
        const bool selected = (item->itemState & ODS_SELECTED) != 0;
        FillRectangle(item->hDC, item->rcItem, selected ? kSurfaceRaised : kSurface);
        RECT label = item->rcItem;
        if (activity_.empty()) {
            label.left += Scale(14); label.top += Scale(3); label.bottom = label.top + Scale(20);
            DrawTextBlock(item->hDC, L"No agent activity", label, body_font_, kText);
            label.top += Scale(20); label.bottom += Scale(17);
            DrawTextBlock(item->hDC, state_.manual_active ? L"Manual override is keeping your PC awake." :
                L"New agent work will appear here automatically.", label, small_font_, kMuted);
            return true;
        }
        const std::size_t index = reason_page_ * kSourcesPerPage + item->itemID;
        if (index >= activity_.size()) { return true; }
        const Latch& source = activity_[index];
        if (item->itemID > 0) {
            FillRectangle(item->hDC, RECT{item->rcItem.left + Scale(30), item->rcItem.top,
                item->rcItem.right - Scale(12), item->rcItem.top + 1}, RGB(31, 38, 47));
        }
        const COLORREF accent = source.in_grace_period ? kWarning : ProviderColor(source.provider);
        DrawStatusDot(item->hDC, item->rcItem.left + Scale(15), item->rcItem.top + Scale(18), Scale(3), accent);
        label.left += Scale(30); label.right -= Scale(155);
        label.top += Scale(3); label.bottom = label.top + Scale(20);
        DrawTextBlock(item->hDC, source.label, label, body_font_, kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        const wchar_t* kind = source.kind == LatchKind::RestartGrace ? L"Update / restart grace" :
            source.in_grace_period ? L"Finishing · release grace" :
            source.kind == LatchKind::Hook ? L"Agent task" : source.kind == LatchKind::External ? L"External request" :
            source.label.ends_with(L" open") ? L"App presence" : L"Activity detected";
        label.top += Scale(20); label.bottom += Scale(17);
        DrawTextBlock(item->hDC, kind, label, small_font_, kMuted);
        RECT status = item->rcItem;
        status.left = status.right - Scale(150); status.right -= Scale(12);
        DrawTextBlock(item->hDC, LatchSecondaryText(source, state_.now), status, small_font_, accent,
            DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        if ((item->itemState & ODS_FOCUS) != 0) {
            RECT focus = item->rcItem; InflateRect(&focus, -Scale(2), -Scale(2)); DrawFocusRect(item->hDC, &focus);
        }
        return true;
    }
    if (item->CtlType == ODT_COMBOBOX && item->CtlID >= kModeFirst && item->CtlID < kModeFirst + 5) {
        const bool selected = (item->itemState & ODS_SELECTED) != 0;
        FillRectangle(item->hDC, item->rcItem, selected ? kSurfaceRaised : kSurface);
        if (item->itemID < kModeLabels.size()) {
            RECT text = item->rcItem; text.left += Scale(10);
            DrawTextBlock(item->hDC, kModeLabels[item->itemID], text, body_font_, selected ? kBlue : kText);
        }
        if ((item->itemState & ODS_FOCUS) != 0) { DrawFocusRect(item->hDC, &item->rcItem); }
        return true;
    }
    if (item->CtlType != ODT_BUTTON) { return false; }
    const bool primary = item->CtlID == kManualStart && !state_.manual_active;
    const bool pressed = (item->itemState & ODS_SELECTED) != 0;
    const bool disabled = (item->itemState & ODS_DISABLED) != 0 || !IsWindowEnabled(item->hwndItem);
    const bool hovered = !disabled && GetPropW(item->hwndItem, L"AgentLatch.Hover") != nullptr;
    FillRectangle(item->hDC, item->rcItem, IsSurface(static_cast<int>(item->CtlID)) ? kSurface : kBackground);
    FillRoundedRectangle(item->hDC, item->rcItem, Scale(7),
        primary ? (pressed || hovered ? RGB(24, 64, 53) : RGB(21, 54, 44)) : pressed ? kBorder : hovered ? RGB(34, 43, 54) : kSurfaceRaised,
        hovered ? RGB(75, 90, 108) : primary ? RGB(43, 127, 98) : kBorder);
    wchar_t label[128]{};
    GetWindowTextW(item->hwndItem, label, static_cast<int>(std::size(label)));
    DrawTextBlock(item->hDC, label, item->rcItem, body_font_, disabled ? kFaint : primary ? kActive : kText,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    if ((item->itemState & ODS_FOCUS) != 0) {
        RECT focus = item->rcItem; InflateRect(&focus, -Scale(4), -Scale(4));
        DrawFocusRect(item->hDC, &focus);
    }
    return true;
}
bool DashboardControls::MeasureItem(MEASUREITEMSTRUCT* item) const {
    if (item != nullptr && item->CtlType == ODT_LISTVIEW && item->CtlID == kReasons) {
        item->itemHeight = static_cast<UINT>(Scale(40)); return true;
    }
    if (item != nullptr && item->CtlType == ODT_COMBOBOX) {
        item->itemHeight = static_cast<UINT>(Scale(24));
        return true;
    }
    return false;
}
HBRUSH DashboardControls::ControlColor(HDC dc, HWND control) const {
    const int id = GetDlgCtrlID(control);
    COLORREF color = kText;
    if (id == kStatusTitle || id == kStatusBadge) {
        color = !state_.system_request_accepted ? kWarning : state_.active ? kActive : kMuted;
    } else if (id == kManualStatus) { color = state_.manual_active ? kBlue : kMuted; }
    else if (id == kVersion || id == kFooter || id == kReasonsCount || id == kAgentsHint) { color = kFaint; }
    else if (id == kStatusDetail || id == kManualHint || id == kManualNote || id == kEmptyHint ||
             (id >= kProviderDetail && id < kProviderDetail + 5) ||
             (id >= kSettingHint && id <= kSettingHint + 2)) { color = kMuted; }
    if (id >= kProviderDetail && id < kProviderDetail + 5) {
        const Provider provider = kProviders[static_cast<std::size_t>(id - kProviderDetail)];
        const std::wstring status = ProviderTrackingStatus(provider, state_.settings.ProviderMode(provider), state_.latches);
        if (status == L"RUNNING" || status == L"OPEN") { color = kActive; }
        else if (status == L"GRACE") { color = kWarning; }
    }
    SetTextColor(dc, color);
    const COLORREF background = id == kStatusBadge ? BadgeFill(state_) :
        IsStatus(id) ? StatusFill(state_) : (IsSurface(id) || id == kEmptyTitle || id == kEmptyHint) ? kSurface : kBackground;
    SetBkColor(dc, background);
    SetBkMode(dc, TRANSPARENT);
    return id == kStatusBadge ? badge_brush_ : IsStatus(id) ? status_brush_ :
        (IsSurface(id) || id == kEmptyTitle || id == kEmptyHint) ? surface_ : background_;
}
HWND DashboardControls::ActionWindow(UiAction action) const {
    return Find(action == UiAction::ManualMenu ? kManualStart : action == UiAction::MoreMenu ? kMore : 0);
}
void DashboardControls::PaintCombo(HWND window, HDC dc) const {
    RECT bounds{}; GetClientRect(window, &bounds);
    FillRectangle(dc, bounds, kSurface);
    FillRoundedRectangle(dc, bounds, Scale(6), kSurfaceRaised, GetFocus() == window ? kBlue : kBorder);
    const int selected = static_cast<int>(SendMessageW(window, CB_GETCURSEL, 0, 0));
    if (selected >= 0 && selected < 3) {
        RECT text = bounds; text.left += Scale(10); text.right -= Scale(28);
        DrawTextBlock(dc, kModeLabels[static_cast<std::size_t>(selected)], text, body_font_, kText);
    }
    const int x = bounds.right - Scale(15), y = (bounds.bottom - bounds.top) / 2;
    HPEN pen = CreatePen(PS_SOLID, Scale(1), kMuted);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    MoveToEx(dc, x - Scale(4), y - Scale(2), nullptr);
    LineTo(dc, x, y + Scale(2)); LineTo(dc, x + Scale(4), y - Scale(2));
    SelectObject(dc, old_pen); DeleteObject(pen);
}
LRESULT CALLBACK DashboardControls::ComboProcedure(HWND window, UINT message, WPARAM wparam,
    LPARAM lparam, UINT_PTR, DWORD_PTR reference) {
    auto* controls = reinterpret_cast<DashboardControls*>(reference);
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint);
        controls->PaintCombo(window, dc); EndPaint(window, &paint); return 0;
    }
    if (message == WM_PRINTCLIENT) { controls->PaintCombo(window, reinterpret_cast<HDC>(wparam)); return 0; }
    if (message == WM_ERASEBKGND) { return 1; }
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS || message == CB_SETCURSEL) {
        InvalidateRect(window, nullptr, FALSE);
    }
    return DefSubclassProc(window, message, wparam, lparam);
}
void DashboardControls::PaintTabs(HDC dc) const {
    RECT bounds{}; GetClientRect(tabs_, &bounds);
    FillRectangle(dc, bounds, kBackground);
    for (int index = 0; index < 3; ++index) {
        RECT item{}; TabCtrl_GetItemRect(tabs_, index, &item);
        const bool selected = index == static_cast<int>(page_);
        if (selected) {
            RECT fill = item; InflateRect(&fill, -Scale(2), -Scale(2));
            FillRoundedRectangle(dc, fill, Scale(6), kSurfaceRaised, kSurfaceRaised);
        }
        const wchar_t* label = index == 0 ? L"Overview" : index == 1 ? L"Agents" : L"Settings";
        DrawTextBlock(dc, label, item, body_font_, selected ? kText : kMuted,
            DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        if (selected && GetFocus() == tabs_) {
            InflateRect(&item, -Scale(5), -Scale(5)); DrawFocusRect(dc, &item);
        }
    }
}
void DashboardControls::PaintToggle(HWND window, HDC dc) const {
    RECT bounds{}; GetClientRect(window, &bounds);
    FillRectangle(dc, bounds, kSurface);
    const bool checked = SendMessageW(window, BM_GETCHECK, 0, 0) == BST_CHECKED;
    wchar_t label[128]{}; GetWindowTextW(window, label, static_cast<int>(std::size(label)));
    RECT text = bounds; text.right -= Scale(105);
    DrawTextBlock(dc, label, text, body_font_, kText);
    RECT value = bounds; value.left = value.right - Scale(90); value.right -= Scale(48);
    DrawTextBlock(dc, checked ? L"On" : L"Off", value, small_font_, checked ? kActive : kMuted,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    RECT track{bounds.right - Scale(38), Scale(3), bounds.right, Scale(23)};
    FillRoundedRectangle(dc, track, Scale(20), checked ? RGB(27, 113, 87) : kBorder,
        GetFocus() == window ? kBlue : checked ? RGB(27, 113, 87) : kFaint);
    DrawStatusDot(dc, track.left + Scale(checked ? 28 : 10), Scale(13), Scale(7), checked ? kActive : kMuted);
    if (GetFocus() == window) { RECT focus = bounds; InflateRect(&focus, -1, -1); DrawFocusRect(dc, &focus); }
}
LRESULT CALLBACK DashboardControls::ToggleProcedure(HWND window, UINT message, WPARAM wparam,
    LPARAM lparam, UINT_PTR, DWORD_PTR reference) {
    auto* controls = reinterpret_cast<DashboardControls*>(reference);
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint);
        controls->PaintToggle(window, dc); EndPaint(window, &paint); return 0;
    }
    if (message == WM_PRINTCLIENT) { controls->PaintToggle(window, reinterpret_cast<HDC>(wparam)); return 0; }
    if (message == WM_ERASEBKGND) { return 1; }
    const LRESULT result = DefSubclassProc(window, message, wparam, lparam);
    if (message == BM_SETCHECK || message == WM_SETFOCUS || message == WM_KILLFOCUS || message == WM_UPDATEUISTATE) {
        InvalidateRect(window, nullptr, FALSE);
    }
    return result;
}
LRESULT CALLBACK DashboardControls::ReasonsProcedure(HWND window, UINT message, WPARAM wparam,
    LPARAM lparam, UINT_PTR, DWORD_PTR reference) {
    auto* controls = reinterpret_cast<DashboardControls*>(reference);
    if (message == WM_KEYDOWN && (wparam == VK_NEXT || wparam == VK_PRIOR)) {
        controls->ChangeReasonPage(wparam == VK_NEXT ? 1 : -1); return 0;
    }
    if (message == WM_MOUSEWHEEL) {
        controls->ChangeReasonPage(GET_WHEEL_DELTA_WPARAM(wparam) < 0 ? 1 : -1); return 0;
    }
    return DefSubclassProc(window, message, wparam, lparam);
}
LRESULT CALLBACK DashboardControls::ButtonProcedure(HWND window, UINT message, WPARAM wparam,
    LPARAM lparam, UINT_PTR, DWORD_PTR) {
    if (message == WM_MOUSEMOVE && GetPropW(window, L"AgentLatch.Hover") == nullptr) {
        SetPropW(window, L"AgentLatch.Hover", reinterpret_cast<HANDLE>(1));
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0}; TrackMouseEvent(&track);
        InvalidateRect(window, nullptr, FALSE);
    }
    if (message == WM_MOUSELEAVE || message == WM_ENABLE || message == WM_NCDESTROY) {
        RemovePropW(window, L"AgentLatch.Hover"); InvalidateRect(window, nullptr, FALSE);
    }
    return DefSubclassProc(window, message, wparam, lparam);
}
LRESULT CALLBACK DashboardControls::TabProcedure(HWND window, UINT message, WPARAM wparam,
                                                  LPARAM lparam, UINT_PTR, DWORD_PTR reference) {
    auto* controls = reinterpret_cast<DashboardControls*>(reference);
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint);
        controls->PaintTabs(dc); EndPaint(window, &paint); return 0;
    }
    if (message == WM_PRINTCLIENT) { controls->PaintTabs(reinterpret_cast<HDC>(wparam)); return 0; }
    if (message == WM_ERASEBKGND) { return 1; }
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS) { InvalidateRect(window, nullptr, FALSE); }
    return DefSubclassProc(window, message, wparam, lparam);
}
bool DashboardControls::TranslateKey(const MSG& message) {
    if (message.message == WM_KEYDOWN && message.wParam == VK_TAB && (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
        const int direction = (GetKeyState(VK_SHIFT) & 0x8000) != 0 ? 2 : 1;
        SelectPage(static_cast<DashboardPage>((static_cast<int>(page_) + direction) % 3));
        SetFocus(tabs_);
        return true;
    }
    return false;
}
bool DashboardControls::ValidateLayout() const {
    RECT client{}; GetClientRect(parent_, &client);
    for (const auto& control : controls_) {
        RECT bounds{}; GetWindowRect(control.window, &bounds);
        MapWindowPoints(nullptr, parent_, reinterpret_cast<POINT*>(&bounds), 2);
        // Drop-down lists extend beyond their collapsed control bounds by design.
        if (bounds.left < 0 || bounds.top < 0 || bounds.right > client.right ||
            (control.id < kModeFirst || control.id >= kModeFirst + 5 ? bounds.bottom > client.bottom : false)) {
            return false;
        }
        if (control.page >= 0) {
            const bool expected = control.page == static_cast<int>(page_) &&
                ((control.id != kManualStop && control.id != kManualNote) || state_.manual_active) &&
                ((control.id != kPreviousSources && control.id != kNextSources) || activity_.size() > kSourcesPerPage) &&
                (control.id != kReasons || !activity_.empty()) &&
                ((control.id != kEmptyTitle && control.id != kEmptyHint) || activity_.empty());
            if (((GetWindowLongPtrW(control.window, GWL_STYLE) & WS_VISIBLE) != 0) != expected) { return false; }
        }
    }
    return true;
}

namespace {
// Seed each independent child surface, rather than relying on a pre-painted
// parent. A full dashboard export alone can hide unpainted rounded corners.
bool ControlPaintCoversBackground(const DashboardControls& controls, HWND window,
                                 COLORREF backdrop, bool button) {
    RECT bounds{};
    GetClientRect(window, &bounds);
    HDC dc = CreateCompatibleDC(nullptr);
    if (dc == nullptr) { return false; }
    HBITMAP bitmap = CreateBitmap(bounds.right, bounds.bottom, 1, 32, nullptr);
    if (bitmap == nullptr) { DeleteDC(dc); return false; }
    HGDIOBJ previous = SelectObject(dc, bitmap);
    bool valid = true;
    for (COLORREF seed : {RGB(255, 255, 255), RGB(255, 0, 255)}) {
        for (UINT flags : {0u, static_cast<UINT>(ODS_DISABLED), static_cast<UINT>(ODS_SELECTED), static_cast<UINT>(ODS_FOCUS),
                           static_cast<UINT>(ODS_SELECTED | ODS_FOCUS)}) {
            FillRectangle(dc, bounds, seed);
            if (button) {
                DRAWITEMSTRUCT item{};
                item.CtlType = ODT_BUTTON;
                item.CtlID = static_cast<UINT>(GetDlgCtrlID(window));
                item.itemAction = ODA_DRAWENTIRE;
                item.itemState = flags;
                item.hwndItem = window;
                item.hDC = dc;
                item.rcItem = bounds;
                valid = controls.DrawItem(&item) && valid;
            } else {
                SendMessageW(window, flags == 0 ? WM_PRINT : WM_PRINTCLIENT,
                    reinterpret_cast<WPARAM>(dc), PRF_CLIENT | PRF_ERASEBKGND);
            }
            for (POINT corner : {POINT{0, 0}, POINT{bounds.right - 1, 0},
                                 POINT{0, bounds.bottom - 1}, POINT{bounds.right - 1, bounds.bottom - 1}}) {
                valid = GetPixel(dc, corner.x, corner.y) == backdrop && valid;
            }
        }
    }
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    return valid;
}
}  // namespace

bool RunDashboardContractTests(const std::wstring& output_directory) {
    // Real child controls, without showing a desktop window or touching settings.
    for (UINT dpi : {96u, 120u, 144u, 168u, 192u}) {
        DashboardRenderer renderer; renderer.Initialize(dpi);
        const SIZE size = renderer.PreferredClientSize();
        HWND parent = CreateWindowExW(0, L"STATIC", L"AgentLatch UI contract", WS_POPUP,
            0, 0, size.cx, size.cy, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (parent == nullptr) { return false; }
        bool valid = true;
        {
            DashboardControls controls;
            valid = controls.Initialize(parent, dpi);
            DashboardState state;
            state.active = true;
            state.manual_active = true;
            state.now = 1000;
            state.manual_deadline = 28801000;
            for (int index = 0; index < 8; ++index) {
                Latch latch;
                latch.kind = LatchKind::Hook; latch.provider = Provider::Codex;
                latch.label = L"Concurrent task " + std::to_wstring(index);
                state.latches.push_back(latch);
            }
            controls.Update(state);
            valid = ControlPaintCoversBackground(controls, controls.TabWindow(), kBackground, false) && valid;
            for (int id : {kMore, kManualStart, kManualStop, kDiagnostics, kRepair, kPreviousSources, kNextSources}) {
                valid = ControlPaintCoversBackground(controls, GetDlgItem(parent, id),
                    IsSurface(id) ? kSurface : kBackground, true) && valid;
            }
            for (int id = kModeFirst; id < kModeFirst + 5; ++id) {
                valid = ControlPaintCoversBackground(controls, GetDlgItem(parent, id), kSurface, false) && valid;
            }
            for (int id : {kScreen, kStartup, kNotifications}) {
                HWND toggle = GetDlgItem(parent, id);
                for (WPARAM checked : {static_cast<WPARAM>(BST_UNCHECKED), static_cast<WPARAM>(BST_CHECKED)}) {
                    SendMessageW(toggle, BM_SETCHECK, checked, 0);
                    valid = SendMessageW(toggle, BM_GETCHECK, 0, 0) == static_cast<LRESULT>(checked) &&
                        ControlPaintCoversBackground(controls, toggle, kSurface, false) && valid;
                }
            }
            for (DashboardPage page : {DashboardPage::Overview, DashboardPage::Agents, DashboardPage::Settings}) {
                controls.SelectPage(page);
                valid = controls.ValidateLayout() && valid;
            }
            valid = TabCtrl_GetItemCount(controls.TabWindow()) == 3 && valid;
            RECT tab_bounds{}, last_tab{};
            GetClientRect(controls.TabWindow(), &tab_bounds);
            TabCtrl_GetItemRect(controls.TabWindow(), 2, &last_tab);
            valid = last_tab.right < tab_bounds.right && valid;
            for (std::size_t index = 0; index < kProviders.size(); ++index) {
                const int id = kModeFirst + static_cast<int>(index);
                HWND combo = GetDlgItem(parent, id);
                for (int selection = 0; selection < 3; ++selection) {
                    SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(selection), 0);
                    Provider provider{}; DetectionMode mode{};
                    valid = controls.ReadProviderChange(MAKEWPARAM(id, CBN_SELCHANGE), &provider, &mode) &&
                        provider == kProviders[index] && mode == IndexMode(selection) && valid;
                }
            }
            HWND reasons = GetDlgItem(parent, kReasons);
            valid = ListView_GetItemCount(reasons) == 5 && ListView_GetCountPerPage(reasons) >= 5 && valid;
            valid = (GetWindowLongPtrW(reasons, GWL_STYLE) & (WS_HSCROLL | WS_VSCROLL)) == 0 && valid;
            controls.ChangeReasonPage(1);
            wchar_t last_page_label[64]{};
            ListView_GetItemText(reasons, 0, 0, last_page_label, static_cast<int>(std::size(last_page_label)));
            valid = ListView_GetItemCount(reasons) == 3 && std::wstring(last_page_label) == L"Concurrent task 5" && valid;
            controls.Update(state);
            ListView_GetItemText(reasons, 0, 0, last_page_label, static_cast<int>(std::size(last_page_label)));
            valid = std::wstring(last_page_label) == L"Concurrent task 5" && valid;
            controls.ChangeReasonPage(-1);
            valid = ListView_GetItemCount(reasons) == 5 && valid;
            state.latches.clear(); state.manual_active = false; state.active = false;
            controls.Update(state); controls.SelectPage(DashboardPage::Overview);
            valid = controls.ValidateLayout() && ListView_GetItemCount(GetDlgItem(parent, kReasons)) == 1 && valid;
            DashboardState manual_only = state;
            manual_only.manual_active = true; manual_only.active = true;
            Latch manual_source; manual_source.provider = Provider::Manual; manual_source.kind = LatchKind::Manual;
            manual_source.id = L"manual:keep-awake";
            manual_source.label = L"Manual keep-awake"; manual_only.latches.push_back(manual_source);
            controls.Update(manual_only);
            valid = controls.ValidateLayout() && (GetWindowLongPtrW(reasons, GWL_STYLE) & WS_VISIBLE) == 0 &&
                (GetWindowLongPtrW(GetDlgItem(parent, kManualStop), GWL_STYLE) & WS_VISIBLE) != 0 && valid;
            Latch custom_manual = manual_source;
            custom_manual.id = L"external:custom-manual";
            custom_manual.kind = LatchKind::External;
            custom_manual.label = L"Custom keep-awake request";
            manual_only.latches.push_back(custom_manual);
            controls.Update(manual_only);
            wchar_t custom_label[64]{};
            ListView_GetItemText(reasons, 0, 0, custom_label, static_cast<int>(std::size(custom_label)));
            valid = controls.ValidateLayout() && ListView_GetItemCount(reasons) == 1 &&
                std::wstring(custom_label) == custom_manual.label &&
                (GetWindowLongPtrW(reasons, GWL_STYLE) & WS_VISIBLE) != 0 && valid;
            controls.Update(state);
            if (!output_directory.empty()) {
                state.manual_active = true; state.active = true;
                Latch manual; manual.provider = Provider::Manual; manual.kind = LatchKind::Timer;
                manual.id = L"manual:keep-awake";
                manual.label = L"Manual keep-awake"; manual.expires_at = state.manual_deadline;
                state.latches.push_back(manual);
                valid = ExportDashboardScreenshots(state, JoinPath(output_directory, std::to_wstring(dpi)), dpi) && valid;
                const std::wstring scenarios = JoinPath(output_directory, std::to_wstring(dpi));
                state.manual_deadline = 0;
                state.latches.front().kind = LatchKind::Manual;
                valid = ExportDashboardScreenshots(state, JoinPath(scenarios, L"indefinite"), dpi) && valid;
                state.system_request_accepted = false;
                state.system_request_error = ERROR_ACCESS_DENIED;
                valid = ExportDashboardScreenshots(state, JoinPath(scenarios, L"error"), dpi) && valid;
                state.system_request_accepted = true;
                state.latches.clear(); state.manual_active = false; state.active = false;
                valid = ExportDashboardScreenshots(state, JoinPath(scenarios, L"idle"), dpi) && valid;
                state.active = true;
                for (int index = 0; index < 8; ++index) {
                    Latch reason;
                    reason.provider = Provider::ClaudeCode;
                    reason.kind = LatchKind::Hook;
                    reason.label = L"Long agent task label that must stay inside the reason column " + std::to_wstring(index);
                    reason.instance_count = 12;
                    state.latches.push_back(reason);
                }
                valid = ExportDashboardScreenshots(state, JoinPath(scenarios, L"overflow"), dpi) && valid;
                state.latches.resize(4);
                state.latches[0].label = L"Codex task";
                state.latches[0].provider = Provider::Codex;
                state.latches[0].kind = LatchKind::Detector;
                state.latches[0].instance_count = 2;
                state.latches[1].label = L"Claude Code · general-purpose";
                state.latches[1].instance_count = 1;
                state.latches[2].label = L"Cursor task";
                state.latches[2].provider = Provider::Cursor;
                state.latches[2].instance_count = 1;
                state.latches[3].label = L"Claude Code task";
                state.latches[3].in_grace_period = true;
                state.latches[3].expires_at = state.now + 120000;
                state.manual_active = true;
                valid = ExportDashboardScreenshots(state, JoinPath(scenarios, L"busy"), dpi) && valid;
                controls.Update(state);
                valid = ListView_GetItemCount(reasons) == 4 &&
                    (GetWindowLongPtrW(reasons, GWL_STYLE) & (WS_HSCROLL | WS_VSCROLL)) == 0 && valid;
            }
            DestroyWindow(parent);
        }
        if (!valid) { return false; }
    }
    return true;
}

}  // namespace agent_latch
