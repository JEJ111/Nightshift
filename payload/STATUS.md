# NightShift 0.5.1 preview

This compatibility release supports the October 2 Nivalis Nights Steam build **25680465**, retaining support for **25653325** and **25603526**. The menu now displays **0.5.1**.

## Confirmed for this release

- All 177 used game API references resolve in the regenerated bindings; all 11 hook signatures remain compatible. Rendering implementation is unchanged from the stable release.
- The player approved gameplay in Meridian Market on RTX 5070. 2x, 3x, 4x and Off were selected before the final card text correction; the exact final native bridge was retained. The final managed DLL loaded all hooks, showed the corrected card, and reported active DLAA and 2x generation with API/FG status zero.
- 32 installer checks passed, including a direct upgrade from the actual published 0.5.0-r2 files. Configuration, saves, captures and generated caches were preserved. Fresh install, repeat install, removal, recovery, changed/foreign file protection, running-game guards and path containment were checked.
- The final native bridges passed standalone NGX output/state checks and DXGI ordinary presentation, resize and release checks. These do not replace gameplay measurements.

## Preview limits

This is an integration and player smoke check, not a controlled FPS benchmark. Historical multi-frame standalone counts in release.json are identified as earlier renderer evidence. There is no new final-build all-mode capture. SDK-presented counts do not establish physical monitor pacing. Broader hardware, scene-transition, long-session and moving-object/UI quality testing remains ongoing.

A normal user exit recorded a UnityPlayer shutdown fault matching 18 earlier events dating to September 30. Its root cause is unproven; 0.5.1 does not claim to fix it.

See verification/0.5.1.json for the release evidence summary. DLAA starts on; frame generation starts off each launch. F11 cycles supported FG modes, F5 toggles DLAA and F7 shows/hides the menu. DLSS upscaling is coming soon.
