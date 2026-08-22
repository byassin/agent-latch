# Privacy

AgentLatch is local-only software.

It does not include analytics, advertising, telemetry, crash upload, an account system, a network server, or an auto-updater. It does not interpret, store, display, or transmit prompts, responses, source files, terminal output, clipboard data, browser content, or credentials.

## Data AgentLatch observes

- process IDs, executable names, parent-child relationships, CPU time, and I/O counters for agent detection;
- bounded backward-read blocks from recent local Codex session JSONL files, searched only for the latest `task_started` or `task_complete` marker (AgentLatch does not parse or retain other record content);
- read-only Windows UI Automation metadata for the packaged unified OpenAI app's primary composer command: control type, class name, enabled/offscreen state, accessibility command name, nearest Document name, and owning process ID;
- provider lifecycle JSON sent directly to the hook command, from which it uses event name, session/subagent/task identity, Claude background-task count, and the leaf workspace folder name; and
- settings selected in the dashboard.

Codex session blocks, OpenAI composer metadata, and hook input are processed in memory and discarded. AgentLatch counts Claude's in-flight `background_tasks` array but ignores task subjects, descriptions, shell commands, transcript paths, assistant messages, and other task content. For English UI, the command name is expected to be Send or Stop; for other locales, the learned disabled-idle command name remains in memory only. AgentLatch does not enumerate or read conversation-message or prompt-edit controls. The dashboard displays only a short provider label and workspace leaf name. Active latches are memory-only.

## Data AgentLatch stores

Settings and integration-health markers are stored in `HKCU\Software\AgentLatch`. If enabled, Windows startup stores the executable path in the current user's Run key. The integration installer writes AgentLatch commands into provider-owned JSON files and creates timestamped backup copies beside changed files. Those backups preserve the provider configuration as it existed before AgentLatch changed it and remain under the user's profile until the user removes them.

AgentLatch keeps a bounded local diagnostic history at `%LOCALAPPDATA%\AgentLatch\diagnostics.log`, with one rotated `diagnostics.previous.log`. Each file is limited to 512 KiB. Records contain UTC timestamps, AgentLatch version and process ID, active latch counts and provider names, power-request desired/accepted state and Windows error codes, and watchdog restart outcomes. They do not contain latch IDs, workspace paths, prompts, responses, terminal output, source content, or conversation data. The normal uninstaller removes both logs.

No AgentLatch data is sent off the computer.
