# Claude Hook Authority Design

## Problem

AgentLatch 0.2.2 treats every non-Store `claude.exe` process as an activity-capable Claude Code task root. Current Claude Code can leave a transient daemon, background PTY host, and resumed session process running after its visible window closes. Those helpers perform enough housekeeping CPU work to repeatedly renew AgentLatch's three-minute activity grace, producing a persistent false task latch. The dashboard exposes the symptom as one detector latch with three task instances rather than a lifecycle-hook countdown.

## Requirements

- In **Tasks** mode, an AgentLatch-managed Claude integration must use lifecycle hooks as the authoritative source of active work.
- Claude daemon or PTY process activity must not create a detector latch when those hooks are installed.
- In **Open** mode, any running Claude CLI or desktop process must continue to latch.
- A CLI-only installation without AgentLatch-managed Claude hooks must retain the existing process-activity fallback.
- Hook events must continue to create independent session and subagent leases, release on stop events, and expire safely if a stop event is missed.
- Upgrades must populate the Claude integration status automatically and preserve existing provider-mode settings.

## Considered Approaches

1. **Hook-authoritative Tasks mode when the integration is installed — selected.** Record `IntegrationExpectedClaude` alongside the existing Codex integration status. Suppress only the Claude detector latch in Tasks mode while leaving hook latches and Open-mode presence unchanged. This uses the most precise signal already installed by default and preserves fallback behavior for portable or manually copied binaries.
2. **Parse every Claude command line and exclude known daemon flags.** This retains activity detection but depends on undocumented flags such as `daemon run` and `--bg-pty-host`, requires additional Windows process-command-line plumbing, and is likely to regress when Claude changes its launcher.
3. **Disable Claude process activity globally.** This is simple but breaks task detection for users who intentionally run the CLI without hooks.

## Architecture and Data Flow

The integration installer records `IntegrationExpectedClaude`, `IntegrationCommandClaude`, and `HookSeenClaude` under `HKCU\Software\AgentLatch`, mirroring the existing Codex health fields. `Settings` loads and refreshes those fields and persists `HookSeenClaude` when any Claude hook reaches AgentLatch.

`Settings::UseProcessActivityFallback(provider)` becomes the single policy boundary. It returns false only for Claude Code in Tasks mode when the managed integration is expected. `AgentLatchApp::UpdateDetectorLatches` consults this policy before accepting a detector's `recently_active` result. Hook latches bypass that detector policy and continue through the existing IPC and latch registry path.

```text
Claude process activity ── detector ── policy ──┬─ unmanaged CLI: detector latch
                                                └─ managed hooks: ignored in Tasks mode

Claude lifecycle event ── hook bridge ── IPC ───── hook latch / immediate release
```

## Failure and Compatibility Behavior

If the integration is not installed or was explicitly skipped, AgentLatch retains the 0.2.2 activity fallback. If managed hooks are expected but damaged, Tasks mode intentionally fails idle rather than keeping the PC awake indefinitely; the existing tray integration-repair action restores the hooks. Open mode remains an explicit presence-based override.

## Testing

- Add a self-test that fails unless managed Claude hooks suppress process activity in Tasks mode.
- Cover unmanaged Claude Tasks mode, Claude Open mode, and non-Claude providers to prevent over-broad suppression.
- Extend integration installer tests so Claude status commands remain idempotent without changing unrelated provider configuration.
- Run the native self-test, integration installer tests, default install tests, isolated setup tests, and a release build.
- Upgrade the installed app and verify live that the surviving Claude daemon chain no longer appears under **Running now**, while a synthetic Claude hook event still acquires and releases a hook latch.

