[CmdletBinding(SupportsShouldProcess)]
param(
    [Parameter(Mandatory = $true)]
    [string]$AgentLatchPath,

    [ValidateSet('All', 'Codex', 'Claude', 'Cursor')]
    [string[]]$Provider = @('All'),

    [switch]$Uninstall,

    [string]$ConfigRoot = $HOME
)

$ErrorActionPreference = 'Stop'
$AgentLatchPath = [System.IO.Path]::GetFullPath($AgentLatchPath)
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

function Update-NestedHook {
    param(
        $Hooks,
        [string]$EventName,
        [string]$Command,
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
            if ($entryCommand -eq $Command) {
                $found = $true
                if ($Remove) {
                    $changed = $true
                    continue
                }
            }
            $keptCommands += $entry
        }
        if ($Remove -and $keptCommands.Count -eq 0 -and $commands.Count -gt 0) {
            continue
        }
        if ($Remove -and $keptCommands.Count -ne $commands.Count) {
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
        [bool]$Remove
    )

    $entries = @(Get-ArrayProperty $Hooks $EventName)
    $found = $false
    $updated = @()
    foreach ($entry in $entries) {
        $entryCommand = if ($null -ne $entry.PSObject.Properties['command']) { [string]$entry.command } else { '' }
        if ($entryCommand -eq $Command) {
            $found = $true
            if ($Remove) {
                continue
            }
        }
        $updated += $entry
    }
    if (-not $Remove -and -not $found) {
        $updated += [pscustomobject]@{ command = $Command }
    }
    $changed = ($Remove -and $found) -or (-not $Remove -and -not $found)
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
            $changed = (Update-DirectHook $hooks $eventName $Command ([bool]$Uninstall)) -or $changed
        } else {
            $changed = (Update-NestedHook $hooks $eventName $Command ([bool]$Uninstall)) -or $changed
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

$providers = if ($Provider -contains 'All') { @('Codex', 'Claude', 'Cursor') } else { @($Provider | Select-Object -Unique) }
$quotedExecutable = '"' + $AgentLatchPath + '"'

if ($providers -contains 'Codex') {
    Update-ProviderConfig `
        -Name 'Codex' `
        -Path (Join-Path $ConfigRoot '.codex\hooks.json') `
        -Command "$quotedExecutable --hook codex" `
        -Events @('UserPromptSubmit', 'PreToolUse', 'PostToolUse', 'SubagentStart', 'SubagentStop', 'Stop') `
        -Direct $false
}
if ($providers -contains 'Claude') {
    Update-ProviderConfig `
        -Name 'Claude Code' `
        -Path (Join-Path $ConfigRoot '.claude\settings.json') `
        -Command "$quotedExecutable --hook claude" `
        -Events @('UserPromptSubmit', 'PreToolUse', 'PostToolUse', 'SubagentStart', 'SubagentStop', 'Stop', 'StopFailure', 'SessionEnd') `
        -Direct $false
}
if ($providers -contains 'Cursor') {
    Update-ProviderConfig `
        -Name 'Cursor' `
        -Path (Join-Path $ConfigRoot '.cursor\hooks.json') `
        -Command "$quotedExecutable --hook cursor" `
        -Events @('beforeSubmitPrompt', 'preToolUse', 'postToolUse', 'subagentStart', 'subagentStop', 'afterAgentThought', 'afterAgentResponse', 'stop') `
        -Direct $true
}

Write-Host ''
if ($WhatIfPreference) {
    Write-Host 'Preview complete. No configuration files were written.'
} else {
    Write-Host $(if ($Uninstall) { 'Selected AgentLatch lifecycle hooks were removed.' } else { 'AgentLatch lifecycle hooks are ready. Restart active agent sessions so they reload their configuration.' })
}
