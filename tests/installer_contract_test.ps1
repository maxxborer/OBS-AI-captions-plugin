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
    'set "AI_CAPTION_INSTALLER_SCRIPT=%~dp0Install-AICaptionPlugin.ps1"',
    'Unblock-File -LiteralPath $env:AI_CAPTION_INSTALLER_SCRIPT',
    '-File "%AI_CAPTION_INSTALLER_SCRIPT%"',
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

$launcherProbeRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
    'ai-caption-plugin-launcher-test-' + [Guid]::NewGuid().ToString('N')
)
$probeLauncherPath = Join-Path $launcherProbeRoot 'Install-AICaptionPlugin.cmd'
$probeScriptPath = Join-Path $launcherProbeRoot 'Install-AICaptionPlugin.ps1'
$probeMarkerPath = Join-Path $launcherProbeRoot 'executed.txt'
$previousProcessPolicy = $env:PSExecutionPolicyPreference

try {
    New-Item -ItemType Directory -Path $launcherProbeRoot | Out-Null
    Copy-Item -LiteralPath $launcherPath -Destination $probeLauncherPath
    [System.IO.File]::WriteAllText(
        $probeScriptPath,
        @'
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string] $MarkerPath)
[System.IO.File]::WriteAllText($MarkerPath, 'executed')
'@
    )
    Set-Content -LiteralPath $probeScriptPath -Stream Zone.Identifier -Value "[ZoneTransfer]`r`nZoneId=3"

    $env:PSExecutionPolicyPreference = 'RemoteSigned'
    & $probeLauncherPath -MarkerPath $probeMarkerPath
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $probeMarkerPath -PathType Leaf)) {
        throw 'The launcher could not run an internet-marked package under RemoteSigned.'
    }
    if (@(Get-Item -LiteralPath $probeScriptPath -Stream Zone.Identifier -ErrorAction SilentlyContinue).Count -ne 0) {
        throw 'The launcher did not remove the internet zone marker from its installer script.'
    }
} finally {
    $env:PSExecutionPolicyPreference = $previousProcessPolicy
    if (Test-Path -LiteralPath $launcherProbeRoot) {
        Remove-Item -LiteralPath $launcherProbeRoot -Recurse -Force
    }
}

$manifestDocument = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$manifests = @($manifestDocument.models)
if ($manifests.Count -lt 2) {
    throw 'The local model manifest must include T-One and Nemotron.'
}
$engineSource = Get-Content -LiteralPath (Join-Path $projectRoot 'lib\caption_stream\SherpaTOneCaptionEngine.cpp') -Raw
foreach ($manifest in $manifests) {
    if ([long] $manifest.archiveBytes -le 0 -or [long] $manifest.archiveBytes -gt 1GB) {
        throw 'Every model archive must fit in the 1 GiB download cache.'
    }
    if (-not $manifest.id -or -not $manifest.sha256 -or @($manifest.requiredFiles).Count -eq 0 -or -not $manifest.requiredFileSha256) {
        throw 'The local model integrity contract is incomplete.'
    }
    foreach ($requiredFile in @($manifest.requiredFiles)) {
        $expectedHash = $manifest.requiredFileSha256.PSObject.Properties[[string] $requiredFile].Value
        if (-not $expectedHash -or -not $engineSource.Contains([string] $expectedHash)) {
            throw "The installer and runtime integrity contracts differ for: $($manifest.id)/$requiredFile"
        }
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
