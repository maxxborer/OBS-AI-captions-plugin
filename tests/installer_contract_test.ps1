[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$installerPath = Join-Path $projectRoot 'installer\Install-AICaptionPlugin.ps1'
$launcherPath = Join-Path $projectRoot 'installer\Install-AICaptionPlugin.cmd'
$manifestPath = Join-Path $projectRoot 'installer\local-model.json'

$parseErrors = $null
$tokens = $null
[System.Management.Automation.Language.Parser]::ParseFile(
    $installerPath,
    [ref] $tokens,
    [ref] $parseErrors
) | Out-Null
if ($parseErrors.Count -gt 0) {
    $parseErrors | ForEach-Object { [Console]::Error.WriteLine($_.Message) }
    throw 'Install-AICaptionPlugin.ps1 contains syntax errors.'
}

$launcher = Get-Content -LiteralPath $launcherPath -Raw
$requiredLauncherFragments = @(
    'pwsh.exe',
    '-NoProfile',
    '"%~dp0Install-AICaptionPlugin.ps1"',
    '%*'
)
foreach ($fragment in $requiredLauncherFragments) {
    if (-not $launcher.Contains($fragment)) {
        throw "Install-AICaptionPlugin.cmd is missing required fragment: $fragment"
    }
}
if ($launcher.Contains('powershell.exe') -or $launcher.Contains('-ExecutionPolicy')) {
    throw 'The installer launcher still contains the retired Windows PowerShell compatibility path.'
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ([long] $manifest.archiveBytes -le 0 -or [long] $manifest.archiveBytes -gt 1GB) {
    throw 'The model archive must fit in the 1 GiB download cache.'
}
if (-not $manifest.sha256 -or @($manifest.requiredFiles).Count -eq 0 -or -not $manifest.requiredFileSha256) {
    throw 'The local model integrity contract is incomplete.'
}

$engineSource = Get-Content -LiteralPath (Join-Path $projectRoot 'lib\caption_stream\SherpaTOneCaptionEngine.cpp') -Raw
foreach ($requiredFile in @($manifest.requiredFiles)) {
    $expectedHash = $manifest.requiredFileSha256.PSObject.Properties[[string] $requiredFile].Value
    if (-not $expectedHash -or -not $engineSource.Contains([string] $expectedHash)) {
        throw "The installer and runtime integrity contracts differ for: $requiredFile"
    }
}

$installer = Get-Content -LiteralPath $installerPath -Raw
foreach ($fragment in @(
    "Join-Path `$env:ProgramData 'obs-studio\plugins'",
    'Assert-NoReparsePoint',
    'Assert-SafeTree',
    "Join-Path `$env:SystemRoot 'System32\tar.exe'"
)) {
    if (-not $installer.Contains($fragment)) {
        throw "The hardened installer is missing required fragment: $fragment"
    }
}
if ($installer.Contains("Verb = 'runas'") -or $installer.Contains('Get-Command tar.exe')) {
    throw 'The installer still contains an unsafe elevation or PATH-based compatibility path.'
}

Write-Host 'Installer syntax, modern launcher, integrity checks, and cache contract are valid.'
