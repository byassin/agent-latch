# Interface guide

AgentLatch 0.4 is activity-first: compact navigation and protection status, five full activity rows, then a small independent manual override. It remains a native Win32 tray utility, not a web page or WebView.

## Overview

The protection summary stays visible on every tab, without a redundant status badge. **Ready when your agents are** means normal Windows idle-sleep policy applies; **Sleep protection is on** means Windows accepted AgentLatch's system power request; **Sleep protection needs attention** means a failed request is being retried. READY/PROTECTED/ERROR remain the tray/diagnostic terms. Accepted protection is not a guarantee against explicit Sleep, a forced restart, critical system policy, or power loss.

Use **Keep awake…** in the bottom **Manual override** strip to choose Until stopped, a quick hour preset, or 1–168 custom hours. An active session shows its real remaining time or Until stopped. **Change…** replaces only that session. **Stop** leaves agent tracking and other reasons alone. Saved sessions restore across app restarts; timers retain their original end.

Cancel, Escape, or the custom-hours dialog's close button leaves the existing session unchanged.

The activity list gives priority to agent work. Each row has a colored provider marker, name, signal/grace description, and right-aligned task status. Five complete rows fit without a scrollbar. More than five sources enables named Previous/Next buttons; Page Up/Page Down or the mouse wheel over the list also changes pages. The current page survives monitoring refreshes and clamps safely when sources disappear.

The source count is the number of agent/custom activity rows, not a total task count. One Codex source can contain multiple tasks. The built-in manual session appears only in its separate strip, so it is not duplicated or counted as an agent. Custom external requests remain visible even when their source key is `manual`. A calm, centered empty state explains when the built-in manual override alone is keeping the PC awake. Activity grace and update/restart grace are labeled as grace, not running tasks or predicted completion times.

![Overview](../assets/dashboard.png)

## Agents

Each provider has an explicit dropdown:

| Selection | Behavior |
|---|---|
| Tasks only | Track actual task signals; process activity is a qualified fallback. |
| While open | Protect while the app/CLI is detected, even without a task. |
| Off | Remove this provider's automatic protection; manual protection is independent. |

These map to `Tasks`, `Open`, and `Off` internally. A protected provider disappearing may retain its fixed ten-minute restart grace. A returning app follows the chosen mode again. Selecting a page is navigation only—it never changes tracking policy. Provider names are exposed as accessible names for their native dropdowns.

![Agents](../assets/agents.png)

## Settings

- **Keep the screen on while protected:** affects the display request, not whether the PC itself stays awake.
- **Start AgentLatch when I sign in:** starts monitoring and restores a saved manual session after sign-in.
- **Show status notifications:** controls routine wake-state alerts. Protection failures still alert.

Each preference is a whole-label native checkbox styled as a dark switch, with an explicit On/Off label. Native checked state, keyboard activation and accessibility are retained. **Diagnostic history** opens the local bounded log. **Repair integrations…** is an intentional maintenance action, not a required first-run setup step. Neither action runs just by visiting Settings.

![Settings](../assets/settings.png)

## Keyboard and tray behavior

Use Tab/Shift+Tab between native controls, Ctrl+Tab/Ctrl+Shift+Tab between pages, arrow keys on tabs/dropdowns/lists, Space for checkboxes, and Enter/Space for focused buttons. Focus indicators are visible. Closing the main window keeps monitoring in the tray; More… → Exit ends the process and its current power request, while preserving the saved manual choice for a later launch.

The preferred client size is 620 × 560 logical pixels. Native controls and typography scale with Windows DPI. There are no main-window or activity-list scrollbars; the list uses bounded pages instead. Keyboard focus indicators, disabled pager states, and button hover/press feedback remain visible.

## Capture provenance and validation

The documentation images are off-screen exports of the running app's current state through `--export-ui-screenshots`. They use the same renderer, child windows, labels, and settings as the visible UI. They are not photographs of the desktop, mock data, or evidence of a completed provider task.

Export creates only a hidden `WS_EX_NOACTIVATE` capture window and never changes the visible page, input focus, mouse, keyboard, or system power plan. Output stays local until a user explicitly publishes it. Review any custom external-lease labels before sharing.

Automated native tests cover page visibility, mode mapping, five complete rows, source pagination and page preservation, absence of both scrollbar styles, switch checked state, tab fit, control bounds, and off-screen renders at 96/120/144/168/192 DPI (100–200% scaling). Fixtures include timed, indefinite, idle, error, long-list, and mixed four-source activity states.

Custom-painted tabs, collapsed provider dropdowns, switches, and rounded buttons paint their full rectangular background before their foreground. Independent control tests start with white and magenta pixels and verify all outer corners, including disabled/pressed/focused button states, both switch states, and both print paths. This catches artifacts that a pre-painted whole-dashboard export can conceal. Native accessibility text retains source names and statuses even though rows are custom-painted.

These checks establish UI structure/rendering, not overnight or provider-specific lifecycle qualification. They do not activate a visible window or send mouse/keyboard input.
