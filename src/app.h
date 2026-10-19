#pragma once

#include "agent_detector.h"
#include "diagnostics.h"
#include "latch_registry.h"
#include "power_request.h"
#include "settings.h"
#include "ui.h"
#include "wake_sessions.h"

#include <windows.h>
#include <shellapi.h>

#include <string>

namespace agent_latch {

std::wstring DetectorLatchLabel(const DetectionResult& result, DetectionMode mode);
unsigned int DetectorLatchInstanceCount(const DetectionResult& result, DetectionMode mode);

class AgentLatchApp {
public:
    explicit AgentLatchApp(HINSTANCE instance);
    ~AgentLatchApp();

    AgentLatchApp(const AgentLatchApp&) = delete;
    AgentLatchApp& operator=(const AgentLatchApp&) = delete;

    int Run(bool show_window);

private:
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

    bool Initialize(bool show_window);
    void Shutdown();
    void ShowDashboard();
    void ShowTrayMenu(POINT location);
    void ExecuteAction(UiAction action);
    void Tick(bool scan_agents);
    void ReconcilePowerState();
    void UpdateDetectorLatches();
    bool ProcessIpcMessage(const std::wstring& message);
    void UpdateTrayIcon();
    void ShowTransitionNotification(bool active);
    void ShowProtectionFailureNotification(DWORD error);
    void OpenDiagnostics();
    void LaunchHookSetup();
    void ShowAbout();
    void ShowManualMenu();
    void StartManualSession(unsigned int hours);
    void SaveManualSession();
    void SyncManualLatch();
    DashboardState DashboardSnapshot() const;
    void ChangeProviderMode(Provider provider, DetectionMode mode);

    HICON CreateStateIcon(COLORREF background, COLORREF foreground) const;
    bool AddTrayIcon();
    void RemoveTrayIcon();

    HINSTANCE instance_{nullptr};
    HWND window_{nullptr};
    UINT taskbar_created_message_{0};
    NOTIFYICONDATAW tray_icon_{};
    bool tray_added_{false};
    bool shutting_down_{false};
    bool transition_state_initialized_{false};
    bool last_active_state_{false};
    bool protection_failed_{false};
    bool protection_failure_alerted_{false};
    std::wstring last_latch_diagnostic_;
    std::wstring last_power_diagnostic_;
    HICON idle_icon_{nullptr};
    HICON active_icon_{nullptr};
    HICON manual_icon_{nullptr};
    HICON warning_icon_{nullptr};

    Settings settings_;
    DiagnosticsLog diagnostics_;
    LatchRegistry latches_;
    PowerRequest power_request_;
    AgentDetector detector_;
    DashboardRenderer renderer_;
    DashboardControls controls_;
    AgentRestartGrace restart_grace_;
    ManualKeepAwake manual_;
};

}  // namespace agent_latch
