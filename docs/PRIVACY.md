# Privacy

AgentLatch is local-only software.

It does not include analytics, advertising, telemetry, crash upload, an account system, a network server, or an auto-updater. It does not read prompts, responses, source files, terminal output, clipboard data, browser content, or credentials.

## Data AgentLatch observes

- process IDs, executable names, parent-child relationships, CPU time, and I/O counters for agent detection;
- provider lifecycle JSON sent directly to the hook command, from which it uses event name, session/subagent identity, and the leaf workspace folder name; and
- settings selected in the dashboard.

Hook input is read from standard input, translated in memory, and discarded. The dashboard displays only a short provider label and workspace leaf name. Active latches are memory-only.

## Data AgentLatch stores

Settings are stored in `HKCU\Software\AgentLatch`. If enabled, Windows startup stores the executable path in the current user's Run key. The integration installer writes AgentLatch commands into provider-owned JSON files and creates timestamped backup copies beside changed files.

No AgentLatch data is sent off the computer.
