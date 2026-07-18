#pragma once

#include "agent_detector.h"
#include "latch_registry.h"
#include "power_request.h"
#include "settings.h"
#include "ui.h"

#include <windows.h>
#include <shellapi.h>

#include <string>

namespace agent_latch {

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
    void LaunchHookSetup();
    void ShowAbout();

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
    HICON idle_icon_{nullptr};
    HICON active_icon_{nullptr};
    HICON manual_icon_{nullptr};

    Settings settings_;
    LatchRegistry latches_;
    PowerRequest power_request_;
    AgentDetector detector_;
    DashboardRenderer renderer_;
};

}  // namespace agent_latch
