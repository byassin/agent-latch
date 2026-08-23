# Changelog

All notable changes to AgentLatch will be documented here.

## [Unreleased]

## [0.2.6] - 2026-08-22

### Fixed

- Claude Code hooks now use its shell-free executable-and-arguments form, eliminating PowerShell and Git Bash quoting failures while passing AgentLatch's process exit code directly to Claude.
- Integration repair recognizes and replaces legacy quoted commands, manually repaired bare-path commands, and stale exec-form entries across all 11 Claude lifecycle events.
- Installer tests now verify Claude hook execution from a path containing spaces returns exit code zero, writes `{}` to standard output, and writes nothing to standard error.

## [0.2.5] - 2026-08-22

### Added

- A zero-CPU companion watchdog now restarts AgentLatch after an abnormal exit, with a bounded three-attempt rapid-failure guard. Normal tray Exit and installer shutdown remain final.
- A local, rotating diagnostic history records application lifetime, latch counts and provider names, Windows power-request results, and watchdog recovery events without recording prompts, responses, workspaces, or conversation content.
- Protection failures now raise a dedicated warning even when routine wake-status notifications are disabled.

### Changed

- The dashboard now reports **READY**, **PROTECTED**, or **ERROR** so an active latch is clearly distinguished from a Windows-accepted system power request.
- Sign-in startup and notification descriptions now explain automatic restart behavior and critical protection alerts.
- The tray menu can open the local diagnostic history directly.

### Fixed

- Claude Code no longer releases its session latch when a foreground turn stops with background agents, shell jobs, monitors, workflows, teammates, cloud sessions, or MCP work still in flight. Claude task-registry entries now receive independent latches through `TaskCreated` and `TaskCompleted`.
- System and display power-request errors are tracked independently, retried, and surfaced with their Windows error codes.
- Portable install tests and non-production uninstallers no longer stop the running production instance or delete a startup entry that points to another AgentLatch installation.

## [0.2.4] - 2026-08-17

### Added

- Tasks mode now detects active ChatGPT responses in the unified OpenAI Windows app, including when its window is minimized, without latching merely because the app is open.

### Changed

- OpenAI desktop response detection runs in a read-only background UI Automation worker and observes only the primary composer command metadata. Prompt and conversation content are never read.
- Codex JSONL lifecycle tasks remain authoritative and are deduplicated against the unified app response signal.
- The dashboard identifies a UI-derived latch as either a ChatGPT response or a Codex response instead of presenting both as a generic Codex task.

### Fixed

- A regular ChatGPT response now adds its own active instance while Codex lifecycle tasks run; only a Codex composer response is deduplicated against those tasks.
- Dashboard, tray-menu, and tooltip counts now include concurrent instances represented by an aggregated provider latch.
- Numeric Windows file and product version resources now stay synchronized with the user-visible semantic version.
- Source-build and integration documentation now describes the actual output paths, detection timing, release grace, and OpenAI privacy boundary.
- The installed documentation bundle now includes every local file and image linked from its README.

## [0.2.3] - 2026-08-01

### Fixed

- Claude Code helper daemons and background PTY processes that survive after the visible CLI closes no longer renew a false task latch.

### Changed

- Managed Claude lifecycle hooks are now authoritative in **Tasks** mode. **Open** mode remains presence-based, and portable installs without managed hooks retain conservative CLI activity detection.
- Claude integration health is persisted alongside Codex integration health so AgentLatch can distinguish a managed installation from a process-only portable copy.

## [0.2.2] - 2026-07-31

### Fixed

- Codex tasks resumed from older session folders are now discovered across the full local session tree instead of only today's and yesterday's folders.
- Stale unfinished lifecycle records from a previous Codex desktop run no longer keep the PC awake after Codex restarts.
- The current packaged Codex desktop shell (`ChatGPT.exe` inside the `OpenAI.Codex` package) is recognized without treating unrelated ChatGPT applications as Codex.
- Installer upgrades now preserve a launch-at-sign-in preference enabled from inside AgentLatch.

### Changed

- Native Codex session discovery is refreshed on a bounded five-second cache and remains capped to the 64 most recently modified session files.

## [0.2.1] - 2026-07-18

### Added

- A dashboard and tray setting can now enable or disable Windows wake-status notifications.

### Changed

- Replaced the ambiguous dashboard footer with a clearly labeled Settings card, explanatory text, explicit On/Off states, and familiar switches for screen behavior, Windows sign-in startup, and notifications.

### Fixed

- Reused local CMake build directories no longer retain a stale user-visible version label after a project version bump.

## [0.2.0] - 2026-07-18

### Added

- Per-provider **Tasks**, **Open**, and **Off** modes, with task-only detection as the default.
- Global Google Antigravity lifecycle integration using `PreInvocation`, `PostInvocation`, and `Stop` hooks.
- Recognition of the installed `Cursor.exe` and `Antigravity.exe` desktop applications in **Open** mode.
- The recommended installer now configures every supported lifecycle integration by default, with `-SkipHooks` as an explicit opt-out.
- The uninstaller now removes AgentLatch's integration entries by default, with `-KeepHooks` as an explicit opt-out.
- Codex desktop tasks are detected natively from the local `task_started` / `task_complete` lifecycle stream without requiring hook trust or a chat command.
- Upgrades now stop the previous AgentLatch instance before replacing and relaunching the installed executable.
- A visible version badge now appears in the dashboard header, window title, About dialog, and executable metadata.
- Releases now ship as proper Windows setup executables with upgrade, Start menu, startup, integration, and uninstall support instead of ZIP archives.
- The dashboard is now a compact, agent-focused status utility with running work and a concise agent-tracking list.
- The shipped AgentLatch artwork is now used in the native dashboard as well as the executable, installer, and repository.

### Fixed

- Native Claude desktop and Codex desktop background activity no longer masquerades as an active coding task.
- Cursor completion and Antigravity `fullyIdle` events now release task latches precisely.
- Legacy on/off provider preferences migrate safely to the new three-state modes.
- Integration repair removes stale AgentLatch commands from older preview paths while preserving unrelated hooks.
- Setup integration tests use isolated AppIds so they cannot overwrite a real AgentLatch installation's uninstall registration.
- High-DPI layouts no longer clip the product name, version, idle status, or header controls.

### Changed

- Removed generic interval and indefinite wake controls from the dashboard and tray so AgentLatch stays focused on active agent work.
- Moved integration repair out of the main dashboard and into the tray's advanced recovery menu.
- Replaced the compressed repository screenshot with a lossless high-resolution capture.

## [0.1.0] - 2026-07-17

### Added

- Native Windows tray application and DPI-aware dashboard.
- Reference-counted latches for concurrent agent sessions and subagents.
- Windows system and optional display power requests.
- Activity-aware detection for Codex, Claude Code, Cursor, OpenCode, and Gemini CLI process trees.
- Lifecycle hook bridge for Codex, Claude Code, and Cursor.
- Safe, idempotent hook integration installer with backups and uninstall support.
- Manual 30-minute, one-hour, two-hour, and indefinite wake controls.
- Local renewable lease command-line API.
- Per-provider controls, startup toggle, tray notifications, and crash-safe lease expiry.
- x64 self-tests plus x64 and ARM64 continuous-integration builds.

[Unreleased]: https://github.com/byassin/agent-latch/compare/v0.2.4...HEAD
[0.2.4]: https://github.com/byassin/agent-latch/compare/v0.2.3...v0.2.4
[0.2.3]: https://github.com/byassin/agent-latch/compare/v0.2.2...v0.2.3
[0.2.2]: https://github.com/byassin/agent-latch/compare/v0.2.1...v0.2.2
[0.2.1]: https://github.com/byassin/agent-latch/compare/v0.2.0...v0.2.1
[0.2.0]: https://github.com/byassin/agent-latch/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/byassin/agent-latch/releases/tag/v0.1.0
