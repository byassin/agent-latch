# Agent integrations

AgentLatch gives every provider one of three modes:

1. **Tasks** (default) latches only for native Codex lifecycle state, lifecycle-hook work, or conservative agent CLI activity.
2. **Open** latches whenever the selected app or CLI process exists.
3. **Off** ignores that provider and releases its automatic latches.

Desktop Electron processes perform background work even when no agent is running, so AgentLatch deliberately does not use their CPU or I/O as evidence of a task. Codex desktop uses its local task start/complete lifecycle stream. Claude Code, Cursor, and Google Antigravity use lifecycle hooks in **Tasks** mode. CLI-only processes retain activity detection as a fallback. Every path feeds the same latch registry, so concurrent reasons are counted independently.

## Installing hooks

The normal AgentLatch setup executable configures every supported integration automatically. No separate hook installation step is required.

To repair or update an installed integration, right-click the AgentLatch tray icon and select **Repair or update agent integrations**, or run:

```powershell
.\scripts\install-integrations.ps1 -AgentLatchPath "C:\path\to\AgentLatch.exe"
```

Install only selected providers:

```powershell
.\scripts\install-integrations.ps1 `
  -AgentLatchPath "C:\path\to\AgentLatch.exe" `
  -Provider Codex,Claude,Cursor,Antigravity
```

Preview changes with PowerShell's `-WhatIf`. Remove only AgentLatch's entries with `-Uninstall`:

```powershell
.\scripts\install-integrations.ps1 `
  -AgentLatchPath "C:\path\to\AgentLatch.exe" `
  -Uninstall
```

The setup executable and repair script both:

- parse and preserve the existing JSON object;
- replace stale AgentLatch commands from older installation paths;
- append only missing AgentLatch commands;
- writes no duplicate entries when run again;
- creates a timestamped sibling backup before every changed file;
- writes through a temporary file; and
- never requires administrator rights.

The normal Windows uninstaller removes these entries automatically while leaving every unrelated hook untouched.

Restart active agent sessions after changing hooks so they reload their configuration. Codex desktop requires none of these hook steps; its native lifecycle detection works immediately.

## Codex

Configuration: `%USERPROFILE%\.codex\hooks.json`

For the Codex desktop app, AgentLatch reads the local JSONL lifecycle stream and treats `task_started` without a later `task_complete` as active work. Merely leaving Codex open does not latch. Concurrent active task files become independent latch instances.

For Codex CLI, configured hook events include session start, prompt submission, tool use, subagent start/stop, and task stop. The event's `session_id` becomes the stable latch identity; subagent IDs receive their own latches.

AgentLatch accepts Codex hook JSON on standard input:

```text
"C:\path\to\AgentLatch.exe" --hook codex
```

Codex CLI also has conservative process-activity detection as a fallback. See the official [Codex hooks reference](https://developers.openai.com/codex/hooks/).

## Claude Code

Configuration: `%USERPROFILE%\.claude\settings.json`

Configured events include prompt submission, tool use, subagent start/stop, stop failure, session end, and normal stop. Session and `agent_id` fields give concurrent Claude subagents independent latches.

```text
"C:\path\to\AgentLatch.exe" --hook claude
```

See the official [Claude Code hooks documentation](https://code.claude.com/docs/en/hooks).

The native Claude desktop app is intentionally treated as presence-only. It can latch in **Open** mode, but it is never mistaken for an active Claude Code task.

## Cursor

Configuration: `%USERPROFILE%\.cursor\hooks.json`

AgentLatch uses Cursor's version 1 hook format for prompt, tool, response, stop, and subagent events:

```text
"C:\path\to\AgentLatch.exe" --hook cursor
```

Hook availability varies across Cursor surfaces and versions. The Cursor IDE is recognized in **Open** mode, while **Tasks** mode uses lifecycle events and activity from the separate Cursor agent CLI when present. See Cursor's official [hooks documentation](https://cursor.com/docs/hooks).

## Google Antigravity

Configuration: `%USERPROFILE%\.gemini\config\hooks.json`

AgentLatch installs one namespaced global hook definition and preserves every unrelated Antigravity customization. `PreInvocation` acquires the conversation latch, `PostInvocation` renews it, and `Stop` releases it only when Antigravity reports `fullyIdle: true`. A stop with background work still running keeps a bounded lease.

```text
"C:\path\to\AgentLatch.exe" --hook antigravity --event PreInvocation
```

The installer creates a timestamped backup before changing the global Antigravity hook file. See Google's official [Antigravity hooks documentation](https://www.antigravity.google/docs/hooks).

## OpenCode and Gemini CLI

AgentLatch detects activity in these CLI process trees. Tools can add precise lifecycle support through the generic lease API:

```powershell
AgentLatch.exe --acquire --id my-session --source opencode --label "OpenCode task" --ttl 1800
AgentLatch.exe --release --id my-session
```

Supported source keys are `codex`, `claude`, `cursor`, `opencode`, `gemini`, `antigravity`, `manual`, and `external`.

## Lease behavior

- Hook leases default to 30 minutes and renew whenever useful lifecycle activity arrives.
- External leases default to 30 minutes and accept a TTL from 0 to 86,400 seconds.
- A stop event releases its matching session or subagent immediately.
- A missing stop event cannot keep the computer awake forever: the TTL eventually releases it.
- Disabling a provider in the dashboard removes that provider's automatic and hook latches immediately.
