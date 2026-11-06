# Direct manual controls and read-only activity

Available in **0.5.2**. The title bar and status bar show the installed version. If you still see an older version, install the latest setup executable from the [GitHub release page](https://github.com/byassin/agent-latch/releases/latest).

## Manual override

On **Overview**, choose a duration directly in the **Manual override** group:

- **Until Windows restarts**, or a preset of 1, 2, 4, 8, 12 or 24 hours.
- **Custom hours**, then enter a whole number from **1 to 168** in the field that appears.

Click **Start once** to enable the override. The status becomes **On · Until Windows restarts** or **On · time remaining**, and an inline message confirms that it started. There is no second Apply step. The action becomes **Update duration** and is disabled while the selected duration is unchanged.

To change an active session, choose a different valid duration. **Update duration** becomes enabled; click it to start the replacement duration from that moment. The inline message confirms **Duration updated**, and the action becomes disabled again. Choosing the original duration again cancels the draft change. Equivalent preset/custom values count as the same duration; typing a different value is not an update until you click the action.

Monitoring refreshes do not discard a draft or move the running deadline. Invalid/empty custom input immediately explains the 1–168 range and disables Start/Update duration while leaving the current session and Stop unchanged. If you deliberately want to restart the same duration, use Stop, then Start.

The current On/Off state/countdown is separate from the duration you are choosing. **Stop** ends only the manual session, not automatic agent protection, and restores the Start action. Expiry also returns to Off/Start. The tray's quick-duration menu still works. Both timed and unlimited sessions reset after Windows restarts; app-only restarts retain the session and its original end time. Confirmation is inline, not an additional Windows notification.

## Read-only activity

Activity is a live status list, not a selection box. Each row shows its source, signal and status. Clicking does not select or highlight a row; there is no details pane. The list is skipped by Tab navigation so it does not suggest a row action. Use the mouse wheel or its native scrollbar when there are more sources than fit on screen. Monitoring refreshes preserve the scroll position.

Some sources aggregate several tasks. The count is real detection metadata; it is not a list of individual task names. AgentLatch does not read chat prompts or responses, and does not invent missing task details. An Active status is not a task-completion estimate. A release-grace countdown describes protection timing only.

## Light and dark previews

These lossless 1020 × 780 PNGs are **synthetic test fixtures**, rendered from the actual hidden native controls at 150% layout scale. Their example activity demonstrates the interface, not live agent activity or overnight protection.

![Dark interface with read-only activity and visible manual controls](../assets/dashboard.png)

![Light interface with read-only activity and visible manual controls](../assets/light/dashboard.png)

The preferred client remains 680 × 520 logical pixels, with a 600 × 460 minimum. The activity list scrolls normally without making the whole window a scrolling page.

## Verification boundaries

The release checks include a disposable-runner Claude hook/IPC test that acquires protection, handles background workers, releases the final latch after settling grace, and clears the Windows power request. Installed diagnostics can show live Claude background snapshots and Codex activity; they are not an exhaustive live completion test of every provider session.

The self-test exercises the real in-memory manual-session engine together with hidden native controls: one-step Start, On status, confirmation after model acknowledgement, disabled/guarded same-duration commands, edits/reverts, equivalent preset/custom values, invalid input, update acknowledgement, unchanged timer deadlines, external updates, Stop and expiry. It also covers rejected row selection, sibling hit targets and concurrent Codex parent/worker directory scanning. Native UI checks run in light/dark themes at 100%, 125%, 150%, 175% and 200% layout scales. No production manual override is changed by these tests. The Codex cases confirm the scanner clears final completion before the separately tested detector grace expires.

The blank manual panel in 0.5.0 was a separate paint-clipping bug: its sibling frame could draw over the controls. A repaint regression creates its windows on a separate test desktop that is **never switched to or activated**. It verifies that the frame's real paint DC excludes every visible manual field/button after tab changes and container-only repaints. This test fails without sibling clipping and passes with it. It does not send input or show windows on your working desktop; screenshot export alone would not catch this bug.

Off-screen screenshot exports do not change tabs or preferences in the running dashboard. None of these checks controls the working desktop or proves that Windows cannot be explicitly shut down, restarted or put to sleep.
