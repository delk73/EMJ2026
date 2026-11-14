# Canonical agent contract: Direct or Scut

Updated 2026-11-14. This root `AGENTS.md` is the canonical source of truth for agent architecture instructions. Read the active specification at the start of `DIRECT_OR_SCUT.md` and the latest entry in `DEVLOG.md`. Nested agent guidance supplements this contract; historical Slice/Chunk entries do not override it.

## Active architecture: Unified Shader-Driven Terminal & Unreliable AI Mediation

- Replace the permanent `4x2` eight-feed camera wall and verbose telemetry HUD with one full-viewport, custom-shader 1970s CRT terminal. MOTHER-7, the unreliable Ship AI, is the conversational mediator and optical director: splice live room feeds as inline `[OPTIC BUFFER]` blocks, raster wipes, and inline ECG/schematic shader widgets into the teletype stream. Retain room cameras as optical sources. P2-01 preserves the 4x2 layout only as the opt-in `DebugEightCamGrid` diagnostic view (`F9`, `debug_grid`, `/debug`); startup defaults to `UnifiedTerminal`. Toggling the debug view is presentation only and must never queue simulation commands or inspection changes.
- Keep `FDirectOrScutSimulation` / `UDirectOrScutSimSubsystem` authoritative at 30 Hz for physics, room state, command validation, and a rolling 15s optic buffer history. Presentation, voice timing, and model latency cannot advance or rewrite ground truth. Forensic scrub is read-only playback, never simulation rewind.
- `AI-LINK>` uses a hybrid small-LLM director with deterministic fallback. It emits validated structured `show_cam`, `show_widget`, `sim_action`, and `voice_blend` directives. Only accepted `sim_action` commands enter the bounded next-fixed-tick mutation path. Unreliable claims and curated optics belong to mediation, not simulation truth.
- Plan `FVoicePresentationState` progression `TextOnly` -> `HybridTextAndVoice` -> `VoiceOnly`. In the Act III fourth-wall transition MOTHER-7 stops teletype output and speaks solely via voice; operator input and necessary system feedback remain available.
- `Content/Loom/` is temporary reference only, never a runtime dependency. Complete Phase II — Stage 00 (`P2-00`): Reference Audit, Math Porting, & `Content/Loom/` Removal before closing P2-01. Prefer UE 5.9 native Slate and curve APIs; port only selected, corrected math into `SDirectOrScutWorkstationWidget.h/.cpp` and one owned `MD_UI` material, `/Game/DirectOrScut/Materials/M_DirectOrScut_Terminal`. Bind its parameters through `UMaterialInstanceDynamic`. This supersedes direct reuse of personal shader assets: no Loom functions, instances, curves, atlases, redirectors, or soft/hard references may survive the purge. See the Reference Gap Audit in `DIRECT_OR_SCUT.md`; its proposed signatures are implementation contracts, not existing APIs.
- Preserve deterministic room/crew consequences and the contradiction/override loop. `[1]`-`[5]` TUI inquiry probes and 15s forensic scrub let the operator challenge MOTHER-7. Subtle shader deception tells affect presentation only.

## Milestones and source discipline

Slice 1-14 is retired historical nomenclature for the completed multi-tile workstation baseline at Slice 14. All upcoming work uses these collision-free Phase II milestones; Stage numbers are planning labels only.

| Milestone | Scope |
| --- | --- |
| Phase II — Stage 00 (`P2-00`) | Reference Audit, Math Porting, & `Content/Loom/` Removal. Native-first implementation, owned math/material, dependency audit and purge gate before P2-01 completion. |
| Phase II — Stage 01 (`P2-01`) | Unified Phosphor Terminal, Custom CRT Shader Integration, & AI-Directed Inline `[OPTIC BUFFER]` Wipes, & Toggleable 8-Cam Debug View. Delivered and verified; normal startup is unified, diagnostics are opt-in. |
| Phase II — Stage 02 (`P2-02`) | Hybrid Small-LLM Tool-Calling Director, `[1]`-`[5]` TUI Inquiry Probes, 15s Forensic Buffer Scrub, & Contradiction/Override Loop. |
| Phase II — Stage 03 (`P2-03`) | Directional Room Acoustics, Deception Carrier Tells, & `TextOnly` -> `VoiceOnly` Act III Transition. |
| Phase II — Stage 04 (`P2-04`) | *PROJECT: GLASS HOUSE* Scenario Tuning, Endings, & Full Automation Verification. |

Zero numbered `.cpp` files: `DirectOrScutSlice*.cpp` and `DirectOrScutStage*.cpp`, including tests, are permanently banned. Keep runtime code in canonical domain `.h`/`.cpp` pairs; never create stage-specific implementation units. PIE regression coverage belongs in `DirectOrScutPIETest.cpp`, pure model coverage in `DirectOrScutSimulationTest.cpp`. Use domain-based test names.

Use `Source/EMJ2026/.clang-format`; keep lifecycle ownership explicit, fixed-step backlog retained, and rendering outside the deterministic model. For substantive implementation changes run `preflight.ps1` and `Scripts/Test-DirectOrScut.ps1`, inspect report counts, and distinguish an editor-open skip from a build pass. Preserve historical evidence and known legacy failures; do not claim Phase II implementation or verification from baseline results.

The earlier pivot recording pass was documentation only. Authorized Phase II implementation follows the responsibility matrix and acceptance criteria in `DIRECT_OR_SCUT.md`.
