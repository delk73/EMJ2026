# Canonical agent contract: Direct or Scut

Updated 2026-11-15. This file is the canonical agent guidance. [Docs/DIRECTOR_SCUT_GAME_PLAN.md](Docs/DIRECTOR_SCUT_GAME_PLAN.md) is the **authoritative creative and execution contract**: premise, intended experience, implementation chunks, acceptance criteria and governance. If any document conflicts with it, the plan wins until a deviation is approved. Then read [DIRECT_OR_SCUT.md](DIRECT_OR_SCUT.md) (architecture, current status, historical evidence) and the newest [DEVLOG.md](DEVLOG.md) entry. Historical Slice/Chunk/Experiment notes preserve evidence; they do not override the plan.

## Creative and technical objective

DIRECTOR SCUT is a darkly comic, systems-driven, choose-your-own-adventure mystery. Director Scut, a corporate problem solver known for *venting problems*, operates remotely through the existing MOTHER-7 supervisory terminal connected to industrial carrier THETA-9. Something has happened aboard; reports conflict, unusual circumstances arise in different compartments, and nobody can explain a disturbance that seems to move. Scut has extensive authority but limited knowledge. He must decide what the problem is, how to respond, and what to conclude. Success has many forms and the game recognizes consequences rather than a single correct solution. The final disposition is the player's **director's cut**; the authoritative record independently preserves what happened.

Play begins with Scut already authenticated and connected to the existing terminal, where MOTHER-7 delivers a concise incident briefing. The incident is not automatically an emergency, and no culprit, supernatural cause, reactor-repair premise, login sequence or required cutscene is established. Normal play must not require developer commands or manual experiment activation. MOTHER-7's motives remain ambiguous; she is professional with dry understatement, and humor must be conditional on authoritative state.

The core loop is **Observe -> Interpret -> Direct -> Experience consequences -> Render judgment**, built on the existing feedback chain: **authoritative simulation event -> causal record -> MOTHER-7 report -> player decision (including refusal, challenge, discovery, consequential inaction) -> validated action when chosen -> changed simulation and updated reporting**. The simulation establishes what happens; MOTHER-7 interprets and mediates it; Scut acts on that interpretation. Deterministic offline MOTHER-7 must be fully playable. Local LLM inference is optional and a stretch opportunity: it supplies expression only, never physical authority, and is never a prerequisite for completing the story.

Narrative choices are authored, context-dependent and entered through the existing terminal using a bounded deterministic matcher (exact commands take precedence; ambiguity is clarified, never guessed; destructive actions are never executed on ambiguous language). Do not build a general natural-language parser, dialogue framework, or LLM-dependent decision system. See plan Sections 5–7 for the roaming-disturbance scenario strands and the compositional judgment categories.

## Plan governance (mandatory)

- **Chunk-by-chunk approval.** Implementation proceeds through Chunk A (playable incident and branching judgments), Chunk B (sound, character, presentation) and Chunk C (playtesting and stabilization) as defined in the plan. Begin a chunk only after the user has reviewed the previous chunk and explicitly authorized the next. Documentation approval is not implementation authorization.
- **Deviation reporting.** Do not silently adopt deviations from the plan. Report each proposed deviation with: (1) the affected approved requirement, (2) the proposed change, (3) the implementation reason, (4) the smallest workable alternative, and (5) the effect on player experience and scope. Wait for approval.
- **Protect existing infrastructure.** Do not redesign or reimplement the terminal, deterministic simulation, validation/replay, procedural audio, deterministic MOTHER-7, optional inference adapter, or existing tests without a demonstrated requirement. Do not introduce a new simulation architecture, parser, terminal framework, mandatory LLM, roaming entity, or cutscene.
- **Evidence.** Distinguish existing functionality from intended work, and record what was implemented and tested in `DEVLOG.md`. Do not describe planned work as completed.
- **Scope reduction order.** Cut optional inference, additional discrepancies, extra roaming beats, sophisticated sound synthesis, expanded synonyms and decorative effects before the playable opening, meaningful choices, consequences, sound feedback or ending.

## Responsibilities and boundaries

- **Simulation:** `FDirectOrScutSimulation` owns deterministic 30 Hz physical crew, compartment, door, pressure, and related model state. Keep model stepping independent of rendering, audio, LLMs, and wall-clock latency.
- **Scenario orchestration:** `UDirectOrScutSimSubsystem` owns fixed-step orchestration, additional operational/environmental systems (including power, oxygen overlays, cameras, crises, spoofing, staging, and secret agendas), command/action validation, scenario setup and beats, cues, and ending gates. `ConfigureScenario()` and `EvaluateBeatDeck()` are existing extension points for authored circumstances and state-triggered events.
- **Causal context (Experiment 1):** Build on `MakeContextSnapshot()`, `RecordCue()`, `LogEvent()`, `CommandRecords`, and `ServiceDirector()`. One opt-in `continuity_incident` now links a bounded 64-record operational episode across decisions, validated outcomes and measured telemetry. General causal retention across all scenarios remains future work. Retain significant recommendations, accept/refuse/challenge decisions, discoveries, committed outcomes, evidence, and consequential inaction when observable. A refusal can change later narrative behavior without causing a simulation mutation.
- **MOTHER-7 and inference:** `IShipAIIntentProvider`, `FShipAIRequest`, and the typed directive envelope provide the local inference boundary. Current code supports an offline deterministic provider and an optional loopback HTTP/Ollama adapter. `Config/DefaultGame.ini` configures the Ollama endpoint/model/timeout in this checkout; it does not establish a live connection or prove service availability. Provider output is untrusted until shared validation accepts it.
- **Terminal and player:** `SDirectOrScutWorkstationWidget` presents the unified CRT stream, map/dossiers, inline optics, inquiry probes, and read-only forensic evidence. Player input submits requests; presentation and inquiry must not mutate authoritative state. Terminal-first input: do not bind global printable-key shortcuts. Workstation operations use existing commands and visible buttons; preserve ordinary text editing, Enter, Tab/history and Escape. Do not replace letter shortcuts with modifier combinations.
- **Validation and replay:** Only validated `sim_action` directives enter the bounded next-fixed-tick queue and are revalidated at commit. Keep accepted/rejected outcomes and fixed commit ticks inspectable. Replay resolved actions without calling inference; claims, presentation, and hidden scenario truth must retain distinct provenance.

## Knowledge boundaries

- **Authoritative scenario truth:** Physical state and scenario facts are owned by the deterministic model and scenario orchestration. Hidden objectives and circumstances remain authoritative even when undisclosed.
- **MOTHER-7's knowledge and agenda:** The director may receive selected operational facts and an authored private narrative objective so it can intentionally influence Scut. Its knowledge is not automatically all scenario truth, and its private objective is not automatically disclosed to Scut. Neither the objective nor LLM output grants authority over simulation facts or physical consequences.
- **Scut's player-visible knowledge:** Track only observations, claims, evidence, discoveries, and decisions made available through the terminal or direct interaction. Hidden facts become player knowledge only through authored disclosure or investigation. Do not label personnel by concealed identity (for example, android status) in ordinary introductory text or interface elements.

The current code has scenario and mediated-claim structures but not three persistent knowledge ledgers. Keep these perspectives explicit when designing causal context; do not infer that MOTHER's prompt or Scut's transcript represents the complete world state.

Keep the unified shader-driven phosphor terminal as the active presentation format, not a permanent 4x2 camera wall. Room feeds remain optical sources for inline `[OPTIC BUFFER]` blocks and widgets. The 4x2 grid is an opt-in diagnostic view; presentation focus, shader tells, voice timing, and forensic scrub cannot advance or rewrite the simulation. The 451-sample value-only forensic ring is read-only and is not full-resolution recorded video.

Current code includes the CRT terminal and project-owned `MD_UI` material, typed hybrid director, validated offline fallback and local HTTP integration, six-message chat transcript, scenario beats, event/cue/action records, bounded optical references, contradiction challenges, and resolved-action replay. Existing proactive observations are deterministic event/vital-threshold notices assembled by `ServiceDirector()`; they are not LLM-grounded narrative responses. Experiment 1 retains one operational episode across recommendations, decisions, evidence, outcomes and observed consequences; general scenario-wide retention remains future work. Voice mode validation exists; directional acoustics and actual TTS/voice delivery remain future work. Consult the implementation entries in `DIRECT_OR_SCUT.md` for precise scope and verification evidence.

## Opening experience (A1 implemented; A3 engagement implemented; joint acceptance pending)

[OPENING_SHIFT.md](OPENING_SHIFT.md) retains its filename and specifies the actual opening: Scut is already connected to the terminal and MOTHER-7 briefs him on an uncertain incident. It supersedes the earlier emergency-activation/supply-incident premise. All unsolicited operational information is mediated by MOTHER-7; critical reports remain available deterministically when inference fails; routine/diagnostic collection stays independent of conversation display. Experiment 2 already implements `show_telem` and presentation-only deterministic MOTHER-7 routing (see newest verification entry in DEVLOG). A1 implements the composed connected opening; A2 implements the current compact incident and retained dispatch discrepancy. A3 adds script-driven engagement and contextual choices. A4 judgments remain separate, unimplemented work. See the newest DEVLOG evidence.

## Inference and distribution (stretch, outside the gameplay sequence)

Packaging and distribution are outside the Chunk A–C gameplay sequence. The previously proposed self-contained Windows build (bundled `llama.cpp` server with Qwen2.5-1.5B-Instruct Q4_K_M) is a stretch opportunity only, not implemented or verified, and must not be started without separate authorization. Current `DefaultGame.ini` setup targets a separately installed Ollama service. The deterministic fallback must remain fully playable with no separate install, account, model download, or external inference service.

## Milestones and engineering practice

Slice 1-14 is retired historical nomenclature for the completed multi-tile workstation. Phase II names remain useful for tracking delivered pieces and planned extensions:

| Milestone | Status and focus |
| --- | --- |
| P2-00 | Delivered: reference audit, selected math porting, and `Content/Loom/` removal. |
| P2-01 / P2-01a-c | Delivered: unified terminal, inline media, map-first evidence, and phosphor prompt. |
| P2-02a-b | Delivered: guided input, read-only forensic probes, typed hybrid director, local HTTP adapter, fallback, contradiction loop, and resolved-action replay. |
| P2-02 Experiment 1 | Implemented: one opt-in supply interruption with bounded causal context, restoration/refusal/investigation and measured consequences. See newest DEVLOG for actual verification and limitations. General narrative retention remains future work. |
| P2-02 Experiment 2 | Implemented: `show_telem` and presentation-only deterministic MOTHER-7 routing; terminal-first input correction. See DEVLOG. |
| Chunk A | In progress: A1 opening and A2 incident delivered; A3 engagement implemented; joint acceptance pending; A4 judgments not started (plan Section 9). |
| Chunk B | Approved, not started: sound, character and presentation. |
| Chunk C | Approved, not started: playtesting and stabilization. |
| P2-03 / P2-04 | Legacy planning names (directional acoustics, `TextOnly` -> `VoiceOnly` Act III, *PROJECT: GLASS HOUSE* tuning/endings). Superseded as the active sequence by Chunks A–C; any retained element needs plan-level approval. |

Keep runtime code in canonical domain `.h`/`.cpp` pairs; do not create numbered Slice/Stage implementation units. PIE coverage belongs in `DirectOrScutPIETest.cpp`, pure model coverage in `DirectOrScutSimulationTest.cpp`, with domain-based test names. Follow `Source/EMJ2026/.clang-format`, keep lifecycle ownership explicit, retain fixed-step backlog, and keep presentation outside the deterministic model.

For substantive implementation changes run `preflight.ps1` and `Scripts/Test-DirectOrScut.ps1`; inspect report counts and distinguish an editor-open skip from a build pass. Preserve historical evidence and known legacy failures. Documentation alignment or baseline tests are not evidence that proposed causal storytelling has been implemented.

## A3 engagement implementation

The ordinary branch of `ConfigureScenario()` is the single authored engagement script, extending the A2 beat deck with dialogue, bounded aliases, consequences and pending-matter copy. Preserve explicit console precedence and the comprehensive diagnostic console. Ordinary choices appear proactively in the existing Events surface; contextual matching never delegates physical authority to inference. A3 is separate from A4 judgments/endings and awaits joint acceptance; consult the newest DEVLOG entry for actual verification.
