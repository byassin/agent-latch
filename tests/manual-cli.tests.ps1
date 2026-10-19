[CmdletBinding()]
param([Parameter(Mandatory)][string]$AgentLatchPath)

$ErrorActionPreference = 'Stop'
$AgentLatchPath = (Resolve-Path -LiteralPath $AgentLatchPath).Path
# All of these must fail before starting or messaging a background instance.
$cases = @(
    '--keep-awake',
    '--keep-awake --hours',
    '--keep-awake --hours 0',
    '--keep-awake --hours -1',
    '--keep-awake --hours 169',
    '--keep-awake --hours 1.5',
    '--keep-awake --hours invalid',
    '--keep-awake --indefinite --hours 8',
    '--keep-awake --stop-keep-awake',
    '--export-ui-screenshots'
)
foreach ($arguments in $cases) {
    $process = Start-Process -FilePath $AgentLatchPath -ArgumentList $arguments -Wait -PassThru
    if ($process.ExitCode -ne 2) {
        throw "Expected validation exit 2 for '$arguments'; got $($process.ExitCode)."
    }
}
Write-Host 'Manual/session capture CLI validation tests passed (10 cases).'
