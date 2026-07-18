[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$AgentLatchPath
)

$ErrorActionPreference = 'Stop'
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$installer = Join-Path $repoRoot 'scripts\install-integrations.ps1'
$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("AgentLatchIntegrationTests-" + [guid]::NewGuid().ToString('N'))

function Write-Utf8Json {
    param([string]$Path, $Object)
    [System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($Path)) | Out-Null
    $encoding = [System.Text.UTF8Encoding]::new($false)
    [System.IO.File]::WriteAllText($Path, (($Object | ConvertTo-Json -Depth 16) + [Environment]::NewLine), $encoding)
}

function Count-Command {
    param($Value, [string]$Expected)
    if ($null -eq $Value) { return 0 }
    if ($Value -is [string] -or $Value -is [ValueType]) { return 0 }
    $count = 0
    if ($Value -is [System.Collections.IEnumerable] -and -not ($Value -is [pscustomobject])) {
        foreach ($item in $Value) { $count += Count-Command $item $Expected }
        return $count
    }
    foreach ($property in $Value.PSObject.Properties) {
        if ($property.Name -eq 'command' -and [string]$property.Value -eq $Expected) {
            $count++
        } else {
            $count += Count-Command $property.Value $Expected
        }
    }
    return $count
}

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

function Count-AgentLatchProviderCommand {
    param($Value, [string]$ProviderKey)
    if ($null -eq $Value) { return 0 }
    if ($Value -is [string] -or $Value -is [ValueType]) { return 0 }
    $count = 0
    if ($Value -is [System.Collections.IEnumerable] -and -not ($Value -is [pscustomobject])) {
        foreach ($item in $Value) { $count += Count-AgentLatchProviderCommand $item $ProviderKey }
        return $count
    }
    $pattern = '(?i)AgentLatch\.exe"?\s+--hook\s+' + [regex]::Escape($ProviderKey) + '(?:\s|$)'
    foreach ($property in $Value.PSObject.Properties) {
        if ($property.Name -eq 'command' -and ([string]$property.Value) -match $pattern) {
            $count++
        } else {
            $count += Count-AgentLatchProviderCommand $property.Value $ProviderKey
        }
    }
    return $count
}

try {
    $codexPath = Join-Path $testRoot '.codex\hooks.json'
    $claudePath = Join-Path $testRoot '.claude\settings.json'
    $cursorPath = Join-Path $testRoot '.cursor\hooks.json'
    $antigravityPath = Join-Path $testRoot '.gemini\config\hooks.json'
    $staleCodexCommand = '"C:\OldPreview\AgentLatch.exe" --hook codex'
    $staleClaudeCommand = '"C:\OldPreview\AgentLatch.exe" --hook claude'
    $staleCursorCommand = '"C:\OldPreview\AgentLatch.exe" --hook cursor'
    Write-Utf8Json $codexPath ([ordered]@{
        sentinel = 'codex'
        hooks = [ordered]@{
            SessionStart = @([ordered]@{ hooks = @(
                [ordered]@{ type = 'command'; command = $staleCodexCommand; timeout = 5 },
                [ordered]@{ type = 'command'; command = 'existing-codex-tool.exe'; timeout = 5 }
            ) })
        }
    })
    Write-Utf8Json $claudePath ([ordered]@{
        sentinel = 'claude'
        permissions = [ordered]@{ allow = @('Read') }
        hooks = [ordered]@{
            Stop = @([ordered]@{ hooks = @([ordered]@{ type = 'command'; command = $staleClaudeCommand; timeout = 5 }) })
        }
    })
    Write-Utf8Json $cursorPath ([ordered]@{
        version = 1
        sentinel = 'cursor'
        hooks = [ordered]@{
            stop = @(
                [ordered]@{ command = 'existing-tool.exe --stop' },
                [ordered]@{ command = $staleCursorCommand }
            )
        }
    })
    Write-Utf8Json $antigravityPath ([ordered]@{
        sentinel = 'antigravity'
        'existing-hook' = [ordered]@{
            enabled = $true
            PreInvocation = @([ordered]@{ command = 'existing-antigravity-tool.exe' })
        }
    })

    & $installer -AgentLatchPath $AgentLatchPath -ConfigRoot $testRoot
    $first = @{}
    foreach ($entry in @(
        @{ Name = 'codex'; Path = $codexPath; Provider = 'codex'; Events = 7 },
        @{ Name = 'claude'; Path = $claudePath; Provider = 'claude'; Events = 8 },
        @{ Name = 'cursor'; Path = $cursorPath; Provider = 'cursor'; Events = 8 }
    )) {
        $config = [System.IO.File]::ReadAllText($entry.Path) | ConvertFrom-Json
        if ([string]$config.sentinel -ne $entry.Name) { throw "$($entry.Name) sentinel was not preserved." }
        $command = '"' + [System.IO.Path]::GetFullPath($AgentLatchPath) + '" --hook ' + $entry.Provider
        $count = Count-Command $config $command
        if ($count -ne $entry.Events) { throw "$($entry.Name) expected $($entry.Events) hook commands, found $count." }
        if ((Count-AgentLatchProviderCommand $config $entry.Provider) -ne $entry.Events) {
            throw "$($entry.Name) left a stale or duplicate AgentLatch command behind."
        }
        $first[$entry.Name] = $count
    }
    $antigravityConfig = [System.IO.File]::ReadAllText($antigravityPath) | ConvertFrom-Json
    if ([string]$antigravityConfig.sentinel -ne 'antigravity') { throw 'Antigravity sentinel was not preserved.' }
    $antigravityCommand = '"' + [System.IO.Path]::GetFullPath($AgentLatchPath) + '" --hook antigravity'
    $antigravityCount = Count-CommandPrefix $antigravityConfig $antigravityCommand
    if ($antigravityCount -ne 3) { throw "Antigravity expected 3 hook commands, found $antigravityCount." }
    if ((Count-Command $antigravityConfig 'existing-antigravity-tool.exe') -ne 1) {
        throw 'The existing Antigravity hook was not preserved.'
    }

    & $installer -AgentLatchPath $AgentLatchPath -ConfigRoot $testRoot
    foreach ($entry in @(
        @{ Name = 'codex'; Path = $codexPath; Provider = 'codex' },
        @{ Name = 'claude'; Path = $claudePath; Provider = 'claude' },
        @{ Name = 'cursor'; Path = $cursorPath; Provider = 'cursor' }
    )) {
        $config = [System.IO.File]::ReadAllText($entry.Path) | ConvertFrom-Json
        $command = '"' + [System.IO.Path]::GetFullPath($AgentLatchPath) + '" --hook ' + $entry.Provider
        if ((Count-Command $config $command) -ne $first[$entry.Name]) { throw "$($entry.Name) installer was not idempotent." }
    }
    $antigravityConfig = [System.IO.File]::ReadAllText($antigravityPath) | ConvertFrom-Json
    if ((Count-CommandPrefix $antigravityConfig $antigravityCommand) -ne $antigravityCount) {
        throw 'Antigravity installer was not idempotent.'
    }

    & $installer -AgentLatchPath $AgentLatchPath -ConfigRoot $testRoot -Uninstall
    foreach ($entry in @(
        @{ Name = 'codex'; Path = $codexPath; Provider = 'codex' },
        @{ Name = 'claude'; Path = $claudePath; Provider = 'claude' },
        @{ Name = 'cursor'; Path = $cursorPath; Provider = 'cursor' }
    )) {
        $config = [System.IO.File]::ReadAllText($entry.Path) | ConvertFrom-Json
        if ((Count-AgentLatchProviderCommand $config $entry.Provider) -ne 0) { throw "$($entry.Name) hooks were not removed." }
        if ([string]$config.sentinel -ne $entry.Name) { throw "$($entry.Name) sentinel was lost during uninstall." }
    }
    $antigravityConfig = [System.IO.File]::ReadAllText($antigravityPath) | ConvertFrom-Json
    if ((Count-CommandPrefix $antigravityConfig $antigravityCommand) -ne 0) {
        throw 'Antigravity hooks were not removed.'
    }
    if ([string]$antigravityConfig.sentinel -ne 'antigravity' -or
        (Count-Command $antigravityConfig 'existing-antigravity-tool.exe') -ne 1) {
        throw 'Antigravity configuration was not preserved during uninstall.'
    }
    $cursorConfig = [System.IO.File]::ReadAllText($cursorPath) | ConvertFrom-Json
    if ((Count-Command $cursorConfig 'existing-tool.exe --stop') -ne 1) { throw 'The existing Cursor hook was not preserved.' }
    $codexConfig = [System.IO.File]::ReadAllText($codexPath) | ConvertFrom-Json
    if ((Count-Command $codexConfig 'existing-codex-tool.exe') -ne 1) { throw 'The existing Codex hook was not preserved.' }

    $backups = @(Get-ChildItem -Path $testRoot -Recurse -File -Filter '*.agentlatch-backup-*')
    if ($backups.Count -lt 4) { throw 'Expected backups were not created.' }
    Write-Host 'Integration installer tests passed.'
} finally {
    if (Test-Path -LiteralPath $testRoot) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force
    }
}
