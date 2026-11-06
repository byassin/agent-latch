[CmdletBinding()]
param([Parameter(Mandatory)][string]$InstallerPath)
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted') {
    throw 'This production-mode upgrade test runs only on a disposable GitHub-hosted runner.'
}
$InstallerPath = (Resolve-Path -LiteralPath $InstallerPath).Path
if (Test-Path -LiteralPath ($InstallerPath + '.test-app-id')) {
    throw 'Use the production setup package: an isolated AppId cannot exercise production upgrade detection.'
}
$settingsPath = 'HKCU:\Software\AgentLatch'
$uninstallPath = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{BBC37307-15F1-4F00-8936-60BC06B5FAB5}_is1'
$runPath = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
if ((Test-Path -LiteralPath $settingsPath) -or (Test-Path -LiteralPath $uninstallPath) -or
    (Get-ItemProperty -LiteralPath $runPath -Name AgentLatch -ErrorAction SilentlyContinue) -or
    (Get-Process -Name AgentLatch -ErrorAction SilentlyContinue)) {
    throw 'Refusing to modify an existing AgentLatch profile, installation, startup entry or process.'
}
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class AgentLatchUpgradeWindows {
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern IntPtr FindWindowW(string className, string title);
 public static IntPtr FindMainWindow() { return FindWindowW("AgentLatch.ControlWindow.v1", null); }
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
}
'@
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('AgentLatchUpgradeTests-' + [guid]::NewGuid().ToString('N'))
$installDirectory = Join-Path $testRoot 'installed'
$sideDirectory = Join-Path $testRoot 'side-by-side'
$installedExecutable = Join-Path $installDirectory 'AgentLatch.exe'
$ownedProfile = $false
[IO.Directory]::CreateDirectory($testRoot) | Out-Null
function Get-AppCopies {
    @(Get-CimInstance Win32_Process -Filter "Name='AgentLatch.exe'")
}
function Get-MainId {
    $window = [AgentLatchUpgradeWindows]::FindMainWindow()
    if ($window -eq [IntPtr]::Zero) { return [uint32]0 }
    [uint32]$mainId = 0
    $null = [AgentLatchUpgradeWindows]::GetWindowThreadProcessId($window, [ref]$mainId)
    return $mainId
}
function Assert-HiddenMain {
    $window = [AgentLatchUpgradeWindows]::FindMainWindow()
    if ($window -eq [IntPtr]::Zero -or [AgentLatchUpgradeWindows]::IsWindowVisible($window)) {
        throw 'Expected background-only monitoring without a visible dashboard.'
    }
}
function Wait-Main {
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        $mainId = Get-MainId
        if ($mainId -ne 0) { Assert-HiddenMain; return $mainId }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Setup did not restore the background monitoring process.'
}
function Invoke-Control([string]$arguments) {
    $command = Start-Process -FilePath $installedExecutable -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $null = $command.Handle
    if (-not $command.WaitForExit(30000)) { throw "Control command timed out: $arguments" }
    $command.Refresh()
    if ($command.ExitCode -ne 0) { throw "Control command failed: $arguments ($($command.ExitCode))" }
}
function Stop-App {
    if (Get-MainId) { Invoke-Control '--quit' }
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        if ((Get-AppCopies).Count -eq 0) { return }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Owned test monitoring processes did not stop.'
}
function Invoke-Setup([string]$label, [string]$directory, [string[]]$extra = @()) {
    $setupLog = Join-Path $testRoot ($label + '.log')
    $arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/NOCANCEL', '/NOICONS', '/SKIPHOOKS', '/TASKS=""',
        ('/DIR="' + $directory + '"'), ('/LOG="' + $setupLog + '"')) + $extra
    $setup = Start-Process -FilePath $InstallerPath -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $null = $setup.Handle
    if (-not $setup.WaitForExit(60000)) { throw "Setup timed out: $label" }
    $setup.Refresh()
    if ($setup.ExitCode -ne 0) { throw "Setup failed: $label ($($setup.ExitCode)); see $setupLog" }
    return $setupLog
}
function Assert-NoApp {
    if ((Get-AppCopies).Count -ne 0 -or (Get-MainId) -ne 0) { throw 'Setup unexpectedly launched monitoring.' }
}
function Read-Manual {
    [uint64](Get-ItemPropertyValue -LiteralPath ($settingsPath + '\ManualSession') -Name UntilUtc -ErrorAction SilentlyContinue)
}
try {
    $ownedProfile = $true
    $null = Invoke-Setup 'cold-silent' $installDirectory
    Assert-NoApp
    New-Item -Path $settingsPath -Force | Out-Null
    foreach ($name in @('ModeCodex', 'ModeClaude', 'ModeCursor', 'ModeOpenCode', 'ModeGemini', 'Notifications', 'KeepDisplayOn')) {
        New-ItemProperty -LiteralPath $settingsPath -Name $name -Value 0 -PropertyType DWord -Force | Out-Null
    }
    New-ItemProperty -LiteralPath $settingsPath -Name Appearance -Value 2 -PropertyType DWord -Force | Out-Null
    Invoke-Control '--keep-awake --indefinite'
    $oldMain = Wait-Main
    if ((Read-Manual) -ne 1) { throw 'Could not prepare the test manual override.' }
    $before = Get-ItemProperty -LiteralPath $settingsPath | Select-Object ModeCodex, ModeClaude, ModeCursor, ModeOpenCode, ModeGemini, Notifications, KeepDisplayOn, Appearance | ConvertTo-Json -Compress
    $upgradeLog = Invoke-Setup 'running-silent-upgrade' $installDirectory
    $newMain = Wait-Main
    if ($newMain -eq $oldMain) { throw 'Upgrade did not replace the original process.' }
    $capture = Join-Path $testRoot 'resumed-render'
    Invoke-Control ('--export-ui-screenshots "' + $capture + '"')
    if ((Read-Manual) -ne 1) { throw 'Silent upgrade changed the active manual override.' }
    $resumedStatusPath = Join-Path $testRoot 'resumed-status.json'
    Invoke-Control ('--export-status "' + $resumedStatusPath + '"')
    $resumedStatus = Get-Content -LiteralPath $resumedStatusPath -Raw | ConvertFrom-Json
    if (-not $resumedStatus.manual.active -or -not $resumedStatus.power.system_request_set) {
        throw 'Silent upgrade resumed monitoring without restoring the active manual sleep-protection request.'
    }
    $after = Get-ItemProperty -LiteralPath $settingsPath | Select-Object ModeCodex, ModeClaude, ModeCursor, ModeOpenCode, ModeGemini, Notifications, KeepDisplayOn, Appearance | ConvertTo-Json -Compress
    if ($before -ne $after) { throw 'Silent upgrade changed ordinary preferences.' }
    if (-not (Select-String -LiteralPath $upgradeLog -SimpleMatch 'Resumed AgentLatch in background after a successful silent upgrade.')) {
        throw 'The successful silent-upgrade resume branch was not exercised.'
    }
    Stop-App
    $null = Invoke-Setup 'stopped-silent-upgrade' $installDirectory
    Assert-NoApp
    Start-Process -FilePath $installedExecutable -ArgumentList '--background' -WindowStyle Hidden
    $null = Wait-Main
    $testLog = Invoke-Setup 'test-mode-upgrade' $installDirectory @('/TESTMODE')
    Assert-NoApp
    if (Select-String -LiteralPath $testLog -SimpleMatch 'Resumed AgentLatch in background after a successful silent upgrade.') {
        throw 'Test-mode setup armed background recovery.'
    }
    Start-Process -FilePath $installedExecutable -ArgumentList '--background' -WindowStyle Hidden
    $mainBeforeNoStop = Wait-Main
    # A different target avoids locking/replacing the active executable; /NOSTOP
    # must neither terminate it nor start a new installed copy.
    $noStopLog = Invoke-Setup 'no-stop-side-by-side' $sideDirectory @('/NOSTOP')
    if ((Get-MainId) -ne $mainBeforeNoStop) { throw 'No-stop setup replaced the active process.' }
    Assert-HiddenMain
    if (Select-String -LiteralPath $noStopLog -SimpleMatch 'Resumed AgentLatch in background after a successful silent upgrade.') {
        throw 'No-stop setup attempted background recovery.'
    }
    Stop-App
    Write-Host 'Silent installer upgrade tests passed (cold/stopped stay stopped; running resumes hidden; manual/system power request/preferences preserved; test/no-stop opt-outs).'
} catch {
    # Keep only bounded, test-owned evidence outside the directory cleaned below.
    # Hosted CI uploads this directory on failure; never copy installed binaries.
    $testFailure = $_
    try {
        $failureDirectory = Join-Path $env:RUNNER_TEMP 'AgentLatch-upgrade-failure'
        New-Item -ItemType Directory -Path $failureDirectory -Force | Out-Null
        Get-ChildItem -LiteralPath $testRoot -File | Where-Object { $_.Extension -in @('.log', '.json') } |
            Copy-Item -Destination $failureDirectory -Force
        $testFailure | Out-String | Set-Content -LiteralPath (Join-Path $failureDirectory 'failure.txt') -Encoding utf8
        Write-Host "Preserved installer-upgrade failure evidence: $failureDirectory"
    } catch {
        Write-Warning "Could not preserve installer-upgrade failure evidence: $_"
    }
    throw $testFailure
} finally {
    if ($ownedProfile) {
        if ((Get-AppCopies).Count -ne 0) { Stop-App }
        foreach ($directory in @($sideDirectory, $installDirectory)) {
            $uninstaller = Get-ChildItem -LiteralPath $directory -Filter 'unins*.exe' -File -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($null -ne $uninstaller) {
                $remove = Start-Process -FilePath $uninstaller.FullName -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/NOSTOP') -WindowStyle Hidden -PassThru
                $null = $remove.Handle
                if (-not $remove.WaitForExit(60000)) { throw 'Owned setup uninstaller timed out.' }
                for ($attempt = 0; $attempt -lt 50 -and (Test-Path -LiteralPath $uninstaller.FullName); $attempt++) {
                    Start-Sleep -Milliseconds 100
                }
            }
        }
        if (Test-Path -LiteralPath $settingsPath) { Remove-Item -LiteralPath $settingsPath -Recurse -Force }
        if (Test-Path -LiteralPath $uninstallPath) { Remove-Item -LiteralPath $uninstallPath -Recurse -Force }
        # Get-ItemPropertyValue can throw for an absent value even with
        # SilentlyContinue on some PowerShell hosts. Absence is normal here.
        $startup = (Get-ItemProperty -LiteralPath $runPath -ErrorAction SilentlyContinue).AgentLatch
        if ($startup -and $startup.Contains($testRoot)) { Remove-ItemProperty -LiteralPath $runPath -Name AgentLatch }
    }
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    $resolvedTestRoot = [IO.Path]::GetFullPath($testRoot)
    if (-not $resolvedTestRoot.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or $resolvedTestRoot -eq $tempRoot.TrimEnd('\')) {
        throw 'Refusing to clean a path outside the test temporary directory.'
    }
    if (Test-Path -LiteralPath $resolvedTestRoot) { Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force }
}
