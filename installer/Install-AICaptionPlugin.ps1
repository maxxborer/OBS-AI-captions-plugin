[CmdletBinding()]
param(
    [string] $ObsPluginRoot = (Join-Path $env:ProgramData 'obs-studio\plugins')
)

$ErrorActionPreference = 'Stop'
$pluginName = 'ai-caption-plugin'
$packagePluginDirectory = $PSScriptRoot
$modelManifestPath = Join-Path $PSScriptRoot 'local-model.json'
$cacheLimitBytes = 1GB

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

function Assert-NoReparsePoint([string] $Path) {
    $normalPath = Get-NormalizedPath $Path
    $root = [System.IO.Path]::GetPathRoot($normalPath)
    $current = $root
    foreach ($segment in $normalPath.Substring($root.Length).Split(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.StringSplitOptions]::RemoveEmptyEntries
    )) {
        $current = Join-Path $current $segment
        if (-not (Test-Path -LiteralPath $current)) {
            continue
        }
        $item = Get-Item -LiteralPath $current -Force
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Refusing to use a reparse point during installation: $current"
        }
    }
    return $normalPath
}

function Assert-SafeTree([string] $Path) {
    $safeRoot = Assert-NoReparsePoint $Path
    if (-not (Test-Path -LiteralPath $safeRoot -PathType Container)) {
        return $safeRoot
    }
    foreach ($item in Get-ChildItem -LiteralPath $safeRoot -Force -Recurse) {
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Refusing to traverse a reparse point during installation: $($item.FullName)"
        }
    }
    return $safeRoot
}

function Remove-SafeTree([string] $Path) {
    if (Test-Path -LiteralPath $Path) {
        $safePath = Assert-SafeTree $Path
        Remove-Item -LiteralPath $safePath -Recurse -Force
    }
}

function Get-Sha256([string] $Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Limit-DownloadCache(
    [string] $CacheDirectory,
    [long] $MaximumBytes,
    [string] $ProtectedPath = ''
) {
    if (-not (Test-Path -LiteralPath $CacheDirectory -PathType Container)) {
        return
    }

    $protected = if ([string]::IsNullOrWhiteSpace($ProtectedPath)) { '' } else { Get-NormalizedPath $ProtectedPath }
    $files = @(Get-ChildItem -LiteralPath $CacheDirectory -File | Sort-Object LastWriteTimeUtc)
    [long] $total = ($files | Measure-Object Length -Sum).Sum
    foreach ($file in $files) {
        if ($total -le $MaximumBytes) {
            break
        }
        $safePath = Assert-ChildPath $file.FullName $CacheDirectory
        Assert-NoReparsePoint $safePath | Out-Null
        if ($protected -and (Get-NormalizedPath $safePath) -eq $protected) {
            continue
        }
        Remove-Item -LiteralPath $safePath -Force
        $total -= $file.Length
    }

    if ($total -gt $MaximumBytes) {
        throw "Download cache exceeds the hard limit of $MaximumBytes bytes."
    }
}

function Get-RequiredModelFileMarkers([string] $ModelPath, $Manifest) {
    $fileMarkers = @()
    foreach ($relativePath in @($Manifest.requiredFiles)) {
        if ([string]::IsNullOrWhiteSpace([string] $relativePath)) {
            throw 'The local model manifest contains an invalid required file name.'
        }
        $filePath = Assert-ChildPath (Join-Path $ModelPath $relativePath) $ModelPath
        Assert-NoReparsePoint $filePath | Out-Null
        if (-not (Test-Path -LiteralPath $filePath -PathType Leaf)) {
            throw "The downloaded archive does not contain the required local model file: $relativePath"
        }
        $expectedHash = $Manifest.requiredFileSha256.PSObject.Properties[[string] $relativePath].Value
        $actualHash = Get-Sha256 $filePath
        if (-not $expectedHash -or $actualHash -ne $expectedHash) {
            throw "The downloaded local model file failed its integrity check: $relativePath"
        }
        $fileMarkers += [ordered]@{
            path = $relativePath
            sha256 = $actualHash
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
            Assert-NoReparsePoint $filePath | Out-Null
            if (-not (Test-Path -LiteralPath $filePath -PathType Leaf)) {
                return $false
            }
            $fileMarker = @($markerFiles | Where-Object { $_.path -eq $relativePath })
            $expectedHash = $Manifest.requiredFileSha256.PSObject.Properties[[string] $relativePath].Value
            if (-not $expectedHash -or
                $fileMarker.Count -ne 1 -or
                $fileMarker[0].sha256 -ne $expectedHash -or
                (Get-Sha256 $filePath) -ne $expectedHash) {
                return $false
            }
        }
        return $true
    }
    catch {
        return $false
    }
}

function Install-LocalModel(
    [string] $PluginDirectory,
    [string] $CacheDirectory,
    [long] $MaximumCacheBytes,
    $Manifest
) {
    $modelsDirectory = Join-Path $PluginDirectory 'data\models'
    $modelDirectory = Assert-ChildPath (Join-Path $modelsDirectory $Manifest.modelDirectory) $PluginDirectory
    if (Test-InstalledModel $modelDirectory $Manifest) {
        Write-Host 'Быстрая локальная русская модель уже установлена и прошла проверку.'
        return
    }

    $tarPath = Join-Path $env:SystemRoot 'System32\tar.exe'
    if (-not (Test-Path -LiteralPath $tarPath -PathType Leaf)) {
        throw 'Windows tar.exe is required to unpack the local model. Update Windows and run the installer again.'
    }
    Assert-NoReparsePoint $tarPath | Out-Null

    if ([long] $Manifest.archiveBytes -le 0 -or [long] $Manifest.archiveBytes -gt $MaximumCacheBytes) {
        throw 'The model archive does not fit within the 1 GiB download-cache limit.'
    }

    New-Item -ItemType Directory -Force -Path $modelsDirectory | Out-Null
    New-Item -ItemType Directory -Force -Path $CacheDirectory | Out-Null
    $cacheName = "$($Manifest.sha256)-$($Manifest.archiveFile)"
    $cachedArchive = Assert-ChildPath (Join-Path $CacheDirectory $cacheName) $CacheDirectory
    $downloadPath = Assert-ChildPath (Join-Path $CacheDirectory (".download-" + [guid]::NewGuid().ToString('N'))) $CacheDirectory
    $extractRoot = Assert-ChildPath (Join-Path $modelsDirectory (".extract-" + [guid]::NewGuid().ToString('N'))) $PluginDirectory
    $extractedModelDirectory = Join-Path $extractRoot $Manifest.modelDirectory

    try {
        Limit-DownloadCache $CacheDirectory $MaximumCacheBytes $cachedArchive
        if (Test-Path -LiteralPath $cachedArchive -PathType Leaf) {
            Assert-NoReparsePoint $cachedArchive | Out-Null
            if ((Get-Item -LiteralPath $cachedArchive).Length -eq [long] $Manifest.archiveBytes -and
                (Get-Sha256 $cachedArchive) -eq $Manifest.sha256) {
                (Get-Item -LiteralPath $cachedArchive).LastWriteTimeUtc = [DateTime]::UtcNow
                Write-Host 'Использую проверенную модель из локального кэша.'
            }
            else {
                Remove-Item -LiteralPath $cachedArchive -Force
            }
        }

        if (-not (Test-Path -LiteralPath $cachedArchive -PathType Leaf)) {
            Limit-DownloadCache $CacheDirectory ($MaximumCacheBytes - [long] $Manifest.archiveBytes)
            Write-Host "Скачиваю локальную модель '$($Manifest.name)'..."
            Invoke-WebRequest -Uri $Manifest.url -OutFile $downloadPath
            if ((Get-Item -LiteralPath $downloadPath).Length -ne [long] $Manifest.archiveBytes -or
                (Get-Sha256 $downloadPath) -ne $Manifest.sha256) {
                throw 'Downloaded model does not match the signed release manifest.'
            }
            Move-Item -LiteralPath $downloadPath -Destination $cachedArchive
            Limit-DownloadCache $CacheDirectory $MaximumCacheBytes $cachedArchive
        }

        New-Item -ItemType Directory -Force -Path $extractRoot | Out-Null
        & $tarPath -xjf $cachedArchive -C $extractRoot
        if ($LASTEXITCODE -ne 0) {
            throw 'Could not unpack the downloaded local model.'
        }
        Assert-SafeTree $extractRoot | Out-Null

        $marker = [ordered]@{
            schemaVersion = 2
            archiveSha256 = $Manifest.sha256
            files = @(Get-RequiredModelFileMarkers $extractedModelDirectory $Manifest)
        } | ConvertTo-Json
        Set-Content -LiteralPath (Join-Path $extractedModelDirectory '.ai-caption-model.json') -Value $marker -Encoding utf8

        if (Test-Path -LiteralPath $modelDirectory) {
            Remove-SafeTree $modelDirectory
        }
        Move-Item -LiteralPath $extractedModelDirectory -Destination $modelsDirectory
        if (-not (Test-InstalledModel $modelDirectory $Manifest)) {
            throw 'The installed local model did not pass its post-install integrity check.'
        }
        Write-Host "Локальная модель '$($Manifest.name)' установлена и проверена."
    }
    finally {
        if (Test-Path -LiteralPath $downloadPath -PathType Leaf) {
            Remove-Item -LiteralPath $downloadPath -Force
        }
        if (Test-Path -LiteralPath $extractRoot) {
            Remove-SafeTree $extractRoot
        }
    }
}

if (-not (Test-Path -LiteralPath (Join-Path $packagePluginDirectory 'bin\64bit\ai-caption-plugin.dll') -PathType Leaf)) {
    throw "The package is incomplete. Extract the whole ZIP before running this installer. Missing: $packagePluginDirectory"
}
if (-not (Test-Path -LiteralPath $modelManifestPath -PathType Leaf)) {
    throw "The package is incomplete. Missing model manifest: $modelManifestPath"
}

$manifestDocument = Get-Content -LiteralPath $modelManifestPath -Raw | ConvertFrom-Json
$manifests = @($manifestDocument.models)
if ($manifests.Count -eq 0) {
    throw 'The local model manifest does not contain any models.'
}
foreach ($manifest in $manifests) {
    if (-not ($manifest.id -and $manifest.name -and $manifest.url -and $manifest.archiveBytes -and $manifest.sha256 -and $manifest.modelDirectory -and $manifest.archiveFile -and @($manifest.requiredFiles).Count -and $manifest.requiredFileSha256)) {
        throw 'The local model manifest is invalid.'
    }
}

$requestedPluginRoot = Get-NormalizedPath $ObsPluginRoot
$cacheDirectory = Assert-ChildPath (Join-Path $requestedPluginRoot '.ai-caption-plugin-cache') $requestedPluginRoot

$runningObs = Get-Process -Name 'obs64', 'obs' -ErrorAction SilentlyContinue
if ($runningObs) {
    throw 'Close OBS completely before installing or updating AI Caption Plugin.'
}

New-Item -ItemType Directory -Force -Path $requestedPluginRoot | Out-Null
Assert-NoReparsePoint $requestedPluginRoot | Out-Null
Assert-SafeTree $packagePluginDirectory | Out-Null
$pluginDirectory = Assert-ChildPath (Join-Path $requestedPluginRoot $pluginName) $requestedPluginRoot
$stagingDirectory = Assert-ChildPath (Join-Path $requestedPluginRoot (".$pluginName.staging-" + [guid]::NewGuid().ToString('N'))) $requestedPluginRoot
$backupDirectory = Assert-ChildPath (Join-Path $requestedPluginRoot (".$pluginName.backup-" + [guid]::NewGuid().ToString('N'))) $requestedPluginRoot
$backupCreated = $false
$existingModelsReusable = @{}

try {
    New-Item -ItemType Directory -Force -Path $stagingDirectory | Out-Null
    Copy-Item -Path (Join-Path $packagePluginDirectory '*') -Destination $stagingDirectory -Recurse -Force

    foreach ($manifest in $manifests) {
        $existingModelDirectory = Join-Path $pluginDirectory (Join-Path 'data\models' $manifest.modelDirectory)
        if (Test-Path -LiteralPath $existingModelDirectory -PathType Container) {
            Assert-SafeTree $existingModelDirectory | Out-Null
            $existingModelsReusable[$manifest.id] = Test-InstalledModel $existingModelDirectory $manifest
            if ($existingModelsReusable[$manifest.id]) {
                Write-Host "Найдена проверенная модель '$($manifest.name)'; она будет сохранена при обновлении."
                $stagingModelsDirectory = Join-Path $stagingDirectory 'data\models'
                New-Item -ItemType Directory -Force -Path $stagingModelsDirectory | Out-Null
                Copy-Item -LiteralPath $existingModelDirectory -Destination (Join-Path $stagingModelsDirectory $manifest.modelDirectory) -Recurse -Force
            }
        }
    }

    if (Test-Path -LiteralPath $pluginDirectory) {
        Assert-SafeTree $pluginDirectory | Out-Null
        Move-Item -LiteralPath $pluginDirectory -Destination $backupDirectory
        $backupCreated = $true
    }
    Move-Item -LiteralPath $stagingDirectory -Destination $pluginDirectory

    foreach ($manifest in $manifests) {
        $installedModelDirectory = Join-Path $pluginDirectory (Join-Path 'data\models' $manifest.modelDirectory)
        if ($existingModelsReusable[$manifest.id]) {
            if (-not (Test-InstalledModel $installedModelDirectory $manifest)) {
                throw 'The existing local model could not be preserved during the plugin update.'
            }
            Write-Host "Сохраняю проверенную модель '$($manifest.name)' при обновлении."
        }
        else {
            Install-LocalModel $pluginDirectory $cacheDirectory $cacheLimitBytes $manifest
        }
    }

    if ($backupCreated -and (Test-Path -LiteralPath $backupDirectory)) {
        Remove-SafeTree $backupDirectory
    }
    Write-Host 'AI Caption Plugin обновлён. Локальные модели установлены и доступны в настройках.'
}
catch {
    if ($backupCreated -and (Test-Path -LiteralPath $backupDirectory)) {
        if (Test-Path -LiteralPath $pluginDirectory) {
            Remove-SafeTree $pluginDirectory
        }
        Move-Item -LiteralPath $backupDirectory -Destination $pluginDirectory
    }
    elseif (Test-Path -LiteralPath $pluginDirectory) {
        Remove-SafeTree $pluginDirectory
    }
    throw
}
finally {
    if (Test-Path -LiteralPath $stagingDirectory) {
        Remove-SafeTree $stagingDirectory
    }
}
