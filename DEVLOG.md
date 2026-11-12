# Attraction Puzzle Development Log

### [2026-11-12 09:01] — Baseline inspection

* **Intent:** Establish the actual greenfield baseline before implementation.
* **Changes Made:** Created `DEVLOG.md`. Inspected `EMJ2026.uproject`, `Source/EMJ2026/`, target files, and `Content/`. Existing runtime module contains only its entry point; Content has no files. Project targets UE 5.9; AllToolsets is enabled. Git working tree was clean before changes.
* **Verification Status:** `[VERIFIED IN CODE/COMPILE]` Filesystem inspection only. No UnrealEditor process found; latest editor log records clean shutdown. No live Unreal MCP tools are available in this session; no scene, unsaved asset, or PIE state was modified.
* **Next Immediate Step:** Implement and compile a self-contained node-and-tether greybox actor.

---

### [2026-11-12 09:03] — Foundational attraction simulation

* **Intent:** Provide a configurable continuous force-and-spring board for the first viewport test.
* **Changes Made:** Added `Source/EMJ2026/AttractionSimulation.h` and `.cpp`: `AAttractionSimulation`, `FAttractionNode`, `FAttractionBondRule`, and `FAttractionTether`. Includes local XY position/velocity, mass, pinned anchors, three-channel emit/receive signatures and cross-channel matrix, softened directional fields, bilateral capture radius, valence, unordered bond-type rules, relative-speed eligibility, pairing-specific damped springs and tensile break strain. Uses fixed 120 Hz semi-implicit integration with bounded catch-up, drag, acceleration/speed caps, seeded demo, sphere components, runtime cyan tethers/node labels, reset and Blueprint slice API. Cut/broken pairs remain suppressed until reset. Added `SIMULATION_TEST.md` for editor navigation, semantics and test cases. No maps or other assets were created.
* **Verification Status:** `[VERIFIED IN CODE/COMPILE]` UE 5.9 `Build.bat EMJ2026Editor Win64 Development D:/_source/EMJ2026/EMJ2026.uproject -WaitMutex -NoHotReloadFromIDE` succeeded, including UHT-generated reflection, C++ compilation and DLL link. Log: `Saved/Logs/AttractionBuild.log`. Initial sandboxed invocation exited 1 without diagnostics; authorized build with compiler/cache access succeeded (exit 0). Existing engine Fab/ToolsetRegistry dependency warning remains. `[UNVERIFIED - NEEDS VIEWPORT TEST]` Runtime motion, numerical stability, reconstruction and drawing have not been observed. Player drop/slice input, budgets, objectives and settle timer are outside this foundational slice.
* **Next Immediate Step:** Perform the seeded-demo viewport test in `SIMULATION_TEST.md`, recording anchor immobility, spring motion and finite behavior over 30 seconds.

---

### [2026-11-12 09:12] — Preflight and player interaction

* **Intent:** Make the greybox playable and provide a fast build/boot gate.
* **Changes Made:** Added `preflight.ps1` with engine discovery, editor-open skip, orphaned UBT cleanup/active-build protection, bounded build/boot processes, logs, and `-SkipSmokeTest`. Added `AttractionPlayerController.h/.cpp` containing controller and game mode; updated `Config/DefaultEngine.ini` to use that mode. Added `DropNode` to `AttractionSimulation.h/.cpp`. Controls: LMB drop, Tab cycle three coupling/valence profiles, RMB drag slice with orange feedback, R reset. Includes plane projection, top-down camera, ghost/profile text, temporary demo board when absent, and 128-node safety cap. Updated `SIMULATION_TEST.md`.
* **Verification Status:** `[VERIFIED IN CODE/COMPILE]` Ran `.\preflight.ps1`: UHT/C++/DLL build succeeded; NullRHI editor initialized and exited cleanly, exit 0. Logs: `Saved/Logs/PreflightBuild.log` and `PreflightSmoke.log`. Fixed PowerShell process exit-code handling during verification. Quit-only boot timed out at 120 seconds and was terminated; preserved `PreflightSmoke-QuitTimeout.log`. Final boot uses `-ExecCmds="Quit,CLOSE_SLATE_MAINFRAME"` because UE 5.9 requires the Slate editor close command. Existing Fab dependency and non-Win64 SDK warnings remain. `[UNVERIFIED - NEEDS VIEWPORT TEST]` Headless editor boot does not exercise gameplay/input. Manual instructions: File > Save All; Window > World Settings > GameMode Override = AttractionGameMode; Outliner > AttractionSimulation > Details > Transform = location (0,0,150), zero rotation, unit scale. Play dropdown > Selected Viewport > Play and focus viewport. Tab should change ghost/profile; LMB should add a moving sphere; RMB drag across cyan springs should remove them and draw orange swipes; R should restore six demo nodes. Full checklist is in `SIMULATION_TEST.md`.
* **Next Immediate Step:** Observe drop, swipe and reset in PIE for three cycles and 30 seconds, then record viewport results.

---

### [2026-11-12 09:55] — Liquid heightfield, soft forces and macro zoom

* **Intent:** Present the attraction system as a blended greyscale liquid with gentler motion and smooth tactical-to-macro zoom.
* **Changes Made:** Updated `AttractionSimulation.h/.cpp` for one flat plane, a per-frame 256x256 BGRA texture, compact droplet/filament height kernels, strain thinning, 0.22-second slice pinching, central-difference normals, diffuse/specular/rim/meniscus shading, Gaussian pull/cushion forces and viscous damping. Updated `AttractionPlayerController.h/.cpp` for FInterpTo zoom (600-4200 above board), current-camera perspective rays, greyscale overlays and Balanced/High-Pull/Repulsive Buffer force profiles. Added RHI dependency in `EMJ2026.Build.cs`, `Content/Materials/M_LiquidHeightfield.uasset`, `T_LiquidDefault.uasset`, and material-generation/setup-check scripts. Expanded `SIMULATION_TEST.md`; existing gym map unchanged.
* **Verification Status:** `[VERIFIED IN CODE/COMPILE]` Final `.\preflight.ps1` passed UHT/C++/link and clean NullRHI boot (exit 0; `Saved/Logs/PreflightBuild.log`, `PreflightSmoke.log`). Rendering-enabled material commandlet compiled/saved the unlit shader (exit 0; `LiquidMaterial.log`); corrected NullRHI compilation and sampler mismatch during setup. Read-only gym commandlet passed one-plane/material and six-node drop/reset checks (exit 0; `LiquidSetup.log`). No interactive editor process was running; no map saved. Existing Fab/non-Win64 SDK warnings remain. `[UNVERIFIED - NEEDS VIEWPORT TEST]` Liquid appearance, sloshing, pinching, render-upload performance and cursor alignment over the full zoom range have not been observed.
* **Next Immediate Step:** Open Content Browser > Maps > AttractionGym; World Settings > GameMode Override = AttractionGameMode; Play dropdown > Selected Viewport > Play. Follow the Slice 3 checklist in `SIMULATION_TEST.md`: test merging/highlights, RMB neck pinching, wheel zoom to 600/4200, and off-center drop/swipe alignment at 600/1800/4200. Use its scratch-level two-node fixture to verify strain thinning; record 30 seconds of viewport results.

---

### [2026-11-12 10:19] — Analytical GPU liquid renderer replacement

* **Intent:** Remove CPU bitmap aliasing and extend the neutral backdrop through maximum zoom.
* **Changes Made:** Replaced rasterization in `AttractionSimulation.h/.cpp` with occupied-row uploads to a nearest-filtered, non-sRGB, no-mip 256x3 RGBA32F primitive table and MID counts/extent parameters. Expanded the plane to 24000x24000 while retaining 1200-unit drop bounds. Regenerated `Content/Materials/M_LiquidHeightfield.uasset` using updated `Scripts/create_liquid_material.py` and new `LiquidMetaballs.hlsl`: analytical radial/capsule fields, exact gradients including variable-width pinch derivatives, greyscale lighting and fwidth edge coverage. Deferred material loading avoids the startup-rooted graph-edit assertion encountered during regeneration; backups are in `Saved/Backups`. Updated `check_liquid_setup.py`, added `check_liquid_gradients.py`, and revised `SIMULATION_TEST.md`. Physics, profiles, controls, gym map and existing startup-map settings were preserved. Old `T_LiquidDefault` is unused, retained without references from this renderer.
* **Verification Status:** `[VERIFIED IN CODE/COMPILE]` Rendering-enabled material regeneration, final `.\preflight.ps1` (compile + clean NullRHI boot), and read-only gym wiring/drop/reset checks passed, exit 0. Logs: `Saved/Logs/LiquidAnalyticalMaterial.log`, `PreflightBuild.log`, `PreflightSmoke.log`, `LiquidAnalyticalSetup.log`. Numerical derivative check passed 16000 comparisons, maximum absolute error 9.48e-11. Editor was closed before builds; no map was saved.
* **Verification Status:** `[UNVERIFIED - NEEDS VIEWPORT TEST]` Native-resolution edge quality, GPU data sampling/performance, highlights, pinching and backdrop coverage have not been observed in PIE.
* **Next Immediate Step:** Content Browser > Maps > AttractionGym; Play dropdown > Selected Viewport > Play. Inspect close-up edges at zoom 600 and all four corners at 4200; test merge/swipe/reset and off-center cursor alignment for 30 seconds using `SIMULATION_TEST.md`, then record viewport results.

---
