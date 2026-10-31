[CmdletBinding()]
param(
    [string]$OutputDirectory,
    [string]$AgentLatchPath = (Join-Path $env:LOCALAPPDATA 'AgentLatch\AgentLatch.exe')
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $PSScriptRoot '..\assets'
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
$AgentLatchPath = (Resolve-Path -LiteralPath $AgentLatchPath).Path
if ((Get-Item -LiteralPath $AgentLatchPath).VersionInfo.FileVersionRaw -lt [version]'0.3.0.0') {
    throw 'Off-screen export requires AgentLatch 0.3.0 or newer.'
}
[System.IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
# Product CLI only: no desktop pixels, activation, visible navigation or input.
$arguments = '--export-ui-screenshots "' + $OutputDirectory + '"'
$process = Start-Process -FilePath $AgentLatchPath -ArgumentList $arguments -WindowStyle Hidden -PassThru
$null = $process.Handle
if (-not $process.WaitForExit(30000)) { throw 'AgentLatch capture command timed out.' }
$process.Refresh()
if ($process.ExitCode -ne 0) {
    throw "AgentLatch capture failed with exit code $($process.ExitCode). Update the running app if its IPC is older."
}
Add-Type -AssemblyName System.Drawing
foreach ($name in @('dashboard.png', 'agents.png', 'settings.png')) {
    $path = Join-Path $OutputDirectory $name
    $image = [System.Drawing.Image]::FromFile($path)
    try {
        if ($image.Width -lt 600 -or $image.Height -lt 480) { throw "Unexpected capture size for $name." }
        Write-Host ("Exported {0} ({1} x {2})" -f $path, $image.Width, $image.Height)
    } finally { $image.Dispose() }
}
