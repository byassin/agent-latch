[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$AgentLatchPath
)

$ErrorActionPreference = 'Stop'
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$installer = Join-Path $repoRoot 'scripts\install-integrations.ps1'
$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("AgentLatchIntegrationTests-" + [guid]::NewGuid().ToString('N'))
$statusKey = 'HKCU:\Software\AgentLatch-Test-' + [guid]::NewGuid().ToString('N')

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

function Test-ArgumentsEqual {
    param($Actual, [string[]]$Expected)
    $actualValues = @($Actual)
    if ($actualValues.Count -ne $Expected.Count) { return $false }
    for ($index = 0; $index -lt $Expected.Count; $index++) {
        if (-not [string]::Equals([string]$actualValues[$index], $Expected[$index], [StringComparison]::Ordinal)) {
            return $false
        }
    }
    return $true
}

function Count-ExecCommand {
    param($Value, [string]$ExpectedCommand, [string[]]$ExpectedArguments)
    if ($null -eq $Value) { return 0 }
    if ($Value -is [string] -or $Value -is [ValueType]) { return 0 }
    $count = 0
    if ($Value -is [System.Collections.IEnumerable] -and -not ($Value -is [pscustomobject])) {
        foreach ($item in $Value) { $count += Count-ExecCommand $item $ExpectedCommand $ExpectedArguments }
        return $count
    }
    $commandProperty = $Value.PSObject.Properties['command']
    $argumentsProperty = $Value.PSObject.Properties['args']
    $actualArguments = if ($null -eq $argumentsProperty) { @() } else { @($argumentsProperty.Value) }
    if ($null -ne $commandProperty -and
        [string]::Equals([string]$commandProperty.Value, $ExpectedCommand, [StringComparison]::Ordinal) -and
        (Test-ArgumentsEqual $actualArguments $ExpectedArguments)) {
        $count++
    }
    foreach ($property in $Value.PSObject.Properties) {
        if ($property.Name -notin @('command', 'args')) {
            $count += Count-ExecCommand $property.Value $ExpectedCommand $ExpectedArguments
        }
    }
    return $count
}

function Count-AgentLatchProviderEntry {
    param($Value, [string]$ProviderKey)
    if ($null -eq $Value) { return 0 }
    if ($Value -is [string] -or $Value -is [ValueType]) { return 0 }
    $count = 0
    if ($Value -is [System.Collections.IEnumerable] -and -not ($Value -is [pscustomobject])) {
        foreach ($item in $Value) { $count += Count-AgentLatchProviderEntry $item $ProviderKey }
        return $count
    }
    $pattern = '(?i)AgentLatch\.exe"?\s+--hook\s+' + [regex]::Escape($ProviderKey) + '(?:\s|$)'
    $commandProperty = $Value.PSObject.Properties['command']
    $argumentsProperty = $Value.PSObject.Properties['args']
    $entryCommand = if ($null -eq $commandProperty) { '' } else { [string]$commandProperty.Value }
    $entryArguments = if ($null -eq $argumentsProperty) { @() } else { @($argumentsProperty.Value) }
    $isProviderEntry = $entryCommand -match $pattern
    if (-not $isProviderEntry -and $entryArguments.Count -ge 2) {
        $executableName = [System.IO.Path]::GetFileName($entryCommand)
        $isProviderEntry = [string]::Equals($executableName, 'AgentLatch.exe', [StringComparison]::OrdinalIgnoreCase) -and
            [string]$entryArguments[0] -eq '--hook' -and
            [string]::Equals([string]$entryArguments[1], $ProviderKey, [StringComparison]::OrdinalIgnoreCase)
    }
    if ($isProviderEntry) {
        $count++
    }
    foreach ($property in $Value.PSObject.Properties) {
        if ($property.Name -notin @('command', 'args')) {
            $count += Count-AgentLatchProviderEntry $property.Value $ProviderKey
        }
    }
    return $count
}

try {
    $runtimeDirectory = Join-Path $testRoot 'runtime with spaces'
    [System.IO.Directory]::CreateDirectory($runtimeDirectory) | Out-Null
    $hookExecutablePath = Join-Path $runtimeDirectory 'AgentLatch.exe'
    Copy-Item -LiteralPath $AgentLatchPath -Destination $hookExecutablePath
    $codexPath = Join-Path $testRoot '.codex\hooks.json'
    $claudePath = Join-Path $testRoot '.claude\settings.json'
    $cursorPath = Join-Path $testRoot '.cursor\hooks.json'
    $antigravityPath = Join-Path $testRoot '.gemini\config\hooks.json'
    $staleCodexCommand = '"C:\OldPreview\AgentLatch.exe" --hook codex'
    $staleClaudeCommand = '"C:\OldPreview\AgentLatch.exe" --hook claude'
    $staleBareClaudeCommand = 'C:\OldPreview\AgentLatch.exe --hook claude'
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
            Stop = @([ordered]@{ hooks = @(
                [ordered]@{ type = 'command'; command = $staleClaudeCommand; timeout = 5 },
                [ordered]@{ type = 'command'; command = $staleBareClaudeCommand; timeout = 5 },
                [ordered]@{ type = 'command'; command = 'C:\OldPreview\AgentLatch.exe'; args = @('--hook', 'claude'); timeout = 5 },
                [ordered]@{ type = 'command'; command = 'existing-claude-tool.exe'; timeout = 5 }
            ) })
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

    & $installer -AgentLatchPath $hookExecutablePath -ConfigRoot $testRoot -IntegrationStatusKeyOverride $statusKey
    $integrationStatus = Get-ItemProperty -Path $statusKey
    if ($integrationStatus.IntegrationExpectedCodex -ne 1 -or
        $integrationStatus.IntegrationExpectedClaude -ne 1 -or
        [string]::IsNullOrWhiteSpace([string]$integrationStatus.IntegrationCommandCodex) -or
        [string]::IsNullOrWhiteSpace([string]$integrationStatus.IntegrationCommandClaude) -or
        $integrationStatus.HookSeenCodex -ne 0 -or
        $integrationStatus.HookSeenClaude -ne 0) {
        throw 'Fresh install did not initialize Codex and Claude integration health.'
    }
    $first = @{}
    foreach ($entry in @(
        @{ Name = 'codex'; Path = $codexPath; Provider = 'codex'; Events = 7 },
        @{ Name = 'claude'; Path = $claudePath; Provider = 'claude'; Events = 11 },
        @{ Name = 'cursor'; Path = $cursorPath; Provider = 'cursor'; Events = 8 }
    )) {
        $config = [System.IO.File]::ReadAllText($entry.Path) | ConvertFrom-Json
        if ([string]$config.sentinel -ne $entry.Name) { throw "$($entry.Name) sentinel was not preserved." }
        $resolvedExecutable = [System.IO.Path]::GetFullPath($hookExecutablePath)
        $command = '"' + $resolvedExecutable + '" --hook ' + $entry.Provider
        $count = if ($entry.Provider -eq 'claude') {
            Count-ExecCommand $config $resolvedExecutable @('--hook', 'claude')
        } else {
            Count-Command $config $command
        }
        if ($count -ne $entry.Events) { throw "$($entry.Name) expected $($entry.Events) hook commands, found $count." }
        if ((Count-AgentLatchProviderEntry $config $entry.Provider) -ne $entry.Events) {
            throw "$($entry.Name) left a stale or duplicate AgentLatch command behind."
        }
        $first[$entry.Name] = $count
    }
    $antigravityConfig = [System.IO.File]::ReadAllText($antigravityPath) | ConvertFrom-Json
    if ([string]$antigravityConfig.sentinel -ne 'antigravity') { throw 'Antigravity sentinel was not preserved.' }
    $antigravityCommand = '"' + [System.IO.Path]::GetFullPath($hookExecutablePath) + '" --hook antigravity'
    $antigravityCount = Count-CommandPrefix $antigravityConfig $antigravityCommand
    if ($antigravityCount -ne 3) { throw "Antigravity expected 3 hook commands, found $antigravityCount." }
    if ((Count-Command $antigravityConfig 'existing-antigravity-tool.exe') -ne 1) {
        throw 'The existing Antigravity hook was not preserved.'
    }
    $claudeConfig = [System.IO.File]::ReadAllText($claudePath) | ConvertFrom-Json
    foreach ($eventName in @('PostToolBatch', 'SubagentStart', 'SubagentStop', 'TaskCreated', 'TaskCompleted', 'Stop')) {
        $event = $claudeConfig.hooks.PSObject.Properties[$eventName]
        if ($null -eq $event -or
            (Count-ExecCommand $event.Value ([System.IO.Path]::GetFullPath($hookExecutablePath)) @('--hook', 'claude')) -ne 1) {
            throw "Claude lifecycle event $eventName was not installed exactly once."
        }
    }
    if ((Count-Command $claudeConfig 'existing-claude-tool.exe') -ne 1) {
        throw 'The existing Claude hook was not preserved.'
    }

    $execStart = [System.Diagnostics.ProcessStartInfo]::new()
    $execStart.FileName = [System.IO.Path]::GetFullPath($hookExecutablePath)
    $execStart.UseShellExecute = $false
    $execStart.RedirectStandardInput = $true
    $execStart.RedirectStandardOutput = $true
    $execStart.RedirectStandardError = $true
    $execStart.Arguments = '--hook claude'
    $execProcess = [System.Diagnostics.Process]::Start($execStart)
    $execProcess.StandardInput.Write('{}')
    $execProcess.StandardInput.Close()
    $execOutput = $execProcess.StandardOutput.ReadToEnd().Trim()
    $execError = $execProcess.StandardError.ReadToEnd().Trim()
    $execProcess.WaitForExit()
    if ($execProcess.ExitCode -ne 0 -or $execOutput -ne '{}' -or -not [string]::IsNullOrEmpty($execError)) {
        throw "Claude exec-form hook failed: exit=$($execProcess.ExitCode) stdout='$execOutput' stderr='$execError'"
    }

    New-ItemProperty -Path $statusKey -Name 'HookSeenCodex' -Value 1 -PropertyType DWord -Force | Out-Null
    New-ItemProperty -Path $statusKey -Name 'HookSeenClaude' -Value 1 -PropertyType DWord -Force | Out-Null
    & $installer -AgentLatchPath $hookExecutablePath -ConfigRoot $testRoot -IntegrationStatusKeyOverride $statusKey
    $integrationStatus = Get-ItemProperty -Path $statusKey
    if ($integrationStatus.HookSeenCodex -ne 1 -or $integrationStatus.HookSeenClaude -ne 1) {
        throw 'Idempotent reinstall reset healthy hook-seen status without changing commands.'
    }
    foreach ($entry in @(
        @{ Name = 'codex'; Path = $codexPath; Provider = 'codex' },
        @{ Name = 'claude'; Path = $claudePath; Provider = 'claude' },
        @{ Name = 'cursor'; Path = $cursorPath; Provider = 'cursor' }
    )) {
        $config = [System.IO.File]::ReadAllText($entry.Path) | ConvertFrom-Json
        $resolvedExecutable = [System.IO.Path]::GetFullPath($hookExecutablePath)
        $command = '"' + $resolvedExecutable + '" --hook ' + $entry.Provider
        $count = if ($entry.Provider -eq 'claude') {
            Count-ExecCommand $config $resolvedExecutable @('--hook', 'claude')
        } else {
            Count-Command $config $command
        }
        if ($count -ne $first[$entry.Name]) { throw "$($entry.Name) installer was not idempotent." }
    }
    $antigravityConfig = [System.IO.File]::ReadAllText($antigravityPath) | ConvertFrom-Json
    if ((Count-CommandPrefix $antigravityConfig $antigravityCommand) -ne $antigravityCount) {
        throw 'Antigravity installer was not idempotent.'
    }

    & $installer -AgentLatchPath $hookExecutablePath -ConfigRoot $testRoot -IntegrationStatusKeyOverride $statusKey -Uninstall
    $integrationStatus = Get-ItemProperty -Path $statusKey
    if ($integrationStatus.IntegrationExpectedCodex -ne 0 -or
        $integrationStatus.IntegrationExpectedClaude -ne 0 -or
        $null -ne $integrationStatus.PSObject.Properties['IntegrationCommandCodex'] -or
        $null -ne $integrationStatus.PSObject.Properties['IntegrationCommandClaude']) {
        throw 'Uninstall did not clear Codex and Claude integration expectations.'
    }
    foreach ($entry in @(
        @{ Name = 'codex'; Path = $codexPath; Provider = 'codex' },
        @{ Name = 'claude'; Path = $claudePath; Provider = 'claude' },
        @{ Name = 'cursor'; Path = $cursorPath; Provider = 'cursor' }
    )) {
        $config = [System.IO.File]::ReadAllText($entry.Path) | ConvertFrom-Json
        if ((Count-AgentLatchProviderEntry $config $entry.Provider) -ne 0) { throw "$($entry.Name) hooks were not removed." }
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
    Remove-Item -LiteralPath $statusKey -Recurse -Force -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $testRoot) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force
    }
}
