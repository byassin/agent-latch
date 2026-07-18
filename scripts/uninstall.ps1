[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [string]$InstallDirectory = (Join-Path $env:LOCALAPPDATA 'AgentLatch'),
    [switch]$RemoveHooks,
    [switch]$RemoveSettings
)

$ErrorActionPreference = 'Stop'
$InstallDirectory = [System.IO.Path]::GetFullPath($InstallDirectory)
$executable = Join-Path $InstallDirectory 'AgentLatch.exe'

if ($PSCmdlet.ShouldProcess($InstallDirectory, 'Uninstall AgentLatch')) {
    if (Test-Path -LiteralPath $executable) {
        if ($RemoveHooks) {
            $hookScript = Join-Path $InstallDirectory 'install-integrations.ps1'
            if (Test-Path -LiteralPath $hookScript) {
                & $hookScript -AgentLatchPath $executable -Uninstall
            }
        }
        & $executable --quit
        Start-Sleep -Milliseconds 250
    }

    Remove-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name 'AgentLatch' -ErrorAction SilentlyContinue
    if ($RemoveSettings) {
        Remove-Item -Path 'HKCU:\Software\AgentLatch' -Recurse -Force -ErrorAction SilentlyContinue
    }
    if (Test-Path -LiteralPath $InstallDirectory) {
        Remove-Item -LiteralPath $InstallDirectory -Recurse -Force
    }
    Write-Host 'AgentLatch was uninstalled.'
}
