# Contributing to AgentLatch

Thanks for helping make unattended agent work safer and more dependable on Windows.

## Development setup

Install Visual Studio 2022 Build Tools with Desktop development with C++, plus CMake 3.24 or newer. Install Inno Setup 7 when changing or verifying the Windows installer. Then run:

```powershell
.\scripts\build.ps1
```

The project treats compiler warnings as errors. The x64 build must pass `AgentLatch.exe --self-test`; ARM64 must compile successfully.

The default x64 output is `build-x64\Release\AgentLatch.exe`. Before submitting a behavior change, run the focused checks that cover it. A complete local verification is:

```powershell
.\scripts\build.ps1
.\tests\version.tests.ps1 -AgentLatchPath .\build-x64\Release\AgentLatch.exe -ExpectedVersion 0.3.0
.\tests\manual-cli.tests.ps1 -AgentLatchPath .\build-x64\Release\AgentLatch.exe
.\tests\integration-installer.tests.ps1 -AgentLatchPath .\build-x64\Release\AgentLatch.exe
.\tests\install.tests.ps1 -AgentLatchPath .\build-x64\Release\AgentLatch.exe
```

Installer changes should also be compiled with `build-installer.ps1 -TestBuild` and exercised with `tests\installer.tests.ps1`. That test refuses to run against the production AppId.

The native self-test includes Codex desktop-helper ownership (including intermediary hosts and paths outside the package), standalone CLI preservation, exact lifecycle counts, grace countdown/expiry, resumed work, and dashboard status labels. Keep these regressions covered when changing process detection or release timing.

It also covers fixed restart deadlines, repeated missing scans, provider return, independent providers, Off/mode changes, idle Tasks-mode disarming, SessionEnd/process-removal ordering, manual duration limits, indefinite sessions, original-end-time restoration, and expiry. Native UI contracts create hidden real controls at 96/120/144/168/192 DPI, checking page visibility, explicit mode mapping, five-row capacity, pagination and page preservation, switch checked states, control bounds, tab fit, and absence of horizontal/vertical list scrollbars. Custom-painted controls are tested independently against white/magenta backgrounds so a pre-painted parent cannot hide missed corners. Buttons include disabled/pressed/focused drawing states.

`AgentLatch.exe --self-test --test-render-dir <absolute-directory>` adds 90 PNG render fixtures for timed, indefinite, idle, error, long-list, and mixed four-source states across the five DPI scales. Review representative renders visually as well as running structural tests; a passing bounds check does not establish good visual hierarchy or usability. Fixtures use test state and must not be presented as live-provider evidence. Documentation images instead come from the running app's state through `scripts/capture-dashboard.ps1`, without desktop capture or input/focus changes.

Settings regressions use a temporary registry subtree and process-local HKCU redirection, not the installed app's configuration. They verify actual volatile-key creation, same-session app reload, modeled Windows session-storage removal for both timed/unlimited overrides, original-deadline preservation, legacy/malformed value rejection, explicit Stop, and retention of ordinary preferences. These tests never reboot Windows; they test the storage boundary and the documented volatile-key contract.

`tests/manual-process.tests.ps1` additionally tests the full background app on a disposable GitHub-hosted runner. It refuses an existing app process or settings key, creates its own temporary preferences, and checks timed/unlimited CLI choices, process restart restoration and Stop before cleanup. It never activates a window. Control utilities may auto-start a background daemon, so wait on the command's own process with a timeout; PowerShell `Start-Process -Wait` can otherwise wait for the daemon's whole descendant tree. The plain native `--self-test` process does not launch such a daemon.

For documentation, run `scripts/capture-dashboard.ps1 -AgentLatchPath <installed-exe> -OutputDirectory <directory>`. It asks the running app to export its current state as three full-resolution PNGs without any desktop control, focus change, visible tab switch, or desktop screenshot. Review labels/settings before publishing; keep live-state exports distinct from synthetic test fixtures. See [Interface guide](docs/INTERFACE.md).

## Pull requests

- Keep AgentLatch native, local-only, and usable without administrator rights.
- Preserve existing provider configuration when changing integration scripts.
- Add a bounded timeout to every automatic acquisition path.
- Avoid new runtime dependencies unless they materially improve reliability and remain appropriate for a tray utility.
- Keep UI Automation work off the message-loop thread, restricted to explicitly targeted processes and controls, and fail closed when metadata cannot be classified.
- Do not inspect or retain prompts, responses, conversation messages, terminal output, clipboard data, or source content.
- Update documentation when behavior or command-line options change.
- Explain how the change was tested.

Provider integrations should use documented lifecycle surfaces where available and retain process detection as a fallback.

## Style

C++ is formatted for readability with four-space indentation. Prefer small ownership boundaries, RAII, explicit limits at IPC/parsing boundaries, and ordinary Win32 APIs over hidden background infrastructure.

## Reporting security problems

Do not open a public issue for a suspected vulnerability. Follow [SECURITY.md](SECURITY.md).
