[CmdletBinding()]
param([string]$GamePath)
$ErrorActionPreference='Stop'
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
$GamePath=(Resolve-Path -LiteralPath $GamePath).Path
$gameExecutable=[IO.Path]::GetFullPath((Join-Path $GamePath 'Nivalis Nights.exe'))
$runningGame=@(Get-Process -Name 'Nivalis Nights' -ErrorAction SilentlyContinue | Where-Object { -not $_.Path -or [string]::Equals($_.Path,$gameExecutable,[StringComparison]::OrdinalIgnoreCase) })
if($runningGame.Count -gt 0) { throw 'Close the game before restoring DLAA-only mode.' }
function Contained([string]$relative) {
    $full=[IO.Path]::GetFullPath((Join-Path $GamePath $relative))
    if(-not $full.StartsWith($GamePath.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw "Path escapes game directory: $relative" }
    $cursor=$full
    while($cursor -and $cursor.Length -ge $GamePath.Length) {
        if(Test-Path -LiteralPath $cursor) {
            $item=Get-Item -LiteralPath $cursor -Force
            if(($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Linked path is not an owned game directory. No files changed: $cursor" }
        }
        if([string]::Equals($cursor,$GamePath,[StringComparison]::OrdinalIgnoreCase)) { break }
        $cursor=Split-Path -Parent $cursor
    }
    return $full
}
function Hash([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
$manifestPath=Contained 'nightshift-install.json'
if(-not(Test-Path -LiteralPath $manifestPath)) { throw 'Ownership manifest missing. No files changed.' }
$manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if($manifest.version -notin @('0.4.0','0.5.0','0.5.1','0.5.2')) { throw 'This recovery supports NightShift 0.4.0, 0.5.0, 0.5.1 and 0.5.2 only.' }
$previousPlugin=Join-Path $PSScriptRoot 'rollback\NightShift.dll'
$release=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'release.json') -Raw | ConvertFrom-Json
$currentGameHash=Hash (Contained 'GameAssembly.dll')
$supportedBuild=@($release.supportedGameBuilds | Where-Object { $_.gameAssemblySha256 -eq $currentGameHash })
if($supportedBuild.Count -ne 1 -or (Hash (Contained 'UnityPlayer.dll')) -ne $release.unityPlayerSha256) { throw 'Game binary changed. Revalidate before recovery. No files changed.' }
$detectedGameBuild=$supportedBuild[0].build
$previousHash=$release.recoveryPluginSha256
if(-not $previousHash -or (Hash $previousPlugin) -ne $previousHash) { throw 'Recovery plugin checksum mismatch.' }
$fgPaths=@('dxgi.dll')+@($release.frameGenerationFiles | Where-Object name -ne 'dxgi.dll' | ForEach-Object { 'BepInEx\NightShift\framegen\runtime\'+$_.name })
$pluginRelative='BepInEx\plugins\NightShift\NightShift.dll'
$replace=@($manifest.pluginFiles | Where-Object { $_.path -eq $pluginRelative -or $_.path -in $fgPaths })
if(@($replace | Where-Object path -eq $pluginRelative).Count -ne 1) { throw 'Managed plugin ownership missing or ambiguous. No files changed.' }
foreach($entry in $replace) {
    $target=Contained $entry.path
    if((Test-Path -LiteralPath $target) -and (Hash $target) -ne $entry.hash) { throw "Changed owned file preserved; rollback stopped before making changes: $target" }
}
if($manifest.version -eq '0.5.2' -and $manifest.recoveryMode -eq 'DLAAOnly') {
    if($manifest.gameBuild -ne $detectedGameBuild) { $manifest.gameBuild=$detectedGameBuild; $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8 }
    Write-Host 'NightShift 0.5.2 DLAA-only recovery is already installed.'; return
}
$oldManifest=[IO.File]::ReadAllBytes($manifestPath)
$backups=@{}
foreach($entry in $replace) { $target=Contained $entry.path; if(Test-Path -LiteralPath $target) { $backups[$target]=[IO.File]::ReadAllBytes($target) } }
try {
    Copy-Item -LiteralPath $previousPlugin -Destination (Contained $pluginRelative) -Force
    foreach($entry in $replace | Where-Object path -in $fgPaths) { $target=Contained $entry.path; if(Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target } }
    $remaining=@($manifest.pluginFiles | Where-Object path -notin $fgPaths)
    foreach($entry in $remaining | Where-Object path -eq $pluginRelative) { $entry.hash=$previousHash }
    $restored=[pscustomobject]@{version='0.5.2';gameBuild=$detectedGameBuild;recoveryMode='DLAAOnly';pluginFiles=$remaining;loaderFiles=@($manifest.loaderFiles)}
    $restored | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    Write-Host 'NightShift 0.5.2 DLAA-only recovery installed. The owned frame-generation proxy/runtime files were removed. Existing loader, logs, captures, configuration and saves were preserved.'
} catch {
    foreach($target in $backups.Keys) { [IO.File]::WriteAllBytes($target,$backups[$target]) }
    [IO.File]::WriteAllBytes($manifestPath,$oldManifest)
    throw
}
