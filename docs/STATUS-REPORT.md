# Local status reports — 0.6.0

Settings has **Save status report…**, using a native Save dialog. Choose a `.json` filename in an existing writable folder. The GUI asks before replacing an existing file. A sibling temporary file and final rename prevent a failed write from truncating the previous report. Cancel does nothing; no file is uploaded or opened automatically.

For scripts:

```powershell
AgentLatch.exe --export-status C:\Reports\AgentLatch-status.json
```

The folder must exist. Use an ordinary absolute filesystem path ending in `.json`; device paths, alternate streams and relative paths are rejected. CLI export refuses to overwrite an existing report and does not auto-start monitoring, acquire a latch or change preferences. Exit codes: 0 saved; 2 invalid input; 4 app not running; 5 file/delivery failure. A timeout can occur while the main process is busy; check the chosen destination before retrying.

## Schema 1

- `schema_version`, `app_version`, `captured_utc_ms`: format/version and UTC capture time.
- `protection_requested`: whether any current latch is present.
- `idle_sleep_request_confirmed`: requested protection plus an available handle, set system request and no reported system error.
- `power`: actual handle availability, system/display request flags and native error codes. A request may remain set if Windows failed to clear it; do not infer actual flags from the requested setting alone.
- `preferences`: display behavior, sign-in startup, notifications and activity release delay.
- `manual`: active state, until-Windows-restart choice and rounded-up remaining seconds for a timed session. Zero remaining seconds is not a duration estimate for agents.
- `providers`: fixed provider keys, configured mode, aggregate source/instance/grace counts. Codex/Claude additionally expose whether hooks were configured and whether a hook has ever been received. A historical hook marker is not proof of current end-to-end provider health.
- Provider totals group all matching leases; the separate `manual` object describes only the built-in override, not custom external leases tagged with the manual source key.
- `limitations`: explicit reminder that this is a snapshot, not effective Windows policy or overnight qualification.

The report never serializes latch labels/details/IDs, owner PIDs, paths, usernames, prompts, responses, transcripts or raw diagnostic history. It still reveals app usage counts and preferences; review it before sharing. Export logging records only success/failure.

Native tests exercise schema, redaction, counts, timers, failure state and atomic replacement. PowerShell parses the generated fixture as JSON. Hosted-only tests additionally exercise live IPC without changing the running manual session and verify no-overwrite/no-auto-start behavior.
