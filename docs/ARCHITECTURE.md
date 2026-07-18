# Architecture

AgentLatch is a single native Win32 process with no service, daemon, web runtime, or elevated component.

```text
Agent processes ── process/activity scan ─┐
Codex sessions ─── task lifecycle scan ───┤
                                         │
Lifecycle hooks ── JSON stdin / WM_COPYDATA ──> Latch registry ──> Windows power request
                                         │            │
Local tools ───── renewable lease CLI ───┘            └──> Dashboard + tray state
```

## Components

| Component | Responsibility |
|---|---|
| `AgentDetector` | Reads Codex desktop task lifecycle markers, enumerates known agent roots, distinguishes desktop shells from agent CLIs, follows descendants, and samples eligible CPU/I/O activity. |
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

- **Tasks** accepts native Codex desktop lifecycle state, lifecycle-hook latches, and recent activity from task-capable CLI process trees.
- **Open** accepts process presence, including desktop shells.
- **Off** accepts neither.

Electron desktop applications are presence-capable but not activity-capable because background renderers, update checks, and language servers remain busy while the agent is idle. This prevents Codex desktop, Claude desktop, Cursor, and Antigravity from creating false task latches. Codex desktop is the exception only in signal source: AgentLatch scans recent local session files for the latest explicit `task_started` or `task_complete` marker instead of inferring work from process activity. A configurable grace period covers short CLI thinking, network, or orchestration gaps.

The Codex session scan is read-only and bounded. It considers JSONL files from today and yesterday, checks at most the 64 most recently written sessions plus any cached session that was previously active, and reads backward in 64 KiB blocks until it finds the latest lifecycle marker. Results are cached by path and file size. Each session whose latest marker is `task_started` contributes one active task instance; a later `task_complete` releases it on the next scan.

Hooks are more precise because they name individual sessions, conversations, and subagents. Stop events release their latch immediately; leases are still bounded so a crashed hook source self-recovers. Antigravity's `fullyIdle` signal prevents a stop event from releasing while background work remains.

## Integration and installer safety

Provider configuration updates are additive and idempotent. The integration script parses the existing JSON, removes stale AgentLatch commands for the same provider, preserves unrelated entries, writes a timestamped sibling backup, and replaces the destination through a temporary file.

The Windows setup executable is per-user and uses one stable production AppId so normal upgrades replace the previous version. Before copying files it asks the existing AgentLatch process to exit, then installs integrations automatically and records standard startup and uninstall entries. Setup integration tests are compiled with a random, non-production AppId and are refused by the test harness without a matching marker file, preventing tests from overwriting a real installation's uninstall registration.

## IPC and trust boundary

The first AgentLatch instance owns a named mutex and hidden/control window. Later invocations locate that window and send a bounded, tab-delimited `WM_COPYDATA` message. Message fields are sanitized, payload size is capped, TTLs are capped at 24 hours, and no network listener is opened. The public lease API remains available for local agent tools and orchestrators even though AgentLatch intentionally exposes no generic keep-awake timer controls.

AgentLatch assumes other processes running as the same signed-in user are within the local trust boundary. It does not accept remote requests.

## Persistence

Small settings are stored under `HKCU\Software\AgentLatch`. The optional startup entry is stored under the current user's standard Windows Run key. Active latches are intentionally not persisted across app or system restarts.
