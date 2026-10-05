# NightShift 0.5.2 preview

This compatibility release supports the October 5 Nivalis Nights public Steam build **25738165**, retaining support for **25680465**, **25653325** and **25603526**. The menu displays **0.5.2**.

## Confirmed for this release

- Fresh IL2CPP regeneration and review resolved all 177 used game API references and confirmed all 11 hook signatures and referenced top-level layouts. UnityPlayer and renderer implementation are unchanged from 0.5.1. The existing binary checks remain fail closed.
- The player confirmed the image looks great. The exact final build loaded its hooks, evaluated DLAA successfully, and selected 2x, 3x, 4x and Off in gameplay on RTX 5070 at 2560x1440.
- All 34 installer/preservation checks passed: fresh install, upgrades and recovery from the actual published 0.5.0-r2 and 0.5.1 artifacts, repeat install, uninstall, recovery, changed/foreign file protection, running-game guards, path containment and retention of saves/settings/captures/generated caches.
- The final native bridges passed NGX output, dimension, repeated initialization/shutdown and graphics-state restoration checks, plus DXGI ordinary presentation, resize and release checks. These standalone probes do not replace gameplay measurements.

## Preview limits

This is a player smoke check, not a controlled FPS or physical-display pacing benchmark. Historical multi-frame standalone counts in release.json are earlier renderer evidence. The player reported roughly 60-70 FPS with frame generation off after the game patch; this is an uncontrolled observation and no NightShift 0.5.2 FPS gain is claimed.

Broader hardware, long sessions, scene transitions, moving-object/UI quality and shutdown behavior remain under review. A previously observed Unity shutdown fault is not claimed fixed.

See verification/0.5.2.json for current evidence. Historical verification/0.5.1.json concerns the previous release. DLAA starts on; frame generation starts off. F11 cycles supported frame-generation modes, F5 toggles DLAA and F7 shows/hides the menu. DLSS upscaling is coming soon.
