#include "app.h"

#include "ipc.h"
#include "version.h"
#include "win_util.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cwchar>
#include <sstream>
#include <vector>

namespace agent_latch {
namespace {

constexpr UINT kTrayCallbackMessage = WM_APP + 1;
constexpr UINT_PTR kRefreshTimer = 1;
constexpr UINT kRefreshMilliseconds = 2000;
constexpr UINT kTrayIconId = 1;
constexpr ULONGLONG kMaximumLeaseMilliseconds = 24ULL * 60ULL * 60ULL * 1000ULL;

constexpr UINT kMenuOpen = 100;
constexpr UINT kMenuToggleDisplay = 106;
constexpr UINT kMenuToggleStartup = 107;
constexpr UINT kMenuSetupHooks = 108;
constexpr UINT kMenuAbout = 109;
constexpr UINT kMenuExit = 110;
constexpr UINT kMenuToggleNotifications = 111;

const GUID kTrayGuid = {
    0x8a9a17da,
    0x463d,
    0x4715,
    {0xa6, 0x43, 0x94, 0x28, 0x8a, 0x38, 0xe1, 0x75},
};

std::vector<std::wstring> SplitFields(const std::wstring& value) {
    std::vector<std::wstring> fields;
    std::size_t start = 0;
    for (;;) {
        const std::size_t separator = value.find(L'\t', start);
        fields.push_back(value.substr(start, separator == std::wstring::npos ? std::wstring::npos : separator - start));
        if (separator == std::wstring::npos) {
            break;
        }
        start = separator + 1;
    }
    return fields;
}

bool ParseUnsignedLongLong(const std::wstring& value, ULONGLONG* result) {
    if (result == nullptr || value.empty()) {
        return false;
    }
    wchar_t* end = nullptr;
    const unsigned long long parsed = std::wcstoull(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != L'\0') {
        return false;
    }
    *result = static_cast<ULONGLONG>(parsed);
    return true;
}

bool ParseUnsigned(const std::wstring& value, unsigned int* result) {
    ULONGLONG parsed = 0;
    if (!ParseUnsignedLongLong(value, &parsed) || parsed > 10000) {
        return false;
    }
    *result = static_cast<unsigned int>(parsed);
    return true;
}

LatchKind ParseLatchKind(const std::wstring& value) {
    unsigned int parsed = 0;
    if (!ParseUnsigned(value, &parsed) || parsed > static_cast<unsigned int>(LatchKind::External)) {
        return LatchKind::External;
    }
    return static_cast<LatchKind>(parsed);
}

std::wstring DetectorId(Provider provider) {
    return std::wstring(L"detector:") + ProviderKey(provider);
}

UiAction MenuToAction(UINT command) {
    switch (command) {
        case kMenuToggleDisplay:
            return UiAction::ToggleDisplay;
        case kMenuToggleStartup:
            return UiAction::ToggleStartup;
        case kMenuToggleNotifications:
            return UiAction::ToggleNotifications;
        case kMenuSetupHooks:
            return UiAction::SetupHooks;
        default:
            return UiAction::None;
    }
}

}  // namespace

AgentLatchApp::AgentLatchApp(HINSTANCE instance) : instance_(instance) {}

AgentLatchApp::~AgentLatchApp() {
    Shutdown();
}

bool AgentLatchApp::Initialize(bool show_window) {
    settings_.Load();
    const UINT dpi = GetDpiForSystem();
    renderer_.Initialize(dpi);

    idle_icon_ = CreateStateIcon(RGB(45, 57, 75), RGB(203, 213, 225));
    active_icon_ = CreateStateIcon(RGB(18, 78, 63), RGB(52, 211, 153));
    manual_icon_ = CreateStateIcon(RGB(27, 57, 96), RGB(96, 165, 250));

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = WindowProcedure;
    window_class.hInstance = instance_;
    window_class.hIcon = idle_icon_;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = nullptr;
    window_class.lpszClassName = kWindowClassName;
    window_class.hIconSm = idle_icon_;
    if (RegisterClassExW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    SIZE client_size = renderer_.PreferredClientSize();
    RECT window_rectangle{0, 0, client_size.cx, client_size.cy};
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRectEx(&window_rectangle, style, FALSE, 0);
    const int window_width = window_rectangle.right - window_rectangle.left;
    const int window_height = window_rectangle.bottom - window_rectangle.top;
    RECT work_area{};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0)) {
        work_area = RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    }
    const int x = std::max(work_area.left, work_area.left + (work_area.right - work_area.left - window_width) / 2);
    const int y = std::max(work_area.top, work_area.top + (work_area.bottom - work_area.top - window_height) / 2);

    const std::wstring window_title = L"AgentLatch " + std::wstring(kAgentLatchVersion);
    window_ = CreateWindowExW(
        0,
        kWindowClassName,
        window_title.c_str(),
        style,
        x,
        y,
        window_width,
        window_height,
        nullptr,
        nullptr,
        instance_,
        this);
    if (window_ == nullptr) {
        return false;
    }

    const BOOL dark_mode = TRUE;
    DwmSetWindowAttribute(window_, 20, &dark_mode, sizeof(dark_mode));
    taskbar_created_message_ = RegisterWindowMessageW(L"TaskbarCreated");
    AddTrayIcon();
    SetTimer(window_, kRefreshTimer, kRefreshMilliseconds, nullptr);
    Tick(true);

    if (show_window) {
        ShowDashboard();
    }
    return true;
}

int AgentLatchApp::Run(bool show_window) {
    if (!Initialize(show_window)) {
        return 10;
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

void AgentLatchApp::Shutdown() {
    if (shutting_down_) {
        return;
    }
    shutting_down_ = true;
    power_request_.Apply(false, false);
    RemoveTrayIcon();
    if (window_ != nullptr && IsWindow(window_)) {
        KillTimer(window_, kRefreshTimer);
    }
    if (idle_icon_ != nullptr) {
        DestroyIcon(idle_icon_);
        idle_icon_ = nullptr;
    }
    if (active_icon_ != nullptr) {
        DestroyIcon(active_icon_);
        active_icon_ = nullptr;
    }
    if (manual_icon_ != nullptr) {
        DestroyIcon(manual_icon_);
        manual_icon_ = nullptr;
    }
}

void AgentLatchApp::ShowDashboard() {
    if (window_ == nullptr) {
        return;
    }
    ShowWindow(window_, SW_SHOWNORMAL);
    SetForegroundWindow(window_);
    InvalidateRect(window_, nullptr, FALSE);
}

void AgentLatchApp::Tick(bool scan_agents) {
    const ULONGLONG now = GetTickCount64();
    latches_.Expire(now);
    if (scan_agents) {
        settings_.RefreshIntegrationStatus();
        UpdateDetectorLatches();
    }
    ReconcilePowerState();
    UpdateTrayIcon();
    if (window_ != nullptr && IsWindowVisible(window_)) {
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void AgentLatchApp::UpdateDetectorLatches() {
    const ULONGLONG now = GetTickCount64();
    for (const DetectionResult& result : detector_.Scan(now, settings_.activity_grace_seconds)) {
        const std::wstring id = DetectorId(result.provider);
        const DetectionMode mode = settings_.ProviderMode(result.provider);
        const bool should_latch = mode == DetectionMode::Tasks
                                      ? result.recently_active
                                      : mode == DetectionMode::Open && result.running_instances > 0;
        if (should_latch) {
            const bool task_mode = mode == DetectionMode::Tasks;
            const unsigned int instance_count = task_mode && result.active_task_instances > 0
                                                    ? result.active_task_instances
                                                    : result.running_instances;
            latches_.Upsert(
                id,
                result.provider,
                LatchKind::Detector,
                std::wstring(ProviderName(result.provider)) + (task_mode ? L" task" : L" open"),
                task_mode ? result.activity_detail : result.open_detail,
                now,
                0,
                instance_count);
        } else {
            latches_.Remove(id);
        }
    }
}

void AgentLatchApp::ReconcilePowerState() {
    const bool active = latches_.IsActive();
    power_request_.Apply(active, active && settings_.keep_display_on);
    if (!transition_state_initialized_) {
        last_active_state_ = active;
        transition_state_initialized_ = true;
    } else if (last_active_state_ != active) {
        last_active_state_ = active;
        ShowTransitionNotification(active);
    }
}

bool AgentLatchApp::ProcessIpcMessage(const std::wstring& message) {
    const std::vector<std::wstring> fields = SplitFields(message);
    if (fields.empty()) {
        return false;
    }
    if (fields[0] == L"SHOW") {
        ShowDashboard();
        return true;
    }
    if (fields[0] == L"EXIT") {
        DestroyWindow(window_);
        return true;
    }
    if (fields[0] == L"REMOVE" && fields.size() == 2) {
        latches_.Remove(fields[1]);
        Tick(false);
        return true;
    }
    if (fields[0] == L"SEEN" && fields.size() == 2) {
        const Provider provider = ProviderFromString(fields[1]);
        settings_.MarkHookSeen(provider);
        Tick(false);
        return true;
    }
    if (fields[0] != L"UPSERT" || fields.size() != 8) {
        return false;
    }

    ULONGLONG ttl = 0;
    unsigned int instance_count = 1;
    if (!ParseUnsignedLongLong(fields[6], &ttl) || !ParseUnsigned(fields[7], &instance_count)) {
        return false;
    }
    ttl = std::min(ttl, kMaximumLeaseMilliseconds);
    const Provider provider = ProviderFromString(fields[2]);
    const LatchKind kind = ParseLatchKind(fields[3]);
    const DetectionMode mode = settings_.ProviderMode(provider);
    if ((kind == LatchKind::Hook && mode != DetectionMode::Tasks) ||
        (kind == LatchKind::Detector && mode == DetectionMode::Off)) {
        latches_.Remove(fields[1]);
        Tick(false);
        return true;
    }

    latches_.Upsert(
        fields[1],
        provider,
        kind,
        fields[4],
        fields[5],
        GetTickCount64(),
        ttl,
        instance_count);
    Tick(false);
    return true;
}

void AgentLatchApp::ExecuteAction(UiAction action) {
    switch (action) {
        case UiAction::ToggleCodex:
            settings_.CycleProviderMode(Provider::Codex);
            latches_.RemoveByProvider(Provider::Codex);
            settings_.Save();
            break;
        case UiAction::ToggleClaude:
            settings_.CycleProviderMode(Provider::ClaudeCode);
            latches_.RemoveByProvider(Provider::ClaudeCode);
            settings_.Save();
            break;
        case UiAction::ToggleCursor:
            settings_.CycleProviderMode(Provider::Cursor);
            latches_.RemoveByProvider(Provider::Cursor);
            settings_.Save();
            break;
        case UiAction::ToggleOpenCode:
            settings_.CycleProviderMode(Provider::OpenCode);
            latches_.RemoveByProvider(Provider::OpenCode);
            settings_.Save();
            break;
        case UiAction::ToggleGemini:
            settings_.CycleProviderMode(Provider::GeminiCli);
            latches_.RemoveByProvider(Provider::GeminiCli);
            settings_.Save();
            break;
        case UiAction::ToggleDisplay:
            settings_.keep_display_on = !settings_.keep_display_on;
            settings_.Save();
            break;
        case UiAction::ToggleStartup:
            SetStartWithWindows(!IsStartWithWindowsEnabled(), GetExecutablePath());
            break;
        case UiAction::ToggleNotifications:
            settings_.notifications = !settings_.notifications;
            settings_.Save();
            break;
        case UiAction::SetupHooks:
            LaunchHookSetup();
            break;
        case UiAction::None:
            return;
    }
    Tick(false);
}

void AgentLatchApp::ShowTrayMenu(POINT location) {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }

    AppendMenuW(menu, MF_STRING | MF_DEFAULT, kMenuOpen, L"Open AgentLatch");
    std::wstring status = latches_.IsActive()
                              ? L"Latched · " + std::to_wstring(latches_.Size()) + L" active"
                              : L"Windows can sleep";
    AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, status.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (settings_.keep_display_on ? MF_CHECKED : MF_UNCHECKED), kMenuToggleDisplay, L"Keep display on while latched");
    AppendMenuW(menu, MF_STRING | (IsStartWithWindowsEnabled() ? MF_CHECKED : MF_UNCHECKED), kMenuToggleStartup, L"Start with Windows");
    AppendMenuW(
        menu,
        MF_STRING | (settings_.notifications ? MF_CHECKED : MF_UNCHECKED),
        kMenuToggleNotifications,
        L"Show wake status notifications");
    AppendMenuW(
        menu,
        MF_STRING,
        kMenuSetupHooks,
        L"Repair or update agent integrations...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuAbout, L"About AgentLatch");
    AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit");

    SetForegroundWindow(window_);
    const UINT command = TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, location.x, location.y, 0, window_, nullptr);
    DestroyMenu(menu);
    if (command == kMenuOpen) {
        ShowDashboard();
    } else if (command == kMenuAbout) {
        ShowAbout();
    } else if (command == kMenuExit) {
        DestroyWindow(window_);
    } else {
        ExecuteAction(MenuToAction(command));
    }
}

void AgentLatchApp::LaunchHookSetup() {
    const std::wstring directory = GetExecutableDirectory();
    const std::array<std::wstring, 3> candidates = {
        JoinPath(directory, L"scripts\\install-integrations.ps1"),
        JoinPath(directory, L"install-integrations.ps1"),
        JoinPath(JoinPath(GetLocalAppDataDirectory(), L"AgentLatch"), L"install-integrations.ps1"),
    };
    std::wstring script;
    for (const std::wstring& candidate : candidates) {
        if (FileExists(candidate)) {
            script = candidate;
            break;
        }
    }
    if (script.empty()) {
        MessageBoxW(
            window_,
            L"The integration repair script is not next to AgentLatch.\n\nDownload the complete release package, or see docs/INTEGRATIONS.md in the repository.",
            L"AgentLatch integrations",
            MB_OK | MB_ICONINFORMATION);
        return;
    }

    const std::wstring parameters = L"-NoProfile -ExecutionPolicy Bypass -File \"" + script +
                                    L"\" -AgentLatchPath \"" + GetExecutablePath() + L"\"";
    const HINSTANCE result = ShellExecuteW(
        window_, L"open", L"powershell.exe", parameters.c_str(), directory.c_str(), SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        MessageBoxW(window_, L"Windows could not start the integration repair script.", L"AgentLatch", MB_OK | MB_ICONERROR);
    }
}

void AgentLatchApp::ShowAbout() {
    const std::wstring about_text =
        L"AgentLatch " + std::wstring(kAgentLatchVersion) +
        L"\n\nA lightweight, open-source, agent-aware wake manager for Windows.\n\nNo account · No telemetry · No administrator rights";
    MessageBoxW(
        window_,
        about_text.c_str(),
        L"About AgentLatch",
        MB_OK | MB_ICONINFORMATION);
}

void AgentLatchApp::ShowTransitionNotification(bool active) {
    if (!settings_.notifications || !tray_added_) {
        return;
    }
    tray_icon_.uFlags = NIF_GUID | NIF_INFO;
    lstrcpynW(tray_icon_.szInfoTitle, L"AgentLatch", static_cast<int>(std::size(tray_icon_.szInfoTitle)));
    lstrcpynW(
        tray_icon_.szInfo,
        active ? L"Windows will stay awake while latched work continues."
               : L"The final latch was released. Normal sleep settings are restored.",
        static_cast<int>(std::size(tray_icon_.szInfo)));
    tray_icon_.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    Shell_NotifyIconW(NIM_MODIFY, &tray_icon_);
}

HICON AgentLatchApp::CreateStateIcon(COLORREF background, COLORREF foreground) const {
    constexpr int size = 32;
    HDC screen = GetDC(nullptr);
    HDC color_dc = CreateCompatibleDC(screen);
    HDC mask_dc = CreateCompatibleDC(screen);
    HBITMAP color_bitmap = CreateCompatibleBitmap(screen, size, size);
    HBITMAP mask_bitmap = CreateBitmap(size, size, 1, 1, nullptr);
    HGDIOBJ old_color = SelectObject(color_dc, color_bitmap);
    HGDIOBJ old_mask = SelectObject(mask_dc, mask_bitmap);

    RECT bounds{0, 0, size, size};
    HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(color_dc, &bounds, black);
    HBRUSH white = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    FillRect(mask_dc, &bounds, white);
    DeleteObject(black);

    HBRUSH background_brush = CreateSolidBrush(background);
    HPEN background_pen = CreatePen(PS_SOLID, 1, background);
    SelectObject(color_dc, background_brush);
    SelectObject(color_dc, background_pen);
    Ellipse(color_dc, 1, 1, size - 1, size - 1);
    SelectObject(mask_dc, GetStockObject(BLACK_BRUSH));
    SelectObject(mask_dc, GetStockObject(BLACK_PEN));
    Ellipse(mask_dc, 1, 1, size - 1, size - 1);

    HPEN mark_pen = CreatePen(PS_SOLID, 3, foreground);
    HBRUSH mark_brush = CreateSolidBrush(foreground);
    SelectObject(color_dc, mark_pen);
    SelectObject(color_dc, GetStockObject(NULL_BRUSH));
    Arc(color_dc, 10, 6, 22, 21, 22, 18, 10, 18);
    MoveToEx(color_dc, 10, 13, nullptr);
    LineTo(color_dc, 10, 19);
    MoveToEx(color_dc, 22, 13, nullptr);
    LineTo(color_dc, 22, 19);
    SelectObject(color_dc, mark_brush);
    RoundRect(color_dc, 7, 16, 25, 27, 5, 5);

    SelectObject(color_dc, old_color);
    SelectObject(mask_dc, old_mask);
    DeleteDC(color_dc);
    DeleteDC(mask_dc);
    ReleaseDC(nullptr, screen);
    DeleteObject(background_pen);
    DeleteObject(background_brush);
    DeleteObject(mark_pen);
    DeleteObject(mark_brush);

    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmColor = color_bitmap;
    info.hbmMask = mask_bitmap;
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(color_bitmap);
    DeleteObject(mask_bitmap);
    return icon;
}

bool AgentLatchApp::AddTrayIcon() {
    ZeroMemory(&tray_icon_, sizeof(tray_icon_));
    tray_icon_.cbSize = sizeof(tray_icon_);
    tray_icon_.hWnd = window_;
    tray_icon_.uID = kTrayIconId;
    tray_icon_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_GUID | NIF_SHOWTIP;
    tray_icon_.uCallbackMessage = kTrayCallbackMessage;
    tray_icon_.hIcon = idle_icon_;
    tray_icon_.guidItem = kTrayGuid;
    lstrcpynW(tray_icon_.szTip, L"AgentLatch · Windows can sleep", static_cast<int>(std::size(tray_icon_.szTip)));
    tray_added_ = Shell_NotifyIconW(NIM_ADD, &tray_icon_) != FALSE;
    if (tray_added_) {
        tray_icon_.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &tray_icon_);
    }
    return tray_added_;
}

void AgentLatchApp::RemoveTrayIcon() {
    if (tray_added_) {
        Shell_NotifyIconW(NIM_DELETE, &tray_icon_);
        tray_added_ = false;
    }
}

void AgentLatchApp::UpdateTrayIcon() {
    if (!tray_added_) {
        return;
    }
    const bool manual = latches_.HasKind(LatchKind::Manual) || latches_.HasKind(LatchKind::Timer);
    tray_icon_.uFlags = NIF_ICON | NIF_TIP | NIF_GUID | NIF_SHOWTIP;
    tray_icon_.hIcon = latches_.IsActive() ? (manual ? manual_icon_ : active_icon_) : idle_icon_;
    const std::wstring tip = latches_.IsActive()
                                 ? L"AgentLatch · Latched · " + std::to_wstring(latches_.Size()) + L" active"
                                 : L"AgentLatch · Windows can sleep";
    lstrcpynW(tray_icon_.szTip, tip.c_str(), static_cast<int>(std::size(tray_icon_.szTip)));
    Shell_NotifyIconW(NIM_MODIFY, &tray_icon_);
    SendMessageW(window_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(tray_icon_.hIcon));
    SendMessageW(window_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(tray_icon_.hIcon));
}

LRESULT CALLBACK AgentLatchApp::WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    AgentLatchApp* app = reinterpret_cast<AgentLatchApp*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        app = static_cast<AgentLatchApp*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        app->window_ = window;
    }
    return app == nullptr ? DefWindowProcW(window, message, wparam, lparam)
                          : app->HandleMessage(message, wparam, lparam);
}

LRESULT AgentLatchApp::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == taskbar_created_message_ && taskbar_created_message_ != 0) {
        tray_added_ = false;
        AddTrayIcon();
        UpdateTrayIcon();
        return 0;
    }

    switch (message) {
        case WM_TIMER:
            if (wparam == kRefreshTimer) {
                Tick(true);
            }
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window_, &paint);
            RECT client{};
            GetClientRect(window_, &client);
            DashboardState state;
            state.active = latches_.IsActive();
            state.power_request_available = power_request_.IsAvailable() && power_request_.LastError() == ERROR_SUCCESS;
            state.keep_display_on = settings_.keep_display_on;
            state.start_with_windows = IsStartWithWindowsEnabled();
            state.now = GetTickCount64();
            state.latches = latches_.Snapshot();
            state.settings = settings_;
            renderer_.Paint(dc, client, state);
            EndPaint(window_, &paint);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_LBUTTONUP: {
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ExecuteAction(renderer_.HitTest(point));
            return 0;
        }
        case WM_SETCURSOR: {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(window_, &point);
            if (renderer_.HitTest(point) != UiAction::None) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        }
        case WM_COPYDATA: {
            const COPYDATASTRUCT* data = reinterpret_cast<const COPYDATASTRUCT*>(lparam);
            if (data == nullptr || data->dwData != kCopyDataSignature || data->lpData == nullptr ||
                data->cbData < sizeof(wchar_t) || data->cbData > 8192) {
                return FALSE;
            }
            const std::size_t character_count = data->cbData / sizeof(wchar_t);
            const wchar_t* text = static_cast<const wchar_t*>(data->lpData);
            if (text[character_count - 1] != L'\0') {
                return FALSE;
            }
            return ProcessIpcMessage(std::wstring(text, character_count - 1)) ? TRUE : FALSE;
        }
        case WM_CLOSE:
            ShowWindow(window_, SW_HIDE);
            return 0;
        case WM_DESTROY:
            Shutdown();
            PostQuitMessage(0);
            return 0;
        case WM_DPICHANGED: {
            renderer_.Initialize(HIWORD(wparam));
            const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
            SetWindowPos(
                window_,
                nullptr,
                suggested->left,
                suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOACTIVATE | SWP_NOZORDER);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lparam);
            const SIZE preferred = renderer_.PreferredClientSize();
            info->ptMinTrackSize.x = preferred.cx;
            info->ptMinTrackSize.y = preferred.cy;
            return 0;
        }
        case WM_POWERBROADCAST:
            if (wparam == PBT_APMRESUMEAUTOMATIC || wparam == PBT_APMRESUMESUSPEND) {
                Tick(true);
            }
            return TRUE;
        case kTrayCallbackMessage: {
            const UINT event = LOWORD(lparam);
            if (event == WM_LBUTTONUP || event == NIN_SELECT || event == NIN_KEYSELECT) {
                ShowDashboard();
            } else if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU) {
                POINT location{GET_X_LPARAM(wparam), GET_Y_LPARAM(wparam)};
                if (location.x == -1 && location.y == -1) {
                    GetCursorPos(&location);
                }
                ShowTrayMenu(location);
            }
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}

}  // namespace agent_latch
