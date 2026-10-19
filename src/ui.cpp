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
constexpr COLORREF kFaint = RGB(103, 113, 128);
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
constexpr int kAgentsTitle = 100, kAgentsHint = 101;
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
        MAKEINTRESOURCEW(IDI_AGENTLATCH), IMAGE_ICON, Scale(30), Scale(30), LR_DEFAULTCOLOR));
}
int DashboardRenderer::Scale(int value) const {
    return MulDiv(value, static_cast<int>(dpi_), 96);
}
SIZE DashboardRenderer::PreferredClientSize() const { return SIZE{Scale(600), Scale(480)}; }

void DashboardRenderer::Paint(HDC dc, const RECT& client, const DashboardState& state) {
    FillRectangle(dc, client, kBackground);
    const int right = client.right - Scale(20);
    if (brand_icon_ != nullptr) {
        DrawIconEx(dc, Scale(20), Scale(17), brand_icon_, Scale(30), Scale(30), 0, nullptr, DI_NORMAL);
    }
    RECT header_line{0, Scale(62), client.right, Scale(63)};
    FillRectangle(dc, header_line, kBorder);
    RECT status{Scale(20), Scale(114), right, Scale(192)};
    FillRoundedRectangle(dc, status, Scale(12), StatusFill(state),
        !state.system_request_accepted ? RGB(120, 78, 20) : state.active ? RGB(31, 91, 72) : kBorder);
    const COLORREF accent = !state.system_request_accepted ? kWarning : state.active ? kActive : kIdle;
    DrawStatusDot(dc, Scale(40), Scale(140), Scale(4), accent);
    RECT chip{right - Scale(120), Scale(128), right - Scale(16), Scale(151)};
    FillRoundedRectangle(dc, chip, Scale(7), BadgeFill(state), kBorder);
    if (state.page == DashboardPage::Overview) {
        FillRoundedRectangle(dc, RECT{Scale(20), Scale(207), right, Scale(321)}, Scale(10), kSurface, kBorder);
        FillRoundedRectangle(dc, RECT{Scale(20), Scale(359), right, Scale(444)}, Scale(10), kSurface, kBorder);
    } else if (state.page == DashboardPage::Agents) {
        FillRoundedRectangle(dc, RECT{Scale(20), Scale(234), right, Scale(434)}, Scale(10), kSurface, kBorder);
        for (int index = 1; index < 5; ++index) {
            const int y = Scale(234 + index * 40);
            FillRectangle(dc, RECT{Scale(36), y, right - Scale(16), y + 1}, kBorder);
        }
    } else {
        FillRoundedRectangle(dc, RECT{Scale(20), Scale(207), right, Scale(397)}, Scale(10), kSurface, kBorder);
    }
    FillRectangle(dc, RECT{0, Scale(451), client.right, Scale(452)}, kBorder);
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
    Text(L"AgentLatch", kTitle, -1, RECT{62, 15, 218, 47});
    Text((L"v" + std::wstring(kAgentLatchVersion)).c_str(), kVersion, -1, RECT{226, 27, 370, 44});
    Add(L"BUTTON", L"More…", WS_TABSTOP | BS_OWNERDRAW, kMore, -1, RECT{500, 20, 580, 46}, true);
    tabs_ = Add(WC_TABCONTROLW, L"AgentLatch pages", WS_TABSTOP | TCS_FIXEDWIDTH,
        kTabs, -1, RECT{20, 72, 580, 104});
    if (tabs_ == nullptr) { return false; }
    for (const wchar_t* label : {L"Overview", L"Agents", L"Settings"}) {
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(label);
        TabCtrl_InsertItem(tabs_, TabCtrl_GetItemCount(tabs_), &item);
    }
    SetWindowSubclass(tabs_, TabProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
    Text(L"", kStatusTitle, -1, RECT{60, 126, 454, 151});
    Text(L"", kStatusDetail, -1, RECT{60, 158, 565, 180});
    HWND badge = Text(L"", kStatusBadge, -1, RECT{464, 131, 560, 149}, true);
    SetWindowLongPtrW(badge, GWL_STYLE, GetWindowLongPtrW(badge, GWL_STYLE) | SS_CENTER);
    Text(L"Closing this window keeps monitoring in the tray.", kFooter, -1, RECT{20, 460, 575, 477});

    const int overview = static_cast<int>(DashboardPage::Overview);
    Text(L"Manual keep-awake", kManualTitle, overview, RECT{36, 219, 350, 243});
    HWND manual_status = Text(L"", kManualStatus, overview, RECT{366, 221, 564, 243}, true);
    SetWindowLongPtrW(manual_status, GWL_STYLE,
        (GetWindowLongPtrW(manual_status, GWL_STYLE) & ~SS_TYPEMASK) | SS_RIGHT | SS_ENDELLIPSIS);
    Text(L"Keep awake even if every agent closes or updates.", kManualHint, overview, RECT{36, 249, 562, 269});
    Add(L"BUTTON", L"Keep awake…", WS_TABSTOP | BS_OWNERDRAW, kManualStart, overview, RECT{36, 278, 183, 307});
    Add(L"BUTTON", L"Stop", WS_TABSTOP | BS_OWNERDRAW, kManualStop, overview, RECT{195, 278, 274, 307});
    Text(L"Agent tracking stays on.", kManualNote, overview, RECT{290, 285, 562, 305});
    Text(L"Protection reasons", kReasonsTitle, overview, RECT{20, 335, 350, 355});
    HWND count = Text(L"", kReasonsCount, overview, RECT{450, 339, 580, 356}, true);
    SetWindowLongPtrW(count, GWL_STYLE, GetWindowLongPtrW(count, GWL_STYLE) | SS_RIGHT);
    reasons_ = Add(WC_LISTVIEWW, L"Active protection reasons", WS_TABSTOP | LVS_REPORT |
        LVS_NOCOLUMNHEADER | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS,
        kReasons, overview, RECT{28, 366, 572, 438});
    if (reasons_ == nullptr) { return false; }
    SetWindowTheme(reasons_, L"", L"");
    AccessibleName(reasons_, L"Active protection reasons");
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

    const int agents = static_cast<int>(DashboardPage::Agents);
    Text(L"Agent tracking", kAgentsTitle, agents, RECT{20, 209, 215, 231});
    Text(L"Choose when each app keeps your PC awake.", kAgentsHint, agents, RECT{238, 213, 580, 232});
    for (std::size_t index = 0; index < kProviders.size(); ++index) {
        const int offset = static_cast<int>(index);
        const int top = 234 + offset * 40;
        Text(kProviderLabels[index], kProviderName + offset, agents, RECT{36, top + 5, 372, top + 23});
        Text(L"", kProviderDetail + offset, agents, RECT{36, top + 23, 372, top + 39});
        HWND combo = Add(WC_COMBOBOXW, L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST |
            CBS_OWNERDRAWFIXED | CBS_HASSTRINGS, kModeFirst + offset, agents,
            RECT{400, top + 8, 564, top + 140}, true);
        if (combo == nullptr) { return false; }
        SetWindowTheme(combo, L"", L"");
        for (const wchar_t* mode : kModeLabels) { SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(mode)); }
        AccessibleName(combo, std::wstring(kProviderLabels[index]) + L" tracking mode");
        SetWindowSubclass(combo, ComboProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
    }
    const int settings = static_cast<int>(DashboardPage::Settings);
    const std::array<const wchar_t*, 3> labels = {
        L"Keep the screen on while protected", L"Start AgentLatch when I sign in", L"Show status notifications"};
    const std::array<const wchar_t*, 3> hints = {
        L"Off lets the display turn off. Your PC still stays awake.",
        L"Monitor agents and restore saved manual sessions at sign-in.",
        L"Optional wake alerts. Protection failures always notify you."};
    for (int index = 0; index < 3; ++index) {
        const int top = 222 + index * 60;
        HWND checkbox = Add(L"BUTTON", labels[static_cast<std::size_t>(index)],
            WS_TABSTOP | BS_AUTOCHECKBOX, kScreen + index, settings, RECT{36, top, 564, top + 24});
        SetWindowTheme(checkbox, L"", L"");
        Text(hints[static_cast<std::size_t>(index)], kSettingHint + index, settings,
            RECT{59, top + 27, 564, top + 46});
    }
    Add(L"BUTTON", L"Diagnostic history", WS_TABSTOP | BS_OWNERDRAW,
        kDiagnostics, settings, RECT{20, 409, 207, 439});
    Add(L"BUTTON", L"Repair integrations…", WS_TABSTOP | BS_OWNERDRAW,
        kRepair, settings, RECT{220, 409, 421, 439});
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
    title_font_ = CreateUiFont(dpi_, 18, FW_SEMIBOLD);
    for (const auto& control : controls_) {
        HFONT font = body_font_;
        if (control.id == kTitle) { font = title_font_; }
        else if (control.id == kStatusTitle || control.id == kManualTitle ||
                 control.id == kReasonsTitle || control.id == kAgentsTitle) { font = heading_font_; }
        else if (control.id == kVersion || control.id == kStatusDetail || control.id == kStatusBadge ||
                 control.id == kFooter || control.id == kManualHint || control.id == kManualNote ||
                 control.id == kReasonsCount || control.id == kAgentsHint ||
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
    row_images_ = ImageList_Create(1, Scale(25), ILC_COLOR32, 1, 1);
    if (reasons_ != nullptr) { ListView_SetImageList(reasons_, row_images_, LVSIL_SMALL); }
    Layout();
}
void DashboardControls::Layout() {
    if (parent_ == nullptr) { return; }
    RECT client{};
    GetClientRect(parent_, &client);
    const int extra = client.right - Scale(600);
    for (const auto& control : controls_) {
        RECT rect{Scale(control.bounds.left), Scale(control.bounds.top),
                  Scale(control.bounds.right), Scale(control.bounds.bottom)};
        if (control.right_anchor) { rect.left += extra; rect.right += extra; }
        else if (control.id == kTabs || control.id == kReasons || control.id == kFooter ||
                 control.id == kStatusDetail) { rect.right += extra; }
        MoveWindow(control.window, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, TRUE);
    }
    if (tabs_ != nullptr) {
        TabCtrl_SetItemSize(tabs_, (client.right - Scale(56)) / 3, Scale(30));
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
        state_.active ? (grace_only ? L"Finishing protection" : L"Your PC is staying awake") : L"Ready for agent work");
    TextValue(kStatusBadge, error ? L"ERROR" : state_.active ? L"PROTECTED" : L"READY");
    std::wstring detail;
    if (error) {
        detail = L"Windows rejected the request (error " + std::to_wstring(state_.system_request_error) + L"). Retrying.";
    } else if (state_.active) {
        detail = std::to_wstring(state_.latches.size()) +
            (state_.latches.size() == 1 ? L" protection reason" : L" protection reasons");
        detail += state_.keep_display_on ? L" · screen stays on" : L" · screen may turn off";
        if (!state_.display_request_accepted) { detail += L" · screen request failed"; }
    } else { detail = L"Normal sleep settings apply. Enabled agents are being monitored."; }
    TextValue(kStatusDetail, detail);
    TextValue(kManualStatus, !state_.manual_active ? L"Off" :
        state_.manual_deadline == 0 ? L"Until stopped" : FormatCountdown(state_.manual_deadline, state_.now));
    TextValue(kManualStart, state_.manual_active ? L"Change duration…" : L"Keep awake…");
    TextValue(kReasonsCount, std::to_wstring(state_.latches.size()) +
        (state_.latches.size() == 1 ? L" reason" : L" reasons"));
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
    std::vector<std::pair<std::wstring, std::wstring>> rows;
    for (const auto& latch : state_.latches) { rows.emplace_back(latch.label, LatchSecondaryText(latch, state_.now)); }
    if (rows.empty()) { rows.emplace_back(L"No active protection reasons", L"Monitoring"); }
    if (rows != reason_text_) {
        if (rows.size() != reason_text_.size()) { ListView_DeleteAllItems(reasons_); }
        for (std::size_t index = 0; index < rows.size(); ++index) {
            const int row = static_cast<int>(index);
            if (ListView_GetItemCount(reasons_) <= row) {
                LVITEMW item{};
                item.mask = LVIF_TEXT;
                item.iItem = row;
                item.pszText = rows[index].first.data();
                ListView_InsertItem(reasons_, &item);
            } else { ListView_SetItemText(reasons_, row, 0, rows[index].first.data()); }
            ListView_SetItemText(reasons_, row, 1, rows[index].second.data());
        }
        reason_text_ = std::move(rows);
    }
    LayoutReasonColumns();
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
    const bool primary = item->CtlID == kManualStart;
    const bool pressed = (item->itemState & ODS_SELECTED) != 0;
    FillRectangle(item->hDC, item->rcItem, IsSurface(static_cast<int>(item->CtlID)) ? kSurface : kBackground);
    FillRoundedRectangle(item->hDC, item->rcItem, Scale(7),
        primary ? (pressed ? RGB(24, 64, 53) : RGB(21, 54, 44)) : pressed ? kBorder : kSurfaceRaised,
        primary ? RGB(43, 127, 98) : kBorder);
    wchar_t label[128]{};
    GetWindowTextW(item->hwndItem, label, static_cast<int>(std::size(label)));
    DrawTextBlock(item->hDC, label, item->rcItem, body_font_, primary ? kActive : kText,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    if ((item->itemState & ODS_FOCUS) != 0) {
        RECT focus = item->rcItem; InflateRect(&focus, -Scale(4), -Scale(4));
        DrawFocusRect(item->hDC, &focus);
    }
    return true;
}
bool DashboardControls::MeasureItem(MEASUREITEMSTRUCT* item) const {
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
    else if (id == kStatusDetail || id == kManualHint || id == kManualNote ||
             (id >= kProviderDetail && id < kProviderDetail + 5) ||
             (id >= kSettingHint && id <= kSettingHint + 2)) { color = kMuted; }
    SetTextColor(dc, color);
    const COLORREF background = id == kStatusBadge ? BadgeFill(state_) :
        IsStatus(id) ? StatusFill(state_) : IsSurface(id) ? kSurface : kBackground;
    SetBkColor(dc, background);
    SetBkMode(dc, TRANSPARENT);
    return id == kStatusBadge ? badge_brush_ : IsStatus(id) ? status_brush_ : IsSurface(id) ? surface_ : background_;
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
    FillRoundedRectangle(dc, bounds, Scale(8), kSurface, kBorder);
    for (int index = 0; index < 3; ++index) {
        RECT item{}; TabCtrl_GetItemRect(tabs_, index, &item);
        const bool selected = index == static_cast<int>(page_);
        if (selected) {
            RECT fill = item; InflateRect(&fill, -Scale(2), -Scale(2));
            FillRoundedRectangle(dc, fill, Scale(6), kSurfaceRaised, kBorder);
            FillRectangle(dc, RECT{fill.left + Scale(14), fill.bottom - Scale(2),
                                   fill.right - Scale(14), fill.bottom}, kActive);
        }
        const wchar_t* label = index == 0 ? L"Overview" : index == 1 ? L"Agents" : L"Settings";
        DrawTextBlock(dc, label, item, body_font_, selected ? kText : kMuted,
            DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        if (selected && GetFocus() == tabs_) {
            InflateRect(&item, -Scale(5), -Scale(5)); DrawFocusRect(dc, &item);
        }
    }
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
                ((control.id != kManualStop && control.id != kManualNote) || state_.manual_active);
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
        for (UINT flags : {0u, static_cast<UINT>(ODS_SELECTED), static_cast<UINT>(ODS_FOCUS),
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
            for (int id : {kMore, kManualStart, kManualStop, kDiagnostics, kRepair}) {
                valid = ControlPaintCoversBackground(controls, GetDlgItem(parent, id),
                    IsSurface(id) ? kSurface : kBackground, true) && valid;
            }
            for (int id = kModeFirst; id < kModeFirst + 5; ++id) {
                valid = ControlPaintCoversBackground(controls, GetDlgItem(parent, id), kSurface, false) && valid;
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
            valid = ListView_GetItemCount(GetDlgItem(parent, kReasons)) == 8 && valid;
            valid = (GetWindowLongPtrW(GetDlgItem(parent, kReasons), GWL_STYLE) & WS_HSCROLL) == 0 && valid;
            state.latches.clear(); state.manual_active = false; state.active = false;
            controls.Update(state); controls.SelectPage(DashboardPage::Overview);
            valid = controls.ValidateLayout() && ListView_GetItemCount(GetDlgItem(parent, kReasons)) == 1 && valid;
            if (!output_directory.empty()) {
                state.manual_active = true; state.active = true;
                Latch manual; manual.provider = Provider::Manual; manual.kind = LatchKind::Timer;
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
            }
            DestroyWindow(parent);
        }
        if (!valid) { return false; }
    }
    return true;
}

}  // namespace agent_latch
