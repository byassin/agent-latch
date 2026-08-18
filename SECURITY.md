# Security policy

## Supported versions

Security fixes are applied to the latest released version.

## Reporting a vulnerability

Please use the repository's **Security → Report a vulnerability** flow to send a private GitHub security advisory. Include the affected version, reproduction steps, impact, and any suggested remediation.

Please do not disclose a vulnerability in a public issue before a fix is available. Maintainers will acknowledge a complete report as soon as practical and coordinate validation, remediation, and disclosure with the reporter.

## Security model

AgentLatch is a per-user desktop process. It opens no network port and does not run elevated. Its local IPC assumes processes running as the same signed-in user share that user's trust boundary. Hook and lease payloads are bounded and sanitized, but they are not an authorization boundary between same-user processes.

Codex lifecycle files, provider hook JSON, process metadata, and Windows UI Automation metadata are treated as untrusted detector input. Reads and payloads are bounded, unknown states fail closed, and the OpenAI accessibility probe is restricted to processes in the expected packaged app. The probe is read-only and never sends input to the OpenAI app.
