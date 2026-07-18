# Changelog

All notable changes to AgentLatch will be documented here.

## [Unreleased]

## [0.2.0-preview.6] - 2026-07-18

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
- The dashboard is now a compact status utility with a single wake state, quick timers, running work, and a concise agent-tracking list.

### Fixed

- Native Claude desktop and Codex desktop background activity no longer masquerades as an active coding task.
- Cursor completion and Antigravity `fullyIdle` events now release task latches precisely.
- Legacy on/off provider preferences migrate safely to the new three-state modes.
- Integration repair removes stale AgentLatch commands from older preview paths while preserving unrelated hooks.
- Setup integration tests use isolated AppIds so they cannot overwrite a real AgentLatch installation's uninstall registration.

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

[Unreleased]: https://github.com/byassin/agent-latch/compare/v0.2.0-preview.6...HEAD
[0.2.0-preview.6]: https://github.com/byassin/agent-latch/compare/v0.1.0...v0.2.0-preview.6
[0.1.0]: https://github.com/byassin/agent-latch/releases/tag/v0.1.0
