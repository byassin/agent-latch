[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Executable,

    [ValidateSet('x64', 'ARM64')]
    [string]$Architecture = 'x64',

    [string]$Version = '0.1.0',

    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$Executable = [System.IO.Path]::GetFullPath($Executable)
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "Executable not found: $Executable"
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot 'dist'
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
[System.IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null

$packageName = "AgentLatch-$Version-$Architecture"
$staging = Join-Path $OutputDirectory $packageName
if (Test-Path -LiteralPath $staging) {
    throw "Package staging directory already exists: $staging"
}
[System.IO.Directory]::CreateDirectory($staging) | Out-Null
[System.IO.Directory]::CreateDirectory((Join-Path $staging 'scripts')) | Out-Null
[System.IO.Directory]::CreateDirectory((Join-Path $staging 'docs')) | Out-Null
[System.IO.Directory]::CreateDirectory((Join-Path $staging 'assets')) | Out-Null

Copy-Item -LiteralPath $Executable -Destination (Join-Path $staging 'AgentLatch.exe')
foreach ($name in @('install.ps1', 'install-integrations.ps1', 'uninstall.ps1')) {
    Copy-Item -LiteralPath (Join-Path $repoRoot "scripts\$name") -Destination (Join-Path $staging "scripts\$name")
}
foreach ($name in @('README.md', 'LICENSE', 'CHANGELOG.md', 'CONTRIBUTING.md', 'SECURITY.md')) {
    Copy-Item -LiteralPath (Join-Path $repoRoot $name) -Destination (Join-Path $staging $name)
}
foreach ($name in @('INTEGRATIONS.md', 'ARCHITECTURE.md', 'PRIVACY.md')) {
    Copy-Item -LiteralPath (Join-Path $repoRoot "docs\$name") -Destination (Join-Path $staging "docs\$name")
}
foreach ($name in @('agent-latch.svg', 'dashboard.png')) {
    Copy-Item -LiteralPath (Join-Path $repoRoot "assets\$name") -Destination (Join-Path $staging "assets\$name")
}

$zipPath = Join-Path $OutputDirectory "$packageName.zip"
Compress-Archive -LiteralPath $staging -DestinationPath $zipPath -CompressionLevel Optimal
$hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
$hashLine = "$hash  $([System.IO.Path]::GetFileName($zipPath))"
[System.IO.File]::WriteAllText("$zipPath.sha256", $hashLine + [Environment]::NewLine, [System.Text.UTF8Encoding]::new($false))
Write-Host "Created $zipPath"
Write-Host "SHA256 $hash"
