[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$workflowDirectory = Join-Path $projectRoot '.github\workflows'
$workflows = @(
    Get-ChildItem -LiteralPath $workflowDirectory -File |
        Where-Object { $_.Extension -in @('.yml', '.yaml') }
)
if ($workflows.Count -ne 1) {
    throw "Exactly one CI/release workflow is expected; found $($workflows.Count)."
}

$workflow = Get-Content -LiteralPath $workflows[0].FullName -Raw
if ([regex]::IsMatch($workflow, '(?m)uses:\s+[^\r\n]+@v\d+\s*(?:#.*)?$')) {
    throw 'Every GitHub Action must be pinned to a full commit SHA.'
}
foreach ($fragment in @(
    'persist-credentials: false',
    'github/codeql-action/init@',
    'github/codeql-action/analyze@',
    'build-mode: none',
    'security-events: write',
    'cancel-in-progress: true',
    "hashFiles('buildspec.json', 'cmake/common/FindSherpaOnnx.cmake')"
)) {
    if (-not $workflow.Contains($fragment)) {
        throw "The CI security/performance contract is missing: $fragment"
    }
}
if ($workflow.Contains('tags:')) {
    throw 'Tag pushes must not launch a duplicate release build.'
}

$buildspec = Get-Content -LiteralPath (Join-Path $projectRoot 'buildspec.json') -Raw | ConvertFrom-Json
foreach ($dependency in @('obs-studio', 'prebuilt', 'qt6')) {
    $hash = [string] $buildspec.dependencies.$dependency.hashes.'windows-x64'
    if ($hash -notmatch '^[0-9a-f]{64}$') {
        throw "Dependency $dependency is not pinned by SHA-256."
    }
}

$dependencySetup = Get-Content -LiteralPath (Join-Path $projectRoot 'cmake\common\buildspec_common.cmake') -Raw
if (-not $dependencySetup.Contains('file(SHA256 "${dependencies_dir}/${file}" cached_hash)')) {
    throw 'Cached OBS dependency archives must be re-verified before extraction.'
}

foreach ($requiredFile in @(
    'LICENSE',
    'NOTICE.md',
    'THIRD_PARTY_NOTICES.md',
    'SECURITY.md',
    'third_party\licenses\Apache-2.0.txt',
    'third_party\licenses\MIT-onnxruntime.txt'
)) {
    if (-not (Test-Path -LiteralPath (Join-Path $projectRoot $requiredFile) -PathType Leaf)) {
        throw "Required OSS/security file is missing: $requiredFile"
    }
}

Write-Host 'Repository, dependency, workflow, and OSS contracts are valid.'
