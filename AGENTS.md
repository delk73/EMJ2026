# Canonical agent contract: Direct or Scut

Updated 2026-11-14. This root `AGENTS.md` is the canonical source of truth for agent architecture instructions. Read the active specification at the start of `DIRECT_OR_SCUT.md` and the latest entry in `DEVLOG.md`. Nested agent guidance supplements this contract; historical Slice/Chunk entries do not override it.

## Active architecture: Unified Shader-Driven Terminal & Unreliable AI Mediation

- Replace the permanent `4x2` eight-feed camera wall and verbose telemetry HUD with one full-viewport, custom-shader 1970s CRT terminal. MOTHER-7, the unreliable Ship AI, is the conversational mediator and optical director: splice live room feeds as inline `[OPTIC BUFFER]` blocks, raster wipes, and inline ECG/schematic shader widgets into the teletype stream. Retain room cameras as optical sources rather than a permanent grid.
- Keep `FDirectOrScutSimulation` / `UDirectOrScutSimSubsystem` authoritative at 30 Hz for physics, room state, command validation, and a rolling 15s optic buffer history. Presentation, voice timing, and model latency cannot advance or rewrite ground truth. Forensic scrub is read-only playback, never simulation rewind.
- `AI-LINK>` uses a hybrid small-LLM director with deterministic fallback. It emits validated structured `show_cam`, `show_widget`, `sim_action`, and `voice_blend` directives. Only accepted `sim_action` commands enter the bounded next-fixed-tick mutation path. Unreliable claims and curated optics belong to mediation, not simulation truth.
- Plan `FVoicePresentationState` progression `TextOnly` -> `HybridTextAndVoice` -> `VoiceOnly`. In the Act III fourth-wall transition MOTHER-7 stops teletype output and speaks solely via voice; operator input and necessary system feedback remain available.
- Reuse existing personal CRT, scanline, wipe, and distortion shader assets through UI-domain `UMaterialInstanceDynamic` parameters. Inspect and bind the available assets during implementation; do not invent asset paths or rewrite these shaders from scratch.
- Preserve deterministic room/crew consequences and the contradiction/override loop. `[1]`-`[5]` TUI inquiry probes and 15s forensic scrub let the operator challenge MOTHER-7. Subtle shader deception tells affect presentation only.

## Milestones and source discipline

Slice 1-14 is retired historical nomenclature for the completed multi-tile workstation baseline at Slice 14. All upcoming work uses these collision-free Phase II milestones; Stage numbers are planning labels only.

| Milestone | Scope |
| --- | --- |
| Phase II — Stage 01 (`P2-01`) | Unified Phosphor Terminal, Custom CRT Shader Integration, & AI-Directed Inline `[OPTIC BUFFER]` Wipes. |
| Phase II — Stage 02 (`P2-02`) | Hybrid Small-LLM Tool-Calling Director, `[1]`-`[5]` TUI Inquiry Probes, 15s Forensic Buffer Scrub, & Contradiction/Override Loop. |
| Phase II — Stage 03 (`P2-03`) | Directional Room Acoustics, Deception Carrier Tells, & `TextOnly` -> `VoiceOnly` Act III Transition. |
| Phase II — Stage 04 (`P2-04`) | *PROJECT: GLASS HOUSE* Scenario Tuning, Endings, & Full Automation Verification. |

Zero numbered `.cpp` files: `DirectOrScutSlice*.cpp` and `DirectOrScutStage*.cpp`, including tests, are permanently banned. Keep runtime code in canonical domain `.h`/`.cpp` pairs; never create stage-specific implementation units. PIE regression coverage belongs in `DirectOrScutPIETest.cpp`, pure model coverage in `DirectOrScutSimulationTest.cpp`. Use domain-based test names.

Use `Source/EMJ2026/.clang-format`; keep lifecycle ownership explicit, fixed-step backlog retained, and rendering outside the deterministic model. For substantive implementation changes run `preflight.ps1` and `Scripts/Test-DirectOrScut.ps1`, inspect report counts, and distinguish an editor-open skip from a build pass. Preserve historical evidence and known legacy failures; do not claim Phase II implementation or verification from baseline results.

This pivot recording pass changes documentation only: no `.h`, `.cpp`, `.uasset`, or `.umap` edits. Future implementation follows the responsibility matrix and acceptance criteria in `DIRECT_OR_SCUT.md`.
