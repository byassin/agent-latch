[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$AgentLatchPath
)

$ErrorActionPreference = 'Stop'
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$AgentLatchPath = [System.IO.Path]::GetFullPath($AgentLatchPath)
$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("AgentLatchInstallTests-" + [guid]::NewGuid().ToString('N'))

function Count-CommandPrefix {
    param($Value, [string]$ExpectedPrefix)
    if ($null -eq $Value) { return 0 }
    if ($Value -is [string] -or $Value -is [ValueType]) { return 0 }
    $count = 0
    if ($Value -is [System.Collections.IEnumerable] -and -not ($Value -is [pscustomobject])) {
        foreach ($item in $Value) { $count += Count-CommandPrefix $item $ExpectedPrefix }
        return $count
    }
    foreach ($property in $Value.PSObject.Properties) {
        if ($property.Name -eq 'command' -and ([string]$property.Value).StartsWith($ExpectedPrefix, [StringComparison]::Ordinal)) {
            $count++
        } else {
            $count += Count-CommandPrefix $property.Value $ExpectedPrefix
        }
    }
    return $count
}

try {
    $releaseRoot = Join-Path $testRoot 'release'
    $releaseScripts = Join-Path $releaseRoot 'scripts'
    [System.IO.Directory]::CreateDirectory($releaseScripts) | Out-Null
    Copy-Item -LiteralPath $AgentLatchPath -Destination (Join-Path $releaseRoot 'AgentLatch.exe')
    foreach ($scriptName in @('install.ps1', 'install-integrations.ps1', 'uninstall.ps1')) {
        Copy-Item -LiteralPath (Join-Path $repoRoot "scripts\$scriptName") -Destination (Join-Path $releaseScripts $scriptName)
    }

    $installer = Join-Path $releaseScripts 'install.ps1'
    $installDirectory = Join-Path $testRoot 'installed-default'
    $configRoot = Join-Path $testRoot 'config-default'
    & $installer -InstallDirectory $installDirectory -ConfigRoot $configRoot -NoStop -NoLaunch

    $installedExecutable = [System.IO.Path]::GetFullPath((Join-Path $installDirectory 'AgentLatch.exe'))
    if (-not (Test-Path -LiteralPath $installedExecutable -PathType Leaf)) {
        throw 'The normal installer did not copy AgentLatch.exe.'
    }
    foreach ($entry in @(
        @{ Name = 'Codex'; Path = (Join-Path $configRoot '.codex\hooks.json'); Provider = 'codex' },
        @{ Name = 'Claude'; Path = (Join-Path $configRoot '.claude\settings.json'); Provider = 'claude' },
        @{ Name = 'Cursor'; Path = (Join-Path $configRoot '.cursor\hooks.json'); Provider = 'cursor' },
        @{ Name = 'Antigravity'; Path = (Join-Path $configRoot '.gemini\config\hooks.json'); Provider = 'antigravity' }
    )) {
        if (-not (Test-Path -LiteralPath $entry.Path -PathType Leaf)) {
            throw "$($entry.Name) hooks were not installed by default."
        }
        $config = [System.IO.File]::ReadAllText($entry.Path) | ConvertFrom-Json
        $commandPrefix = '"' + $installedExecutable + '" --hook ' + $entry.Provider
        if ((Count-CommandPrefix $config $commandPrefix) -lt 1) {
            throw "$($entry.Name) does not reference the installed AgentLatch executable."
        }
    }

    & (Join-Path $installDirectory 'uninstall.ps1') -InstallDirectory $installDirectory -ConfigRoot $configRoot -Confirm:$false
    if (Test-Path -LiteralPath $installDirectory) {
        throw 'The uninstaller did not remove the AgentLatch installation directory.'
    }
    foreach ($entry in @(
        @{ Name = 'Codex'; Path = (Join-Path $configRoot '.codex\hooks.json'); Provider = 'codex' },
        @{ Name = 'Claude'; Path = (Join-Path $configRoot '.claude\settings.json'); Provider = 'claude' },
        @{ Name = 'Cursor'; Path = (Join-Path $configRoot '.cursor\hooks.json'); Provider = 'cursor' },
        @{ Name = 'Antigravity'; Path = (Join-Path $configRoot '.gemini\config\hooks.json'); Provider = 'antigravity' }
    )) {
        $config = [System.IO.File]::ReadAllText($entry.Path) | ConvertFrom-Json
        $commandPrefix = '"' + $installedExecutable + '" --hook ' + $entry.Provider
        if ((Count-CommandPrefix $config $commandPrefix) -ne 0) {
            throw "$($entry.Name) hooks were not removed by the normal uninstaller."
        }
    }

    $skipInstallDirectory = Join-Path $testRoot 'installed-skip-hooks'
    $skipConfigRoot = Join-Path $testRoot 'config-skip-hooks'
    & $installer -InstallDirectory $skipInstallDirectory -ConfigRoot $skipConfigRoot -SkipHooks -NoStop -NoLaunch
    if (-not (Test-Path -LiteralPath (Join-Path $skipInstallDirectory 'AgentLatch.exe') -PathType Leaf)) {
        throw 'The process-only installer did not copy AgentLatch.exe.'
    }
    if (Test-Path -LiteralPath $skipConfigRoot) {
        throw '-SkipHooks unexpectedly changed agent configuration.'
    }

    Write-Host 'Default installer tests passed.'
} finally {
    if (Test-Path -LiteralPath $testRoot) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force
    }
}
