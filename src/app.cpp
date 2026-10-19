#include "app.h"

#include "ipc.h"
#include "dashboard_capture.h"
#include "resource.h"
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
constexpr UINT kMenuOpenDiagnostics = 112;
constexpr UINT kMenuManual = 113;
constexpr UINT kMenuStopManual = 114;

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

INT_PTR CALLBACK ManualHoursProcedure(HWND dialog, UINT message, WPARAM wparam, LPARAM) {
    if (message == WM_INITDIALOG) {
        SetDlgItemInt(dialog, IDC_MANUAL_HOURS, 8, FALSE);
        SendDlgItemMessageW(dialog, IDC_MANUAL_HOURS, EM_SETLIMITTEXT, 3, 0);
        return TRUE;
    }
    if (message == WM_COMMAND) {
        if (LOWORD(wparam) == IDCANCEL) {
            EndDialog(dialog, 0);
            return TRUE;
        }
        if (LOWORD(wparam) == IDOK) {
            BOOL valid = FALSE;
            const UINT hours = GetDlgItemInt(dialog, IDC_MANUAL_HOURS, &valid, FALSE);
            if (!valid || hours < 1 || hours > ManualKeepAwake::kMaximumHours) {
                MessageBoxW(dialog, L"Enter a whole number of hours from 1 to 168.",
                            L"AgentLatch", MB_OK | MB_ICONINFORMATION);
                return TRUE;
            }
            EndDialog(dialog, static_cast<INT_PTR>(hours));
            return TRUE;
        }
    }
    return FALSE;
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
        case kMenuManual:
            return UiAction::ManualMenu;
        case kMenuStopManual:
            return UiAction::StopManual;
        default:
            return UiAction::None;
    }
}

std::wstring LatchDiagnosticDetail(const std::vector<Latch>& latches) {
    std::size_t instances = 0;
    std::vector<Provider> providers;
    for (const Latch& latch : latches) {
        instances += latch.instance_count;
        if (std::find(providers.begin(), providers.end(), latch.provider) == providers.end()) {
            providers.push_back(latch.provider);
        }
    }
    std::wostringstream detail;
    detail << L"active=" << (!latches.empty() ? 1 : 0) << L" instances=" << instances;
    if (!providers.empty()) {
        detail << L" providers=";
        for (std::size_t index = 0; index < providers.size(); ++index) {
            if (index > 0) {
                detail << L',';
            }
            detail << ProviderShortName(providers[index]);
        }
    }
    for (const Latch& latch : latches) {
        if (latch.in_grace_period) {
            detail << L" grace=" << ProviderShortName(latch.provider);
        }
    }
    return detail.str();
}

}  // namespace

std::wstring DetectorLatchLabel(const DetectionResult& result, DetectionMode mode) {
    if (mode == DetectionMode::Tasks && result.in_grace_period) {
        return std::wstring(ProviderShortName(result.provider)) + L" · release grace";
    }
    if (mode == DetectionMode::Tasks && result.provider == Provider::Codex) {
        if (result.activity_detail.find(L"ChatGPT response") != std::wstring::npos &&
            result.activity_detail.find(L"Codex task") != std::wstring::npos) {
            return L"OpenAI work";
        }
        if (result.activity_detail == L"ChatGPT is responding") {
            return L"ChatGPT response";
        }
        if (result.activity_detail == L"Codex is responding") {
            return L"Codex response";
        }
    }
    return std::wstring(ProviderName(result.provider)) +
           (mode == DetectionMode::Tasks ? L" task" : L" open");
}

unsigned int DetectorLatchInstanceCount(const DetectionResult& result, DetectionMode mode) {
    return mode == DetectionMode::Open ? result.running_instances
                                      : std::max(1u, result.active_task_instances);
}

AgentLatchApp::AgentLatchApp(HINSTANCE instance) : instance_(instance) {}

AgentLatchApp::~AgentLatchApp() {
    Shutdown();
}

bool AgentLatchApp::Initialize(bool show_window) {
    settings_.Load();
    manual_.Restore(settings_.manual_keep_awake, settings_.manual_end_utc_milliseconds,
                    GetTickCount64(), UtcMilliseconds());
    if (settings_.manual_keep_awake && !manual_.Active()) {
        SaveManualSession();
    }
    SyncManualLatch();
    diagnostics_.Write(
        L"app_started",
        L"version=" + std::wstring(kAgentLatchVersion) + L" pid=" + std::to_wstring(GetCurrentProcessId()));
    if (manual_.Active()) {
        diagnostics_.Write(L"manual_session_restored", manual_.Deadline() == 0 ?
            L"mode=until_stopped" : L"mode=timed original_end_preserved=1");
    }
    const UINT dpi = GetDpiForSystem();
    renderer_.Initialize(dpi);

    idle_icon_ = CreateStateIcon(RGB(45, 57, 75), RGB(203, 213, 225));
    active_icon_ = CreateStateIcon(RGB(18, 78, 63), RGB(52, 211, 153));
    manual_icon_ = CreateStateIcon(RGB(27, 57, 96), RGB(96, 165, 250));
    warning_icon_ = CreateStateIcon(RGB(78, 48, 14), RGB(251, 191, 36));

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
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    AdjustWindowRectEx(&window_rectangle, style, FALSE, 0);
    const int window_width = window_rectangle.right - window_rectangle.left;
    RECT work_area{};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0)) {
        work_area = RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    }
    const int window_height = std::min(window_rectangle.bottom - window_rectangle.top,
                                       work_area.bottom - work_area.top);
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
    if (!controls_.Initialize(window_, dpi)) { return false; }

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
        if (controls_.TranslateKey(message) || IsDialogMessageW(window_, &message)) { continue; }
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
    diagnostics_.Write(
        L"app_stopped",
        L"active=" + std::to_wstring(latches_.IsActive() ? 1 : 0) +
            L" instances=" + std::to_wstring(latches_.ActiveInstanceCount()));
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
    if (warning_icon_ != nullptr) {
        DestroyIcon(warning_icon_);
        warning_icon_ = nullptr;
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

DashboardState AgentLatchApp::DashboardSnapshot() const {
    DashboardState state;
    state.page = controls_.Page();
    state.active = latches_.IsActive();
    state.system_request_accepted = power_request_.IsAvailable() &&
        power_request_.LastSystemError() == ERROR_SUCCESS &&
        (!state.active || power_request_.IsSystemRequired());
    state.system_request_error = power_request_.LastSystemError();
    state.display_request_accepted = !state.active || !settings_.keep_display_on ||
        (power_request_.IsDisplayRequired() && power_request_.LastDisplayError() == ERROR_SUCCESS);
    state.display_request_error = power_request_.LastDisplayError();
    state.keep_display_on = settings_.keep_display_on;
    state.start_with_windows = IsStartWithWindowsEnabled();
    state.manual_active = manual_.Active();
    state.manual_deadline = manual_.Deadline();
    state.now = GetTickCount64();
    state.latches = latches_.Snapshot();
    state.settings = settings_;
    return state;
}

void AgentLatchApp::ChangeProviderMode(Provider provider, DetectionMode mode) {
    if (settings_.ProviderMode(provider) == mode) { return; }
    if (latches_.Find(L"restart:" + std::wstring(ProviderKey(provider))) != nullptr) {
        diagnostics_.Write(L"restart_grace_ended", std::wstring(L"provider=") +
            ProviderShortName(provider) + L" reason=mode_changed");
    }
    settings_.SetProviderMode(provider, mode);
    latches_.RemoveByProvider(provider);
    diagnostics_.Write(L"provider_mode_changed", std::wstring(L"provider=") +
        ProviderShortName(provider) + L" mode=" + DetectionModeLabel(mode));
    if (!settings_.Save()) {
        MessageBoxW(window_, L"Windows could not save the selected agent mode.", L"AgentLatch", MB_OK | MB_ICONWARNING);
    }
    Tick(true);
}

void AgentLatchApp::SaveManualSession() {
    settings_.manual_keep_awake = manual_.Active();
    settings_.manual_end_utc_milliseconds = manual_.UtcEnd();
    if (!settings_.Save()) {
        diagnostics_.Write(L"manual_persistence_failed", L"reason=settings_write_failed");
        MessageBoxW(window_, L"The current session was updated, but Windows could not save it. "
                    L"It may not be restored correctly after restarting AgentLatch.",
                    L"AgentLatch", MB_OK | MB_ICONWARNING);
    }
}

void AgentLatchApp::SyncManualLatch() {
    const ULONGLONG now = GetTickCount64();
    if (manual_.Expire(now)) { SaveManualSession(); }
    if (manual_.Active()) {
        latches_.Upsert(L"manual:keep-awake", Provider::Manual,
            manual_.Deadline() == 0 ? LatchKind::Manual : LatchKind::Timer,
            L"Manual keep-awake", L"Independent of agent activity", now,
            manual_.Deadline() == 0 ? 0 : manual_.Deadline() - now);
    } else {
        latches_.Remove(L"manual:keep-awake");
    }
}

void AgentLatchApp::StartManualSession(unsigned int hours) {
    if (manual_.Start(hours, GetTickCount64(), UtcMilliseconds())) {
        SaveManualSession();
        SyncManualLatch();
        diagnostics_.Write(L"manual_session_started", hours == 0 ? L"mode=until_stopped" :
            L"mode=timed hours=" + std::to_wstring(hours));
        Tick(false);
    }
}

void AgentLatchApp::ShowManualMenu() {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) { return; }
    AppendMenuW(menu, MF_STRING, 1, L"Until stopped");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    constexpr std::array<unsigned int, 6> hours = {1, 2, 4, 8, 12, 24};
    for (unsigned int duration : hours) {
        const std::wstring label = std::to_wstring(duration) + (duration == 1 ? L" hour" : L" hours");
        AppendMenuW(menu, MF_STRING, 100 + duration, label.c_str());
    }
    AppendMenuW(menu, MF_STRING, 2, L"Custom hours...");
    POINT location{};
    GetCursorPos(&location);
    if (GetFocus() == controls_.ActionWindow(UiAction::ManualMenu)) {
        RECT bounds{}; GetWindowRect(controls_.ActionWindow(UiAction::ManualMenu), &bounds);
        location = POINT{bounds.left, bounds.bottom};
    }
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        location.x, location.y, 0, window_, nullptr);
    DestroyMenu(menu);
    if (command == 1) {
        StartManualSession(0);
    } else if (command >= 101 && command <= 124) {
        StartManualSession(command - 100);
    } else if (command == 2) {
        const INT_PTR duration = DialogBoxParamW(instance_, MAKEINTRESOURCEW(IDD_MANUAL_HOURS),
            window_, ManualHoursProcedure, 0);
        if (duration > 0) { StartManualSession(static_cast<unsigned int>(duration)); }
    }
}

void AgentLatchApp::Tick(bool scan_agents) {
    const ULONGLONG now = GetTickCount64();
    if (manual_.Expire(now)) {
        diagnostics_.Write(L"manual_session_ended", L"reason=timer_expired");
        SaveManualSession();
    }
    for (const Latch& latch : latches_.Snapshot()) {
        if (latch.kind == LatchKind::RestartGrace && latch.expires_at <= now) {
            diagnostics_.Write(L"restart_grace_ended", std::wstring(L"provider=") +
                ProviderShortName(latch.provider) + L" reason=deadline_reached");
        }
    }
    latches_.Expire(now);
    if (scan_agents) {
        settings_.RefreshIntegrationStatus();
        UpdateDetectorLatches();
    }
    ReconcilePowerState();
    UpdateTrayIcon();
    controls_.Update(DashboardSnapshot());
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
                                      ? result.recently_active && settings_.UseProcessActivityFallback(result.provider)
                                      : mode == DetectionMode::Open && result.running_instances > 0;
        if (should_latch) {
            const bool task_mode = mode == DetectionMode::Tasks;
            const unsigned int instance_count = DetectorLatchInstanceCount(result, mode);
            latches_.Upsert(
                id,
                result.provider,
                LatchKind::Detector,
                DetectorLatchLabel(result, mode),
                task_mode ? result.activity_detail : result.open_detail,
                now,
                task_mode && result.in_grace_period ? result.grace_remaining_milliseconds : 0,
                instance_count,
                task_mode && result.in_grace_period);
        } else {
            if (latches_.Find(id) != nullptr) {
                diagnostics_.Write(L"detector_released", std::wstring(L"provider=") +
                    ProviderShortName(result.provider) + L" reason=" +
                    (mode == DetectionMode::Off ? L"disabled" :
                     result.running_instances == 0 ? L"process_absent" : L"activity_finished"));
            }
            latches_.Remove(id);
        }
        const std::wstring restart_id = L"restart:" + std::wstring(ProviderKey(result.provider));
        const Latch* previous_restart = latches_.Find(restart_id);
        const bool had_restart = previous_restart != nullptr;
        bool protected_work = false;
        for (const Latch& latch : latches_.Snapshot()) {
            if (latch.provider == result.provider && latch.kind != LatchKind::RestartGrace) {
                protected_work = true;
                break;
            }
        }
        const ULONGLONG deadline = restart_grace_.Update(result.provider, mode,
            result.running_instances > 0, protected_work, now);
        if (deadline != 0) {
            latches_.Upsert(restart_id, result.provider, LatchKind::RestartGrace,
                std::wstring(ProviderShortName(result.provider)) + L" · restart grace",
                L"Waiting for the app to return after a close, update, or restart",
                now, deadline - now, 1, true);
            if (!had_restart) {
                diagnostics_.Write(L"restart_grace_started", std::wstring(L"provider=") +
                    ProviderShortName(result.provider) + L" seconds=600 reason=process_absent");
            }
        } else {
            latches_.Remove(restart_id);
            if (had_restart) {
                diagnostics_.Write(L"restart_grace_ended", std::wstring(L"provider=") +
                    ProviderShortName(result.provider) + L" reason=" +
                    (mode == DetectionMode::Off ? L"disabled" :
                     result.running_instances > 0 ? L"app_returned" : L"deadline_reached"));
            }
        }
    }
}

void AgentLatchApp::ReconcilePowerState() {
    const bool active = latches_.IsActive();
    const std::vector<Latch> snapshot = latches_.Snapshot();
    const std::wstring latch_detail = LatchDiagnosticDetail(snapshot);
    if (latch_detail != last_latch_diagnostic_) {
        diagnostics_.Write(L"latch_state", latch_detail);
        last_latch_diagnostic_ = latch_detail;
    }

    power_request_.Apply(active, active && settings_.keep_display_on);
    const DWORD system_error = power_request_.LastSystemError();
    const DWORD display_error = power_request_.LastDisplayError();
    const bool system_confirmed = active && power_request_.IsAvailable() &&
                                  power_request_.IsSystemRequired() && system_error == ERROR_SUCCESS;
    const bool display_confirmed = !active || !settings_.keep_display_on ||
                                   (power_request_.IsDisplayRequired() && display_error == ERROR_SUCCESS);
    protection_failed_ = active && !system_confirmed;

    std::wostringstream power_detail;
    power_detail << L"active=" << (active ? 1 : 0)
                 << L" system=" << (power_request_.IsSystemRequired() ? 1 : 0)
                 << L" display=" << (power_request_.IsDisplayRequired() ? 1 : 0)
                 << L" system_error=" << system_error
                 << L" display_error=" << display_error;
    const std::wstring power_diagnostic = power_detail.str();
    if (power_diagnostic != last_power_diagnostic_) {
        diagnostics_.Write(
            protection_failed_ ? L"power_request_failed"
                               : display_confirmed ? L"power_request_state" : L"display_request_failed",
            power_diagnostic);
        last_power_diagnostic_ = power_diagnostic;
    }

    if (protection_failed_ && !protection_failure_alerted_) {
        ShowProtectionFailureNotification(system_error);
        protection_failure_alerted_ = true;
    } else if (!protection_failed_) {
        protection_failure_alerted_ = false;
    }
    if (!transition_state_initialized_) {
        last_active_state_ = active;
        transition_state_initialized_ = true;
    } else if (last_active_state_ != active) {
        last_active_state_ = active;
        if (!active || !protection_failed_) {
            ShowTransitionNotification(active);
        }
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
    if (fields[0] == L"CAPTURE_UI" && fields.size() == 2 && !fields[1].empty()) {
        const bool success = ExportDashboardScreenshots(DashboardSnapshot(), fields[1], GetDpiForWindow(window_));
        diagnostics_.Write(L"dashboard_capture", success ? L"success=1" : L"success=0");
        return success;
    }
    if (fields[0] == L"MANUAL_START" && fields.size() == 2) {
        ULONGLONG hours = 0;
        if (!ParseUnsignedLongLong(fields[1], &hours) || hours > ManualKeepAwake::kMaximumHours) {
            return false;
        }
        StartManualSession(static_cast<unsigned int>(hours));
        return true;
    }
    if (fields[0] == L"MANUAL_STOP" && fields.size() == 1) {
        ExecuteAction(UiAction::StopManual);
        return true;
    }
    if (fields[0] == L"REMOVE" && fields.size() == 2) {
        const Latch* existing = latches_.Find(fields[1]);
        const Provider provider = existing == nullptr ? Provider::External : existing->provider;
        const bool released_hook = existing != nullptr && existing->kind == LatchKind::Hook;
        latches_.Remove(fields[1]);
        if (released_hook && settings_.ProviderMode(provider) == DetectionMode::Tasks) {
            if (HoldFinalHookRelease(latches_, provider, GetTickCount64())) {
                // Updaters can send SessionEnd before the process disappears.
                // Keep power continuously held until the next presence samples.
                diagnostics_.Write(L"hook_release_grace", std::wstring(L"provider=") +
                    ProviderShortName(provider) + L" seconds=5");
            }
        }
        Tick(true);
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
    if (kind == LatchKind::Hook) {
        latches_.Remove(L"hook-settle:" + std::wstring(ProviderKey(provider)));
    }
    Tick(false);
    return true;
}

void AgentLatchApp::ExecuteAction(UiAction action) {
    switch (action) {
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
        case UiAction::ManualMenu:
            ShowManualMenu();
            break;
        case UiAction::StopManual:
            manual_.Stop();
            latches_.Remove(L"manual:keep-awake");
            diagnostics_.Write(L"manual_session_ended", L"reason=user_stopped");
            SaveManualSession();
            break;
        case UiAction::OpenDiagnostics:
            OpenDiagnostics();
            break;
        case UiAction::MoreMenu: {
            RECT bounds{}; GetWindowRect(controls_.ActionWindow(UiAction::MoreMenu), &bounds);
            POINT location{bounds.left, bounds.bottom};
            ShowTrayMenu(location);
            break;
        }
        case UiAction::None:
            return;
    }
    Tick(true);
}

void AgentLatchApp::ShowTrayMenu(POINT location) {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }

    AppendMenuW(menu, MF_STRING | MF_DEFAULT, kMenuOpen, L"Open AgentLatch");
    std::wstring status = latches_.IsActive()
                              ? L"Latched · " + std::to_wstring(latches_.ActiveInstanceCount()) + L" active"
                              : L"Windows can sleep";
    AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, status.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuManual, manual_.Active() ? L"Change manual keep-awake..." : L"Keep awake manually...");
    if (manual_.Active()) {
        AppendMenuW(menu, MF_STRING, kMenuStopManual, L"Stop manual keep-awake");
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (settings_.keep_display_on ? MF_CHECKED : MF_UNCHECKED), kMenuToggleDisplay, L"Keep display on while latched");
    AppendMenuW(
        menu,
        MF_STRING | (IsStartWithWindowsEnabled() ? MF_CHECKED : MF_UNCHECKED),
        kMenuToggleStartup,
        L"Start automatically at sign-in");
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
    AppendMenuW(menu, MF_STRING, kMenuOpenDiagnostics, L"Open diagnostic history");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuAbout, L"About AgentLatch");
    AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit");

    SetForegroundWindow(window_);
    const UINT command = TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, location.x, location.y, 0, window_, nullptr);
    DestroyMenu(menu);
    if (command == kMenuOpen) {
        ShowDashboard();
    } else if (command == kMenuOpenDiagnostics) {
        OpenDiagnostics();
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
        L"\n\nA lightweight, open-source, agent-aware wake manager for Windows."
        L"\n\nUnexpected-exit recovery · Local diagnostic history"
        L"\nNo account · No telemetry · No administrator rights";
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

void AgentLatchApp::ShowProtectionFailureNotification(DWORD error) {
    if (!tray_added_) {
        return;
    }
    tray_icon_.uFlags = NIF_GUID | NIF_INFO;
    lstrcpynW(
        tray_icon_.szInfoTitle,
        L"AgentLatch sleep protection failed",
        static_cast<int>(std::size(tray_icon_.szInfoTitle)));
    const std::wstring message =
        L"Active agent work was detected, but Windows rejected the keep-awake request (error " +
        std::to_wstring(error) + L"). AgentLatch will keep retrying.";
    lstrcpynW(tray_icon_.szInfo, message.c_str(), static_cast<int>(std::size(tray_icon_.szInfo)));
    tray_icon_.dwInfoFlags = NIIF_WARNING | NIIF_NOSOUND;
    Shell_NotifyIconW(NIM_MODIFY, &tray_icon_);
}

void AgentLatchApp::OpenDiagnostics() {
    const std::wstring path = diagnostics_.Path();
    if (path.empty() || !FileExists(path)) {
        MessageBoxW(window_, L"The diagnostic history is not available.", L"AgentLatch", MB_OK | MB_ICONWARNING);
        return;
    }
    const HINSTANCE result = ShellExecuteW(
        window_, L"open", path.c_str(), nullptr, GetExecutableDirectory().c_str(), SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        MessageBoxW(window_, L"Windows could not open the diagnostic history.", L"AgentLatch", MB_OK | MB_ICONERROR);
    }
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
    tray_icon_.hIcon = protection_failed_
                           ? warning_icon_
                           : latches_.IsActive() ? (manual ? manual_icon_ : active_icon_) : idle_icon_;
    const std::wstring tip = protection_failed_
                                 ? L"AgentLatch · Sleep protection error"
                                 : latches_.IsActive()
                                 ? L"AgentLatch · Latched · " + std::to_wstring(latches_.ActiveInstanceCount()) + L" active"
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
            renderer_.Paint(dc, client, DashboardSnapshot());
            EndPaint(window_, &paint);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_COMMAND: {
            if (LOWORD(wparam) == IDCANCEL) { ShowWindow(window_, SW_HIDE); return 0; }
            Provider provider{}; DetectionMode mode{};
            if (controls_.ReadProviderChange(wparam, &provider, &mode)) {
                ChangeProviderMode(provider, mode);
            } else { ExecuteAction(controls_.ActionForCommand(wparam)); }
            return 0;
        }
        case WM_NOTIFY:
            if (controls_.HandleNotify(reinterpret_cast<const NMHDR*>(lparam))) { return 0; }
            break;
        case WM_DRAWITEM:
            if (controls_.DrawItem(reinterpret_cast<const DRAWITEMSTRUCT*>(lparam))) { return TRUE; }
            break;
        case WM_MEASUREITEM:
            if (controls_.MeasureItem(reinterpret_cast<MEASUREITEMSTRUCT*>(lparam))) { return TRUE; }
            break;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLORLISTBOX:
            return reinterpret_cast<LRESULT>(controls_.ControlColor(
                reinterpret_cast<HDC>(wparam), reinterpret_cast<HWND>(lparam)));
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
            controls_.SetDpi(HIWORD(wparam));
            const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
            MONITORINFO monitor{};
            monitor.cbSize = sizeof(monitor);
            GetMonitorInfoW(MonitorFromRect(suggested, MONITOR_DEFAULTTONEAREST), &monitor);
            const int height = std::min(suggested->bottom - suggested->top,
                                        monitor.rcWork.bottom - monitor.rcWork.top);
            const int top = std::clamp(suggested->top, monitor.rcWork.top,
                                       std::max(monitor.rcWork.top, monitor.rcWork.bottom - height));
            SetWindowPos(
                window_,
                nullptr,
                suggested->left,
                top,
                suggested->right - suggested->left,
                height,
                SWP_NOACTIVATE | SWP_NOZORDER);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lparam);
            const SIZE preferred = renderer_.PreferredClientSize();
            info->ptMinTrackSize.x = preferred.cx;
            info->ptMinTrackSize.y = std::min<LONG>(preferred.cy, GetSystemMetrics(SM_CYSCREEN));
            return 0;
        }
        case WM_SIZE:
            controls_.Layout();
            return 0;
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
