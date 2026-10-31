[CmdletBinding()]
param([Parameter(Mandatory)][string]$AgentLatchPath)
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'This full-app registry test runs only on a disposable GitHub-hosted runner.'
}
$AgentLatchPath = (Resolve-Path -LiteralPath $AgentLatchPath).Path
$settingsPath = 'HKCU:\Software\AgentLatch'
if (Test-Path -LiteralPath $settingsPath) { throw 'Refusing to change existing AgentLatch preferences.' }
if (Get-Process -Name AgentLatch -ErrorAction SilentlyContinue) { throw 'Refusing to test alongside an existing AgentLatch process.' }
$created = $false
function Invoke-Control([string]$arguments) {
    $command = Start-Process -FilePath $AgentLatchPath -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
    if ($command.ExitCode -ne 0) { throw "App command failed: $arguments ($($command.ExitCode))" }
}
function Read-Manual {
    $value = Get-ItemProperty -LiteralPath ($settingsPath + '\ManualSession') -ErrorAction SilentlyContinue
    if ($null -eq $value -or $null -eq $value.UntilUtc) { return [uint64]0 }
    return [uint64]$value.UntilUtc
}
function Stop-App {
    $copies = Get-CimInstance Win32_Process -Filter "Name='AgentLatch.exe'" | Where-Object { $_.ExecutablePath -eq $AgentLatchPath }
    Invoke-Control '--quit'
    foreach ($copy in $copies) { Wait-Process -Id $copy.ProcessId -Timeout 15 -ErrorAction SilentlyContinue }
}
try {
    New-Item -Path $settingsPath -Force | Out-Null
    $created = $true
    New-ItemProperty -LiteralPath $settingsPath -Name Notifications -Value 0 -PropertyType DWord | Out-Null
    foreach ($arguments in @('--keep-awake --hours 1', '--keep-awake --indefinite')) {
        Invoke-Control $arguments
        $expected = Read-Manual
        if ($expected -eq 0) { throw 'Full app did not persist the manual override.' }
        Stop-App
        Start-Process -FilePath $AgentLatchPath -ArgumentList '--background' -WindowStyle Hidden
        # A synchronous capture waits for initialization without activating a window.
        $renderPath = Join-Path $env:RUNNER_TEMP 'AgentLatchManualProcessRenders'
        Invoke-Control ('--export-ui-screenshots "' + $renderPath + '"')
        if ((Read-Manual) -ne $expected) { throw 'App-only restart changed the original manual state.' }
        Invoke-Control '--stop-keep-awake'
        if ((Read-Manual) -ne 0) { throw 'Full app did not persist Stop.' }
        Stop-App
    }
    Write-Host 'Full-app manual persistence tests passed (timed, unlimited, app restart, Stop).'
} finally {
    if (Get-Process -Name AgentLatch -ErrorAction SilentlyContinue) { Stop-App }
    if ($created) { Remove-Item -LiteralPath $settingsPath -Recurse -Force }
}
