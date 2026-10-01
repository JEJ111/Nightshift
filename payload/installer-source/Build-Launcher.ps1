[CmdletBinding()]
param([string]$OutputDirectory=(Join-Path $PSScriptRoot 'build'))
$ErrorActionPreference='Stop'
if(-not(Get-Command cl.exe -ErrorAction SilentlyContinue) -or -not(Get-Command rc.exe -ErrorAction SilentlyContinue)) { throw 'Use an x64 Visual Studio Developer PowerShell with the Windows SDK.' }
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
Push-Location $PSScriptRoot
try {
    & rc.exe /nologo /fo (Join-Path $OutputDirectory 'Launcher.res') 'Launcher.rc'
    if($LASTEXITCODE) { throw 'Resource build failed.' }
    & cl.exe /nologo /std:c++17 /O2 /W4 /EHsc /MT 'Launcher.cpp' (Join-Path $OutputDirectory 'Launcher.res') ("/Fo:"+(Join-Path $OutputDirectory 'Launcher.obj')) ("/Fe:"+(Join-Path $OutputDirectory 'Install NightShift.exe')) /link /MANIFEST:NO user32.lib
    if($LASTEXITCODE) { throw 'Installer application build failed.' }
} finally { Pop-Location }
