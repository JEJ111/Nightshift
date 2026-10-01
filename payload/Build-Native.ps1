[CmdletBinding()]
param(
    [string]$OutputDirectory=(Join-Path $PSScriptRoot 'build\native'),
    [string]$DlssSdkDirectory
)
$ErrorActionPreference='Stop'
if(-not(Get-Command cl.exe -ErrorAction SilentlyContinue) -or -not(Get-Command ml64.exe -ErrorAction SilentlyContinue)) {
    throw 'Run in an x64 Visual Studio Developer PowerShell with the C++ toolchain and Windows SDK.'
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
$source=Join-Path $PSScriptRoot 'native'
$minhook=Join-Path $PSScriptRoot 'third-party\MinHook'
$streamline=Join-Path $PSScriptRoot 'third-party\Streamline\include'
if($DlssSdkDirectory) {
    $DlssSdkDirectory=[IO.Path]::GetFullPath($DlssSdkDirectory)
    $ngxInclude=Join-Path $DlssSdkDirectory 'include'
    $ngxLibrary=Join-Path $DlssSdkDirectory 'lib\Windows_x86_64\x64\nvsdk_ngx_s.lib'
    if(-not(Test-Path -LiteralPath (Join-Path $ngxInclude 'nvsdk_ngx.h')) -or -not(Test-Path -LiteralPath $ngxLibrary)) {
        throw 'DlssSdkDirectory must contain the NVIDIA DLSS SDK include directory and x64 nvsdk_ngx_s.lib.'
    }
}
$objects=@()
Push-Location $OutputDirectory
try {
    & ml64.exe /nologo /c "/Fo$OutputDirectory\Forwarders.obj" (Join-Path $source 'Forwarders.asm')
    if($LASTEXITCODE) { throw 'Forwarder assembly failed.' }
    $objects+=Join-Path $OutputDirectory 'Forwarders.obj'
    foreach($relative in @('buffer.c','hook.c','trampoline.c','hde\hde64.c')) {
        $name=[IO.Path]::GetFileNameWithoutExtension($relative)
        $object=Join-Path $OutputDirectory ($name+'.obj')
        & cl.exe /nologo /O2 /MT /c ("/I"+(Join-Path $minhook 'include')) ("/Fo:"+$object) (Join-Path (Join-Path $minhook 'src') $relative)
        if($LASTEXITCODE) { throw "MinHook compilation failed: $relative" }
        $objects+=$object
    }
    $sources=@('Session.cpp','Bridge.cpp','Proxy.cpp') | ForEach-Object { Join-Path $source $_ }
    & cl.exe /nologo /std:c++17 /O2 /EHsc /MT /LD ("/I"+$streamline) ("/I"+(Join-Path $minhook 'include')) @sources @objects "/Fe:$OutputDirectory\dxgi.dll" /link ("/DEF:"+(Join-Path $source 'dxgi.def')) d3d11.lib d3d12.lib d3dcompiler.lib wintrust.lib crypt32.lib bcrypt.lib user32.lib
    if($LASTEXITCODE) { throw 'Presentation bridge link failed.' }
    & cl.exe /nologo /std:c++17 /O2 /EHsc /MT ("/I"+$streamline) (Join-Path $source 'ProxyProbe.cpp') "/Fe:$OutputDirectory\ProxyProbe.exe" /link d3d11.lib user32.lib
    if($LASTEXITCODE) { throw 'Proxy test build failed.' }
    if($DlssSdkDirectory) {
        & cl.exe /nologo /std:c++17 /O2 /EHsc /MT /LD ("/I"+$ngxInclude) (Join-Path $source 'NightShiftDLSS.cpp') "/Fe:$OutputDirectory\NightShiftDLSS.dll" /link $ngxLibrary d3d11.lib dxgi.lib version.lib wintrust.lib crypt32.lib advapi32.lib shell32.lib shlwapi.lib ole32.lib user32.lib uuid.lib
        if($LASTEXITCODE) { throw 'NGX/DLAA bridge build failed.' }
        Write-Host "Built $OutputDirectory\NightShiftDLSS.dll."
    }
    Write-Host "Built $OutputDirectory\dxgi.dll and ProxyProbe.exe. Building does not install or launch anything."
} finally { Pop-Location }
