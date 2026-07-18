# Contributing to AgentLatch

Thanks for helping make unattended agent work safer and more dependable on Windows.

## Development setup

Install Visual Studio 2022 Build Tools with Desktop development with C++, plus CMake 3.24 or newer. Install Inno Setup 7 when changing or verifying the Windows installer. Then run:

```powershell
.\scripts\build.ps1
```

The project treats compiler warnings as errors. The x64 build must pass `AgentLatch.exe --self-test`; ARM64 must compile successfully.

## Pull requests

- Keep AgentLatch native, local-only, and usable without administrator rights.
- Preserve existing provider configuration when changing integration scripts.
- Add a bounded timeout to every automatic acquisition path.
- Avoid new runtime dependencies unless they materially improve reliability and remain appropriate for a tray utility.
- Update documentation when behavior or command-line options change.
- Explain how the change was tested.

Provider integrations should use documented lifecycle surfaces where available and retain process detection as a fallback.

## Style

C++ is formatted for readability with four-space indentation. Prefer small ownership boundaries, RAII, explicit limits at IPC/parsing boundaries, and ordinary Win32 APIs over hidden background infrastructure.

## Reporting security problems

Do not open a public issue for a suspected vulnerability. Follow [SECURITY.md](SECURITY.md).
