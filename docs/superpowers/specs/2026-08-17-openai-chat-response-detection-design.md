# OpenAI Chat Response Detection Design

## Goal

AgentLatch must keep Windows awake while the unified OpenAI Windows app is actively producing either a ChatGPT response or a Codex task result. Merely opening the app, typing an unsent prompt, or leaving an idle conversation visible must not create a latch.

## Context

The OpenAI Windows app now hosts both ChatGPT conversations and Codex tasks in the same packaged `ChatGPT.exe` process tree. AgentLatch already detects Codex task lifecycle markers in local `.codex/sessions` JSONL files, but Chat mode does not publish those markers. Process presence and CPU or I/O activity are deliberately insufficient because Electron background work continues while the user-facing agent is idle.

Live inspection of the packaged app established a narrower response signal: while a response is in progress, the primary composer control changes from Send to Stop. Windows UI Automation exposes that control as an enabled Chrome button whose class list contains both `size-token-button-composer` and `bg-primary-solid`. The nearest Document ancestor identifies whether the active surface is ChatGPT or Codex. The app exposes control metadata but no conversation text is required.

## Selected approach

Add a read-only Windows UI Automation probe alongside the existing Codex lifecycle scanner. The probe runs on its own thread, observes only the unified OpenAI package's top-level windows, selects the primary composer button by stable class tokens, and classifies its nearest Document surface. A small state machine distinguishes the idle composer label from the response-time label, including localized labels learned from stable disabled idle samples.

This approach is preferred over process activity because it observes a user-facing lifecycle state rather than background work. It is preferred over app-file or cache scraping because those artifacts are noisy, private, and undocumented. No hooks are injected into the OpenAI process.

## Architecture

### `openai_ui_activity`

The new component has two separable responsibilities:

1. A pure classifier receives normalized UI Automation observations and produces `Inactive`, `Responding`, or `Unknown` state plus a `ChatGPT`, `Codex`, or `Unknown` surface. This logic is deterministic and covered by built-in self-tests.
2. A Windows UI Automation worker discovers observations for packaged `ChatGPT.exe` root process IDs and publishes the latest immutable snapshot to `AgentDetector`. COM and UI Automation work never run on the Win32 message-loop thread.

The worker polls every two seconds, matching the existing detector cadence. Target process IDs are replaced atomically as process snapshots change. If COM initialization, element enumeration, property access, or ancestry walking fails, the worker publishes no active response and tries again on the next poll.

### Candidate selection

A composer candidate must satisfy all of these conditions:

- UI Automation control type is Button.
- The element belongs to a root `ChatGPT.exe` process from a path containing `\\WindowsApps\\OpenAI.Codex_`.
- Its class name contains the exact whitespace-delimited tokens `size-token-button-composer` and `bg-primary-solid`.
- It is enabled before it can represent an active response. UI Automation's
  `IsOffscreen` value is recorded for diagnostics but is not a rejection
  condition because minimized and background windows commonly report their
  descendants as offscreen while generation continues.

The classifier walks the control-view parent chain to the nearest Document ancestor. A Document name equal to `ChatGPT` maps to the ChatGPT surface; `Codex` maps to the Codex surface. Other or missing names are unknown and fail closed.

### Localized label learning

English starts with a built-in idle label of `Send` and active label of `Stop`. For other locales, the classifier learns an idle label only after seeing the same non-empty composer name while the control is disabled for three consecutive observations. A single disabled transition cannot replace the learned idle label. Once an idle label is known, an enabled, visible composer with a different non-empty name is responding. An enabled composer with the idle label is inactive, including when a prompt has been typed but not submitted.

Label learning is in-memory and contains only the accessibility name of the composer command. It is reset when AgentLatch exits. This keeps v0.2.4 free of new persistent user data while allowing localized sessions to calibrate after an idle sample.

### Detector merge and deduplication

`AgentDetector` remains the only producer of provider detection results. It passes packaged OpenAI root PIDs to the worker and merges the latest UI snapshot with Codex session lifecycle state:

- One or more active Codex JSONL sessions remain authoritative and determine the task count and existing Codex detail text.
- A ChatGPT-surface response contributes one active instance when no Codex lifecycle task represents it, with detail `ChatGPT is responding`.
- A Codex-surface UI response is a fallback only when the JSONL scan reports zero active Codex tasks, with detail `Codex is responding`.
- A UI response never increments an already nonzero Codex lifecycle count, preventing double counting.

While the UI signal is active, `last_activity` is refreshed. When it disappears, the existing configurable activity grace period, currently three minutes by default, controls delayed release exactly as it does for other task activity.

## User experience

No new setting is required. The existing Codex/OpenAI provider row and Tasks mode gain ChatGPT response awareness automatically. The Running now card uses precise text for the winning signal: `ChatGPT is responding`, `Codex is responding`, or the existing singular/plural Codex task wording.

Open mode continues to latch on process presence, and Off continues to suppress detection. Existing hooks, CLI leases, notifications, startup behavior, and display preference are unchanged.

## Privacy and security

The probe is strictly read-only. It reads only UI Automation metadata needed to identify the primary composer control: process ID, control type, class name, enabled/offscreen state, accessibility name, and nearest Document name. It does not read conversation messages, prompt text, edit controls, clipboard data, network traffic, app databases, or web content. It does not send input or manipulate the OpenAI app.

All processing remains local. No new network endpoint, service, privilege, dependency, or telemetry is introduced.

## Failure behavior

The integration fails safe toward allowing sleep. Unknown UI structure, unknown surfaces, empty labels, inaccessible windows, COM failure, app updates, minimized-window enumeration failure, and worker shutdown all produce no new UI-based latch. Existing Codex JSONL detection, lifecycle hooks, process-presence Open mode, and local leases continue independently.

The last UI snapshot has a bounded freshness window. If the worker cannot refresh it for more than two polling intervals, `AgentDetector` ignores it rather than extending a stale response indefinitely.

## Testing and acceptance

Automated self-tests cover:

- exact composer class-token matching and unrelated-button rejection;
- Send idle, typed-but-unsent Send, and Stop responding behavior;
- ChatGPT and Codex Document surface classification;
- localized idle-label learning after three stable disabled observations;
- rejection of empty, disabled, unknown-surface, and stale observations, while
  preserving a valid response signal for an offscreen/minimized composer;
- resistance to a one-sample disabled transition overwriting the idle label;
- merge behavior for ChatGPT response, Codex fallback, and Codex lifecycle deduplication.

Native validation must include a clean x64 Release build, built-in self-test, existing installer/integration scripts, and a live read-only probe against the installed unified OpenAI app. ARM64 must continue to compile in CI. Manual acceptance should exercise Chat idle, typed unsent, Chat response start/stop, Codex response without double counting, multiple Codex tasks, background/minimized behavior, and a non-English locale when available.

## Release scope

The feature ships as AgentLatch `0.2.4`. README, architecture, privacy/detection documentation, CI test version strings, and installer examples must reflect the new behavior. Publishing a GitHub release remains a separate release operation after the feature branch is reviewed.
