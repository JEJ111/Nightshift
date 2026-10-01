# NightShift 0.5.0

A community preview mod adding NVIDIA frame generation and native-resolution DLAA to Nivalis Nights.

## Install

Save and close the game, extract the entire ZIP, then double-click **Install NightShift.exe**. Launch normally through Steam and load your save. See **INSTALL.md** for the Steam-folder walkthrough and Windows settings.

| Key | Action |
| --- | --- |
| F11 | Off -> 2x -> 3x -> 4x -> Off, skipping unsupported hardware modes |
| F5 | Toggle native-resolution DLAA on/off |
| F7 | Show or hide the NightShift menu |

DLAA starts enabled for gameplay. Frame generation starts off each launch. Hiding the menu preserves the selected modes. DLSS upscaling is **coming soon**; Quality, Balanced and Performance are not selectable in this release.

## Hardware and game compatibility

DLAA requires NVIDIA GeForce RTX hardware. NVIDIA frame generation requires compatible RTX 40/50 series hardware, a supported driver, and Hardware-accelerated GPU scheduling. Multi-frame generation depends on the driver's reported capabilities; 3x/4x generally require RTX 50 series. The mod queries the actual maximum and never requests a higher mode.

The game continues rendering through **DirectX 11**. A game-local DXGI bridge presents frames through an independent D3D12 device on the same adapter.

This preview targets Steam builds **25653325** and **25603526**, Unity **2020.3.44f1**. Installation and native bootstrap check the game binaries. A changed game build stops installation or disables the hooks until reviewed.

## Validation

The October 1 game update (25653325) was checked against regenerated IL2CPP bindings. DLAA GPU evaluations and all three FG mode activations were confirmed in Meridian Market; a snapshot recorded 14,627 generated frames with API/FG status zero. The snapshot itself was taken while rendering was suspended, so its instantaneous FPS is not a mode benchmark.

- NVIDIA 2x frame generation was confirmed in crowded gameplay in NightShift 0.4.0; the player confirmed markedly smoother presentation.
- The exact updated DXGI bridge on RTX 5070 produced **356 SDK-presented frames from 180 rendered frames at 2x** (four frames lost during startup), **540 from 180 at 3x**, and **720 from 180 at 4x**.
- All tested mode transitions, return to Off, resize, resource release and API/FG status checks passed. The standalone test is not a game benchmark or physical-display pacing measurement.
- Fresh installation, upgrade, rollback and full removal passed in an isolated fixture. Unknown DXGI files were preserved.
- The menu separates rendered FPS from NVIDIA SDK-presented FPS.
- Broader hardware, scene-transition, long-session, shutdown and visual-quality validation remains ongoing. This is a preview release.

## Removal and recovery

Close the game, then run **Tools/Uninstall NightShift.cmd**. Only unchanged files owned by this installer are removed. An owned loader is retained if other plugins need it.

**Tools/Restore Working DLSS.cmd** restores the bundled earlier 0.3.4 DLSS-only plugin, preserving its native DLSS bridge and removing the owned frame-generation files. Unknown or changed files stop rollback before changes.

## Source

Managed code is in src/; the DLAA and frame-generation bridges are in native/. The package includes Streamline headers and MinHook source needed by the frame-generation build, plus their license notices.

Managed build: .NET SDK, dotnet build src/NightShift.csproj -c Release. The project references the installed BepInEx 788 loader and its generated game bindings; these game-specific bindings are not redistributed.

Native build: x64 Visual Studio Developer PowerShell, Build-Native.ps1. This builds into its output directory without installing or launching the game.

The DLAA bridge and NVIDIA DLSS runtime are retained for native-resolution anti-aliasing. Legacy upscaling implementation remains in source for future work but is blocked from configuration and key selection.

## Support

NightShift is free. Optional support: https://paypal.me/examjaxx

NightShift is independent of ION LANDS, the publisher, and NVIDIA. Third-party components retain their own licenses; see third-party/.

Installer revision 2: the main Install NightShift.exe application has a NightShift icon and checks extraction. The support files are in payload/ and removal/recovery tools are in Tools/. The verified game plugin and rendering binaries remain version 0.5.0.
