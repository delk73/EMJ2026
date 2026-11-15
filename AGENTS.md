# Canonical agent contract: Direct or Scut

Updated 2026-11-15. This file is the canonical agent guidance. Start with the active design at the beginning of [DIRECT_OR_SCUT.md](DIRECT_OR_SCUT.md) and the newest [DEVLOG.md](DEVLOG.md) entry. Historical Slice/Chunk notes preserve evidence; they do not override the active direction.

## Creative and technical objective

DIRECT OR SCUT is a simulation-grounded, LLM-assisted systems mystery. Director Ridley Scut is summoned remotely into an emergency already underway aboard industrial carrier THETA-9 after local recovery attempts have proved insufficient. Through MOTHER-7's terminal, he must establish what happened, assess conflicting accounts, and intervene while conditions continue evolving. Earlier-event investigation informs immediate operational decisions throughout the emergency.

The proposed opening connects the terminal to a concise, operationally credible MOTHER-7 emergency briefing with one or two subtle, evidence-backed inconsistencies worth investigating. Scut discovers essential capabilities through `help`, `menu`, and questions to MOTHER-7; essential actions must not require guessing undocumented commands. MOTHER-7's motives remain ambiguous: she may be concealing misconduct, protecting crew, or trying to save them under constraints Scut does not understand. No interpretation is established as the opening truth or an implemented plot.

The primary objective is a playable feedback loop: **authoritative simulation event -> causal narrative context -> local LLM interpretation -> player decision -> validated action when chosen -> changed simulation and updated narrative**. Decisions also include refusal, challenge, discovery, and consequential inaction; these can alter later narrative behavior without mutating simulation state. The simulation establishes what happens; MOTHER-7 interprets and mediates it; Scut acts on that interpretation and changes what happens next. The LLM supplies expression and suggestions, never physical authority.

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
- **Scut's player-visible knowledge:** Track only observations, claims, evidence, discoveries, and decisions made available through the terminal or direct interaction. Hidden facts become player knowledge only through authored disclosure or investigation.

The current code has scenario and mediated-claim structures but not three persistent knowledge ledgers. Keep these perspectives explicit when designing causal context; do not infer that MOTHER's prompt or Scut's transcript represents the complete world state.

Keep the unified shader-driven phosphor terminal as the active presentation format, not a permanent 4x2 camera wall. Room feeds remain optical sources for inline `[OPTIC BUFFER]` blocks and widgets. The 4x2 grid is an opt-in diagnostic view; presentation focus, shader tells, voice timing, and forensic scrub cannot advance or rewrite the simulation. The 451-sample value-only forensic ring is read-only and is not full-resolution recorded video.

Current code includes the CRT terminal and project-owned `MD_UI` material, typed hybrid director, validated offline fallback and local HTTP integration, six-message chat transcript, scenario beats, event/cue/action records, bounded optical references, contradiction challenges, and resolved-action replay. Existing proactive observations are deterministic event/vital-threshold notices assembled by `ServiceDirector()`; they are not LLM-grounded narrative responses. Experiment 1 retains one operational episode across recommendations, decisions, evidence, outcomes and observed consequences; general scenario-wide retention remains future work. Voice mode validation exists; directional acoustics and actual TTS/voice delivery remain future work. Consult the implementation entries in `DIRECT_OR_SCUT.md` for precise scope and verification evidence.

## Proposed emergency supervisory activation

[OPENING_SHIFT.md](OPENING_SHIFT.md) retains its filename but specifies emergency supervisory activation, not routine shift handover, as the proposed scenario and presentation direction. All unsolicited operational information should be mediated by MOTHER-7; critical reports must remain available deterministically when inference fails. Keep routine/diagnostic collection independent of conversation display. Implementation order remains routing first, then emergency activation and briefing around an already-underway supply incident with discoverable controls, then one overlapping incident using existing scenario facilities. Experiment 2 implements `show_telem` and presentation-only deterministic MOTHER-7 routing; see newest DEVLOG for actual verification. The composed emergency opening, prior local-attempt circumstances and overlapping scenario remain proposed.

## Proposed game-jam distribution

Target a self-contained Windows build of approximately 2 GB total, including the game, local inference runtime, and model. The preferred candidate is Qwen2.5-1.5B-Instruct Q4_K_M in GGUF format, hosted by a bundled `llama.cpp` server through the existing OpenAI-compatible HTTP provider. This is a deployment proposal, not an implemented or verified capability; current `DefaultGame.ini` setup targets a separately installed Ollama service. Keep the deterministic fallback playable whenever the bundled runtime or model is unavailable, slow, or invalid. Do not require a separate install, account, model download, or external inference service for the fallback experience.

## Milestones and engineering practice

Slice 1-14 is retired historical nomenclature for the completed multi-tile workstation. Phase II names remain useful for tracking delivered pieces and planned extensions:

| Milestone | Status and focus |
| --- | --- |
| P2-00 | Delivered: reference audit, selected math porting, and `Content/Loom/` removal. |
| P2-01 / P2-01a-c | Delivered: unified terminal, inline media, map-first evidence, and phosphor prompt. |
| P2-02a-b | Delivered: guided input, read-only forensic probes, typed hybrid director, local HTTP adapter, fallback, contradiction loop, and resolved-action replay. |
| P2-02 Experiment 1 | Implemented: one opt-in supply interruption with bounded causal context, restoration/refusal/investigation and measured consequences. See newest DEVLOG for actual verification and limitations. General narrative retention remains future work. |
| P2-03 | Planned: directional room acoustics, deception carrier tells, and the `TextOnly` -> `VoiceOnly` Act III transition. |
| P2-04 | Planned: *PROJECT: GLASS HOUSE* scenario tuning, endings, and full automation verification. |

Keep runtime code in canonical domain `.h`/`.cpp` pairs; do not create numbered Slice/Stage implementation units. PIE coverage belongs in `DirectOrScutPIETest.cpp`, pure model coverage in `DirectOrScutSimulationTest.cpp`, with domain-based test names. Follow `Source/EMJ2026/.clang-format`, keep lifecycle ownership explicit, retain fixed-step backlog, and keep presentation outside the deterministic model.

For substantive implementation changes run `preflight.ps1` and `Scripts/Test-DirectOrScut.ps1`; inspect report counts and distinguish an editor-open skip from a build pass. Preserve historical evidence and known legacy failures. Documentation alignment or baseline tests are not evidence that proposed causal storytelling has been implemented.
