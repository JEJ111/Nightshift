[CmdletBinding()]
param([string]$GamePath, [switch]$Uninstall, [switch]$RemoveOwnedLoader, [switch]$DevelopmentTest)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$loaderHash = 'F4CC496BD098A0DF4164B81E3737297707F13A47C2478DBA2F60EEFAB784817A'
$coreHash = '46CF1EF802BDF8CB6587FC1E4D98F7AF1073A60487B39A64E230E6F6E4C23AEC'

if (-not $GamePath) {
    $steam = (Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam').SteamPath
    $libraries = @($steam)
    foreach ($line in Get-Content -LiteralPath (Join-Path $steam 'steamapps\libraryfolders.vdf')) {
        if ($line -match '"path"\s+"(.+?)"') { $libraries += $Matches[1].Replace('\\','\') }
    }
    foreach ($library in $libraries | Select-Object -Unique) {
        $candidate = Join-Path $library 'steamapps\common\Nivalis Nights'
        if (Test-Path -LiteralPath (Join-Path $candidate 'Nivalis Nights.exe')) { $GamePath = $candidate; break }
    }
}
if (-not $GamePath) { throw 'Nivalis Nights not found. Pass -GamePath with its installation folder.' }
$GamePath = (Resolve-Path -LiteralPath $GamePath).Path
if (-not (Test-Path -LiteralPath (Join-Path $GamePath 'Nivalis Nights.exe'))) { throw 'Not a Nivalis Nights installation.' }
$gameExecutable = [IO.Path]::GetFullPath((Join-Path $GamePath 'Nivalis Nights.exe'))
$runningGame = @(Get-Process -Name 'Nivalis Nights' -ErrorAction SilentlyContinue | Where-Object { -not $_.Path -or [string]::Equals($_.Path,$gameExecutable,[StringComparison]::OrdinalIgnoreCase) })
if ($runningGame.Count -gt 0) { throw 'Close the game before installing or removing NightShift.' }

function Resolve-Contained([string]$relative) {
    $full = [IO.Path]::GetFullPath((Join-Path $GamePath $relative))
    if (-not $full.StartsWith($GamePath.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "Path escapes game directory: $relative" }
    $cursor=$full
    while ($cursor -and $cursor.Length -ge $GamePath.Length) {
        if (Test-Path -LiteralPath $cursor) {
            $item=Get-Item -LiteralPath $cursor -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Linked path is not an owned game directory. No files changed: $cursor" }
        }
        if ([string]::Equals($cursor,$GamePath,[StringComparison]::OrdinalIgnoreCase)) { break }
        $cursor=Split-Path -Parent $cursor
    }
    return $full
}
function File-Hash([string]$path) { return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
$manifestPath = Resolve-Contained 'nightshift-install.json'
$pluginPath = Resolve-Contained 'BepInEx\plugins\NightShift\NightShift.dll'
$owned = $null
if (Test-Path -LiteralPath $manifestPath) { $owned = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json }
if ($Uninstall) {
    if (-not $owned) { throw 'No NightShift ownership manifest. Refusing to remove files that were not installed by this installer.' }
    foreach ($entry in @($owned.pluginFiles)+@($owned.loaderFiles)) { $null=Resolve-Contained $entry.path }
    foreach ($entry in $owned.pluginFiles) {
        $target = Resolve-Contained $entry.path
        if ((Test-Path -LiteralPath $target) -and (File-Hash $target) -eq $entry.hash) { Remove-Item -LiteralPath $target }
        elseif (Test-Path -LiteralPath $target) { Write-Warning "Preserving changed file: $target" }
    }
    $keepManifest = $true
    if ($RemoveOwnedLoader) {
        $otherMods = @(Get-ChildItem -LiteralPath (Resolve-Contained 'BepInEx\plugins') -Filter '*.dll' -File -Recurse -ErrorAction SilentlyContinue)
        $patchers = @(Get-ChildItem -LiteralPath (Resolve-Contained 'BepInEx\patchers') -Filter '*.dll' -File -Recurse -ErrorAction SilentlyContinue)
        if ($otherMods.Count -gt 0 -or $patchers.Count -gt 0) { throw 'NightShift removed. Other plugins/patchers remain, so their loader was preserved.' }
        # Only individually owned, unchanged files. Generated caches, captures,
        # saves, other mods and changed loader files are always preserved.
        $preserved = $false
        foreach ($entry in $owned.loaderFiles) {
            $target = Resolve-Contained $entry.path
            if ((Test-Path -LiteralPath $target) -and (File-Hash $target) -eq $entry.hash) { Remove-Item -LiteralPath $target }
            elseif (Test-Path -LiteralPath $target) { $preserved = $true; Write-Warning "Preserving changed loader file: $target" }
        }
        $keepManifest = $preserved
    }
    if (-not $keepManifest) { Remove-Item -LiteralPath $manifestPath }
    Write-Host 'NightShift removed. Saves, captures, configuration and generated caches were preserved.'
    return
}
$currentGameHash=File-Hash (Resolve-Contained 'GameAssembly.dll')
$releaseManifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'release.json') -Raw | ConvertFrom-Json
$supportedBuild = @($releaseManifest.supportedGameBuilds | Where-Object { $_.gameAssemblySha256 -eq $currentGameHash })
if ($supportedBuild.Count -ne 1) { throw 'This preview supports Steam builds 25603526, 25653325 and 25680465. The game binary has changed; revalidate before installing.' }
$detectedGameBuild=$supportedBuild[0].build
$conflict = @(Get-ChildItem -LiteralPath (Resolve-Contained 'BepInEx\plugins') -Filter '*Lumen*.dll' -Recurse -File -ErrorAction SilentlyContinue)
if ($conflict.Count -gt 0) { throw 'Lumen already installed. Use one renderer optimizer at a time.' }
$sourcePlugin = Join-Path $PSScriptRoot 'NightShift.dll'
$plannedPlugins = @(
    [pscustomobject]@{source=$sourcePlugin;path='BepInEx\plugins\NightShift\NightShift.dll';hash=$releaseManifest.pluginSha256},
    [pscustomobject]@{source=(Join-Path $PSScriptRoot 'native-runtime\NightShiftDLSS.dll');path='BepInEx\NightShift\native\NightShiftDLSS.dll';hash=$releaseManifest.nativeBridgeSha256},
    [pscustomobject]@{source=(Join-Path $PSScriptRoot 'native-runtime\nvngx_dlss.dll');path='BepInEx\NightShift\native\nvngx_dlss.dll';hash=$releaseManifest.dlssRuntimeSha256}
)
foreach ($file in $releaseManifest.frameGenerationFiles) {
    $relative = if ($file.name -eq 'dxgi.dll') { 'dxgi.dll' } else { 'BepInEx\NightShift\framegen\runtime\' + $file.name }
    $plannedPlugins += [pscustomobject]@{source=(Join-Path $PSScriptRoot ('framegen-runtime\' + $file.name));path=$relative;hash=$file.sha256}
}
if ((File-Hash (Resolve-Contained 'UnityPlayer.dll')) -ne $releaseManifest.unityPlayerSha256) { throw 'Unity renderer binary changed. Revalidate the presentation bridge before installing.' }
$pluginBackups = @{}
$previousManifest = $null
if (Test-Path -LiteralPath $manifestPath) { $previousManifest = [IO.File]::ReadAllBytes($manifestPath) }
foreach ($entry in $plannedPlugins) {
    if ((File-Hash $entry.source) -ne $entry.hash) { throw "Release checksum mismatch: $($entry.source)" }
    $target = Resolve-Contained $entry.path
    if (Test-Path -LiteralPath $target) {
        $previousEntry = @($owned.pluginFiles | Where-Object { $_.path -eq $entry.path })
        if ($previousEntry.Count -ne 1 -or (File-Hash $target) -ne $previousEntry[0].hash) { throw "Existing file is not owned or has changed. Preserving it: $target" }
        $pluginBackups[$target] = [IO.File]::ReadAllBytes($target)
    }
}

$loaderFiles = @()
if ($owned) { $loaderFiles = @($owned.loaderFiles) }
$corePath = Resolve-Contained 'BepInEx\core\BepInEx.Unity.IL2CPP.dll'
$stage = $null
$copied = @()
try {
    if (Test-Path -LiteralPath $corePath) {
        if ((File-Hash $corePath) -ne $coreHash) { throw 'Existing BepInEx version differs from tested build 788. Preserving it.' }
        Write-Host 'Using the existing BepInEx 788 loader.'
    } else {
        $loaderZip = Join-Path $PSScriptRoot 'BepInEx-788.zip'
        if ((File-Hash $loaderZip) -ne $loaderHash) { throw 'BepInEx archive checksum mismatch.' }
        $stage = Join-Path ([IO.Path]::GetTempPath()) ('NightShift-' + [guid]::NewGuid().ToString())
        Expand-Archive -LiteralPath $loaderZip -DestinationPath $stage
        $stagedFiles = @(Get-ChildItem -LiteralPath $stage -File -Recurse)
        $planned = @()
        foreach ($file in $stagedFiles) {
            $relative = $file.FullName.Substring($stage.Length + 1)
            $target = Resolve-Contained $relative
            if (Test-Path -LiteralPath $target) { throw "Loader file conflict; preserving your installation: $target" }
            $planned += [pscustomobject]@{ source=$file.FullName; path=$relative; hash=(File-Hash $file.FullName) }
        }
        foreach ($entry in $planned) {
            $target = Resolve-Contained $entry.path
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
            Copy-Item -LiteralPath $entry.source -Destination $target
            $copied += $target
            $loaderFiles += [pscustomobject]@{path=$entry.path;hash=$entry.hash}
        }
    }
    $pluginFiles = @()
    foreach ($entry in $plannedPlugins) {
        $target = Resolve-Contained $entry.path
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
        Copy-Item -LiteralPath $entry.source -Destination $target
        $copied += $target
        $pluginFiles += [pscustomobject]@{path=$entry.path;hash=(File-Hash $target)}
    }
    $manifest = [pscustomobject]@{version=$releaseManifest.version;gameBuild=$detectedGameBuild;pluginFiles=$pluginFiles;loaderFiles=$loaderFiles}
    $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    Write-Host 'NightShift 0.5.1 installed. F11: frame generation Off/2x/3x/4x. F5: DLAA on/off. F7: show/hide menu.'
    Write-Host 'DLAA starts ON in gameplay; frame generation starts OFF. Launch normally through Steam. DLSS upscaling is coming soon.'
} catch {
    # Roll back explicit owned updates as well as newly copied files.
    foreach ($path in $copied) {
        if ($pluginBackups.ContainsKey($path)) { [IO.File]::WriteAllBytes($path,$pluginBackups[$path]) }
        elseif (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path }
    }
    if ($previousManifest) { [IO.File]::WriteAllBytes($manifestPath,$previousManifest) }
    elseif (Test-Path -LiteralPath $manifestPath) { Remove-Item -LiteralPath $manifestPath }
    throw
} finally {
    if ($stage) {
        $resolvedStage = [IO.Path]::GetFullPath($stage)
        $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
        if ($resolvedStage.StartsWith($tempRoot,[StringComparison]::OrdinalIgnoreCase) -and (Split-Path -Leaf $resolvedStage).StartsWith('NightShift-')) {
            Remove-Item -LiteralPath $resolvedStage -Recurse -Force
        }
    }
}
