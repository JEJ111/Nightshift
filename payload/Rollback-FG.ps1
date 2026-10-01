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
if(Get-Process -Name 'Nivalis Nights' -ErrorAction SilentlyContinue) { throw 'Close the game before restoring the previous DLSS build.' }
function Contained([string]$relative) {
    $full=[IO.Path]::GetFullPath((Join-Path $GamePath $relative))
    if(-not $full.StartsWith($GamePath.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw "Path escapes game directory: $relative" }
    return $full
}
function Hash([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
$manifestPath=Contained 'nightshift-install.json'
if(-not(Test-Path -LiteralPath $manifestPath)) { throw 'Ownership manifest missing. No files changed.' }
$manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if($manifest.version -eq '0.3.4') { Write-Host 'The previous DLSS build is already installed.'; return }
if($manifest.version -notin @('0.4.0','0.5.0')) { throw 'This rollback supports NightShift 0.4.0 and 0.5.0 only.' }
$previousPlugin=Join-Path $PSScriptRoot 'rollback\NightShift.dll'
$previousHash='C21977C3F8773B7821B2D25A60DE81B46B22426E280136B121C44CC8D6999CF2'
if((Hash $previousPlugin) -ne $previousHash) { throw 'Previous DLSS plugin checksum mismatch.' }
$release=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'release.json') -Raw | ConvertFrom-Json
$fgPaths=@('dxgi.dll')+@($release.frameGenerationFiles | Where-Object name -ne 'dxgi.dll' | ForEach-Object { 'BepInEx\NightShift\framegen\runtime\'+$_.name })
$pluginRelative='BepInEx\plugins\NightShift\NightShift.dll'
$replace=@($manifest.pluginFiles | Where-Object { $_.path -eq $pluginRelative -or $_.path -in $fgPaths })
foreach($entry in $replace) {
    $target=Contained $entry.path
    if((Test-Path -LiteralPath $target) -and (Hash $target) -ne $entry.hash) { throw "Changed owned file preserved; rollback stopped before making changes: $target" }
}
$oldManifest=[IO.File]::ReadAllBytes($manifestPath)
$backups=@{}
foreach($entry in $replace) { $target=Contained $entry.path; if(Test-Path -LiteralPath $target) { $backups[$target]=[IO.File]::ReadAllBytes($target) } }
try {
    Copy-Item -LiteralPath $previousPlugin -Destination (Contained $pluginRelative) -Force
    foreach($entry in $replace | Where-Object path -in $fgPaths) { $target=Contained $entry.path; if(Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target } }
    $remaining=@($manifest.pluginFiles | Where-Object path -notin $fgPaths)
    foreach($entry in $remaining | Where-Object path -eq $pluginRelative) { $entry.hash=$previousHash }
    $restored=[pscustomobject]@{version='0.3.4';gameBuild=$manifest.gameBuild;pluginFiles=$remaining;loaderFiles=@($manifest.loaderFiles)}
    $restored | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    Write-Host 'NightShift 0.3.4 restored. The owned frame-generation proxy/runtime files were removed. Existing loader, logs, captures, configuration and saves were preserved.'
} catch {
    foreach($target in $backups.Keys) { [IO.File]::WriteAllBytes($target,$backups[$target]) }
    [IO.File]::WriteAllBytes($manifestPath,$oldManifest)
    throw
}
