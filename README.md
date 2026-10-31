<p align="center">
  <img src="assets/agent-latch.svg" width="112" alt="AgentLatch icon">
</p>

<h1 align="center">AgentLatch</h1>

<p align="center"><strong>Your agents run. Your PC stays awake.</strong></p>

AgentLatch is a lightweight, open-source Windows tray app that prevents idle sleep while AI agents work—or whenever you explicitly need your PC to stay awake. It understands concurrent agents, exposes every active reason, and lets normal Windows sleep behavior return after the final source, manual session, and safety grace finish.

<p align="center">
  <img src="assets/dashboard.png" width="620" alt="AgentLatch Overview with activity-first layout and compact manual override">
</p>
<p align="center"><sub>Off-screen capture rendered from the running app's state—not a desktop screenshot or fabricated example.</sub></p>

## A compact Windows utility

AgentLatch has three tabs, with protection status always visible:

- **Overview:** see five complete agent-activity rows at once. Previous/next controls and Page Up/Page Down reveal additional sources without a bright system scrollbar. Manual protection has a separate, compact override strip.
- **Agents:** select **Tasks only**, **While open**, or **Off** explicitly for each provider. These correspond to Tasks, Open, and Off mode in the technical documentation.
- **Settings:** choose screen behavior, launch at sign-in, and notifications; open diagnostics or repair integrations when needed.

The 620 × 560 logical-pixel window uses native Windows controls with consistent dark styling: compact navigation, a single protection summary, two-line activity rows, and labeled On/Off switches. Switches retain native checkbox state and keyboard/accessibility behavior. Use **Tab / Shift+Tab** between controls, **Ctrl+Tab / Ctrl+Shift+Tab** between pages, arrow keys within tabs/dropdowns/lists, and **Space / Enter** on focused controls. Closing the window leaves monitoring in the tray; **More… → Exit** exits the app.

<details>
<summary>Agents and Settings screenshots</summary>

![Agents tab with explicit per-provider dropdowns](assets/agents.png)
![Settings tab with clearly labeled On/Off switches](assets/settings.png)

</details>

## Why AgentLatch

- **Agent-aware:** watches Codex, Claude Code, Cursor, OpenCode, and Google Antigravity/Gemini CLI.
- **Concurrency-safe:** lifecycle-backed conversations, sessions, subagents, and Claude background tasks keep independent latches. A regular ChatGPT response counts alongside active Codex tasks; only an overlapping Codex composer response is deduplicated against its lifecycle task.
- **Your choice of precision:** set every provider independently to **Tasks**, **Open**, or **Off**.
- **Task-first by default:** the unified OpenAI app's response state, Codex lifecycle records, and provider hooks track actual work; conservative CLI activity detection is the fallback.
- **Open when you want it:** presence mode deliberately latches while the selected app or CLI is merely running.
- **Restart-safe:** a protected agent disappearing gets a fixed ten-minute restart grace, covering brief app update/restart gaps without latching forever.
- **Manual when you need it:** keep awake until Windows restarts, choose a quick duration, or enter 1–168 hours. Manual protection survives app-only restarts but resets after a Windows restart.
- **Transparent:** the dashboard shows exactly what is keeping the machine awake and why.
- **Self-healing:** a zero-CPU watchdog restarts AgentLatch after an abnormal exit, with a bounded restart-loop guard.
- **Verifiable:** the dashboard distinguishes a detected latch from a Windows-accepted sleep-protection request, and a local diagnostic history explains every transition.
- **Native and private:** one Win32 executable, no account, no service, no telemetry, and no administrator rights.

AgentLatch uses a Windows power request. **Sleep protection is on** means Windows accepted `PowerRequestSystemRequired`; **Sleep protection needs attention** means the request failed and AgentLatch is retrying. The tray/diagnostic states remain PROTECTED and ERROR. It does not jiggle the mouse, synthesize keystrokes, change the system power plan, or prevent a user-initiated shutdown or sleep.

## Provider support

| Provider | Tasks mode | Open mode | Lifecycle hooks |
|---|---|---|---|
| OpenAI / Codex | ChatGPT response state + native Codex lifecycle + hooks/CLI activity | Unified desktop app or CLI | Codex tasks; CLI sessions and subagents |
| Claude Code | Managed hooks; CLI activity for portable installs | Desktop app or CLI | Sessions, subagents, background work, and task registry |
| Cursor | Hooks + agent CLI activity | Cursor IDE or agent CLI | Agent and subagent events |
| OpenCode | CLI activity | CLI process | External lease API |
| Google Antigravity / Gemini CLI | Antigravity hooks + CLI activity | Antigravity app or Gemini CLI | Antigravity conversations |

In **Tasks** mode, opening an Electron app by itself never latches the computer. For the unified OpenAI Windows app, AgentLatch recognizes both active ChatGPT responses and Codex task lifecycle state, including while the app is minimized. Claude `Stop` events retain the session latch when Claude reports in-flight background work, and task/subagent completion releases only the matching independent latch. Detector activity is sampled every two seconds and retains a three-minute safety grace by default while the provider remains open. If a protected provider disappears, a separate ten-minute restart grace covers its return. Turning a provider **Off** cancels its automatic protection immediately. Hook leases expire automatically if an agent crashes or never sends a final event. Choose each provider's mode from its dropdown on the **Agents** tab; changing tabs never changes a tracking mode.

## Manual keep-awake and overnight use

On **Overview**, under **Manual override**, select **Keep awake…**, then **Until Windows restarts**, a quick duration (1, 2, 4, 8, 12, or 24 hours), or **Custom hours…** (1–168 whole hours). The same controls are available from the tray menu. The countdown is the actual manual session time remaining—not an estimate of agent completion.

Manual protection stays active even if every agent closes or updates. **Change…** replaces the existing manual session; **Stop** cancels only manual protection, leaving agent monitoring and any other protection reasons running. Keeping the screen on remains an independent switch on **Settings**; it is not needed to keep the PC awake.

**Restarting Windows clears manual override**, including any remaining timed session. You must enable it again after restarting. Automatic agent monitoring still starts normally if launch at sign-in is enabled.

Restarting/updating only AgentLatch restores the manual session within the same Windows registry-hive lifetime; a timed session retains its original end instead of starting a fresh full-length timer. Choosing **Exit** releases protection while AgentLatch is not running, but does not erase that same-session choice. **Stop** cancels it. Sleep/hibernate alone does not unload this session state; signing out can also clear it when Windows unloads the user registry hive. Upgrading from the old permanent format intentionally starts manual override Off once, because the old value cannot establish which boot enabled it.

For overnight work, choose a manual duration covering the night or **Until Windows restarts**. Automatic restart grace covers only ten minutes, and an agent update can interrupt the agent's own work even when Windows stays awake. No mode prevents forced restarts, power loss, explicit Sleep/shutdown, or critical system actions.

## Install

1. Open the [latest GitHub release](https://github.com/byassin/agent-latch/releases/latest).
2. Download the x64 setup executable for an Intel or AMD Windows PC, or the ARM64 setup executable for a Windows on Arm PC.
3. Double-click the setup executable.
4. Choose whether AgentLatch should start with Windows, then select **Install**.

Setup installs AgentLatch for the current user without an administrator prompt, creates normal Start menu and Windows uninstall entries, replaces an older running copy cleanly, and launches the new version. If AgentLatch later exits abnormally, a companion watchdog restarts it automatically; choosing **Exit** remains final. The exact stable version is always visible beside the AgentLatch name in the dashboard and in the window title.

Codex, Claude Code, Cursor, and Google Antigravity lifecycle integrations are installed automatically. Existing provider configuration is preserved, duplicate entries are avoided, and a timestamped backup is made before a changed JSON file is written. Claude Code hooks use its shell-free executable-and-arguments format, avoiding command-line quoting problems and returning AgentLatch's process status directly. The Windows uninstaller removes only AgentLatch's own integration entries.

OpenAI desktop detection is native and automatic for the packaged unified Windows app. AgentLatch reads Codex's local start/complete lifecycle markers and observes only the primary composer button's Windows accessibility metadata so ChatGPT responses also keep the PC awake. English Send/Stop names are recognized directly; other locales are learned conservatively from a stable disabled idle command. AgentLatch never reads prompt, response, or conversation content. If the expected control cannot be classified after an app update, detection fails closed instead of keeping the PC awake. No chat command, hook trust dialog, or separate setup step is required. Codex CLI hooks remain an additional signal when available.

Windows may display a SmartScreen warning until project releases are Authenticode-signed.

### Verify a download

Each release includes a `.sha256` sidecar beside every setup executable and a combined `SHA256SUMS.txt`. Compare the installer hash with either published value before running it:

```powershell
$installer = Get-Item .\AgentLatch-Setup-*-x64.exe
Get-FileHash $installer.FullName -Algorithm SHA256
Get-Content "$($installer.FullName).sha256"
```

Replace the version and architecture in those filenames with the asset you downloaded.

## Command-line lease API

Any local tool can acquire a renewable latch:

```powershell
AgentLatch.exe --acquire --id build-42 --source external --label "Release build" --detail "ARM64 package" --ttl 900
AgentLatch.exe --release --id build-42
```

Leases are local to the signed-in Windows session, fields are bounded and sanitized, and TTLs are capped at 24 hours. Repeating `--acquire` with the same ID renews that lease.

Other commands:

```text
--show           Open the dashboard
--quit           Exit the background app
--self-test      Run the built-in core tests
--hook PROVIDER  Accept one lifecycle event as JSON on stdin
```

Manual protection also supports `AgentLatch.exe --keep-awake --hours 8`, `AgentLatch.exe --keep-awake --indefinite`, and `AgentLatch.exe --stop-keep-awake`. These control the same saved manual session as the dashboard, not a separate external lease. Invalid/missing hours return exit code 2; a successful local delivery returns 0.

## Protection health and diagnostics

After detected work ends, Overview shows a **release grace** reason and a **Releasing in…** countdown during the default three-minute detector grace. Agents describes this as **Finishing protection**. This counts as one protection reason, never as multiple running tasks. Fresh work resumes normal task counts; otherwise the detector latch releases when the countdown expires. Desktop Codex app servers and sandbox helpers do not renew this timer merely by doing background CPU or I/O work. **While open** instead shows **App open · keeping awake** when the provider is present.

When a protected provider disappears, **restart grace** shows a separate fixed ten-minute countdown and Agents describes it as **Waiting for app restart**. It applies in **Open** mode, or in **Tasks** mode when protection was observed just before the process disappeared. An idle Tasks-mode app does not arm it. AgentLatch cannot distinguish an intentional close from an update, so both use the same bounded grace. The app returning clears restart grace and normal mode rules take over; repeated missing-process scans do not extend it. Switching the provider **Off** cancels it. Independent hook leases and manual sessions may still hold protection after a grace ends.

The final hook release also gets five seconds of settling grace, covering an updater sending SessionEnd immediately before the process disappears. Ordinary hook completion can therefore remain protected for roughly five seconds even without further work; expiry is processed on the next two-second reconciliation tick.

The dashboard has three explicit system states:

- **Ready when your agents are** (READY in the tray) — no work is latched and normal Windows sleep policy applies.
- **Sleep protection is on** (PROTECTED) — one or more latches are active and Windows accepted the system keep-awake request.
- **Sleep protection needs attention** (ERROR) — work is active but Windows rejected the request; AgentLatch shows the error, alerts once, and retries every reconciliation cycle.

Lifecycle-hook rows show **Active** (or a concurrent active count). Their renewable safety timeout is not an estimate of task completion. A **Releasing in…** countdown describes detector or restart grace; manual timers show their actual time remaining. The activity count describes agent/custom sources, not task totals: one row can contain several concurrent tasks. The built-in manual override is shown only in its strip, not duplicated in the activity list. Custom requests remain visible, including external leases using the `manual` source key. All activity sources remain available through the native, keyboard-accessible five-row pages; monitoring refreshes preserve the current page.

Use **Open diagnostic history** in the tray menu to inspect the bounded local event log. It contains timestamps, AgentLatch version and process ID, active latch counts and provider names, Windows error codes, detector release reasons, restart-grace transitions, and manual-session starts/stops/expiry. It never contains prompts, responses, workspace paths, terminal output, or conversation content. The current and previous log are each capped at 512 KiB.

### Screenshots without desktop control

`AgentLatch.exe --export-ui-screenshots C:\path\to\captures` exports `dashboard.png`, `agents.png`, and `settings.png` from one current state snapshot. It renders the real native controls in a hidden, non-activating window: no mouse/keyboard input, visible tab switch, desktop capture, or focus change. The developer wrapper is `scripts/capture-dashboard.ps1`. Exports reflect current provider settings and labels; review them before publishing. This is a UI capture, not an independent power-policy or overnight qualification test.

## Build from source

Requirements:

- Windows 10 or Windows 11
- Visual Studio 2022 Build Tools with Desktop development with C++
- CMake 3.24 or newer
- Inno Setup 7 when building the Windows setup executable

```powershell
git clone https://github.com/byassin/agent-latch.git
cd agent-latch
.\scripts\build.ps1
```

The x64 build script runs the executable's self-test before reporting success. CI also compiles ARM64.

Build the setup executable after compiling AgentLatch:

```powershell
.\scripts\build-installer.ps1 -Executable .\build-x64\Release\AgentLatch.exe -Version 0.4.2
```

## Design principles

- A wake reason is a lease, never an unexplained global switch.
- The display is allowed to turn off by default while the system stays awake.
- Routine wake-status notifications can be disabled; a protection failure still raises one critical warning per failure episode.
- Unexpected process exits restart automatically, while a normal tray Exit remains final.
- Dashboard settings explain the difference between PC wake protection, keeping the screen on, and launching AgentLatch at Windows sign-in.
- Every automatic path has a timeout or observable process state.
- Manual indefinite protection is an explicit user choice, visibly labeled and independently stoppable.
- Provider integrations are additive and editable; existing hook configuration belongs to the user.
- Normal Windows sleep behavior returns when the last latch releases; activity and restart grace are bounded, while a manual session follows the duration you chose.

Read [Interface guide](docs/INTERFACE.md), [Architecture](docs/ARCHITECTURE.md), [Privacy](docs/PRIVACY.md), and [Contributing](CONTRIBUTING.md) for more.

## License

AgentLatch is available under the [MIT License](LICENSE).
