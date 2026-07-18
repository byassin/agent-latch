[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Executable,

    [ValidateSet('x64', 'ARM64')]
    [string]$Architecture = 'x64',

    [string]$Version = '0.2.0',

    [string]$OutputDirectory,

    [string]$InnoCompiler,

    [switch]$TestBuild,

    [string]$AppId
)

$ErrorActionPreference = 'Stop'
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$Executable = [System.IO.Path]::GetFullPath($Executable)
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "Executable not found: $Executable"
}

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot 'dist'
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
[System.IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null

if ([string]::IsNullOrWhiteSpace($InnoCompiler)) {
    $compilerCandidates = @(
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 7\ISCC.exe'),
        'C:\Program Files\Inno Setup 7\ISCC.exe',
        'C:\Program Files (x86)\Inno Setup 7\ISCC.exe',
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
        'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
    )
    $InnoCompiler = $compilerCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
}
if ([string]::IsNullOrWhiteSpace($InnoCompiler) -or -not (Test-Path -LiteralPath $InnoCompiler -PathType Leaf)) {
    throw 'Inno Setup Compiler 7 or 6 was not found. Install JRSoftware.InnoSetup.7 with winget.'
}
$InnoCompiler = [System.IO.Path]::GetFullPath($InnoCompiler)

$semanticMatch = [regex]::Match($Version, '^(?<major>\d+)\.(?<minor>\d+)\.(?<patch>\d+)(?:-[A-Za-z0-9.-]*?(?<build>\d+))?$')
if (-not $semanticMatch.Success) {
    throw "Version must resemble 1.2.3 or 1.2.3-preview.4: $Version"
}
$buildNumber = if ($semanticMatch.Groups['build'].Success) { [int]$semanticMatch.Groups['build'].Value } else { 0 }
$versionInfoVersion = '{0}.{1}.{2}.{3}' -f
    $semanticMatch.Groups['major'].Value,
    $semanticMatch.Groups['minor'].Value,
    $semanticMatch.Groups['patch'].Value,
    $buildNumber

$productionAppId = [guid]'BBC37307-15F1-4F00-8936-60BC06B5FAB5'
if ([string]::IsNullOrWhiteSpace($AppId)) {
    $resolvedAppId = if ($TestBuild) { [guid]::NewGuid() } else { $productionAppId }
} else {
    $parsedAppId = [guid]::Empty
    if (-not [guid]::TryParse($AppId, [ref]$parsedAppId)) {
        throw "AppId must be a GUID: $AppId"
    }
    $resolvedAppId = $parsedAppId
}
if ($TestBuild -and $resolvedAppId -eq $productionAppId) {
    throw 'A test installer cannot use the production AgentLatch AppId.'
}
if (-not $TestBuild -and $resolvedAppId -ne $productionAppId) {
    throw 'A production installer must use the stable AgentLatch AppId. Add -TestBuild for an isolated test installer.'
}
$appIdValue = '{{' + $resolvedAppId.ToString().ToUpperInvariant() + '}'
$outputBaseFilename = "AgentLatch-Setup-$Version-$Architecture" + $(if ($TestBuild) { '-test' } else { '' })

$setupScript = Join-Path $repoRoot 'installer\AgentLatch.iss'
$arguments = @(
    "/DAppVersion=$Version",
    "/DVersionInfoVersion=$versionInfoVersion",
    "/DArchitecture=$Architecture",
    "/DSourceExecutable=$Executable",
    "/DRepoRoot=$repoRoot",
    "/DOutputDirectory=$OutputDirectory",
    "/DAgentLatchAppIdValue=$appIdValue",
    "/DOutputBaseFilename=$outputBaseFilename",
    $setupScript
)
$process = Start-Process -FilePath $InnoCompiler -ArgumentList $arguments -Wait -PassThru -NoNewWindow
if ($process.ExitCode -ne 0) {
    throw "Inno Setup failed with exit code $($process.ExitCode)."
}

$installerPath = Join-Path $OutputDirectory "$outputBaseFilename.exe"
if (-not (Test-Path -LiteralPath $installerPath -PathType Leaf)) {
    throw "Expected installer was not created: $installerPath"
}
$hash = (Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash.ToLowerInvariant()
$hashLine = "$hash  $([System.IO.Path]::GetFileName($installerPath))"
[System.IO.File]::WriteAllText("$installerPath.sha256", $hashLine + [Environment]::NewLine, [System.Text.UTF8Encoding]::new($false))
if ($TestBuild) {
    [System.IO.File]::WriteAllText(
        "$installerPath.test-app-id",
        $resolvedAppId.ToString('D') + [Environment]::NewLine,
        [System.Text.UTF8Encoding]::new($false))
}
Write-Host "Created $installerPath"
Write-Host "SHA256 $hash"
