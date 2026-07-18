[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$InstallerPath
)

$ErrorActionPreference = 'Stop'
$InstallerPath = [System.IO.Path]::GetFullPath($InstallerPath)
if (-not (Test-Path -LiteralPath $InstallerPath -PathType Leaf)) {
    throw "Installer not found: $InstallerPath"
}
$testAppIdMarker = "$InstallerPath.test-app-id"
if (-not (Test-Path -LiteralPath $testAppIdMarker -PathType Leaf)) {
    throw 'Refusing to run setup integration tests against a production installer. Build an isolated installer with build-installer.ps1 -TestBuild.'
}
$testAppId = [guid]::Empty
if (-not [guid]::TryParse(([System.IO.File]::ReadAllText($testAppIdMarker).Trim()), [ref]$testAppId) -or
    $testAppId -eq [guid]'BBC37307-15F1-4F00-8936-60BC06B5FAB5') {
    throw 'The setup test marker does not contain a safe, non-production AppId.'
}

$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("AgentLatchSetupTests-" + [guid]::NewGuid().ToString('N'))
[System.IO.Directory]::CreateDirectory($testRoot) | Out-Null
$uninstaller = $null
$uninstallSucceeded = $false
try {
    $installDirectory = Join-Path $testRoot 'installed'
    $configRoot = Join-Path $testRoot 'config'
    $setupLog = Join-Path $testRoot 'setup.log'
    $setupArguments = @(
        '/VERYSILENT',
        '/SUPPRESSMSGBOXES',
        '/NORESTART',
        '/NOCANCEL',
        '/NOICONS',
        '/TESTMODE',
        '/NOSTOP',
        '/TASKS=""',
        "/AGENTLATCHCONFIGROOT=$configRoot",
        "/DIR=$installDirectory",
        "/LOG=$setupLog"
    )
    $setup = Start-Process -FilePath $InstallerPath -ArgumentList $setupArguments -Wait -PassThru
    if ($setup.ExitCode -ne 0) {
        throw "Setup failed with exit code $($setup.ExitCode). See $setupLog"
    }
    $uninstaller = Get-ChildItem -LiteralPath $installDirectory -Filter 'unins*.exe' -File | Select-Object -First 1
    if ($null -eq $uninstaller) {
        throw 'Setup did not install its Windows uninstaller.'
    }

    foreach ($relativePath in @(
        'AgentLatch.exe',
        'install-integrations.ps1'
    )) {
        if (-not (Test-Path -LiteralPath (Join-Path $installDirectory $relativePath) -PathType Leaf)) {
            throw "Setup did not install $relativePath."
        }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $installDirectory '.integrations-installed') -PathType Leaf)) {
        throw 'Setup did not record its installed integrations.'
    }
    foreach ($relativePath in @(
        '.codex\hooks.json',
        '.claude\settings.json',
        '.cursor\hooks.json',
        '.gemini\config\hooks.json'
    )) {
        if (-not (Test-Path -LiteralPath (Join-Path $configRoot $relativePath) -PathType Leaf)) {
            throw "Setup did not install integration configuration $relativePath."
        }
    }

    $selfTest = Start-Process -FilePath (Join-Path $installDirectory 'AgentLatch.exe') -ArgumentList '--self-test' -Wait -PassThru
    if ($selfTest.ExitCode -ne 0) {
        throw "The installed executable self-test failed with exit code $($selfTest.ExitCode)."
    }

    $uninstall = Start-Process `
        -FilePath $uninstaller.FullName `
        -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/NOSTOP') `
        -Wait `
        -PassThru
    if ($uninstall.ExitCode -ne 0) {
        throw "Uninstall failed with exit code $($uninstall.ExitCode)."
    }
    $uninstallSucceeded = $true
    for ($attempt = 0; $attempt -lt 50 -and (Test-Path -LiteralPath $installDirectory); $attempt++) {
        Start-Sleep -Milliseconds 100
    }
    if (Test-Path -LiteralPath $installDirectory) {
        throw 'Uninstall left the installation directory behind.'
    }
    $remainingHookReference = Get-ChildItem -LiteralPath $configRoot -Recurse -File |
        Where-Object { $_.Name -notlike '*.agentlatch-backup-*' } |
        Select-String -SimpleMatch 'AgentLatch.exe' |
        Select-Object -First 1
    if ($null -ne $remainingHookReference) {
        throw "Uninstall left an AgentLatch hook reference in $($remainingHookReference.Path)."
    }

    Write-Host 'Windows setup installer tests passed.'
} finally {
    if (-not $uninstallSucceeded -and $null -ne $uninstaller -and (Test-Path -LiteralPath $uninstaller.FullName)) {
        Start-Process `
            -FilePath $uninstaller.FullName `
            -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/NOSTOP') `
            -Wait | Out-Null
    }
    $tempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
    $resolvedTestRoot = [System.IO.Path]::GetFullPath($testRoot)
    if ($resolvedTestRoot.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -and
        $resolvedTestRoot -ne $tempRoot -and
        (Test-Path -LiteralPath $resolvedTestRoot)) {
        Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
    }
}
