# Privacy

AgentLatch is local-only software.

It does not include analytics, advertising, telemetry, crash upload, an account system, a network server, or an auto-updater. It does not interpret, store, display, or transmit prompts, responses, source files, terminal output, clipboard data, browser content, or credentials.

## Data AgentLatch observes

- process IDs, executable names, parent-child relationships, CPU time, and I/O counters for agent detection;
- bounded backward-read blocks from recent local Codex session JSONL files, searched only for the latest `task_started` or `task_complete` marker (AgentLatch does not parse or retain other record content);
- provider lifecycle JSON sent directly to the hook command, from which it uses event name, session/subagent identity, and the leaf workspace folder name; and
- settings selected in the dashboard.

Codex session blocks and hook input are processed in memory and discarded. The dashboard displays only a short provider label and workspace leaf name. Active latches are memory-only.

## Data AgentLatch stores

Settings are stored in `HKCU\Software\AgentLatch`. If enabled, Windows startup stores the executable path in the current user's Run key. The integration installer writes AgentLatch commands into provider-owned JSON files and creates timestamped backup copies beside changed files.

No AgentLatch data is sent off the computer.
