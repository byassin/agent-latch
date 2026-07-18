[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$InstallDirectory = (Join-Path $env:LOCALAPPDATA 'AgentLatch'),
    [switch]$StartWithWindows,
    [switch]$InstallHooks
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
    New-Item -ItemType Directory -Path $InstallDirectory -Force | Out-Null
    Copy-Item -LiteralPath $sourceExecutable -Destination (Join-Path $InstallDirectory 'AgentLatch.exe') -Force
    foreach ($scriptName in @('install-integrations.ps1', 'uninstall.ps1')) {
        $candidate = Join-Path $PSScriptRoot $scriptName
        if (Test-Path -LiteralPath $candidate) {
            Copy-Item -LiteralPath $candidate -Destination (Join-Path $InstallDirectory $scriptName) -Force
        }
    }

    $installedExecutable = Join-Path $InstallDirectory 'AgentLatch.exe'
    if ($StartWithWindows) {
        $runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
        New-Item -Path $runKey -Force | Out-Null
        New-ItemProperty -Path $runKey -Name 'AgentLatch' -Value ('"{0}" --background' -f $installedExecutable) -PropertyType String -Force | Out-Null
    }
    if ($InstallHooks) {
        & (Join-Path $InstallDirectory 'install-integrations.ps1') -AgentLatchPath $installedExecutable
    }
    Start-Process -FilePath $installedExecutable
    Write-Host "AgentLatch installed to $InstallDirectory"
}
