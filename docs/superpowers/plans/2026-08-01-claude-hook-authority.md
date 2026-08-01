# Claude Hook Authority Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> **Status:** Completed and released as [AgentLatch v0.2.3](https://github.com/byassin/agent-latch/releases/tag/v0.2.3) on 2026-08-01 through [PR #4](https://github.com/byassin/agent-latch/pull/4). The hook-seen path was verified with isolated registry tests and a controlled live Claude start/stop cycle instead of making the native self-test write to the user's registry.

**Goal:** Release AgentLatch 0.2.3 so installed Claude lifecycle hooks are authoritative in Tasks mode and persistent Claude daemon activity cannot create false wake latches.

**Architecture:** Persist Claude integration health beside the existing Codex fields, expose one Settings policy for process-activity fallback, and apply it only when reconciling detector latches. Hook latches and Open-mode presence continue through their existing paths.

**Tech Stack:** C++20 Win32, Windows Registry, PowerShell integration installer, CMake/Ninja, Inno Setup 7.

## Global Constraints

- No service, daemon, telemetry, administrator requirement, or new dependency.
- Claude Tasks mode uses hooks when AgentLatch installed them.
- Claude Open mode remains presence based.
- Unmanaged Claude CLI use retains process-activity fallback.
- The fix ships as stable version 0.2.3 for x64 and ARM64.

---

### Task 1: Add the detector fallback policy with a failing regression test

**Files:**
- Modify: `src/main.cpp`
- Modify: `src/settings.h`
- Modify: `src/settings.cpp`
- Modify: `src/app.cpp`

**Interfaces:**
- Produces: `bool Settings::UseProcessActivityFallback(Provider provider) const`
- Consumes: `Settings::ProviderMode(Provider)` and `claude_integration_expected`

- [x] **Step 1: Write the failing self-test**

Add assertions in `RunSelfTests()` that require these literal outcomes:

```cpp
Settings managed;
managed.claude_mode = DetectionMode::Tasks;
managed.claude_integration_expected = true;
if (managed.UseProcessActivityFallback(Provider::ClaudeCode) ||
    !managed.UseProcessActivityFallback(Provider::Codex)) {
    return 54;
}
managed.claude_integration_expected = false;
if (!managed.UseProcessActivityFallback(Provider::ClaudeCode)) {
    return 55;
}
managed.claude_integration_expected = true;
managed.claude_mode = DetectionMode::Open;
if (!managed.UseProcessActivityFallback(Provider::ClaudeCode)) {
    return 56;
}
```

- [x] **Step 2: Run the build and verify RED**

Run the existing release build. Expected: compilation fails because `claude_integration_expected` and `UseProcessActivityFallback` do not exist.

- [x] **Step 3: Implement the minimal policy**

Add `claude_integration_expected` and `claude_hook_seen` fields to `Settings`. Implement:

```cpp
bool Settings::UseProcessActivityFallback(Provider provider) const {
    return provider != Provider::ClaudeCode || claude_mode != DetectionMode::Tasks ||
           !claude_integration_expected;
}
```

In `UpdateDetectorLatches`, require this policy only for Tasks-mode detector activity; leave Open mode unchanged.

- [x] **Step 4: Run the native self-test and verify GREEN**

Build Release and run `AgentLatch.exe --self-test` with `Start-Process -Wait -PassThru`. Expected: exit code 0.

### Task 2: Persist managed Claude integration status

**Files:**
- Modify: `src/settings.cpp`
- Modify: `scripts/install-integrations.ps1`
- Modify: `tests/integration-installer.tests.ps1`

**Interfaces:**
- Registry inputs: `IntegrationExpectedClaude`, `IntegrationCommandClaude`, `HookSeenClaude`
- Existing IPC input: `SEEN\tclaude`

- [x] **Step 1: Add failing settings and installer coverage**

Add controlled integration-installer coverage for fresh install, idempotent reinstall, and uninstall status while preserving unrelated Claude JSON. Verify the Claude hook-seen path with a controlled live lifecycle event so the native self-test does not modify the user's registry.

- [x] **Step 2: Verify RED**

Run the new installer-status test before adding the override and persistence support. Expected: failure because the status-test input and fresh-install handling do not exist.

- [x] **Step 3: Implement registry persistence**

Load and refresh the three Claude values. Generalize `MarkHookSeen` to write `HookSeenCodex` or `HookSeenClaude`. Update the PowerShell installer to set/reset the Claude status fields when operating against the real user profile.

- [x] **Step 4: Verify GREEN**

Run native self-test plus `integration-installer.tests.ps1` and `install.tests.ps1`. Expected: all exit successfully.

### Task 3: Package and verify AgentLatch 0.2.3

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `src/version.h`
- Modify: `resources/version.rc.in`
- Modify: `resources/app.manifest`
- Modify: `installer/AgentLatch.iss`
- Modify: `scripts/build-installer.ps1`
- Modify: `.github/workflows/ci.yml`
- Modify: `README.md`
- Modify: `CHANGELOG.md`
- Modify: `docs/ARCHITECTURE.md`
- Modify: `docs/INTEGRATIONS.md`

**Interfaces:**
- Produces: x64 and ARM64 `AgentLatch-Setup-0.2.3-<architecture>.exe` installers, SHA-256 sidecars, and `SHA256SUMS.txt`

- [x] **Step 1: Update version and documentation**

Set every stable version field to `0.2.3` / `0.2.3.0`. Document the Claude hook-authority rule, fallback behavior, and fixed false-positive daemon chain.

- [x] **Step 2: Run full verification**

Run the release build, native self-test, integration installer test, default install test, isolated setup test, and `git diff --check`. Expected: all exit 0.

- [x] **Step 3: Build and install the production setup**

Build the x64 production installer, verify its SHA-256 sidecar, perform a silent upgrade, and confirm installed executable version `0.2.3` plus preserved startup registration.

- [x] **Step 4: Verify the original live symptom**

With the existing Claude daemon, PTY host, and resumed session processes still running, open the AgentLatch dashboard. Expected: Claude is absent from **Running now** in Tasks mode. Send a controlled Claude `UserPromptSubmit` hook event and matching `Stop`; expected: a countdown hook latch appears and then releases immediately.

- [x] **Step 5: Inspect final scope**

Run `git status -sb`, `git diff --check`, and `git diff --stat`. Expected: only the 0.2.3 detector-policy, integration-status, tests, version, and documentation files are modified.
