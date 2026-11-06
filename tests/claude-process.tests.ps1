[CmdletBinding()]
param([Parameter(Mandatory)][string]$AgentLatchPath)
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'This background-app hook test runs only on a disposable GitHub-hosted runner.'
}
$AgentLatchPath = (Resolve-Path -LiteralPath $AgentLatchPath).Path
$settingsPath = 'HKCU:\Software\AgentLatch'
if (Test-Path -LiteralPath $settingsPath) { throw 'Refusing to change existing AgentLatch preferences.' }
if (Get-Process -Name AgentLatch -ErrorAction SilentlyContinue) { throw 'Refusing to test alongside an existing AgentLatch process.' }
$created = $false
$log = Join-Path $env:LOCALAPPDATA 'AgentLatch\diagnostics.log'
function Invoke-Control([string]$arguments) {
    $command = Start-Process -FilePath $AgentLatchPath -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $null = $command.Handle
    if (-not $command.WaitForExit(30000)) { throw "Command timed out: $arguments" }
    $command.Refresh()
    if ($command.ExitCode -ne 0) { throw "Command failed: $arguments ($($command.ExitCode))" }
}
function Send-Hook([hashtable]$payload) {
    $info = [System.Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $AgentLatchPath
    $info.Arguments = '--hook claude'
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $hook = [System.Diagnostics.Process]::Start($info)
    $hook.StandardInput.Write(($payload | ConvertTo-Json -Depth 8 -Compress))
    $hook.StandardInput.Close()
    if (-not $hook.WaitForExit(10000)) { throw 'Hook exceeded its execution deadline.' }
    if ($hook.ExitCode -ne 0 -or $hook.StandardOutput.ReadToEnd().Trim() -ne '{}') {
        throw "Claude hook failed: $($hook.StandardError.ReadToEnd())"
    }
    $hook.Dispose()
}
function Assert-Idle {
    $deadline = [DateTime]::UtcNow.AddSeconds(12)
    do {
        $state = @(Get-Content -LiteralPath $log | Where-Object { $_ -match '\tlatch_state\t' } | Select-Object -Last 1)
        if ($state.Count -eq 1 -and $state[0] -match '\tactive=0 instances=0$') {
            $power = Get-Content -LiteralPath $log | Where-Object { $_ -match '\tpower_request_state\t' } | Select-Object -Last 1
            if ($power -match 'active=0 system=0 display=0') { return }
        }
        Start-Sleep -Milliseconds 300
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Completed Claude session did not release its final latch within settling grace.'
}
try {
    New-Item -Path $settingsPath -Force | Out-Null
    $created = $true
    foreach ($name in @('Notifications', 'ModeCodex', 'ModeCursor', 'ModeOpenCode', 'ModeGemini')) {
        New-ItemProperty -LiteralPath $settingsPath -Name $name -Value 0 -PropertyType DWord | Out-Null
    }
    New-ItemProperty -LiteralPath $settingsPath -Name ModeClaude -Value 1 -PropertyType DWord | Out-Null
    New-ItemProperty -LiteralPath $settingsPath -Name IntegrationExpectedClaude -Value 1 -PropertyType DWord | Out-Null
    Start-Process -FilePath $AgentLatchPath -ArgumentList '--background' -WindowStyle Hidden
    Invoke-Control ('--export-ui-screenshots "' + (Join-Path $env:RUNNER_TEMP 'AgentLatchClaudeProcessRenders') + '"')
    Send-Hook @{hook_event_name='UserPromptSubmit';session_id='ci-one'}
    Send-Hook @{hook_event_name='SubagentStart';session_id='ci-one';agent_id='worker';agent_type='Explore'}
    Send-Hook @{hook_event_name='Stop';session_id='ci-one';background_tasks=@(@{id='worker';type='subagent';status='running'})}
    Send-Hook @{hook_event_name='SubagentStop';session_id='ci-one';agent_id='worker';background_tasks=@()}
    Assert-Idle
    Send-Hook @{hook_event_name='UserPromptSubmit';session_id='ci-two'}
    Send-Hook @{hook_event_name='SubagentStart';session_id='ci-two';agent_id='a'}
    Send-Hook @{hook_event_name='SubagentStart';session_id='ci-two';agent_id='b'}
    Send-Hook @{hook_event_name='SessionEnd';session_id='ci-two'}
    Assert-Idle
    Send-Hook @{hook_event_name='TaskCreated';session_id='ci-plan';task_id='todo'}
    Assert-Idle
    Send-Hook @{hook_event_name='PreToolUse';session_id='ci-worker';agent_id='worker'}
    Send-Hook @{hook_event_name='SubagentStop';session_id='ci-worker';agent_id='worker'}
    Send-Hook @{hook_event_name='PostToolUse';session_id='ci-worker';agent_id='worker'}
    Assert-Idle
    Send-Hook @{hook_event_name='UserPromptSubmit';session_id='ci-idle'}
    Send-Hook @{hook_event_name='Notification';session_id='ci-idle';notification_type='idle_prompt'}
    Assert-Idle
    Write-Host 'Background-app Claude hook tests passed: stdin/stdout, IPC, workers, snapshots, session cleanup, idle, planning, late events and final power release.'
} finally {
    if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log -Tail 25 | Write-Host }
    $copies = @(Get-CimInstance Win32_Process -Filter "Name='AgentLatch.exe'" | Where-Object { $_.ExecutablePath -eq $AgentLatchPath })
    if ($copies.Count -gt 0) {
        Invoke-Control '--quit'
        foreach ($copy in $copies) { Wait-Process -Id $copy.ProcessId -Timeout 15 -ErrorAction SilentlyContinue }
    }
    if ($created) { Remove-Item -LiteralPath $settingsPath -Recurse -Force }
}
