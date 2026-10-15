# NightShift 0.5.4

A community preview mod adding NVIDIA frame generation and native-resolution DLAA to Nivalis Nights.

## Install

Save and close the game, extract the entire ZIP, then double-click **Install NightShift.exe**. Launch normally through Steam and load your save. See **INSTALL.md** for the Steam-folder walkthrough and Windows settings.

| Key | Action |
| --- | --- |
| F11 | Off -> 2x -> 3x -> 4x -> Off, skipping unsupported hardware modes |
| F5 | Toggle native-resolution DLAA on/off |
| F7 | Show or hide the NightShift menu |

DLAA starts enabled for gameplay. Frame generation starts off each launch. Hiding the menu preserves the selected modes. DLSS upscaling is **coming soon**; Quality, Balanced and Performance are not selectable in this release.

Already have NightShift 0.5.0, 0.5.1, 0.5.2 or 0.5.3 installed? Save and close the game, extract 0.5.4 into a new folder, and run its installer over the existing installation. You do not need to uninstall first. Your configuration, saves, captures and generated caches are preserved. If an installed mod file was changed or belongs to another mod, the installer stops with an explanation.

## Hardware and game compatibility

DLAA requires NVIDIA GeForce RTX hardware. NVIDIA frame generation requires compatible RTX 40/50 series hardware, a supported driver, and Hardware-accelerated GPU scheduling. Multi-frame generation depends on the driver's reported capabilities; 3x/4x generally require RTX 50 series. The mod queries the actual maximum and never requests a higher mode.

The game continues rendering through **DirectX 11**. A game-local DXGI bridge presents frames through an independent D3D12 device on the same adapter.

This preview supports Steam build **25959175**, Unity **2020.3.49f1**. Earlier supported Unity 2020.3.44f1 pairs remain guarded separately; mixed game/engine pairs are refused. Installation and native bootstrap check the game binaries. A changed game build stops installation or disables the hooks until reviewed.

## Validation

October 15 compatibility release for public Steam build 25959175. Regenerated IL2CPP review resolved all 177 used API references and confirmed all 11 hook signatures. The game upgraded Unity from 2020.3.44f1 to 2020.3.49f1. Exact game/UnityPlayer pairing guards were updated in the installer, recovery, managed plugin and native proxy. Renderer hooks/interfaces remain valid; removed game debug helpers and texture overloads are unused by NightShift.

The player confirmed that all modes work and look correct. Fresh 0.5.4 logs show successful hook startup, DLAA GPU evaluation and 2x, 3x, 4x and Off selected in gameplay on RTX 5070 at 2560x1440.

All 39 installer/preservation checks passed, including upgrades and recovery from the actual published 0.5.0-r2, 0.5.1, 0.5.2 and 0.5.3 artifacts. Native NGX and DXGI probes passed. Existing configuration, saves, settings, other mod files and caches were preserved during the actual upgrade.

This is a player smoke check, not a controlled FPS or physical-display pacing benchmark. Broader hardware, long sessions, scene transitions and moving-object/UI quality remain under review. The previously observed Unity shutdown fault is not claimed fixed.

See STATUS.md and verification/0.5.4.json for current evidence.

## Removal and recovery

Close the game, then run **Tools/Uninstall NightShift.cmd**. Only unchanged files owned by this installer are removed. An owned loader is retained if other plugins need it.

**Tools/Restore Working DLSS.cmd** restores the matching 0.5.4 DLAA-only plugin, preserving its native DLSS bridge and removing the owned frame-generation files. Unknown or changed files stop rollback before changes.

## Source

Managed code is in src/; the DLAA and frame-generation bridges are in native/. The package includes Streamline headers and MinHook source needed by the frame-generation build, plus their license notices.

Managed build: .NET SDK, dotnet build src/NightShift.csproj -c Release. The project references the installed BepInEx 788 loader and its generated game bindings; these game-specific bindings are not redistributed.

Native build: x64 Visual Studio Developer PowerShell, Build-Native.ps1. This builds into its output directory without installing or launching the game.

The DLAA bridge and NVIDIA DLSS runtime are retained for native-resolution anti-aliasing. Legacy upscaling implementation remains in source for future work but is blocked from configuration and key selection.

## Support

NightShift is free. Optional support: https://paypal.me/examjaxx

NightShift is independent of ION LANDS, the publisher, and NVIDIA. Third-party components retain their own licenses; see third-party/.
