param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$binaryDirectory = if ($Configuration -eq 'Debug') {
    Join-Path $projectRoot 'CatalogSpawner\bin\Debug'
} else {
    Join-Path $projectRoot 'CatalogSpawner\bin'
}
$binary = Join-Path $binaryDirectory 'CatalogSpawner.asi'
$stageDirectory = Join-Path $projectRoot 'stage\CatalogSpawner'
$packageDirectory = Join-Path $projectRoot 'dist'
$packageDataDirectory = Join-Path $packageDirectory 'CatalogSpawner'
$archive = Join-Path $packageDirectory 'CatalogSpawner.zip'

if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) {
    throw "Build output not found: $binary"
}

New-Item -ItemType Directory -Force -Path $packageDirectory | Out-Null
New-Item -ItemType Directory -Force -Path $packageDataDirectory | Out-Null
$legacyPackageBinary = Join-Path $packageDirectory 'GTAVCatalogSpawner.asi'
if (Test-Path -LiteralPath $legacyPackageBinary) {
    Remove-Item -LiteralPath $legacyPackageBinary -Force
}
Copy-Item -LiteralPath $binary -Destination (Join-Path $packageDirectory 'CatalogSpawner.asi') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'CATALOG_CONFIGURATION.zh-CN.md') -Destination $packageDataDirectory -Force
Copy-Item -Path (Join-Path $stageDirectory '*') -Destination $packageDataDirectory -Recurse -Force
if (Test-Path -LiteralPath $archive) {
    Remove-Item -LiteralPath $archive -Force
}
$archiveItems = @($packageDataDirectory, (Join-Path $packageDirectory 'CatalogSpawner.asi'))
Compress-Archive -LiteralPath $archiveItems -DestinationPath $archive

Write-Output "Package prepared at $packageDirectory"
