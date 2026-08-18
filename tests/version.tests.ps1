[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$AgentLatchPath,

    [Parameter(Mandatory)]
    [string]$ExpectedVersion
)

$ErrorActionPreference = 'Stop'
$resolvedPath = (Resolve-Path -LiteralPath $AgentLatchPath).Path
$versionInfo = (Get-Item -LiteralPath $resolvedPath).VersionInfo
$expectedNumeric = [version]("$ExpectedVersion.0")

if ($versionInfo.FileVersionRaw -ne $expectedNumeric) {
    throw "FileVersionRaw is $($versionInfo.FileVersionRaw); expected $expectedNumeric."
}
if ($versionInfo.ProductVersionRaw -ne $expectedNumeric) {
    throw "ProductVersionRaw is $($versionInfo.ProductVersionRaw); expected $expectedNumeric."
}
if ($versionInfo.FileVersion -ne $ExpectedVersion) {
    throw "FileVersion is '$($versionInfo.FileVersion)'; expected '$ExpectedVersion'."
}
if ($versionInfo.ProductVersion -ne $ExpectedVersion) {
    throw "ProductVersion is '$($versionInfo.ProductVersion)'; expected '$ExpectedVersion'."
}

Write-Host "Executable version metadata tests passed ($ExpectedVersion)."
