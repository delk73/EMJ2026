# Canonical agent contract: Direct or Scut

Updated 2026-11-14. This root `AGENTS.md` is the canonical source of truth for agent architecture instructions. Read the active specification at the start of `DIRECT_OR_SCUT.md` and the latest entry in `DEVLOG.md`. Nested agent guidance supplements this contract; historical Slice/Chunk entries do not override it.

## Active architecture: Unified Shader-Driven Terminal & Unreliable AI Mediation

- Replace the permanent `4x2` eight-feed camera wall and verbose telemetry HUD with one full-viewport, custom-shader 1970s CRT terminal. MOTHER-7, the unreliable Ship AI, is the conversational mediator and optical director: splice live room feeds as inline `[OPTIC BUFFER]` blocks, raster wipes, and inline ECG/schematic shader widgets into the teletype stream. Retain room cameras as optical sources. P2-01 preserves the 4x2 layout only as the opt-in `DebugEightCamGrid` diagnostic view (`F9`, `debug_grid`, `/debug`); startup defaults to `UnifiedTerminal`. Toggling the debug view is presentation only and must never queue simulation commands or inspection changes.
- P2-01b makes the full left 56% column a clipped scrolling MOTHER-7/SYSTEM/DIRECTOR teletype log above the inquiry bar and prompt. Media is spliced inline through at most 128 lightweight `FTerminalStreamEntry` records with optional `FInlineAttachmentRef` metadata. Only the newest two optic references have live pooled MIDs/render-target bindings; older optics collapse to clickable request-time reference lines. Vector attachments allocate no extra textures or materials. The right 44% AI-reported map/vitals/environment deck remains persistent; map-row/reference clicks append fresh live entries. Debug toggles remain presentation-only.
- P2-01c keeps the left column on continuous phosphor glass: transparent prompt, presentation-clock blinking block cursor, flat inverted softkeys and etched inline frames. Comparison metadata stays in value-only history (`Subjects[2]`, `SubjectCount`, `bSideBySide`); paired optics share the same hard two-MID pool. ECG overlays, schematic/optic comparisons, and one-shot ocular vectors/header affordances are presentation only and cannot enqueue simulation commands. Ocular millimeters are a mediated presentation envelope, not a new authoritative biometric channel.
- P2-02a adds presentation-owned deterministic grammar completion, dim inline ghost suffix and clickable parameter guide, 32-command draft-preserving history, token erasure, contextual deck targets and active inquiry probes. Prompt Tab/Shift+Tab never traverse focus; empty-prompt inquiry hotkeys consume their matching character events. Override stages/submits normal validated console commands and grants no privilege. The 451-sample value-only forensic ring is recorded after authoritative fixed ticks; read-only copy lookup and presentation-clock schematic/optic-state playback cannot advance, restore or mutate simulation. Missing/expired coverage is explicit; live room textures are never represented as recorded raster video. The persistent right deck remains live.
- Keep `FDirectOrScutSimulation` / `UDirectOrScutSimSubsystem` authoritative at 30 Hz for physics, room state, command validation, and a rolling 15s optic buffer history. Presentation, voice timing, and model latency cannot advance or rewrite ground truth. Forensic scrub is read-only playback, never simulation rewind.
- `AI-LINK>` uses a hybrid small-LLM director with deterministic fallback. It emits validated structured `show_cam`, `show_widget`, `sim_action`, and `voice_blend` directives. Only accepted `sim_action` commands enter the bounded next-fixed-tick mutation path. Unreliable claims and curated optics belong to mediation, not simulation truth.
- Plan `FVoicePresentationState` progression `TextOnly` -> `HybridTextAndVoice` -> `VoiceOnly`. In the Act III fourth-wall transition MOTHER-7 stops teletype output and speaks solely via voice; operator input and necessary system feedback remain available.
- Bind the single project-owned `MD_UI` material (`/Game/DirectOrScut/Materials/M_DirectOrScut_Terminal`) through `UMaterialInstanceDynamic` parameters (`OpticRenderTarget`, wipe, scanline, distortion, and presentation tell scalars). Prefer UE 5.9 native Slate and curve APIs; do not reintroduce `Content/Loom/` dependencies.
- Preserve deterministic room/crew consequences and the contradiction/override loop. `[1]`-`[5]` TUI inquiry probes and 15s forensic scrub let the operator challenge MOTHER-7. Subtle shader deception tells affect presentation only.

## Milestones and source discipline

Slice 1-14 is retired historical nomenclature for the completed multi-tile workstation baseline at Slice 14. All upcoming work uses these collision-free Phase II milestones; Stage numbers are planning labels only.

| Milestone | Scope |
| --- | --- |
| Phase II — Stage 00 (`P2-00`) | Reference Audit, Math Porting, & `Content/Loom/` Removal. Delivered and verified. |
| Phase II — Stage 01 (`P2-01`) | Unified Phosphor Terminal, Custom CRT Shader Integration, & AI-Directed Inline `[OPTIC BUFFER]` Wipes, & Toggleable 8-Cam Debug View. Delivered and verified; normal startup is unified, diagnostics are opt-in. |
| Phase II - Stage 01a (`P2-01a`) | Paneled CRT Terminal Layout, Persistent Overhead Station Schematic, & Compact Telemetry Deck. Delivered and verified. |
| Phase II - Stage 01b (`P2-01b`) | Full-Height Scrolling Left Teletype Stream & Bounded Reference-Backed Inline Media. Delivered and verified. |
| Phase II - Stage 01c (`P2-01c`) | Seamless Phosphor Glass Surface, Blinking Block Prompt, & Comparative Inline Telemetry Affordances. Delivered and verified. |
| Phase II - Stage 02a (`P2-02a`) | Guided Terminal Typing, Directive Manipulation, Active TUI Probes, & Read-Only 15s Forensic Scrub. Implementation and verification recorded in the active specification. |
| Phase II — Stage 02 (`P2-02`) | Hybrid Small-LLM Tool-Calling Director, `[1]`-`[5]` TUI Inquiry Probes, 15s Forensic Buffer Scrub, & Contradiction/Override Loop. |
| Phase II — Stage 03 (`P2-03`) | Directional Room Acoustics, Deception Carrier Tells, & `TextOnly` -> `VoiceOnly` Act III Transition. |
| Phase II — Stage 04 (`P2-04`) | *PROJECT: GLASS HOUSE* Scenario Tuning, Endings, & Full Automation Verification. |

Zero numbered `.cpp` files: `DirectOrScutSlice*.cpp` and `DirectOrScutStage*.cpp`, including tests, are permanently banned. Keep runtime code in canonical domain `.h`/`.cpp` pairs; never create stage-specific implementation units. PIE regression coverage belongs in `DirectOrScutPIETest.cpp`, pure model coverage in `DirectOrScutSimulationTest.cpp`. Use domain-based test names.

Use `Source/EMJ2026/.clang-format`; keep lifecycle ownership explicit, fixed-step backlog retained, and rendering outside the deterministic model. For substantive implementation changes run `preflight.ps1` and `Scripts/Test-DirectOrScut.ps1`, inspect report counts, and distinguish an editor-open skip from a build pass. Preserve historical evidence and known legacy failures; do not claim Phase II implementation or verification from baseline results.

The earlier pivot recording pass was documentation only. Authorized Phase II implementation follows the responsibility matrix and acceptance criteria in `DIRECT_OR_SCUT.md`.
