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
    '--export-ui-screenshots',
    '--export-ui-screenshots unused --capture-theme',
    '--export-ui-screenshots unused --capture-theme invalid',
    '--export-status',
    '--export-status relative.json',
    '--export-status C:\test.txt',
    '--export-status C:\test:stream.json',
    '--release',
    '--release --id',
    ('--release --id ' + ('x' * 192)),
    '--acquire --id',
    '--unknown',
    '--keep-awake --indefinite --quit',
    '--self-test --quit',
    '--release --id one --id two',
    '--acquire --label --quit --id',
    '--startup-relay --quit',
    '--startup-relay --hook claude',
    '--startup-relay --self-test',
    '--startup-relay --export-status C:\unused.json',
    '--startup-relay --startup-check --background',
    '--startup-relay --watchdog --quit',
    '--startup-relay --restart-attempt 2'
)
foreach ($arguments in $cases) {
    $process = Start-Process -FilePath $AgentLatchPath -ArgumentList $arguments -Wait -PassThru
    if ($process.ExitCode -ne 2) {
        throw "Expected validation exit 2 for '$arguments'; got $($process.ExitCode)."
    }
}
Write-Host ('Manual/session capture CLI validation tests passed ({0} cases).' -f $cases.Count)
