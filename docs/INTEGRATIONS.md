# Agent integrations

AgentLatch gives every provider one of three modes:

1. **Tasks** (default) latches only for active unified-app responses, native Codex lifecycle state, lifecycle-hook work, or conservative agent CLI activity when no authoritative managed integration exists.
2. **Open** latches whenever the selected app or CLI process exists.
3. **Off** ignores that provider and releases its automatic latches.

Desktop Electron processes perform background work even when no agent is running, so AgentLatch deliberately does not use their CPU or I/O as evidence of a task. The unified OpenAI app uses its primary composer response state for ChatGPT and its local task start/complete lifecycle stream for Codex. Claude Code, Cursor, and Google Antigravity use lifecycle hooks in **Tasks** mode. CLI-only processes retain activity detection as a fallback. Every path feeds the same latch registry. Distinct lifecycle identities are counted independently. A regular ChatGPT response counts alongside active Codex tasks; only an overlapping Codex composer response is deduplicated.

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
- write no duplicate entries when run again;
- create a timestamped sibling backup before every changed file;
- write through a temporary file; and
- never requires administrator rights.

The normal Windows uninstaller removes these entries automatically while leaving every unrelated hook untouched.

Restart active agent sessions after changing hooks so they reload their configuration. The unified OpenAI desktop app requires none of these hook steps; its native response and Codex lifecycle detection work immediately.

## OpenAI desktop and Codex CLI

Configuration: `%USERPROFILE%\.codex\hooks.json`

For the packaged unified OpenAI desktop app, AgentLatch recognizes an active ChatGPT or Codex response from the primary composer command's read-only Windows accessibility metadata. It targets only the expected `OpenAI.Codex` app package, samples every two seconds, and distinguishes the ChatGPT and Codex Documents for an accurate dashboard label. English Send/Stop names are recognized directly; other locales are learned only after a disabled idle command remains stable for three observations. The signal continues while the app is minimized and fails closed if an app update makes the expected control unavailable. AgentLatch does not read the prompt, response, or conversation.

For Codex tasks, AgentLatch checks the local JSONL lifecycle stream every five seconds and treats `task_started` without a later `task_complete` as active work. Merely leaving the app open does not latch. Concurrent active task files become independent latch instances. An overlapping Codex composer signal is deduplicated, while a simultaneous regular ChatGPT response adds another active instance and appears as combined OpenAI work in the dashboard.

For Codex CLI, configured hook events include session start, prompt submission, tool use, subagent start/stop, and task stop. The event's `session_id` becomes the stable latch identity; subagent IDs receive their own latches.

AgentLatch accepts Codex hook JSON on standard input:

```text
"C:\path\to\AgentLatch.exe" --hook codex
```

Codex CLI also has conservative process-activity detection as a fallback. See the official [Codex hooks reference](https://learn.chatgpt.com/docs/hooks).

## Claude Code

Configuration: `%USERPROFILE%\.claude\settings.json`

Configured events include prompt submission, individual and batched tool use, subagent start/stop, task creation/completion, stop failure, session end, and normal stop. Session, `agent_id`, and `task_id` fields give concurrent Claude work independent latches.

Claude includes an in-flight `background_tasks` summary when the foreground turn stops but shell work, subagents, monitors, workflows, teammates, cloud sessions, or MCP tasks are still running. AgentLatch uses only the number of entries in that array: a non-empty summary keeps the session latch and reports the concurrent count, while a later empty `Stop` releases it. Task subjects, descriptions, commands, model output, and transcript content are ignored. `TaskCreated` and `TaskCompleted` provide an additional independent lifecycle for Claude's task registry and agent-team work.

AgentLatch installs Claude commands using Claude Code's shell-free executable-and-arguments form:

```json
{
  "type": "command",
  "command": "C:\\path\\to\\AgentLatch.exe",
  "args": ["--hook", "claude"],
  "timeout": 5
}
```

Claude invokes the executable directly, so Windows paths containing spaces do not depend on PowerShell or Git Bash quoting, and Claude receives AgentLatch's process exit code directly. Repairing integrations automatically replaces both the older quoted shell command and manually repaired bare-path variants.

For a normal setup installation, these managed hooks are authoritative in **Tasks** mode. AgentLatch does not treat CPU or I/O from Claude's surviving transient daemon, background PTY host, or resumed helper processes as proof that a task is still running. **Open** mode still latches on Claude process presence. Portable copies without managed hooks retain conservative Claude CLI activity detection as a fallback; run the integration installer to opt into precise lifecycle behavior.

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

- Native detector state is refreshed every two seconds; Codex lifecycle files are rediscovered every five seconds.
- Detector activity retains a 180-second safety grace by default while the provider remains open. Closing the provider or switching it **Off** releases the detector latch on the next reconciliation cycle.
- Hook leases default to 30 minutes and renew whenever useful lifecycle activity arrives.
- External leases default to 30 minutes and accept a TTL from 0 to 86,400 seconds.
- A stop event releases its matching hook session or subagent immediately; an independent native detector signal or its safety grace can still keep that provider latched.
- A missing stop event cannot keep the computer awake forever: the TTL eventually releases it.
- Disabling a provider in the dashboard removes that provider's automatic and hook latches immediately.

## Diagnosing a missed latch or sleep

First check the dashboard state. **READY** means no active source was detected. **PROTECTED** means work was detected and Windows accepted the system keep-awake request. **ERROR** means work was detected but the Windows request failed and is being retried.

Use **Open diagnostic history** from the tray menu to correlate application starts, latch counts and provider names, power-request results, and watchdog restarts. The log intentionally omits session IDs, workspace paths, prompts, responses, and conversation content. For an independent operating-system view, run `powercfg /requests` from an elevated Windows Terminal while the dashboard says **PROTECTED**.
