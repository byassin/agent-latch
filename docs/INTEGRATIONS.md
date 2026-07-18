# Agent integrations

AgentLatch has two complementary awareness layers:

1. The built-in detector follows known agent process trees and recent CPU/I/O activity.
2. Optional hooks create one renewable latch per agent session or subagent.

The detector needs no setup. Hooks improve precision when a provider exposes lifecycle events. Both feed the same latch registry, so concurrent reasons are counted independently.

## Installing hooks

Open AgentLatch and select **Set up hooks**, or run:

```powershell
.\scripts\install-integrations.ps1 -AgentLatchPath "C:\path\to\AgentLatch.exe"
```

Install only selected providers:

```powershell
.\scripts\install-integrations.ps1 `
  -AgentLatchPath "C:\path\to\AgentLatch.exe" `
  -Provider Codex,Claude
```

Preview changes with PowerShell's `-WhatIf`. Remove only AgentLatch's entries with `-Uninstall`:

```powershell
.\scripts\install-integrations.ps1 `
  -AgentLatchPath "C:\path\to\AgentLatch.exe" `
  -Uninstall
```

The installer:

- parses and preserves the existing JSON object;
- appends only missing AgentLatch commands;
- writes no duplicate entries when run again;
- creates a timestamped sibling backup before every changed file;
- writes through a temporary file; and
- never requires administrator rights.

Restart active agent sessions after changing hooks so they reload their configuration.

## Codex

Configuration: `%USERPROFILE%\.codex\hooks.json`

Configured events include prompt submission, tool use, subagent start/stop, and task stop. The event's `session_id` becomes the stable latch identity; subagent IDs receive their own latches.

AgentLatch accepts Codex hook JSON on standard input:

```text
"C:\path\to\AgentLatch.exe" --hook codex
```

Codex still has process detection as a fallback. See the official [Codex hooks reference](https://developers.openai.com/codex/hooks/).

## Claude Code

Configuration: `%USERPROFILE%\.claude\settings.json`

Configured events include prompt submission, tool use, subagent start/stop, stop failure, session end, and normal stop. Session and `agent_id` fields give concurrent Claude subagents independent latches.

```text
"C:\path\to\AgentLatch.exe" --hook claude
```

See the official [Claude Code hooks documentation](https://code.claude.com/docs/en/hooks).

## Cursor

Configuration: `%USERPROFILE%\.cursor\hooks.json`

AgentLatch uses Cursor's version 1 hook format for prompt, tool, response, stop, and subagent events:

```text
"C:\path\to\AgentLatch.exe" --hook cursor
```

Hook availability varies across Cursor surfaces and versions. When a lifecycle event is unavailable, the built-in Cursor agent process detector continues to work. See Cursor's official [hooks documentation](https://cursor.com/docs/agent/hooks).

## OpenCode and Gemini CLI

AgentLatch detects these process trees automatically. Tools can add precise lifecycle support through the generic lease API:

```powershell
AgentLatch.exe --acquire --id my-session --source opencode --label "OpenCode task" --ttl 1800
AgentLatch.exe --release --id my-session
```

Supported source keys are `codex`, `claude`, `cursor`, `opencode`, `gemini`, `manual`, and `external`.

## Lease behavior

- Hook leases default to 30 minutes and renew whenever useful lifecycle activity arrives.
- External leases default to 30 minutes and accept a TTL from 0 to 86,400 seconds.
- A stop event releases its matching session or subagent immediately.
- A missing stop event cannot keep the computer awake forever: the TTL eventually releases it.
- Disabling a provider in the dashboard removes that provider's automatic and hook latches immediately.
