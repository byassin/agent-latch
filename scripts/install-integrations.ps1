[CmdletBinding(SupportsShouldProcess)]
param(
    [Parameter(Mandatory = $true)]
    [string]$AgentLatchPath,

    [ValidateSet('All', 'Codex', 'Claude', 'Cursor', 'Antigravity')]
    [string[]]$Provider = @('All'),

    [switch]$Uninstall,

    [string]$ConfigRoot = $HOME,

    [string]$InstallMarkerPath
)

$ErrorActionPreference = 'Stop'
$AgentLatchPath = [System.IO.Path]::GetFullPath($AgentLatchPath)
if (-not [string]::IsNullOrWhiteSpace($InstallMarkerPath)) {
    $InstallMarkerPath = [System.IO.Path]::GetFullPath($InstallMarkerPath)
}
if ($Uninstall -and
    -not [string]::IsNullOrWhiteSpace($InstallMarkerPath) -and
    (Test-Path -LiteralPath $InstallMarkerPath -PathType Leaf)) {
    try {
        $marker = [System.IO.File]::ReadAllText($InstallMarkerPath) | ConvertFrom-Json
        if (-not [string]::IsNullOrWhiteSpace([string]$marker.ConfigRoot)) {
            $ConfigRoot = [string]$marker.ConfigRoot
        }
    } catch {
        Write-Warning "AgentLatch could not read its integration marker; using the current user profile. $($_.Exception.Message)"
    }
}
$ConfigRoot = [System.IO.Path]::GetFullPath($ConfigRoot)
if (-not (Test-Path -LiteralPath $AgentLatchPath -PathType Leaf)) {
    throw "AgentLatch executable not found: $AgentLatchPath"
}

function Get-OrAddObjectProperty {
    param(
        [Parameter(Mandatory = $true)]$Object,
        [Parameter(Mandatory = $true)][string]$Name
    )

    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) {
        $value = [pscustomobject]@{}
        if ($null -eq $property) {
            $Object | Add-Member -MemberType NoteProperty -Name $Name -Value $value
        } else {
            $property.Value = $value
        }
        return $value
    }
    return $property.Value
}

function Get-ArrayProperty {
    param($Object, [string]$Name)
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) {
        return @()
    }
    return @($property.Value)
}

function Set-ArrayProperty {
    param($Object, [string]$Name, [object[]]$Value)
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) {
        $Object | Add-Member -MemberType NoteProperty -Name $Name -Value @($Value)
    } else {
        $property.Value = @($Value)
    }
}

function Set-ObjectProperty {
    param($Object, [string]$Name, $Value)
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) {
        $Object | Add-Member -MemberType NoteProperty -Name $Name -Value $Value
    } else {
        $property.Value = $Value
    }
}

function Read-JsonObject {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        return [pscustomobject]@{}
    }
    $text = [System.IO.File]::ReadAllText($Path)
    if ([string]::IsNullOrWhiteSpace($text)) {
        return [pscustomobject]@{}
    }
    try {
        $result = $text | ConvertFrom-Json
    } catch {
        throw "Cannot update invalid JSON in $Path. Fix the file or move it aside, then try again. $($_.Exception.Message)"
    }
    if ($null -eq $result) {
        return [pscustomobject]@{}
    }
    return $result
}

function Save-JsonObject {
    param([string]$Path, $Object)
    $directory = [System.IO.Path]::GetDirectoryName($Path)
    [System.IO.Directory]::CreateDirectory($directory) | Out-Null
    if (Test-Path -LiteralPath $Path) {
        $timestamp = Get-Date -Format 'yyyyMMdd-HHmmssfff'
        Copy-Item -LiteralPath $Path -Destination "$Path.agentlatch-backup-$timestamp" -Force
    }
    $json = $Object | ConvertTo-Json -Depth 32
    $tempPath = "$Path.agentlatch-tmp"
    $encoding = [System.Text.UTF8Encoding]::new($false)
    [System.IO.File]::WriteAllText($tempPath, $json + [Environment]::NewLine, $encoding)
    Move-Item -LiteralPath $tempPath -Destination $Path -Force
}

function Test-AgentLatchProviderCommand {
    param(
        [string]$Command,
        [string]$ProviderKey
    )

    if ([string]::IsNullOrWhiteSpace($Command)) {
        return $false
    }
    $pattern = '(?i)AgentLatch\.exe"?\s+--hook\s+' + [regex]::Escape($ProviderKey) + '(?:\s|$)'
    return $Command -match $pattern
}

function Update-NestedHook {
    param(
        $Hooks,
        [string]$EventName,
        [string]$Command,
        [string]$ProviderKey,
        [bool]$Remove
    )

    $groups = @(Get-ArrayProperty $Hooks $EventName)
    $changed = $false
    $found = $false
    $updatedGroups = @()

    foreach ($group in $groups) {
        $commands = @(Get-ArrayProperty $group 'hooks')
        $keptCommands = @()
        foreach ($entry in $commands) {
            $entryCommand = if ($null -ne $entry.PSObject.Properties['command']) { [string]$entry.command } else { '' }
            if (Test-AgentLatchProviderCommand $entryCommand $ProviderKey) {
                if (-not $Remove -and -not $found -and $entryCommand -eq $Command) {
                    $found = $true
                    $keptCommands += $entry
                } else {
                    $changed = $true
                }
                continue
            }
            $keptCommands += $entry
        }
        if ($Remove -and $keptCommands.Count -eq 0 -and $commands.Count -gt 0) {
            continue
        }
        if ($keptCommands.Count -ne $commands.Count) {
            Set-ArrayProperty $group 'hooks' $keptCommands
        }
        $updatedGroups += $group
    }

    if (-not $Remove -and -not $found) {
        $updatedGroups += [pscustomobject]@{
            hooks = @(
                [pscustomobject]@{
                    type = 'command'
                    command = $Command
                    timeout = 5
                }
            )
        }
        $changed = $true
    }

    if ($changed) {
        Set-ArrayProperty $Hooks $EventName $updatedGroups
    }
    return $changed
}

function Update-DirectHook {
    param(
        $Hooks,
        [string]$EventName,
        [string]$Command,
        [string]$ProviderKey,
        [bool]$Remove
    )

    $entries = @(Get-ArrayProperty $Hooks $EventName)
    $found = $false
    $changed = $false
    $updated = @()
    foreach ($entry in $entries) {
        $entryCommand = if ($null -ne $entry.PSObject.Properties['command']) { [string]$entry.command } else { '' }
        if (Test-AgentLatchProviderCommand $entryCommand $ProviderKey) {
            if (-not $Remove -and -not $found -and $entryCommand -eq $Command) {
                $found = $true
                $updated += $entry
            } else {
                $changed = $true
            }
            continue
        }
        $updated += $entry
    }
    if (-not $Remove -and -not $found) {
        $updated += [pscustomobject]@{ command = $Command }
        $changed = $true
    }
    if ($changed) {
        Set-ArrayProperty $Hooks $EventName $updated
    }
    return $changed
}

function Update-ProviderConfig {
    param(
        [string]$Name,
        [string]$Path,
        [string]$Command,
        [string]$ProviderKey,
        [string[]]$Events,
        [bool]$Direct
    )

    $root = Read-JsonObject $Path
    if ($Name -eq 'Cursor' -and $null -eq $root.PSObject.Properties['version']) {
        $root | Add-Member -MemberType NoteProperty -Name 'version' -Value 1
    }
    $hooks = Get-OrAddObjectProperty $root 'hooks'
    $changed = $false
    foreach ($eventName in $Events) {
        if ($Direct) {
            $changed = (Update-DirectHook $hooks $eventName $Command $ProviderKey ([bool]$Uninstall)) -or $changed
        } else {
            $changed = (Update-NestedHook $hooks $eventName $Command $ProviderKey ([bool]$Uninstall)) -or $changed
        }
    }

    if (-not $changed) {
        Write-Host "$Name integration already in the requested state."
        return
    }

    $verb = if ($Uninstall) { 'Remove AgentLatch hooks from' } else { 'Install AgentLatch hooks in' }
    if ($PSCmdlet.ShouldProcess($Path, $verb)) {
        Save-JsonObject $Path $root
        Write-Host "Updated $Name hooks: $Path"
    }
}

function Update-AntigravityConfig {
    param(
        [string]$Path,
        [string]$ExecutableCommand
    )

    $root = Read-JsonObject $Path
    $propertyName = 'agent-latch'
    $existing = $root.PSObject.Properties[$propertyName]
    $changed = $false

    if ($Uninstall) {
        if ($null -ne $existing) {
            $root.PSObject.Properties.Remove($propertyName)
            $changed = $true
        }
    } else {
        $handler = {
            param([string]$EventName)
            [pscustomobject]@{
                type = 'command'
                command = "$ExecutableCommand --event $EventName"
                timeout = 5
            }
        }
        $definition = [pscustomobject]@{
            enabled = $true
            PreInvocation = @(& $handler 'PreInvocation')
            PostInvocation = @(& $handler 'PostInvocation')
            Stop = @(& $handler 'Stop')
        }
        $desiredJson = $definition | ConvertTo-Json -Depth 16 -Compress
        $existingJson = if ($null -eq $existing) { '' } else { $existing.Value | ConvertTo-Json -Depth 16 -Compress }
        if ($desiredJson -ne $existingJson) {
            Set-ObjectProperty $root $propertyName $definition
            $changed = $true
        }
    }

    if (-not $changed) {
        Write-Host 'Google Antigravity integration already in the requested state.'
        return
    }

    $verb = if ($Uninstall) { 'Remove AgentLatch hooks from' } else { 'Install AgentLatch hooks in' }
    if ($PSCmdlet.ShouldProcess($Path, $verb)) {
        Save-JsonObject $Path $root
        Write-Host "Updated Google Antigravity hooks: $Path"
    }
}

$providers = if ($Provider -contains 'All') { @('Codex', 'Claude', 'Cursor', 'Antigravity') } else { @($Provider | Select-Object -Unique) }
$quotedExecutable = '"' + $AgentLatchPath + '"'

if ($providers -contains 'Codex') {
    Update-ProviderConfig `
        -Name 'Codex' `
        -Path (Join-Path $ConfigRoot '.codex\hooks.json') `
        -Command "$quotedExecutable --hook codex" `
        -ProviderKey 'codex' `
        -Events @('SessionStart', 'UserPromptSubmit', 'PreToolUse', 'PostToolUse', 'SubagentStart', 'SubagentStop', 'Stop') `
        -Direct $false
}
if ($providers -contains 'Claude') {
    Update-ProviderConfig `
        -Name 'Claude Code' `
        -Path (Join-Path $ConfigRoot '.claude\settings.json') `
        -Command "$quotedExecutable --hook claude" `
        -ProviderKey 'claude' `
        -Events @('UserPromptSubmit', 'PreToolUse', 'PostToolUse', 'SubagentStart', 'SubagentStop', 'Stop', 'StopFailure', 'SessionEnd') `
        -Direct $false
}
if ($providers -contains 'Cursor') {
    Update-ProviderConfig `
        -Name 'Cursor' `
        -Path (Join-Path $ConfigRoot '.cursor\hooks.json') `
        -Command "$quotedExecutable --hook cursor" `
        -ProviderKey 'cursor' `
        -Events @('beforeSubmitPrompt', 'preToolUse', 'postToolUse', 'subagentStart', 'subagentStop', 'afterAgentThought', 'afterAgentResponse', 'stop') `
        -Direct $true
}
if ($providers -contains 'Antigravity') {
    Update-AntigravityConfig `
        -Path (Join-Path $ConfigRoot '.gemini\config\hooks.json') `
        -ExecutableCommand "$quotedExecutable --hook antigravity"
}

$userProfile = [System.IO.Path]::GetFullPath([Environment]::GetFolderPath('UserProfile'))
if ([string]::Equals($ConfigRoot.TrimEnd('\'), $userProfile.TrimEnd('\'), [StringComparison]::OrdinalIgnoreCase) -and
    $providers -contains 'Codex') {
    $statusKey = 'HKCU:\Software\AgentLatch'
    if ($PSCmdlet.ShouldProcess($statusKey, 'Update Codex integration health status')) {
        New-Item -Path $statusKey -Force | Out-Null
        if ($Uninstall) {
            New-ItemProperty -Path $statusKey -Name 'IntegrationExpectedCodex' -Value 0 -PropertyType DWord -Force | Out-Null
            Remove-ItemProperty -Path $statusKey -Name 'IntegrationCommandCodex' -ErrorAction SilentlyContinue
        } else {
            $codexCommand = "$quotedExecutable --hook codex"
            $previousCommand = [string](Get-ItemPropertyValue -Path $statusKey -Name 'IntegrationCommandCodex' -ErrorAction SilentlyContinue)
            if (-not [string]::Equals($previousCommand, $codexCommand, [StringComparison]::Ordinal)) {
                New-ItemProperty -Path $statusKey -Name 'HookSeenCodex' -Value 0 -PropertyType DWord -Force | Out-Null
            }
            New-ItemProperty -Path $statusKey -Name 'IntegrationExpectedCodex' -Value 1 -PropertyType DWord -Force | Out-Null
            New-ItemProperty -Path $statusKey -Name 'IntegrationCommandCodex' -Value $codexCommand -PropertyType String -Force | Out-Null
        }
    }
}

Write-Host ''
if (-not $WhatIfPreference -and -not [string]::IsNullOrWhiteSpace($InstallMarkerPath)) {
    if ($Uninstall) {
        Remove-Item -LiteralPath $InstallMarkerPath -Force -ErrorAction SilentlyContinue
    } else {
        [System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($InstallMarkerPath)) | Out-Null
        $marker = [pscustomobject]@{
            ConfigRoot = $ConfigRoot
            InstalledAt = [DateTimeOffset]::Now.ToString('O')
        }
        [System.IO.File]::WriteAllText(
            $InstallMarkerPath,
            (($marker | ConvertTo-Json) + [Environment]::NewLine),
            [System.Text.UTF8Encoding]::new($false))
    }
}

if ($WhatIfPreference) {
    Write-Host 'Preview complete. No configuration files were written.'
} else {
    Write-Host $(if ($Uninstall) { 'Selected AgentLatch lifecycle hooks were removed.' } else { 'AgentLatch lifecycle hooks are ready. Restart active agent sessions so they reload their configuration. Codex desktop task detection works automatically and requires no chat command.' })
}
