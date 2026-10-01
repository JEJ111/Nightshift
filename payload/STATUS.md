# NightShift 0.5.0 preview

F11 cycles supported NVIDIA Off/2x/3x/4x modes, F5 toggles DLAA, and F7 shows/hides the menu. DLAA starts enabled for gameplay; FG starts off. DLSS upscaling is coming soon.

## Confirmed

- Existing in-game NVIDIA 2x implementation produced thousands of generated frames in crowded gameplay.
- Updated bridge: 180 rendered frames -> 356 SDK presents at 2x (startup), 540 at 3x, 720 at 4x. API/FG status = 0.
- Active-to-active multiplier changes and return to Off, shared textures, resize and resource release passed.
- Fresh installation, upgrade, rollback and removal passed in an isolated fixture; foreign DXGI preserved.

## Still limited

This preview is bound to the tested Steam/Unity binaries. Higher multiplier tests use the exact bridge in a standalone workload, not a controlled street benchmark. SDK-presented counts do not establish physical monitor pacing. Broader hardware, long-session, visual quality and game-update testing remain ongoing.

Source and recovery tooling are included.
