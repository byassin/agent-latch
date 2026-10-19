#pragma once

#include "ui.h"

namespace agent_latch {

// Renders the real HWND control tree from one live state snapshot. Never shows
// a window, changes the visible tab, captures the desktop, or changes focus.
bool ExportDashboardScreenshots(const DashboardState& state, const std::wstring& directory, UINT dpi);

}  // namespace agent_latch
