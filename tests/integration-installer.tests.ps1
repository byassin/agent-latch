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

try {
    $codexPath = Join-Path $testRoot '.codex\hooks.json'
    $claudePath = Join-Path $testRoot '.claude\settings.json'
    $cursorPath = Join-Path $testRoot '.cursor\hooks.json'
    Write-Utf8Json $codexPath ([ordered]@{ sentinel = 'codex'; hooks = [ordered]@{} })
    Write-Utf8Json $claudePath ([ordered]@{ sentinel = 'claude'; permissions = [ordered]@{ allow = @('Read') }; hooks = [ordered]@{} })
    Write-Utf8Json $cursorPath ([ordered]@{ version = 1; sentinel = 'cursor'; hooks = [ordered]@{ stop = @([ordered]@{ command = 'existing-tool.exe --stop' }) } })

    & $installer -AgentLatchPath $AgentLatchPath -ConfigRoot $testRoot
    $first = @{}
    foreach ($entry in @(
        @{ Name = 'codex'; Path = $codexPath; Provider = 'codex'; Events = 6 },
        @{ Name = 'claude'; Path = $claudePath; Provider = 'claude'; Events = 8 },
        @{ Name = 'cursor'; Path = $cursorPath; Provider = 'cursor'; Events = 8 }
    )) {
        $config = [System.IO.File]::ReadAllText($entry.Path) | ConvertFrom-Json
        if ([string]$config.sentinel -ne $entry.Name) { throw "$($entry.Name) sentinel was not preserved." }
        $command = '"' + [System.IO.Path]::GetFullPath($AgentLatchPath) + '" --hook ' + $entry.Provider
        $count = Count-Command $config $command
        if ($count -ne $entry.Events) { throw "$($entry.Name) expected $($entry.Events) hook commands, found $count." }
        $first[$entry.Name] = $count
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

    & $installer -AgentLatchPath $AgentLatchPath -ConfigRoot $testRoot -Uninstall
    foreach ($entry in @(
        @{ Name = 'codex'; Path = $codexPath; Provider = 'codex' },
        @{ Name = 'claude'; Path = $claudePath; Provider = 'claude' },
        @{ Name = 'cursor'; Path = $cursorPath; Provider = 'cursor' }
    )) {
        $config = [System.IO.File]::ReadAllText($entry.Path) | ConvertFrom-Json
        $command = '"' + [System.IO.Path]::GetFullPath($AgentLatchPath) + '" --hook ' + $entry.Provider
        if ((Count-Command $config $command) -ne 0) { throw "$($entry.Name) hooks were not removed." }
        if ([string]$config.sentinel -ne $entry.Name) { throw "$($entry.Name) sentinel was lost during uninstall." }
    }
    $cursorConfig = [System.IO.File]::ReadAllText($cursorPath) | ConvertFrom-Json
    if ((Count-Command $cursorConfig 'existing-tool.exe --stop') -ne 1) { throw 'The existing Cursor hook was not preserved.' }

    $backups = @(Get-ChildItem -Path $testRoot -Recurse -File -Filter '*.agentlatch-backup-*')
    if ($backups.Count -lt 3) { throw 'Expected backups were not created.' }
    Write-Host 'Integration installer tests passed.'
} finally {
    if (Test-Path -LiteralPath $testRoot) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force
    }
}
