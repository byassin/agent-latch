[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [string]$InstallDirectory = (Join-Path $env:LOCALAPPDATA 'AgentLatch'),
    [switch]$KeepHooks,
    [Parameter(DontShow)]
    [switch]$RemoveHooks,
    [switch]$RemoveSettings,
    [string]$ConfigRoot = ([Environment]::GetFolderPath('UserProfile')),
    [Parameter(DontShow)]
    [switch]$NoStop,
    [Parameter(DontShow)]
    [string]$StartupRegistryPath = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
)

$ErrorActionPreference = 'Stop'
if ($KeepHooks -and $RemoveHooks) {
    throw '-KeepHooks and the legacy -RemoveHooks switch cannot be used together.'
}
$InstallDirectory = [System.IO.Path]::GetFullPath($InstallDirectory)
$executable = Join-Path $InstallDirectory 'AgentLatch.exe'

if ($PSCmdlet.ShouldProcess($InstallDirectory, 'Uninstall AgentLatch')) {
    if (Test-Path -LiteralPath $executable) {
        if (-not $KeepHooks) {
            $hookScript = Join-Path $InstallDirectory 'install-integrations.ps1'
            if (Test-Path -LiteralPath $hookScript) {
                & $hookScript -AgentLatchPath $executable -ConfigRoot $ConfigRoot -Uninstall
            }
        }
        if (-not $NoStop) {
            & $executable --quit
            Start-Sleep -Milliseconds 250
        }
    }

    $expectedStartupCommand = '"{0}" --background' -f $executable
    $currentStartupCommand = Get-ItemPropertyValue `
        -Path $StartupRegistryPath `
        -Name 'AgentLatch' `
        -ErrorAction SilentlyContinue
    if ([string]::Equals(
            [string]$currentStartupCommand,
            $expectedStartupCommand,
            [StringComparison]::OrdinalIgnoreCase)) {
        Remove-ItemProperty -Path $StartupRegistryPath -Name 'AgentLatch' -ErrorAction SilentlyContinue
    }
    if ($RemoveSettings) {
        Remove-Item -Path 'HKCU:\Software\AgentLatch' -Recurse -Force -ErrorAction SilentlyContinue
    }
    if (Test-Path -LiteralPath $InstallDirectory) {
        Remove-Item -LiteralPath $InstallDirectory -Recurse -Force
    }
    Write-Host 'AgentLatch was uninstalled.'
    if ($KeepHooks) {
        Write-Host 'Agent integrations were kept by request.'
    } else {
        Write-Host 'AgentLatch entries were removed from supported agent configurations.'
    }
}
