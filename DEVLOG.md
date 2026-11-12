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
