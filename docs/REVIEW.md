# Source review and improvements — 2026-11-06

Review for **0.6.0 and 0.6.1**. Local validation uses isolated test targets; production installation is a separate release step. Current hosted results are recorded on [CI](https://github.com/byassin/agent-latch/actions/workflows/ci.yml), the [release workflow](https://github.com/byassin/agent-latch/actions/workflows/release.yml) and linked [pull requests](https://github.com/byassin/agent-latch/pulls). Release evidence is available separately for [0.6.0](https://github.com/byassin/agent-latch/releases/tag/v0.6.0) and [0.6.1](https://github.com/byassin/agent-latch/releases/tag/v0.6.1).

## Scope

Reviewed the native UI/manual flow, settings, process and lifecycle detection, Claude foreground/background tracking, Codex session scanning, Windows power requests, tray/watchdog behavior, CLI/IPC, diagnostics, installer, CI and documentation. This is a source/reliability/usability review, not a formal security certification or an overnight/physical-hardware qualification.

## Selected fixes and features

| Finding | Change |
|---|---|
| Cancelled Codex turns had no terminal marker handling | Recognize `turn_aborted` and start/complete aliases; parse direct JSON metadata and reject nested/escaped lookalikes. Preserve normal grace rather than holding forever. |
| Initial `PowerCreateRequest` failure was permanent while UI promised retries | Recreate at most every two seconds, apply current intent on recovery, normalize missing error codes, preserve set/clear truth. |
| Silent setup stopped a running app but did not relaunch it | Resume hidden only at successful setup completion and only for a previously running production upgrade. Cold/stopped/test/no-stop installs do not launch. |
| Release timing was a hidden registry setting | Native Agents selector for 30s, 1/3/5/10/30m; retain custom saved values and the default 3m. Hooks/manual timers retain separate timing. |
| Troubleshooting needed raw logs/screenshots | Local JSON report with aggregate state/native errors, no labels/IDs/paths/chat data, atomic file replacement and no auto-start/upload. |
| Tray registration failure could hide the app indefinitely | Bounded registration retry and modification-failure recovery, without changing tracking settings. |
| Some preference saves and IPC failures were reported as success | Check results, restore displayed preferences on save failure, explain failures, return delivery errors and avoid starting a daemon just to release an absent lease. |
| CLI option scans treated option-looking label values as commands | Consume value arguments before recognizing flags; reject unknown/duplicate options and mixed primary commands instead of launching or stopping the app accidentally. |
| Settings/timer initialization failures were ignored | Fail initialization and log the component rather than run without functioning monitoring. |

Claude's idle Tasks view also provides first-hook guidance when hooks are expected but none has ever been received. Active/grace state takes precedence. Activity remains read-only; no selection or detail pane was reintroduced. No background network/update checker, telemetry, injected input or system power-policy rewrite was added.

The Codex terminal/alias vocabulary was checked against the [official protocol](https://raw.githubusercontent.com/openai/codex/main/codex-rs/protocol/src/protocol.rs). Installer resume uses `ssDone`, documented as successful completion in the [official Inno events reference](https://jrsoftware.org/ishelp/topic_scriptevents.htm); cancellation does not run the resume branch.

## Validation and boundaries

The local candidate uses strict compiler warnings and isolated native self-tests: fake power APIs for failure/recovery, metadata/cache cancellation cases, real in-memory manual sessions, hidden native controls at five scales in both themes, real repaint clipping on a never-activated test desktop, and JSON/privacy/atomic-write checks. The new UI tests found and corrected font clipping at 125% scaling.

Isolated portable/setup tests must use test-owned folders/registry keys and no-stop/no-launch switches. The production-mode silent-upgrade regression is deliberately restricted to a disposable GitHub-hosted runner; it is added to CI/release workflows and is **not run against the user's machine**. Local compilation and passing unit tests are not a claim of remote CI completion, physical ARM runtime, a live Explorer restart test, cancelled-installation UI testing or effective Windows policy.

Local validation completed: warning-as-error x64 build; native self-test (including power failures, cancellation/read-boundary cases, manual flow and 180 light/dark UI fixtures); JSON/schema/privacy/atomic-write checks; 25 invalid-CLI cases; version metadata; integration merging; isolated portable install/uninstall; and isolated Inno setup install/uninstall. The running 0.5.2 process and preferences were left untouched during these tests. Hosted-only production upgrade tests are required by CI and release workflows, not run locally; they cover same-binary reinstall/resume, not a previous-version-binary migration matrix.

## Important remaining limitations

Version 0.6.1 addresses the startup settings mismatch seen during local 0.6.0 installation: checked unpackaged launch normalization and fresh atomic preference snapshots. See [startup context](ARCHITECTURE.md#startup-settings-context). The historical package context was not captured; failure-injection coverage is not proof of that event's exact cause or a real-MSIX integration test.

Follow-up local validation: reproduced stale missing-key fallback against the pre-fix settings object (five Off modes incorrectly retained), then passed the same isolated probe with five default Tasks modes after the fix. Warning-as-error x64 compilation, combined native self-test, real hidden direct/relay identity probes, 32 invalid-CLI cases, status-report JSON/privacy checks, version metadata and temporary integration merging passed. The self-test rendered 180 light/dark fixtures. Native settings tests rotate all five modes through Off/Tasks/Open, verify legacy precedence/fallback and reject an injected late read failure without partial state replacement. The installed 0.6.0 monitor/watchdog and live saved preferences were unchanged. Hosted MSVC/x64 runtime and ARM64 compilation checks, release setup/upgrade verification and official-asset installation are separate required gates; consult the linked workflows for current results. Real-MSIX-parent integration and physical ARM/overnight qualification are not established by these checks.

1. **Claude restart gaps:** automatic foreground/worker state is memory-only. An AgentLatch/watchdog/update restart during a quiet long call can lose that signal until another active hook. Historical `hook_seen` is not current session state. Manual keep-awake is the explicit independent safeguard; silently restoring indefinite stale agents would be unsafe.
2. **Large Codex histories:** discovery recursively traverses history and session scanning is synchronous on the main loop. Very large/growing histories need an incremental/background scanner design with cache and cancellation tests; that larger architectural change is not bundled here.
3. **Missing/ambiguous lifecycle events:** per-file latest metadata is not turn-ID-correlated orchestration. Missing terminals or unusual interleavings cannot be conclusively classified from process presence alone.
4. **Shared log rotation:** main/watchdog rotation lacks cross-process serialization. A rare concurrent rotation can misplace/drop records; reports intentionally do not treat retained logs as complete history.
5. **Persistence is not a transaction:** settings contain multiple registry values. Failure is now surfaced, but partial writes under exceptional registry failures are not an atomic multi-value transaction.
6. Windows can still explicitly sleep/restart/shut down or act on critical battery/power policy. API acceptance and source tests do not establish that none of those events can occur.
7. Installer background resume runs only after successful completion. A failed/cancelled setup is not promised to restore a monitor that setup already stopped; do not treat this change as an atomic upgrade/rollback power handoff.

These are recorded limits, not claims that all possible provider/Windows behaviors have been solved.
