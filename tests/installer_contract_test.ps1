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
if (-not $manifest.sha256 -or @($manifest.requiredFiles).Count -eq 0) {
    throw 'The local model integrity contract is incomplete.'
}

Write-Host 'Installer syntax, modern launcher, integrity checks, and cache contract are valid.'
