# Architecture

AgentLatch is a single native Win32 process with no service, daemon, web runtime, or elevated component.

```text
Agent processes ── process/activity scan ─┐
                                         │
Lifecycle hooks ── JSON stdin / WM_COPYDATA ──> Latch registry ──> Windows power request
                                         │            │
Local tools ───── renewable lease CLI ───┘            └──> Dashboard + tray state
```

## Components

| Component | Responsibility |
|---|---|
| `AgentDetector` | Enumerates known agent roots, follows their descendant processes, and samples CPU/I/O activity. |
| `HookBridge` | Reads bounded JSON hook input, extracts lifecycle identity, and converts events to lease operations. |
| `LatchRegistry` | Owns independent manual, timer, detector, hook, and external latches; expires bounded leases. |
| `PowerRequest` | Creates and reconciles Windows `PowerSetRequest` system/display requirements. |
| `AgentLatchApp` | Hosts the message loop, tray icon, single-instance IPC, settings, and reconciliation cycle. |
| `DashboardRenderer` | Paints the DPI-aware interface and hit targets with native GDI. |

## Power semantics

When the registry changes from zero to one active latch, AgentLatch sets `PowerRequestSystemRequired`. When the registry returns to zero, it clears that request. If **Keep display on** is enabled, it also sets `PowerRequestDisplayRequired` for the same active interval.

The request blocks automatic idle sleep. It does not block an explicit user sleep, shutdown, restart, or critical system action.

## Detection semantics

The process detector scans every two seconds. A known agent root owns its descendant process tree. CPU and I/O deltas mark the tree active; a configurable grace period avoids releasing the latch during short thinking, network, or orchestration gaps.

Hooks are more precise because they name individual sessions and subagents. They are still bounded leases rather than permanent flags, so a crashed hook source self-recovers.

## IPC and trust boundary

The first AgentLatch instance owns a named mutex and hidden/control window. Later invocations locate that window and send a bounded, tab-delimited `WM_COPYDATA` message. Message fields are sanitized, payload size is capped, TTLs are capped at 24 hours, and no network listener is opened.

AgentLatch assumes other processes running as the same signed-in user are within the local trust boundary. It does not accept remote requests.

## Persistence

Small settings are stored under `HKCU\Software\AgentLatch`. The optional startup entry is stored under the current user's standard Windows Run key. Active latches are intentionally not persisted across app or system restarts.
