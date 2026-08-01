[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$installerPath = Join-Path $projectRoot 'installer\Install-AICaptionPlugin.ps1'
$launcherPath = Join-Path $projectRoot 'installer\Install-AICaptionPlugin.cmd'

$installerBytes = [System.IO.File]::ReadAllBytes($installerPath)
if ($installerBytes.Length -lt 3 -or
    $installerBytes[0] -ne 0xEF -or
    $installerBytes[1] -ne 0xBB -or
    $installerBytes[2] -ne 0xBF) {
    throw 'Install-AICaptionPlugin.ps1 must use UTF-8 with BOM for Windows PowerShell 5.1 compatibility.'
}

$windowsPowerShell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
if (-not (Test-Path -LiteralPath $windowsPowerShell -PathType Leaf)) {
    throw "Windows PowerShell was not found at $windowsPowerShell"
}

$escapedInstallerPath = $installerPath.Replace("'", "''")
$parseCommand = @"
`$parseErrors = `$null
`$tokens = `$null
[System.Management.Automation.Language.Parser]::ParseFile('$escapedInstallerPath', [ref]`$tokens, [ref]`$parseErrors) | Out-Null
if (`$parseErrors.Count -gt 0) {
    `$parseErrors | ForEach-Object { [Console]::Error.WriteLine(`$_.Message) }
    exit 1
}
"@

& $windowsPowerShell -NoLogo -NoProfile -ExecutionPolicy Bypass -Command $parseCommand
if ($LASTEXITCODE -ne 0) {
    throw 'Install-AICaptionPlugin.ps1 does not parse in Windows PowerShell 5.1.'
}

$launcher = Get-Content -LiteralPath $launcherPath -Raw
$requiredLauncherFragments = @(
    'powershell.exe',
    '-NoProfile',
    '-ExecutionPolicy Bypass',
    '"%~dp0Install-AICaptionPlugin.ps1"',
    '%*'
)

foreach ($fragment in $requiredLauncherFragments) {
    if (-not $launcher.Contains($fragment)) {
        throw "Install-AICaptionPlugin.cmd is missing required fragment: $fragment"
    }
}

Write-Host 'Installer is compatible with Windows PowerShell 5.1 and has a policy-safe launcher.'
