# NightShift 0.5.2

1. Save and close Nivalis Nights.
2. In File Explorer, right-click the downloaded ZIP and choose **Extract All**, then click **Extract**. Open the newly extracted folder.
3. Open the extracted NightShift-0.5.2 folder and double-click **Install NightShift.exe**.
4. Launch the game through Steam. The first launch can take longer while the included BepInEx loader creates its game bindings.
5. Load your save. **F11** cycles frame generation; **F5** toggles DLAA; **F7** hides or shows the menu.

DLAA starts enabled in gameplay. Frame generation starts off each launch. DLSS upscaling is coming soon.

The installer normally finds Steam automatically. If it does not, use Steam -> Nivalis Nights -> Manage -> Browse local files. Drag the game folder onto Install NightShift.exe, or pass its full path to install.ps1 using -GamePath. If access to the game directory is denied, run the installer as administrator.

Already have NightShift 0.5.0 or 0.5.1 installed? Save and close the game, extract 0.5.2 into a new folder, and run its installer over the existing installation. You do not need to uninstall first. Your configuration, saves, captures and generated caches are preserved. If an installed mod file was changed or belongs to another mod, the installer stops with an explanation.

## Requirements

- Windows with an NVIDIA GeForce RTX GPU for DLAA.
- RTX 40/50 series for NVIDIA frame generation; 3x/4x need supported multi-frame-generation hardware, typically RTX 50 series.
- A supported NVIDIA driver and Hardware-accelerated GPU scheduling enabled.
- Nivalis Nights Steam builds 25738165, 25680465, 25653325 and 25603526, DirectX 11. Game files are checked by their SHA-256 hashes before installation.
- An otherwise standard rendering setup. Unknown or modified mod files are preserved and cause the installer to stop rather than overwrite them.

To enable GPU scheduling: Windows Settings -> System -> Display -> Graphics -> Default graphics settings. Restart Windows if requested.

## Removal and recovery

Close the game, then double-click **Tools/Uninstall NightShift.cmd**. Only unchanged files owned by NightShift are removed. Other mods, generated files, and saves are preserved.

**Tools/Restore Working DLSS.cmd** restores the matching 0.5.2 DLAA-only plugin and removes the owned frame-generation bridge. F5 toggles DLAA; frame generation is unavailable until NightShift is installed again.

This is an independent community preview, unaffiliated with ION LANDS, the publisher, or NVIDIA. Broader hardware, game-update, scene-transition and visual-quality testing remains ongoing.
