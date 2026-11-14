# Source architecture

The repository-root [AGENTS.md](../../AGENTS.md) is the canonical agent contract. The active [Phase II specification](../../DIRECT_OR_SCUT.md#architectural-pivot--phase-ii-specification) supersedes historical requirements to preserve the permanent eight-feed, two-tier workstation.

- Implement the Unified Shader-Driven Terminal & Unreliable AI Mediation architecture: one full-viewport CRT teletype, MOTHER-7 mediation, inline `[OPTIC BUFFER]` feeds/wipes and ECG/schematic widgets. Room cameras remain optical sources.
- Keep `DirectOrScutSimulation` / `UDirectOrScutSimSubsystem` authoritative at 30 Hz, including rolling 15s optic history. `AI-LINK>` small-LLM directives (`show_cam`, `show_widget`, `sim_action`, `voice_blend`) require validation and deterministic fallback; only accepted actions mutate at fixed ticks.
- Bind existing personal shader assets via UI-domain `UMaterialInstanceDynamic` parameters. Keep visual deception and read-only forensic playback outside ground truth.
- Plan `FVoicePresentationState`: `TextOnly` -> `HybridTextAndVoice` -> `VoiceOnly`; Act III MOTHER-7 output becomes voice-only.
- Use `P2-01` through `P2-04` for upcoming milestones; Slice 1-14 is completed historical baseline. Never add numbered source or test files: `DirectOrScutSlice*.cpp` and `DirectOrScutStage*.cpp` are permanently banned. Runtime methods belong in canonical domain header/implementation pairs.
- Put end-to-end Unreal PIE regression coverage in `DirectOrScutPIETest.cpp`; put pure simulation tests in `DirectOrScutSimulationTest.cpp`. Use domain-based test names.
- Use this directory's `.clang-format`. Keep lifecycle ownership explicit and simulation changes deterministic at 30 Hz with retained backlog.
- Run `preflight.ps1` and `Scripts/Test-DirectOrScut.ps1` after substantive implementation changes. Verify report counts; an editor-open preflight skip is not a build pass. This documentation-only pivot does not implement or verify Phase II runtime behavior.
