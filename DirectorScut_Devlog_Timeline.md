# DIRECTOR SCUT — Timestamped Development Log Summary

**Jam:** Epic AI MegaJam 2026 · **Engine:** Unreal Engine 5.9 · **Repository:** `delk73/EMJ2026`  
**Coverage:** November 12–15, 2026 · **Timezone:** Pacific (America/Los_Angeles, UTC−08:00 during the jam)  
**Sources:** Repository [`DEVLOG.md`](https://github.com/delk73/EMJ2026/blob/main/DEVLOG.md), inspected alongside the repository's Git commit history (through commit [`2e2aa86`](https://github.com/delk73/EMJ2026/commit/2e2aa8676292cf35863b323457789e7baab8d44d), November 15, 4:31 PM PST).

## How timestamps are represented

The original `DEVLOG.md` is **not uniformly timestamped**. November 12 attraction experiments contain minute-level log headings. Most Director Scut and late-stage entries identify only a calendar date. For those, the table gives the associated **Git commit time**, labeled **commit**, rather than misrepresenting it as the moment the task began or passed its tests. These are historical checkpoints, not a complete time-tracking ledger. Some working-session changes may not have been committed at their time of completion.

## November 12 — Field and attraction experimentation

| Time (PST) | Recorded work and outcome |
|---|---|
| **08:23 — commit** | `EMJ2026` initial repository commit; establishes the recorded project history during the jam. |
| **09:01 — log** | Greenfield UE 5.9 workspace inspection. Empty content baseline and source module confirmed. |
| **09:03 — log** | Foundational attraction simulation: force interactions, spring/tether bonds, node profiles and 120 Hz integration; C++ compilation verified. |
| **09:12 — log** | Player drop, slice and reset controls, plus `preflight.ps1`; build/headless boot verified, manual viewport validation pending. |
| **09:55 — log** | Initial liquid-heightfield, soft forces and macro zoom. |
| **10:19 — log** | Analytical GPU liquid rendering and HLSL gradients replace the CPU bitmap presentation. |
| **10:54 — log** | Widescreen SDF tray, Driver/Buffer/Anchor archetypes and tactile interactions. |
| **11:57 — log** | Pivot within the experiment toward local Builders, trimming, grafting and reward fields. |
| **12:27 — log** | Camera-centering and extended zoom correction. |
| **12:53 — log** | Single Builder perimeter growth and fading reward paint. |
| **13:20 — log** | Contextual patch dragging and guarded grafting. |
| **13:44 — log** | Root ring, strut picking and orphan cleanup. |
| **15:23 — commit** | Bilinear reward field, target hysteresis and multi-pair graft baseline checked into Git. |

**Interpretation:** November 12 produced a substantial exploratory simulation and shader program, not yet the playable THETA-9 surveillance mystery. Subsequent Director Scut work followed a meaningful design pivot.

## November 13 — Director Scut takes shape

| Time (PST) | Recorded work and outcome |
|---|---|
| **06:59 — commit** | Clinical spacecraft blockout, pod scale and nine-door topology refined. |
| **08:22 — commit** | Director Scut 30 Hz deterministic ship simulation, compartment navigation and vacuum-ejection rules. |
| **10:02 — commit** | CCTV multiplexer and input-focus routing. |
| **10:51–11:29 — commits** | Console commands, UI improvements, drop-down interactions, feeds and drill-down controls. |
| **12:35 — commit** | Modular 4×2 soundstage, biometrics, scenario beats and crew-follow interface. |
| **14:59–17:44 — commits** | Deterministic experiment endings, two-tier CRT workstation, cold-start gate, active suspicion and camera sabotage/repair. |
| **18:48–20:06 — commits** | Crew staging, choreography, medical postures, navigation barriers and private agendas. |
| **21:08–21:43 — commits** | Information-warfare mechanics, oxygen/power coupling, mutiny countdown and runtime/test consolidation. |

**Interpretation:** Most of the ship simulation and surveillance interface was implemented **during** the jam, not reused as a pre-jam system.

## November 14 — Rebuilding around MOTHER-7

| Time (PST) | Recorded work and outcome |
|---|---|
| **08:07 — commit** | Witness rules, uplink heat, room quick-actions and diegetic presentation. |
| **08:20–08:42 — commits** | Active architecture pivot; Loom gap audit and explicit port-and-purge contract. |
| **09:13 — commit** | P2-00 independent terminal material completed; Loom assets removed from project dependencies. |
| **09:45–11:05 — commits** | Unified phosphor terminal, paneled CRT layout, full-height scrolling teletype and bounded inline media. |
| **12:26 — commit** | Continuous phosphor surface, comparative telemetry, prompt behavior and MOTD boot. |
| **14:07 — commit** | Prompt completion, pixel-aligned cursor and read-only short forensic scrub. |
| **15:05 — commit** | Typed director action envelope, shared validation, deterministic fallback, replay and contradiction controls. |
| **18:11 — commit** | Optional local Ollama (`qwen2.5:3b`) adapter and live directive routing under validation. |
| **20:05 — commit** | Evidence viewport and rolling conversational transcript. |
| **22:58 — commit** | Documentation aligns the game with simulation-grounded, MOTHER-7-mediated storytelling and recognizes implementation gaps. |

**Source qualification:** The terminal material authoring script inspected two selected Loom function graphs **read-only** and authored its own UI HLSL. The record documents removal of shipped Loom dependencies, not evidence of imported shaders powering the final interface.

## November 15 — Playable loop, verification and packaged release

| Time (PST) | Recorded work and outcome |
|---|---|
| **00:03 — commit** | Simulation-grounded narrative continuity: bounded causal records, experimental supply incident and measured consequence paths. |
| **03:07 — commit** | Terminal-first UI controls, on-demand telemetry and MOTHER-7 alert routing; corresponding devlog entries record successful PIE regression checks and earlier corrections. |
| **10:32 — commit** | Final canonical game plan established. Approved A1–A4 playable loop with later presentation and packaging. |
| **12:11 — commit** | **A1:** Normal remote session starts already authenticated with MOTHER-7 greeting. |
| **13:36 — commit** | **A2:** Overlapping incident reports, conflicting dispatch claims, independent optical observation and consequential personnel responses. Visible event-menu routing corrected. |
| **14:58 — commit** | **A3:** Proactive script-driven MOTHER-7 engagement, authored actions/aliases/disclosures and validated response execution. |
| **15:51 — commit** | **A4:** Judgment, alternative dispositions, retained evidence and official final report; automation establishes guided and investigation-led completion paths. |
| **16:06 — commit** | Windows x64 Shipping build and release evidence committed; standalone offline startup and ZIP checks documented. |
| **16:31 — commit** | Bounded terminal/audio polish and final playable release committed. Full 17-test PIE suite reported clean; Shipping archive and offline launch verified. |

**Validation detail:** The A4 full run originally reported 16 successful tests plus one warning; targeted post-fix suites passed. The later presentation/audio acceptance run reported **17 successful PIE tests, no failures or warnings**. The Shipping ZIP verification reported **27 files passing CRC and SHA-256**, and the packaged launcher started without an inference server. The release log expressly distinguishes automated startup tests from a complete human playthrough or clean-machine installation test.

**Note on later uncommitted work:** These sources extend through the last Git commit inspected. Any final surveillance/crew revisions, trailer recording or newly repackaged submission files **must be added after their results are actually verified**. They are not included as completed milestones merely because they were requested.

## Cross-cutting findings from the record

**Scope and creative pivots.** The initial field/attachment experiment evolved into an industrial ship simulation, then a terminal-mediated supervisory mystery. The project maintained a deterministic model while repeatedly reworking the player-facing interface. The authoritative plan only crystallized on November 15.

**Verification versus experience.** The log includes substantial compiler, material, test and offline-launch evidence, along with initially failed or warned runs and their fixes. But it also repeatedly distinguishes headless or scripted PIE success from human viewport acceptance. The developer continued to report a gap between functioning systems and the desired atmosphere and engagement.

**Interaction overhead.** The companion AAR records the developer's assessment of recurring prompt corrections, supervisory work, quota reset prompts and unexpected usage depletion. These observations should remain distinguishable from claims of established causes; this timestamped timeline does not independently measure service quota events.

**Origin and attribution.** Git history places the repository start and Director Scut implementation during the November 12–15 jam. Loom material functions were inspected as references before their assets were purged; they are not established as shipped shaders. The history does not establish pre-jam reuse of simulation or terminal source code. Third-party content, if any, should be attributed based on a separate final asset inventory.

## Source navigation

- Original chronological + reverse-chronological mixed record: [`DEVLOG.md`](https://github.com/delk73/EMJ2026/blob/main/DEVLOG.md)
- Architecture and implementation status: [`DIRECT_OR_SCUT.md`](https://github.com/delk73/EMJ2026/blob/main/DIRECT_OR_SCUT.md)
- Canonical late-jam plan: [`Docs/DIRECTOR_SCUT_GAME_PLAN.md`](https://github.com/delk73/EMJ2026/blob/main/Docs/DIRECTOR_SCUT_GAME_PLAN.md)
- Companion reflection: [`DirectorScut_AAR_Final.md`](DirectorScut_AAR_Final.md)

*Links into the repository require access while it remains private. Publish the timeline/AAR separately if they are intended for public judging.*
