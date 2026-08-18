# OpenAI Chat Response Detection Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Detect active ChatGPT responses in the unified OpenAI Windows app without latching merely because the app is open.

**Architecture:** Add a pure composer-state classifier and a read-only Windows UI Automation worker in one focused component. `AgentDetector` supplies only packaged OpenAI root PIDs, consumes a fresh immutable snapshot, and merges it with authoritative Codex JSONL lifecycle state without double counting.

**Tech Stack:** C++20, native Win32, Windows UI Automation COM API, CMake, built-in `--self-test`, PowerShell integration/installer tests.

**Spec:** `docs/superpowers/specs/2026-08-17-openai-chat-response-detection-design.md`

## Global Constraints

- Ship as version `0.2.4`.
- Poll the unified app off the Win32 UI thread every two seconds.
- Observe only packaged `ChatGPT.exe` roots whose path contains `\\WindowsApps\\OpenAI.Codex_`.
- Read only process/control metadata; never read prompt or conversation content and never inject input.
- Treat a UI snapshot older than two polling intervals as inactive.
- Keep existing Codex JSONL lifecycle state authoritative and never double count it.
- Unknown or changed UI structure fails closed and does not create a new latch.
- No service, elevation, network endpoint, telemetry, or third-party runtime dependency.

---

### Task 1: Pure composer classifier

**Files:**
- Create: `src/openai_ui_activity.h`
- Create: `src/openai_ui_activity.cpp`
- Modify: `src/main.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `OpenAISurface`, `OpenAIResponseState`, `OpenAIComposerObservation`, `OpenAIActivitySnapshot`, `OpenAIComposerClassifier::Observe`, `IsOpenAIComposerClass`, and `RunOpenAIUiActivitySelfTests`.
- Consumes: only C++ standard-library types and Win32 tick values in the pure classifier path.

- [ ] **Step 1: Declare the wished-for classifier API and write failing behavior tests**

Add the header types and call a new `RunOpenAIUiActivitySelfTests()` from `RunSelfTests()`. The tests use literal observations such as:

```cpp
OpenAIComposerClassifier classifier;
OpenAIComposerObservation stop{
    L"Stop",
    L"inline-flex size-token-button-composer bg-primary-solid",
    L"ChatGPT",
    true,
    false,
};
const OpenAIActivitySnapshot active = classifier.Observe(stop, 1000);
if (active.state != OpenAIResponseState::Responding ||
    active.surface != OpenAISurface::ChatGPT) {
    return false;
}
```

Cover exact class tokens, unrelated classes, English Send/Stop, typed-unsent Send, ChatGPT/Codex/unknown Documents, disabled controls, offscreen/minimized Stop, empty labels, three-sample localized idle learning, and resistance to a single disabled transition.

- [ ] **Step 2: Build and verify RED**

Run the portable CMake/Ninja build command for the worktree and execute `AgentLatch.exe --self-test` with `Start-Process -Wait -PassThru`.

Expected: compilation or link failure names the missing classifier implementation. It must fail because the new behavior does not exist, not because of a malformed test.

- [ ] **Step 3: Implement the minimal pure classifier**

Implement exact whitespace-delimited class-token matching, case-insensitive `ChatGPT`/`Codex` Document mapping, English Send/Stop handling, and the three-disabled-sample localized idle state machine. Disabled observations always classify inactive; offscreen status does not suppress a valid targeted response.

The enabled branch must follow this order:

```cpp
if (EqualsInsensitive(name, L"Send") || EqualsInsensitive(name, learned_idle_label_)) {
    return InactiveSnapshot(surface, now);
}
if (EqualsInsensitive(name, L"Stop") || !learned_idle_label_.empty()) {
    return RespondingSnapshot(surface, now);
}
return UnknownSnapshot(now);
```

- [ ] **Step 4: Build and verify GREEN**

Run the complete native build and `--self-test` again.

Expected: executable builds with warnings treated as errors and self-test exits `0`.

- [ ] **Step 5: Commit the classifier cycle**

```powershell
git add CMakeLists.txt src/main.cpp src/openai_ui_activity.h src/openai_ui_activity.cpp
git commit -m "feat: classify unified OpenAI response state"
```

---

### Task 2: Snapshot freshness and Codex deduplication

**Files:**
- Modify: `src/openai_ui_activity.h`
- Modify: `src/openai_ui_activity.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Produces: `OpenAIMergedActivity MergeOpenAIActivity(unsigned int active_codex_tasks, const OpenAIActivitySnapshot& snapshot, ULONGLONG now, ULONGLONG maximum_age)`.
- Consumes: classifier snapshots from Task 1.

- [ ] **Step 1: Write failing merge tests**

Use literal snapshots and assert these consumer-visible results:

```cpp
const OpenAIMergedActivity chat = MergeOpenAIActivity(
    0,
    {OpenAIResponseState::Responding, OpenAISurface::ChatGPT, 9000},
    10000,
    4000);
// active_instances == 1; detail == L"ChatGPT is responding"
```

Also cover Codex UI fallback, one and two authoritative Codex tasks, no increment when both signals are active, inactive/unknown snapshots, a future timestamp, and a snapshot older than `maximum_age`.

- [ ] **Step 2: Build and verify RED**

Run the full native build and self-test.

Expected: link failure for `MergeOpenAIActivity`, proving the new merge contract is not implemented.

- [ ] **Step 3: Implement minimal freshness and merge behavior**

Return the Codex lifecycle count and existing singular/plural task detail whenever `active_codex_tasks > 0`. Otherwise accept only a `Responding` snapshot with a known surface and `snapshot.observed_at <= now` whose age is within `maximum_age`; map ChatGPT to `ChatGPT is responding` and Codex to `Codex is responding`.

- [ ] **Step 4: Build and verify GREEN**

Run the build and self-test; expected exit is `0`.

- [ ] **Step 5: Commit the merge cycle**

```powershell
git add src/main.cpp src/openai_ui_activity.h src/openai_ui_activity.cpp
git commit -m "feat: merge OpenAI response activity safely"
```

---

### Task 3: Background Windows UI Automation probe

**Files:**
- Modify: `src/openai_ui_activity.h`
- Modify: `src/openai_ui_activity.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `OpenAIUnifiedActivityProbe::SetTargets(std::vector<DWORD>)` and `OpenAIUnifiedActivityProbe::Snapshot() const`.
- Consumes: `OpenAIComposerClassifier::Observe` from Task 1.

- [ ] **Step 1: Write failing lifecycle tests for the worker-facing state boundary**

Add real state-holder tests that set target PID lists, verify normalization removes zero/duplicates and sorts IDs, and verify `Snapshot()` starts unknown. Keep UI Automation itself unmocked; the external COM boundary is covered by live validation in Task 6.

- [ ] **Step 2: Build and verify RED**

Run the build and self-test.

Expected: missing `OpenAIUnifiedActivityProbe` methods cause compilation/link failure.

- [ ] **Step 3: Implement the worker and UI Automation query**

Use a private implementation owning `std::jthread`, `std::mutex`, and `std::condition_variable_any`. Initialize COM with `COINIT_MULTITHREADED` inside the worker, create `IUIAutomation`, enumerate top-level windows for only the normalized target PIDs, find descendant Button controls, read the class/name/enabled/offscreen properties, and walk to the nearest Document with the control-view walker. Publish one immutable snapshot under the mutex; prefer any responding observation over inactive or unknown observations.

The worker waits with its stop token for two seconds between scans and releases every COM interface/BSTR on all paths. `SetTargets` wakes the worker. An empty target list publishes unknown immediately.

- [ ] **Step 4: Link Windows UI Automation and verify GREEN**

Add `ole32`, `oleaut32`, and `uiautomationcore` to `target_link_libraries`. Run the full native build and self-test; expected exit is `0` with no compiler warnings.

- [ ] **Step 5: Commit the probe cycle**

```powershell
git add CMakeLists.txt src/openai_ui_activity.h src/openai_ui_activity.cpp src/main.cpp
git commit -m "feat: probe unified OpenAI activity off thread"
```

---

### Task 4: Integrate the probe with `AgentDetector`

**Files:**
- Modify: `src/agent_detector.h`
- Modify: `src/agent_detector.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: probe and merge interfaces from Tasks 2-3.
- Produces: existing `DetectionResult` with accurate `active_task_instances`, `recently_active`, `last_activity`, and `activity_detail` for unified OpenAI activity.

- [ ] **Step 1: Write a failing detector-result regression test around the pure merge seam**

Add a table-driven assertion that a ChatGPT UI response produces one task-mode instance and exact ChatGPT detail, while two Codex lifecycle tasks plus the same UI snapshot remain two, not three. The production mutation these tests catch is bypassing the merge helper or incrementing both sources.

- [ ] **Step 2: Build and verify RED**

Temporarily route a test through a new `ApplyOpenAIActivityToDetectionResult` declaration and run self-test.

Expected: link failure because the detector seam is absent.

- [ ] **Step 3: Implement detector integration**

Store `OpenAIUnifiedActivityProbe` as an `AgentDetector` member. During process snapshot classification, collect only root packaged `ChatGPT.exe` PIDs and call `SetTargets`. Read the latest snapshot, merge it with `ScanCodexDesktopSessions`, and use the merged count/detail for `Provider::Codex`. Refresh `last_activity_` while the merged activity is nonzero so the existing configured grace period controls release.

- [ ] **Step 4: Build and verify GREEN**

Run the native build and complete self-test. Expected exit is `0`; existing provider-classification and Codex lifecycle tests remain green.

- [ ] **Step 5: Commit detector integration**

```powershell
git add src/agent_detector.h src/agent_detector.cpp src/main.cpp
git commit -m "feat: latch on unified OpenAI responses"
```

---

### Task 5: Version and documentation

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `README.md`
- Modify: `docs/ARCHITECTURE.md`
- Modify: `docs/PRIVACY.md` if present, otherwise document the privacy boundary in `README.md` and `docs/ARCHITECTURE.md`
- Modify: `.github/workflows/ci.yml`
- Modify: installer/version references found by `rg -n "0\\.2\\.3|ChatGPT|Codex sessions"`

**Interfaces:**
- Consumes: completed behavior from Tasks 1-4.
- Produces: truthful public product, architecture, privacy, and release documentation for `0.2.4`.

- [ ] **Step 1: Inventory every version and detector statement**

Run:

```powershell
rg -n "0\.2\.3|ChatGPT|Codex desktop|session scan|privacy" . -g "!build*/**" -g "!.worktrees/**"
```

- [ ] **Step 2: Update version and public explanation**

Set the CMake project version and CI installer test label to `0.2.4`. Explain that Tasks mode detects both explicit Codex task lifecycles and the unified app's response-state composer metadata, including the minimized-window behavior, two-second polling, existing release grace, no content access, and fail-closed compatibility behavior.

- [ ] **Step 3: Verify documentation consistency**

Search for stale `0.2.3` examples and contradictory claims that desktop shells are never activity-capable. Version-history references may remain when explicitly historical; current build/release examples must be `0.2.4`.

- [ ] **Step 4: Commit documentation and version**

```powershell
git add CMakeLists.txt README.md docs .github/workflows
git commit -m "docs: document OpenAI response detection"
```

---

### Task 6: Full native, installer, and live validation

**Files:**
- Modify only if a failing test exposes a defect; every defect starts a new failing regression test.

**Interfaces:**
- Consumes: the complete `0.2.4` branch.
- Produces: fresh evidence for release readiness.

- [ ] **Step 1: Clean x64 Release build and self-test**

Configure a fresh Release directory, compile all sources, then run:

```powershell
$process = Start-Process -FilePath .\build-release\AgentLatch.exe -ArgumentList '--self-test' -Wait -PassThru
if ($process.ExitCode -ne 0) { throw "Self-test failed: $($process.ExitCode)" }
```

- [ ] **Step 2: Run existing integration suites**

```powershell
.\tests\integration-installer.tests.ps1 -AgentLatchPath .\build-release\AgentLatch.exe
.\tests\install.tests.ps1 -AgentLatchPath .\build-release\AgentLatch.exe
```

Both scripts must exit `0` and restore their temporary state.

- [ ] **Step 3: Build and test the setup executable**

Run `scripts/build-installer.ps1` for x64 version `0.2.4`, then its random-AppId test build. Confirm both commands exit `0`, the setup binary exists, and the compiled executable reports version `0.2.4`.

- [ ] **Step 4: Live read-only OpenAI signal check**

With the installed unified OpenAI app running, query its primary composer through the same UI Automation properties and confirm the worker can see the expected class tokens, accessibility name, nearest Document, target process ID, and minimized/offscreen state. During a Codex task, confirm JSONL plus UI state still yields the authoritative Codex task count rather than an extra latch.

Do not send UI input or read conversation content. Chat idle/typed/streaming locale scenarios that require a human conversation action are reported separately if they cannot be exercised safely in the current task.

- [ ] **Step 5: Review requirements and working tree**

Re-read the spec, inspect `git diff main...HEAD`, verify no generated build artifacts are tracked, and run `git status --short --branch`. Record any unexercised manual acceptance case explicitly.

- [ ] **Step 6: Commit any test-driven corrections**

If validation required changes, commit each red/green correction separately. Otherwise leave the already verified commits unchanged.
