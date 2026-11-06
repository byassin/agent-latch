param([Parameter(Mandatory)][string]$ReportPath)
$ErrorActionPreference='Stop'
$raw=Get-Content -LiteralPath $ReportPath -Raw
$report=$raw | ConvertFrom-Json
if($report.schema_version -ne 1 -or $report.captured_utc_ms -ne 100000){throw 'Wrong status schema or fixture'}
if($report.manual.active -ne $true -or $report.manual.until_windows_restarts -ne $true){throw 'Manual state missing'}
if($report.idle_sleep_request_confirmed -ne $true -or $report.power.system_request_set -ne $true){throw 'Power state missing'}
$codex=@($report.providers | Where-Object key -eq codex)
if($codex.Count -ne 1 -or $codex[0].instances -ne 3 -or $codex[0].grace_sources -ne 1){throw 'Provider counts wrong'}
if($raw -match 'SECRET|prompt|workspace|session_id|owner_pid'){throw 'Private fixture content in report'}
'Status report JSON/schema/privacy tests passed.'
