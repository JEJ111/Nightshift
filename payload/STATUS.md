# NightShift 0.5.3 preview

October 12 compatibility release for public Steam build 25828494. Regenerated IL2CPP review resolved all 177 used API references and confirmed all 11 hook signatures. UnityPlayer and renderer implementation are unchanged; removed Singleton profiler helpers are unused by NightShift.

The player confirmed that all modes work and look correct. Fresh 0.5.3 logs show successful hook startup, DLAA GPU evaluation and 2x, 3x, 4x and Off selected in gameplay on RTX 5070 at 2560x1440.

All 36 installer/preservation checks passed, including upgrades and recovery from the actual published 0.5.0-r2, 0.5.1 and 0.5.2 artifacts. Native NGX and DXGI probes passed. Existing configuration, saves, settings, other mod files and caches were preserved during the actual upgrade.

This is a player smoke check, not a controlled FPS or physical-display pacing benchmark. Broader hardware, long sessions, scene transitions and moving-object/UI quality remain under review. The previously observed Unity shutdown fault is not claimed fixed.

See verification/0.5.3.json for current evidence. Older verification files are historical. DLAA starts on; frame generation starts off. F11 cycles supported modes, F5 toggles DLAA and F7 shows/hides the menu.
