# Source architecture

- Never add slice-numbered source or test files. Keep runtime methods in the canonical header/implementation pair for their class.
- Put end-to-end Unreal PIE regression coverage in `DirectOrScutPIETest.cpp`; put pure simulation tests in `DirectOrScutSimulationTest.cpp`.
- Keep all eight soundstage feeds, vanilla lighting, the two-tier CRT workstation, and `AI-LINK>` intent routing intact.
- Use this directory's `.clang-format` for C++. Keep lifecycle ownership explicit and simulation changes deterministic at 30 Hz.
- Run `preflight.ps1` and `Scripts/Test-DirectOrScut.ps1` after substantive changes. Verify report counts; an editor-open preflight skip is not a build pass.
