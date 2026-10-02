# NightShift 0.5.1

NVIDIA frame generation and native-resolution DLAA for **Nivalis Nights**. This is the source for the 0.5.1 preview, for Steam build 25680465.

**[Download NightShift and installation guide](https://nightshift-nivalis.its-jaxx.chatgpt.site)**

## Install and play

Download the ZIP from the website. Close the game, right-click the ZIP and choose **Extract All**, then open the extracted folder and double-click **Install NightShift.exe**. Launch the game normally through Steam and load your save.

Already have NightShift 0.5.0 installed? Save and close the game, extract 0.5.1 into a new folder, and run its installer over the existing installation. You do not need to uninstall first. Your configuration, saves, captures and generated caches are preserved. If an installed mod file was changed or belongs to another mod, the installer stops with an explanation.

| Key | Action |
| --- | --- |
| **F11** | Cycle Off -> 2x -> 3x -> 4x -> Off, skipping unsupported modes |
| **F5** | Toggle native-resolution DLAA |
| **F7** | Show or hide the NightShift menu |

DLAA starts on. Frame generation starts off each launch. Hiding the menu keeps your selected modes running. **DLSS upscaling is coming soon.**

This preview supports Steam builds **25680465**, **25653325** and **25603526**, using Unity **2020.3.44f1**. The installer and plugin check the game binaries before enabling hooks. Frame generation modes depend on the GPU and driver capabilities reported by NVIDIA. See [installation requirements](payload/INSTALL.md) and [validation status](payload/STATUS.md).

## Source layout

The folders follow the download's layout so the installer and its support scripts can be reviewed together.

| Path | Contents |
| --- | --- |
| `payload/src/` | BepInEx IL2CPP plugin, DLAA integration, frame-generation inputs, controls, and diagnostics |
| `payload/native/` | D3D11/D3D12 presentation bridge, NGX bridge for DLAA, and standalone probes |
| `payload/installer-source/` | Native Windows installer launcher, icon, and manifest |
| `payload/install.ps1` | Installation, ownership checks, and removal |
| `payload/Rollback-FG.ps1` | Recovery to the matching 0.5.1 DLAA-only build |
| `payload/third-party/` | Streamline headers, MinHook source, and license notices |
| `payload/release.json` | Manifest for the tested 0.5.1 download |
| `Tools/` | Download-package installation, removal, and recovery entry points |

The download includes compiled files and runtime dependencies. A source checkout does not include those binaries or the game-generated assemblies; use the website ZIP for installation. The release manifest records the shipping binaries, so a locally rebuilt DLL will need a freshly generated manifest before it can be packaged with the installer.

## Build

Use Windows x64 with:

- .NET SDK 8 or newer, with support for targeting .NET 6.
- Visual Studio 2022 C++ Build Tools, the Windows SDK, and x64 MASM.
- BepInEx **6.0.0-be.788** installed for your own Nivalis Nights copy. Launch it once to generate the IL2CPP bindings in `BepInEx/interop`.
- The [NVIDIA DLSS SDK](https://github.com/NVIDIA/DLSS) for building the NGX/DLAA bridge. The shipping build used commit `374959484e79a640feaba44c93ac8cfb0a03f5b5`. Obtain its headers and libraries separately under NVIDIA's license.

### Managed plugin

```powershell
dotnet build .\payload\src\NightShift.csproj -c Release
```

The project defaults to the usual Steam installation path. For another installation, pass the path to its **BepInEx** folder:

```powershell
dotnet build .\payload\src\NightShift.csproj -c Release '-p:BepRoot=D:\SteamLibrary\steamapps\common\Nivalis Nights\BepInEx'
```

The result is `payload/src/bin/Release/net6.0/NightShift.dll`.

### Native bridges

In an **x64 Visual Studio Developer PowerShell**, from the repository root:

```powershell
.\payload\Build-Native.ps1 -DlssSdkDirectory 'C:\SDKs\DLSS'
```

This builds `dxgi.dll`, the frame-generation presentation probe, and `NightShiftDLSS.dll` under `payload/build/native`. Without `-DlssSdkDirectory`, it builds the frame-generation bridge and probe only. The scripts compile files without installing anything or launching the game or probes.

The game stays on **DirectX 11**. The frame-generation bridge uses a separate D3D12 presentation device on the same adapter.

### Installer launcher

From the same developer shell:

```powershell
.\payload\installer-source\Build-Launcher.ps1
```

The executable is written to `payload/installer-source/build/Install NightShift.exe`. A complete download package also needs the managed DLL, native bridges, BepInEx loader, NVIDIA runtime files, and the rollback payload described by `payload/release.json`.

## What's next

The long-term goal is to improve native game performance while preserving the crowds and visual quality. DLSS upscaling is planned; broader profiling of crowded scenes, NPC updates, rendering work, and frame pacing will guide future optimizations. Frame generation's SDK-presented FPS is separate from the game's rendered FPS and physical display measurements.

## License and support

NightShift's original code is licensed under [MIT](LICENSE). Third-party components retain their own licenses in [payload/third-party](payload/third-party).

NightShift is free. If it helps, you can [buy me a coffee](https://paypal.me/examjaxx).

An independent community mod, unaffiliated with ION LANDS, the publisher, or NVIDIA.
