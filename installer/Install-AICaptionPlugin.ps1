[CmdletBinding()]
param(
    [string] $ObsPluginRoot = (Join-Path $env:ProgramData 'obs-studio\plugins')
)

$ErrorActionPreference = 'Stop'
$pluginName = 'ai-caption-plugin'
$packagePluginDirectory = $PSScriptRoot
$modelManifestPath = Join-Path $PSScriptRoot 'local-model.json'

function Get-NormalizedPath([string] $Path) {
    return [System.IO.Path]::GetFullPath($Path).TrimEnd([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar)
}

function Assert-ChildPath([string] $Path, [string] $Parent) {
    $normalPath = Get-NormalizedPath $Path
    $normalParent = Get-NormalizedPath $Parent
    if (-not $normalPath.StartsWith($normalParent + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Unsafe installation path: $normalPath"
    }
    return $normalPath
}

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-Sha256([string] $Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-RequiredModelFileMarkers([string] $ModelPath, $Manifest) {
    $fileMarkers = @()
    foreach ($relativePath in @($Manifest.requiredFiles)) {
        if ([string]::IsNullOrWhiteSpace([string] $relativePath)) {
            throw 'The local model manifest contains an invalid required file name.'
        }
        $filePath = Assert-ChildPath (Join-Path $ModelPath $relativePath) $ModelPath
        if (-not (Test-Path -LiteralPath $filePath -PathType Leaf)) {
            throw "The downloaded archive does not contain the required local model file: $relativePath"
        }
        $fileMarkers += [ordered]@{
            path = $relativePath
            sha256 = Get-Sha256 $filePath
        }
    }
    return $fileMarkers
}

function Test-InstalledModel([string] $ModelPath, $Manifest) {
    $markerPath = Join-Path $ModelPath '.ai-caption-model.json'
    if (-not (Test-Path -LiteralPath $markerPath -PathType Leaf)) {
        return $false
    }

    try {
        $marker = Get-Content -LiteralPath $markerPath -Raw | ConvertFrom-Json
        if ($marker.archiveSha256 -ne $Manifest.sha256) {
            return $false
        }

        $markerFiles = @($marker.files)
        $requiredFiles = @($Manifest.requiredFiles)
        if ($requiredFiles.Count -eq 0 -or $markerFiles.Count -ne $requiredFiles.Count) {
            return $false
        }

        foreach ($relativePath in $requiredFiles) {
            $filePath = Assert-ChildPath (Join-Path $ModelPath $relativePath) $ModelPath
            if (-not (Test-Path -LiteralPath $filePath -PathType Leaf)) {
                return $false
            }
            $fileMarker = @($markerFiles | Where-Object { $_.path -eq $relativePath })
            if ($fileMarker.Count -ne 1 -or $fileMarker[0].sha256 -ne (Get-Sha256 $filePath)) {
                return $false
            }
        }
        return $true
    }
    catch {
        return $false
    }
}

function Install-LocalModel([string] $PluginDirectory, $Manifest) {
    $modelsDirectory = Join-Path $PluginDirectory 'data\models'
    $modelDirectory = Assert-ChildPath (Join-Path $modelsDirectory $Manifest.modelDirectory) $PluginDirectory
    if (Test-InstalledModel $modelDirectory $Manifest) {
        Write-Host 'Быстрая локальная русская модель уже установлена и прошла проверку.'
        return
    }

    $tar = Get-Command tar.exe -ErrorAction SilentlyContinue
    if (-not $tar) {
        throw 'Windows tar.exe is required to unpack the local model. Update Windows and run the installer again.'
    }

    New-Item -ItemType Directory -Force -Path $modelsDirectory | Out-Null
    $downloadPath = Assert-ChildPath (Join-Path $modelsDirectory (".$($Manifest.archiveFile).download")) $PluginDirectory
    $extractRoot = Assert-ChildPath (Join-Path $modelsDirectory (".extract-" + [guid]::NewGuid().ToString('N'))) $PluginDirectory
    $extractedModelDirectory = Join-Path $extractRoot $Manifest.modelDirectory

    try {
        Write-Host 'Скачиваю быструю локальную русскую модель (около 128 МБ)...'
        Invoke-WebRequest -Uri $Manifest.url -OutFile $downloadPath
        if ((Get-Sha256 $downloadPath) -ne $Manifest.sha256) {
            throw 'Downloaded model SHA-256 does not match the signed release manifest.'
        }

        New-Item -ItemType Directory -Force -Path $extractRoot | Out-Null
        & $tar.Source -xjf $downloadPath -C $extractRoot
        if ($LASTEXITCODE -ne 0) {
            throw 'Could not unpack the downloaded local model.'
        }

        $marker = [ordered]@{
            schemaVersion = 2
            archiveSha256 = $Manifest.sha256
            files = @(Get-RequiredModelFileMarkers $extractedModelDirectory $Manifest)
        } | ConvertTo-Json
        Set-Content -LiteralPath (Join-Path $extractedModelDirectory '.ai-caption-model.json') -Value $marker -Encoding utf8

        if (Test-Path -LiteralPath $modelDirectory) {
            Remove-Item -LiteralPath $modelDirectory -Recurse -Force
        }
        Move-Item -LiteralPath $extractedModelDirectory -Destination $modelsDirectory
        if (-not (Test-InstalledModel $modelDirectory $Manifest)) {
            throw 'The installed local model did not pass its post-install integrity check.'
        }
        Write-Host 'Локальная модель установлена и проверена.'
    }
    finally {
        if (Test-Path -LiteralPath $downloadPath) {
            Remove-Item -LiteralPath $downloadPath -Force
        }
        if (Test-Path -LiteralPath $extractRoot) {
            Remove-Item -LiteralPath $extractRoot -Recurse -Force
        }
    }
}

if (-not (Test-Path -LiteralPath (Join-Path $packagePluginDirectory 'bin\64bit\ai-caption-plugin.dll') -PathType Leaf)) {
    throw "The package is incomplete. Extract the whole ZIP before running this installer. Missing: $packagePluginDirectory"
}
if (-not (Test-Path -LiteralPath $modelManifestPath -PathType Leaf)) {
    throw "The package is incomplete. Missing model manifest: $modelManifestPath"
}

$manifest = Get-Content -LiteralPath $modelManifestPath -Raw | ConvertFrom-Json
if (-not ($manifest.url -and $manifest.sha256 -and $manifest.modelDirectory -and $manifest.archiveFile -and @($manifest.requiredFiles).Count)) {
    throw 'The local model manifest is invalid.'
}

$defaultPluginRoot = Get-NormalizedPath (Join-Path $env:ProgramData 'obs-studio\plugins')
$requestedPluginRoot = Get-NormalizedPath $ObsPluginRoot
if ($requestedPluginRoot -eq $defaultPluginRoot -and -not (Test-Administrator)) {
    Write-Host 'Запрашиваю права администратора для установки плагина в OBS...'
    $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $PSCommandPath, '-ObsPluginRoot', $requestedPluginRoot)
    $process = Start-Process -FilePath 'powershell.exe' -Verb RunAs -ArgumentList $arguments -PassThru -Wait
    exit $process.ExitCode
}

$runningObs = Get-Process -Name 'obs64', 'obs' -ErrorAction SilentlyContinue
if ($runningObs) {
    throw 'Close OBS completely before installing or updating AI Caption Plugin.'
}

New-Item -ItemType Directory -Force -Path $requestedPluginRoot | Out-Null
$pluginDirectory = Assert-ChildPath (Join-Path $requestedPluginRoot $pluginName) $requestedPluginRoot
$stagingDirectory = Assert-ChildPath (Join-Path $requestedPluginRoot (".$pluginName.staging-" + [guid]::NewGuid().ToString('N'))) $requestedPluginRoot
$backupDirectory = Assert-ChildPath (Join-Path $requestedPluginRoot (".$pluginName.backup-" + [guid]::NewGuid().ToString('N'))) $requestedPluginRoot
$backupCreated = $false
$existingModelReusable = $false

try {
    New-Item -ItemType Directory -Force -Path $stagingDirectory | Out-Null
    Copy-Item -Path (Join-Path $packagePluginDirectory '*') -Destination $stagingDirectory -Recurse -Force

    $existingModelDirectory = Join-Path $pluginDirectory (Join-Path 'data\models' $manifest.modelDirectory)
    if (Test-Path -LiteralPath $existingModelDirectory -PathType Container) {
        $existingModelReusable = Test-InstalledModel $existingModelDirectory $manifest
        if ($existingModelReusable) {
            Write-Host 'Найдена проверенная локальная модель; она будет сохранена при обновлении.'
        }
        $stagingModelsDirectory = Join-Path $stagingDirectory 'data\models'
        New-Item -ItemType Directory -Force -Path $stagingModelsDirectory | Out-Null
        Copy-Item -LiteralPath $existingModelDirectory -Destination (Join-Path $stagingModelsDirectory $manifest.modelDirectory) -Recurse -Force
    }

    if (Test-Path -LiteralPath $pluginDirectory) {
        Move-Item -LiteralPath $pluginDirectory -Destination $backupDirectory
        $backupCreated = $true
    }
    Move-Item -LiteralPath $stagingDirectory -Destination $pluginDirectory

    $installedModelDirectory = Join-Path $pluginDirectory (Join-Path 'data\models' $manifest.modelDirectory)
    if ($existingModelReusable) {
        if (-not (Test-InstalledModel $installedModelDirectory $manifest)) {
            throw 'The existing local model could not be preserved during the plugin update.'
        }
        Write-Host 'Сохраняю проверенную локальную модель при обновлении.'
    }
    else {
        Install-LocalModel $pluginDirectory $manifest
    }

    if ($backupCreated -and (Test-Path -LiteralPath $backupDirectory)) {
        Remove-Item -LiteralPath $backupDirectory -Recurse -Force
    }
    Write-Host 'AI Caption Plugin обновлён. Быстрая русская модель установлена и выбрана автоматически; Google API не требуется.'
}
catch {
    if ($backupCreated -and (Test-Path -LiteralPath $backupDirectory)) {
        if (Test-Path -LiteralPath $pluginDirectory) {
            Remove-Item -LiteralPath $pluginDirectory -Recurse -Force
        }
        Move-Item -LiteralPath $backupDirectory -Destination $pluginDirectory
    }
    elseif (Test-Path -LiteralPath $pluginDirectory) {
        Remove-Item -LiteralPath $pluginDirectory -Recurse -Force
    }
    throw
}
finally {
    if (Test-Path -LiteralPath $stagingDirectory) {
        Remove-Item -LiteralPath $stagingDirectory -Recurse -Force
    }
}
