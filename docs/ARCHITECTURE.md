# Architecture

AgentLatch is one native Win32 executable running in two lightweight modes: the tray application and a blocking companion watchdog. It has no service, web runtime, administrator requirement, or elevated component.

```text
Agent processes ── process/activity scan ─┐
Codex sessions ─── task lifecycle scan ───┤
OpenAI composer ── UI Automation worker ──┤
                                         │
Lifecycle hooks ── JSON stdin / WM_COPYDATA ──> Latch registry ──> Windows power request
                                         │            │
Manual controls + local lease CLI ──────┘            ├──> Dashboard + tray state
                                                      └──> Bounded diagnostic history

Main process <──── wait/restart on abnormal exit ──── Watchdog process
```

## Components

| Component | Responsibility |
|---|---|
| `AgentDetector` | Reads Codex desktop task lifecycle markers, supplies packaged OpenAI process IDs to the response probe, merges authoritative signals, enumerates known agent roots, follows descendants, and samples eligible CLI CPU/I/O activity. |
| `OpenAIUnifiedActivityProbe` | Runs Windows UI Automation on a background thread and classifies only the unified OpenAI app's primary composer command as idle or responding. |
| `HookBridge` | Reads bounded JSON hook input, extracts lifecycle identity, and converts events to lease operations. |
| `LatchRegistry` | Owns independent detector, hook, and external latches; expires bounded leases. |
| `AgentRestartGrace` | Tracks protected process disappearance and fixed ten-minute per-provider recovery deadlines. |
| `ManualKeepAwake` | Owns the explicit manual session, validates duration, restores the remaining interval, and expires timed protection. |
| `PowerRequest` | Creates and reconciles Windows `PowerSetRequest` system/display requirements. |
| `DiagnosticsLog` | Appends bounded, content-free protection and lifecycle events to a rotating local log. |
| `Watchdog` | Waits without polling, restarts an abnormally terminated main process, and stops after three rapid failures. |
| `AgentLatchApp` | Hosts the message loop, tray icon, single-instance IPC, settings, and reconciliation cycle. |
| `DashboardRenderer` | Paints the compact DPI-aware visual shell and protection surfaces with native GDI. |
| `DashboardControls` | Owns real Windows tabs, buttons, provider dropdowns, checkboxes, accessible labels, focus, and the complete reasons list. |
| `ExportDashboardScreenshots` | Renders one live state snapshot into hidden native controls and exports three PNGs without desktop capture or input. |

## Power semantics

When the registry changes from zero to one active latch, AgentLatch sets `PowerRequestSystemRequired`. When the registry returns to zero, it clears that request. If **Keep display on** is enabled, it also sets `PowerRequestDisplayRequired` for the same active interval. Failed operations retain their desired/actual mismatch, so the two-second reconciliation cycle retries them. System and display errors are tracked independently.

The dashboard reports **Sleep protection is on** (PROTECTED in the tray) only when an active latch exists, the power-request handle is valid, Windows accepted the system request, and the cached system state is set. A failed system request changes the dashboard to **Sleep protection needs attention** and the tray to ERROR, records the Windows error code, and emits one critical warning per failure episode. Routine transition notifications remain optional.

The request blocks automatic idle sleep. It does not block an explicit user sleep, shutdown, restart, or critical system action.

## Detection semantics

The process detector scans every two seconds. A known agent root owns its descendant process tree. Each provider has an independent mode:

- **Tasks** accepts unified OpenAI app response state, native Codex desktop lifecycle state, lifecycle-hook latches, and recent activity from task-capable CLI process trees when the provider has no authoritative managed integration.
- **Open** accepts process presence, including desktop shells.
- **Off** accepts neither.

Electron desktop applications are presence-capable but their process CPU or I/O is not activity-capable because background renderers, update checks, and language servers remain busy while the agent is idle. This prevents the OpenAI, Claude, Cursor, and Antigravity shells from creating false task latches. The unified OpenAI app has two explicit signal sources: AgentLatch scans recent local Codex session files for the latest `task_started` or `task_complete` marker and independently observes the primary composer command changing between idle and response state. The packaged `ChatGPT.exe` shell is classified as OpenAI/Codex only when it runs from the `OpenAI.Codex` Windows app package.

The OpenAI response probe runs outside the Win32 message-loop thread and polls every two seconds. It enumerates top-level windows only for the packaged OpenAI root PIDs supplied by `AgentDetector`, finds a Button whose class contains the exact `size-token-button-composer` and `bg-primary-solid` tokens, and walks to the nearest Document ancestor to distinguish ChatGPT from Codex. English Send and Stop names are recognized directly. Other locales learn a stable disabled idle command after three observations. A minimized composer remains valid even when UI Automation reports it as offscreen. Unknown Documents, empty names, inaccessible controls, COM failures, and snapshots older than four seconds produce no new latch.

Codex JSONL state remains authoritative. When one or more lifecycle tasks are active, a simultaneous Codex composer response does not add another instance. A regular ChatGPT response is a distinct concurrent reason and adds one instance even while Codex tasks run. A ChatGPT response or a Codex composer response with no matching JSONL task contributes one active instance and precise dashboard detail. Concurrent native OpenAI reasons share one source row with an exact concurrent count in its Status column. The overview headline counts source rows; the tray menu and tooltip retain total active-instance counts. While active, either native OpenAI signal refreshes the provider activity timestamp.

The detector's activity grace defaults to 180 seconds and is clamped to 30–1,800 seconds when loaded. After a response ends or a Codex completion marker is discovered, the OpenAI detector can therefore remain latched for up to three minutes while the packaged app remains open. This covers brief thinking, network, orchestration, and signal-transition gaps. Closing the provider releases that activity detector latch, but may replace it atomically with the restart grace below. Provider **Off** mode releases its automatic latches immediately. Activity grace does not alter independent hook or external lease deadlines.

### Update/restart gaps

`AgentRestartGrace` observes each provider's mode, presence, and protection state. Open-mode presence arms it. Tasks-mode presence arms it only while a detector or hook reason is observed, with a five-second observation tolerance for a SessionEnd hook arriving just before process removal. Releasing the final hook for a Tasks-mode provider creates one fixed five-second `hook-settle:` release-grace latch and immediately scans presence; this prevents a hook-before-process-removal ordering from briefly clearing the power request. Normal hook completion therefore has up to five seconds of settling grace. An idle app with no recently protected work never arms Tasks-mode restart grace. Once an armed provider disappears, the app adds one `RestartGrace` latch with a fixed `now + 600,000 ms` deadline before reconciling power. Repeated absent scans reuse that deadline; expiry cannot re-arm while the process remains absent. A failed/empty process snapshot is therefore tolerated for a bounded interval rather than causing immediate release.

The returning process cancels restart grace; current mode rules decide subsequent protection. Deliberate closes and updates cannot reliably be distinguished, so both receive the same grace. Turning a provider Off removes the grace immediately, and mode changes reset the state machine. Independent providers have independent deadlines. Restart grace is memory-only: it does not persist across AgentLatch restarts. Manual protection is the explicit alternative for longer or host-app restart gaps.

### Manual protection

One reserved `manual:keep-awake` latch represents the dashboard/tray manual session. A zero-hour selection means explicit indefinite protection (`Manual` kind, no expiry). Timed selections accept 1–168 whole hours (`Timer` kind). A replacement resets this one manual session; Stop removes only it. Neither provider Off nor agent process disappearance affects a manual latch. The display setting applies equally to manual and automatic protection.

During a running session, `GetTickCount64` supplies the fixed expiry and avoids wall-clock edits sliding the timer. The logical state holds enabled/indefinite status or an absolute UTC end in FILETIME-epoch milliseconds, serialized as the single registry value described below. Restoration computes remaining time, rejecting expired, malformed, or more-than-168-hour future deadlines; it never grants the original duration again. Exit clears the Windows request, not the saved manual choice. The next app launch/sign-in restores the choice if still applicable. A failed settings write is logged and shown to the user.

For a normal installed copy, the integration installer records that Claude lifecycle hooks are managed. In that state, Claude hooks are authoritative in **Tasks** mode and the generic Claude CLI activity result is suppressed. This prevents transient daemons, background PTY hosts, and resumed helper sessions from extending a latch after Claude has reported completion or its visible CLI has closed. **Open** mode deliberately continues to use process presence. A portable copy with no managed-hook status retains conservative CLI activity as a fallback.

Both OpenAI desktop sources are read-only and bounded. The UI Automation probe reads only process/control metadata: process ID, control type, class name, enabled/offscreen state, accessibility command name, and nearest Document name. It never reads conversation messages, prompt edits, clipboard data, or app databases and never sends input. Every five seconds the Codex session scanner recursively discovers JSONL files across the full local session tree, then checks at most the 64 most recently written sessions plus any cached session that was previously active. This allows a task resumed from an older date folder to remain visible. Files last written before the current Codex desktop process generation are rejected, with a small timestamp tolerance, so an orphaned `task_started` marker from a previous app run cannot create a stale latch. The scanner reads backward in 64 KiB blocks until it finds the latest lifecycle marker, and caches results by path, size, and write time. Each eligible session whose latest marker is `task_started` contributes one active task instance; a later `task_complete` removes that authoritative active instance on the next scan, after which the detector grace described above may still apply.

Hooks are more precise because they name individual sessions, conversations, subagents, and Claude task-registry entries. Normal stop and completion events release only their matching latch; leases remain bounded so a crashed hook source self-recovers. A Claude `Stop` with a non-empty `background_tasks` array retains the session latch and uses the array length as its concurrent instance count. AgentLatch does not inspect the entries' descriptions, commands, or other content. `TaskCreated` and `TaskCompleted` use `session_id` plus `task_id` as an independent identity, so foreground completion cannot release background task work. Antigravity's `fullyIdle` signal provides the equivalent background-work guard for that provider.

Codex process classification recognizes both the Windows package and the bundled `%LOCALAPPDATA%\OpenAI\Codex\bin` runtime. Process-tree ownership is resolved before sampling, including intermediary hosts; Codex desktop descendants are collapsed and cannot supply CPU/I/O activity. An independent CLI tree remains activity-capable. Only a packaged desktop root establishes the session-file generation cutoff.

Detector results carry explicit grace state and remaining time. The app represents grace with one detector instance and a bounded lease ending at the original activity deadline; normal reconciliation does not slide that deadline. Fresh lifecycle/UI activity restores exact concurrent counts. Standalone CLI activity can renew its conservative activity grace. Overview shows a release/restart-grace reason and countdown; Agents describes finishing protection or waiting for an app restart. The While open selection maps to Open internally and describes present apps as App open · keeping awake. These presentation states do not alter Windows power-request semantics.

Hook lease expiry is an internal recovery bound and is displayed as **Active**, never as a predicted task duration. Detector grace retains its explicit release countdown. DashboardControls retains all automatic sources and projects five at a time into an owner-drawn native ListView. Concurrent task counts remain distinct from the number of source rows. Manual protection is represented separately by the override strip and is excluded only from the displayed automatic-source count, never from the power-request inputs.

## Integration and installer safety

Provider configuration updates are additive and idempotent. The integration script parses the existing JSON, removes stale AgentLatch commands for the same provider, preserves unrelated entries, writes a timestamped sibling backup, and replaces the destination through a temporary file. Claude Code entries use its shell-free executable-and-arguments form, so hook paths bypass shell parsing and expose the direct AgentLatch process exit code. Repair also recognizes and replaces legacy quoted and bare-path shell commands. For Codex and Claude it stores per-user integration-expected, installed-command, and hook-seen health values under `HKCU\Software\AgentLatch`.

The Windows setup executable is per-user and uses one stable production AppId so normal upgrades replace the previous version. Before copying files it asks the existing AgentLatch process to exit normally, then installs integrations automatically and records standard startup and uninstall entries. Normal exit code zero tells the watchdog not to restart the old copy. Setup integration tests are compiled with a random, non-production AppId and are refused by the test harness without a matching marker file, preventing tests from overwriting a real installation's uninstall registration.

The main process launches a companion copy in `--watchdog` mode after it owns the single-instance mutex. The watchdog blocks on the main-process handle without polling or holding a power request. Exit code zero is final. A nonzero exit restarts AgentLatch in the background after two seconds. Runtime of at least 60 seconds resets the rapid-failure count; otherwise only three consecutive restart attempts are allowed. A restarted main process creates its own successor watchdog.

## IPC and trust boundary

The first AgentLatch instance owns a named mutex and hidden/control window. Later invocations locate that window and send a bounded, tab-delimited `WM_COPYDATA` message. Message fields are sanitized, payload size is capped, external TTLs are capped at 24 hours, and no network listener is opened. Manual sessions use separate internal state and can run for up to 168 hours or indefinitely by explicit user choice; they do not change external lease limits.

AgentLatch assumes other processes running as the same signed-in user are within the local trust boundary. It does not accept remote requests.

## Persistence

Small settings are stored under `HKCU\Software\AgentLatch`. The optional startup entry is stored under the current user's standard Windows Run key. Automatic, hook, external, and restart-grace latches are intentionally not persisted. The explicit manual state is one `REG_QWORD`, `ManualKeepAwakeUntilUtc`: 0 means Off, 1 means Until stopped, and higher values are an absolute FILETIME-epoch millisecond end. One value avoids an interrupted pair of writes accidentally changing the restoration mode. The state is reconstructed as one latch on launch. Dashboard/tray controls and the bounded `--keep-awake` / `--stop-keep-awake` CLI share this same state.

`%LOCALAPPDATA%\AgentLatch\diagnostics.log` stores privacy-safe state transitions, including detector release causes, restart-grace start/recovery/expiry, and manual session start/stop/expiry or persistence failure. It rotates to `diagnostics.previous.log` at 512 KiB, so total diagnostic history is bounded to roughly 1 MiB. The normal uninstaller removes both files and saved settings.

## Native tabbed interface

The preferred client size is 620 × 560 logical pixels. Overview is activity-first: five full two-line source rows, bounded previous/next navigation, and a compact manual-override strip. Agents owns five explicit mode selectors; Settings owns three switch-styled native checkboxes and maintenance buttons. Header, version, a single protection summary, and tray-behavior footer stay visible. Navigation never mutates provider policy. Hidden-page controls lose WS_VISIBLE and cannot receive normal tab navigation.

Controls are real HWNDs: tab control, buttons, dropdown-list ComboBoxes, auto-checkboxes, static labels, and report ListView. Native behavior supplies focus, keyboard input and accessibility; GDI paints the shell, two-line list rows, switch visuals, and tab/dropdown chrome. Switch state remains BM_GETCHECK/BM_SETCHECK on BS_AUTOCHECKBOX; painting does not replace native input semantics. Custom-painted child controls fill their entire rectangular backdrop before drawing rounded shapes. MSAA property services annotate provider dropdowns, the activity list and pager buttons. IsDialogMessage handles Tab/Shift+Tab, and the message loop handles Ctrl+Tab/Ctrl+Shift+Tab. Native modal input validates custom hours.

Dropdown selections map explicitly to Tasks/Open/Off rather than cycling a row. The app reconciles settings and protection after a selection; it does not reset an open dropdown on each monitoring tick. Source pagination preserves its current page during refreshes and clamps it after removals. Native rows retain accessible names/statuses, selection, and arrow-key behavior; Page Up/Page Down and mouse-wheel events over the list change pages. The list always fits at most five full rows, so neither native scrollbar is required. Paging/visual filtering does not remove underlying latches or change power reconciliation. Manual-override and activity-empty states are independent.

## Off-screen captures

The optional `--export-ui-screenshots <directory>` command sends bounded local IPC to the running app (starting it in background if needed). The app takes one DashboardState snapshot and renders each page in a separate hidden WS_EX_NOACTIVATE tool window. The visible window, selected page, settings, power requests and input focus are not changed. No screen pixels, other app windows, mouse or keyboard are read/controlled by export.

The same renderer and native child controls paint into a 32-bit bitmap; Windows Imaging Component encodes PNGs. Only `dashboard.png`, `agents.png`, and `settings.png` are exported. The directory is user-supplied within the existing same-user IPC trust boundary. Export events log success/failure only, never destination paths or screenshot content. Provider/custom lease labels visible in the UI can appear in exports and should be reviewed before sharing.

Native self-tests create hidden controls at 96/120/144/168/192 DPI and check page visibility, layout bounds, mode mapping, five-row capacity, paging completeness/preservation, switch states, absence of both scrollbar styles, and tab fit. Independent control paint tests seed white/magenta surfaces before rendering custom controls. Optional `--test-render-dir` captures cover timed, indefinite, idle, error, long-list and mixed four-source synthetic states; documentation captures use running-app state. Render review is a separate visual-quality check, not implied by a passing structural test. Neither is an independent OS power-policy or overnight qualification test.
