# Architecture

AgentLatch is a single native Win32 process with no service, daemon, web runtime, or elevated component.

```text
Agent processes ── process/activity scan ─┐
Codex sessions ─── task lifecycle scan ───┤
OpenAI composer ── UI Automation worker ──┤
                                         │
Lifecycle hooks ── JSON stdin / WM_COPYDATA ──> Latch registry ──> Windows power request
                                         │            │
Local tools ───── renewable lease CLI ───┘            └──> Dashboard + tray state
```

## Components

| Component | Responsibility |
|---|---|
| `AgentDetector` | Reads Codex desktop task lifecycle markers, supplies packaged OpenAI process IDs to the response probe, merges authoritative signals, enumerates known agent roots, follows descendants, and samples eligible CLI CPU/I/O activity. |
| `OpenAIUnifiedActivityProbe` | Runs Windows UI Automation on a background thread and classifies only the unified OpenAI app's primary composer command as idle or responding. |
| `HookBridge` | Reads bounded JSON hook input, extracts lifecycle identity, and converts events to lease operations. |
| `LatchRegistry` | Owns independent detector, hook, and external latches; expires bounded leases. |
| `PowerRequest` | Creates and reconciles Windows `PowerSetRequest` system/display requirements. |
| `AgentLatchApp` | Hosts the message loop, tray icon, single-instance IPC, settings, and reconciliation cycle. |
| `DashboardRenderer` | Paints the DPI-aware interface and hit targets with native GDI. |

## Power semantics

When the registry changes from zero to one active latch, AgentLatch sets `PowerRequestSystemRequired`. When the registry returns to zero, it clears that request. If **Keep display on** is enabled, it also sets `PowerRequestDisplayRequired` for the same active interval.

The request blocks automatic idle sleep. It does not block an explicit user sleep, shutdown, restart, or critical system action.

## Detection semantics

The process detector scans every two seconds. A known agent root owns its descendant process tree. Each provider has an independent mode:

- **Tasks** accepts unified OpenAI app response state, native Codex desktop lifecycle state, lifecycle-hook latches, and recent activity from task-capable CLI process trees when the provider has no authoritative managed integration.
- **Open** accepts process presence, including desktop shells.
- **Off** accepts neither.

Electron desktop applications are presence-capable but their process CPU or I/O is not activity-capable because background renderers, update checks, and language servers remain busy while the agent is idle. This prevents the OpenAI, Claude, Cursor, and Antigravity shells from creating false task latches. The unified OpenAI app has two explicit signal sources: AgentLatch scans recent local Codex session files for the latest `task_started` or `task_complete` marker and independently observes the primary composer command changing between idle and response state. The packaged `ChatGPT.exe` shell is classified as OpenAI/Codex only when it runs from the `OpenAI.Codex` Windows app package.

The OpenAI response probe runs outside the Win32 message-loop thread and polls every two seconds. It enumerates top-level windows only for the packaged OpenAI root PIDs supplied by `AgentDetector`, finds a Button whose class contains the exact `size-token-button-composer` and `bg-primary-solid` tokens, and walks to the nearest Document ancestor to distinguish ChatGPT from Codex. English Send and Stop names are recognized directly. Other locales learn a stable disabled idle command after three observations. A minimized composer remains valid even when UI Automation reports it as offscreen. Unknown Documents, empty names, inaccessible controls, COM failures, and snapshots older than four seconds produce no new latch.

Codex JSONL state remains authoritative. When one or more lifecycle tasks are active, a simultaneous Codex composer response does not add another instance. A regular ChatGPT response is a distinct concurrent reason and adds one instance even while Codex tasks run. A ChatGPT response or a Codex composer response with no matching JSONL task contributes one active instance and precise dashboard detail. Concurrent native OpenAI reasons share one compact provider row, but its instance count, dashboard headline, tray menu, and tooltip all reflect the full number of active reasons. While active, either native OpenAI signal refreshes the provider activity timestamp.

The detector's activity grace defaults to 180 seconds and is clamped to 30–1,800 seconds when loaded. After a response ends or a Codex completion marker is discovered, the OpenAI detector can therefore remain latched for up to three minutes while the packaged app remains open. This covers brief thinking, network, orchestration, and signal-transition gaps. Closing the provider makes `running_instances` zero and releases the detector latch on the next reconciliation cycle. Provider **Off** mode also releases it immediately. This grace applies to detector state, not to the independent lifetime of hook or external leases.

For a normal installed copy, the integration installer records that Claude lifecycle hooks are managed. In that state, Claude hooks are authoritative in **Tasks** mode and the generic Claude CLI activity result is suppressed. This prevents transient daemons, background PTY hosts, and resumed helper sessions from extending a latch after Claude has reported completion or its visible CLI has closed. **Open** mode deliberately continues to use process presence. A portable copy with no managed-hook status retains conservative CLI activity as a fallback.

Both OpenAI desktop sources are read-only and bounded. The UI Automation probe reads only process/control metadata: process ID, control type, class name, enabled/offscreen state, accessibility command name, and nearest Document name. It never reads conversation messages, prompt edits, clipboard data, or app databases and never sends input. Every five seconds the Codex session scanner recursively discovers JSONL files across the full local session tree, then checks at most the 64 most recently written sessions plus any cached session that was previously active. This allows a task resumed from an older date folder to remain visible. Files last written before the current Codex desktop process generation are rejected, with a small timestamp tolerance, so an orphaned `task_started` marker from a previous app run cannot create a stale latch. The scanner reads backward in 64 KiB blocks until it finds the latest lifecycle marker, and caches results by path, size, and write time. Each eligible session whose latest marker is `task_started` contributes one active task instance; a later `task_complete` removes that authoritative active instance on the next scan, after which the detector grace described above may still apply.

Hooks are more precise because they name individual sessions, conversations, and subagents. Stop events release their latch immediately; leases are still bounded so a crashed hook source self-recovers. Antigravity's `fullyIdle` signal prevents a stop event from releasing while background work remains.

## Integration and installer safety

Provider configuration updates are additive and idempotent. The integration script parses the existing JSON, removes stale AgentLatch commands for the same provider, preserves unrelated entries, writes a timestamped sibling backup, and replaces the destination through a temporary file. For Codex and Claude it also stores per-user integration-expected, installed-command, and hook-seen health values under `HKCU\Software\AgentLatch`.

The Windows setup executable is per-user and uses one stable production AppId so normal upgrades replace the previous version. Before copying files it asks the existing AgentLatch process to exit, then installs integrations automatically and records standard startup and uninstall entries. Setup integration tests are compiled with a random, non-production AppId and are refused by the test harness without a matching marker file, preventing tests from overwriting a real installation's uninstall registration.

## IPC and trust boundary

The first AgentLatch instance owns a named mutex and hidden/control window. Later invocations locate that window and send a bounded, tab-delimited `WM_COPYDATA` message. Message fields are sanitized, payload size is capped, TTLs are capped at 24 hours, and no network listener is opened. The public lease API remains available for local agent tools and orchestrators even though AgentLatch intentionally exposes no generic keep-awake timer controls.

AgentLatch assumes other processes running as the same signed-in user are within the local trust boundary. It does not accept remote requests.

## Persistence

Small settings are stored under `HKCU\Software\AgentLatch`. The optional startup entry is stored under the current user's standard Windows Run key. Active latches are intentionally not persisted across app or system restarts.
