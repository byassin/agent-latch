[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$InstallDirectory = (Join-Path $env:LOCALAPPDATA 'AgentLatch'),
    [switch]$StartWithWindows,
    [switch]$SkipHooks,
    [switch]$NoStop,
    [switch]$NoLaunch,
    [string]$ConfigRoot = ([Environment]::GetFolderPath('UserProfile'))
)

$ErrorActionPreference = 'Stop'
$sourceRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sourceExecutable = Join-Path $sourceRoot 'AgentLatch.exe'
if (-not (Test-Path -LiteralPath $sourceExecutable)) {
    $sourceExecutable = Join-Path $PSScriptRoot 'AgentLatch.exe'
}
if (-not (Test-Path -LiteralPath $sourceExecutable)) {
    throw 'AgentLatch.exe must be beside install.ps1 or in the release root.'
}

$InstallDirectory = [System.IO.Path]::GetFullPath($InstallDirectory)
if ($PSCmdlet.ShouldProcess($InstallDirectory, 'Install AgentLatch')) {
    if (-not $NoStop) {
        $quitProcess = Start-Process -FilePath $sourceExecutable -ArgumentList '--quit' -Wait -PassThru
        if ($quitProcess.ExitCode -ne 0) {
            throw "Could not stop the current AgentLatch instance (exit code $($quitProcess.ExitCode))."
        }
        Start-Sleep -Milliseconds 300
    }

    New-Item -ItemType Directory -Path $InstallDirectory -Force | Out-Null
    $installedExecutable = Join-Path $InstallDirectory 'AgentLatch.exe'
    if (-not [string]::Equals(
            [System.IO.Path]::GetFullPath($sourceExecutable),
            [System.IO.Path]::GetFullPath($installedExecutable),
            [StringComparison]::OrdinalIgnoreCase)) {
        for ($attempt = 0; $attempt -lt 20; $attempt++) {
            try {
                Copy-Item -LiteralPath $sourceExecutable -Destination $installedExecutable -Force -ErrorAction Stop
                break
            } catch {
                if ($attempt -eq 19) { throw }
                Start-Sleep -Milliseconds 100
            }
        }
    }
    foreach ($scriptName in @('install-integrations.ps1', 'uninstall.ps1')) {
        $candidate = Join-Path $PSScriptRoot $scriptName
        if (Test-Path -LiteralPath $candidate) {
            Copy-Item -LiteralPath $candidate -Destination (Join-Path $InstallDirectory $scriptName) -Force
        }
    }
    if ($StartWithWindows) {
        $runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
        New-Item -Path $runKey -Force | Out-Null
        New-ItemProperty -Path $runKey -Name 'AgentLatch' -Value ('"{0}" --background' -f $installedExecutable) -PropertyType String -Force | Out-Null
    }
    if (-not $SkipHooks) {
        $integrationInstaller = Join-Path $InstallDirectory 'install-integrations.ps1'
        if (-not (Test-Path -LiteralPath $integrationInstaller -PathType Leaf)) {
            throw 'install-integrations.ps1 is required unless -SkipHooks is specified.'
        }
        & $integrationInstaller -AgentLatchPath $installedExecutable -ConfigRoot $ConfigRoot
    }
    if (-not $NoLaunch) {
        Start-Process -FilePath $installedExecutable
    }
    Write-Host "AgentLatch installed to $InstallDirectory"
    if ($SkipHooks) {
        Write-Host 'Agent integrations were skipped by request.'
    } else {
        Write-Host 'Agent integrations were installed for Codex, Claude Code, Cursor, and Google Antigravity.'
        Write-Host 'Codex desktop task lifecycle detection works automatically; no chat command or manual hook review is required.'
    }
}
