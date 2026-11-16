# DIRECTOR SCUT

A darkly comic, systems-driven mystery made for Epic AI MegaJam 2026 and its
**DIRECTOR'S CUT** theme.

You are Director Scut, remotely supervising the industrial carrier THETA-9
through MOTHER-7's terminal. Reports conflict; camera views, telemetry, and
retained records offer other perspectives. Investigate, direct the crew, and
decide what the official account will say. The simulation keeps its own record:
filing a conclusion does not rewrite what happened.

This repository is the candid development project, not a cleaned-up SDK or a
claim that every experiment succeeded. It retains the development log,
historical experiments, test evidence, and known rough edges.

## Status

The game-jam project is complete. Its final documented Windows x64 Shipping
package and gameplay trailer were verified, but are **not included in this Git
checkout**; generated release files live under the ignored `Saved/` directory.
The source project was verified with Unreal Engine 5.9. The current gameplay
PIE suite passed in the final documented run, while an older Chunk 2 simulation
fixture still fails on legacy map expectations. See the [development log](DEVLOG.md)
for exact results, preserved intermediate failures, and limits.

This is a compact jam-scale experience, not a complete mystery campaign.
Automated checks and captures do not establish exhaustive playtesting across
hardware, display sizes, or clean Windows installations.

## Play

The ordinary game begins at the connected supervisory terminal with a
MOTHER-7 briefing. Use the visible response controls or type `help` in the
terminal to discover available commands. The comprehensive console supports
investigation and operational actions; ordinary narrative choices are
available through MOTHER-7.

The core game runs offline with deterministic MOTHER-7. Optional local
inference through Ollama is an experimental integration, not a requirement.
The project configuration points to a local endpoint by default; this does not
mean an inference server or model is installed or running.

## Open the source project

### Requirements

- Windows x64
- Unreal Engine 5.9
- Visual Studio 2022 with the C++ desktop/game development toolchain for
  building the project

### Run in Unreal Editor

1. Clone or download this repository.
2. Open `EMJ2026.uproject` in Unreal Engine 5.9 and allow the C++ project to
   build.
3. Open the default map, `/Game/Maps/DirectOrScut_Soundstage`, if it is not
   already open.
4. Press **Play**.

### Build and tests

The repository includes Windows PowerShell verification scripts:

```powershell
.\preflight.ps1
.\Scripts\Test-DirectOrScut.ps1 -Tests DirectOrScut.PIE -ReportName MyRun
```

The preflight performs source/build checks and a headless editor smoke test
where the required Unreal installation is available. The PIE test command
requires an available Unreal Editor installation. Historical and legacy test
results are documented in the [development log](DEVLOG.md); do not treat every
old report in the repository as a current passing result.

## Project notes

- [DEVLOG.md](DEVLOG.md) is the chronological implementation and verification
  record, including failed intermediate runs.
- [DIRECT_OR_SCUT.md](DIRECT_OR_SCUT.md) describes the project architecture and
  implementation history.
- [Docs/DIRECTOR_SCUT_GAME_PLAN.md](Docs/DIRECTOR_SCUT_GAME_PLAN.md) records the
  approved creative direction and intended experience; it is a plan, not proof
  that every planned feature shipped.
- [Director_Scut_2026_MegaJam_AAR.md](Director_Scut_2026_MegaJam_AAR.md) is the
  after-action report and includes historical attribution and scope notes.

## License and third-party material

Original project materials are offered under the MIT License; see
[LICENSE](LICENSE). Unreal Engine, plugins, and third-party tools or content are
not relicensed by this notice and remain subject to their own terms.

The project's historical asset inventory is best effort. Do not assume every binary asset or
dependency in the repository is MIT-licensed or freely redistributable; verify
its provenance and applicable terms before reusing or distributing it.
