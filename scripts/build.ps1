[CmdletBinding()]
param(
    [ValidateSet('x64', 'ARM64')]
    [string]$Architecture = 'x64',

    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [string]$BuildDirectory
)

$ErrorActionPreference = 'Stop'
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $repoRoot ("build-$($Architecture.ToLowerInvariant())")
}

& cmake -S $repoRoot -B $BuildDirectory -G 'Visual Studio 17 2022' -A $Architecture
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& cmake --build $BuildDirectory --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }

$executable = Join-Path $BuildDirectory "$Configuration\AgentLatch.exe"
if ($Architecture -eq 'x64') {
    $process = Start-Process -FilePath $executable -ArgumentList '--self-test' -Wait -PassThru
    if ($process.ExitCode -ne 0) {
        throw "AgentLatch self-test failed with exit code $($process.ExitCode)."
    }
}
Write-Host "Built $executable"
