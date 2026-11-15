# DIRECT OR SCUT - Opening Shift: Connected Incident Briefing

**Status:** Approved opening specification, aligned with [Docs/DIRECTOR_SCUT_GAME_PLAN.md](Docs/DIRECTOR_SCUT_GAME_PLAN.md) (2026-11-15). A1 connected startup and the current A2 incident are implemented; A3 script-driven engagement is implemented and tested; joint acceptance is pending. A4 judgment/report implementation is tested; the connected-to-closure loop remains subject to joint acceptance. See DEVLOG for evidence and limits. The plan governs any conflict.
**Objective:** Compose the existing simulation, terminal, and MOTHER-7 systems into one playable opening: Director Scut is already connected to the remote supervisory terminal and receives a concise MOTHER-7 incident briefing.

The filename `OPENING_SHIFT.md` is retained for continuity. This document supersedes the earlier "emergency supervisory activation" proposal (an emergency already underway, a reactor supply interruption and prior local recovery attempts as the premise). That premise is withdrawn; the existing Experiment 1 supply interruption remains a technical/regression fixture, not the opening.

## 1. Player experience

When play begins, Director Scut is **already authenticated and connected** to the existing remote supervisory terminal for THETA-9; he is not aboard the carrier. There is no login sequence, authentication animation, required cutscene, or terminal redesign. A cinematic introduction may eventually precede play but is not required.

MOTHER-7 delivers a concise incident briefing through the existing interface. Something has happened aboard THETA-9; accounts conflict and unusual circumstances have been reported in different compartments. Nobody can explain it. The briefing does not name a culprit, a supernatural phenomenon, or a mandatory emergency, and the incident is not automatically an emergency: severity and urgency emerge from actual conditions and decisions.

The play loop is **Observe -> Interpret -> Direct -> Experience consequences -> Render judgment.** The ship continues operating regardless of Scut's attention; problems can develop while other matters are investigated, and inaction or delay may have consequences.

## 1a. Opening briefing requirements

The briefing is concise and operationally credible. It:

- Distinguishes measured conditions, crew accounts, and unknowns, and presents reports as MOTHER-7's claims.
- Introduces uncertainty without resolving it, leaving at least one discrepancy that independent evidence (cameras, telemetry, forensic record, personnel) can check.
- Points to documented discovery paths (`help`, `menu`, questions to MOTHER-7) and offers immediately actionable investigative approaches.
- Introduces crew by names, assignments, reported histories and observable behavior only. Concealed identities (for example, android status) and agendas are not disclosed in introductory text or interface elements.
- Does not depend on developer commands, manual experiment activation, or `continuity_incident`.

From the first prompt the player may question MOTHER-7, request reports and records, inspect cameras and telemetry, review forensic information, contact or direct personnel through supported facilities, authorize operational actions, or refuse and defer.

## 1a.1. A3 engagement through the existing terminal

The current incident uses one authored ordinary beat-deck script in `ConfigureScenario()`. MOTHER-7 volunteers developments and presents visible authored responses without requiring menu commands. Scut may accept a provisional dispatch account, inspect independently retained observations, authorize reduced workload, maintain coverage, investigate, or defer. Bounded contextual phrases supplement direct selection; exact operational commands retain precedence. Pending matters persist while simulation consequences continue, and independently disclosed evidence can change later dialogue without establishing a culprit or correct explanation. The comprehensive console remains available throughout.

## 1b. Progression toward judgment

The incident develops as a compact set of overlapping strands (plan Section 5): unexplained observations across compartments, personnel responses, and Scut's interventions. It is a pattern of observations that may or may not share a cause, not a new roaming entity. Decisions are presented as authored, context-dependent approaches with explicit prerequisites and consequences and are entered through the existing terminal using a bounded deterministic matcher (exact commands first; ambiguity clarified; no destructive action on ambiguous language). Consequential actions validate through the existing authoritative mechanisms.

Play concludes when Scut renders judgment: his **director's cut** of events, compared against the independently preserved authoritative record. The outcome categories (Resolved, Explained, Contained, Reclassified, Insufficient evidence, Vented) are compositional, and the final report draws on actual state, evidence encountered, personnel conditions, consequences and remaining uncertainty without altering historical facts. The existing 600 s / 18,000-tick experiment deadline and legacy endings are not authorized to constrain this flow; their treatment is a Chunk A scoping decision to be reported before implementation.

MOTHER-7 may then offer a brief, dry assessment conditional on authoritative state.

## 1c. Current starting state versus the approved opening

Today, cold boot shows an ASCII MOTD and a Briefing state in which crew directives are blocked until `[ENTER]` or `start_experiment` initializes the run. Chunk A must make an ordinary game start land in the briefed, connected session without that step, while preserving the existing initialization path and tests as an explicit fixture. This conflict is recorded in the DEVLOG entry for plan approval.

## 2. Governing presentation rule

**All unsolicited operational information reaches Scut through MOTHER-7.**

The primary terminal contains:

- Scut's input and direct command results.
- MOTHER-7's conversational responses.
- Operational alerts delivered in MOTHER-7's voice.
- Requested inline instruments, optical views, and evidence.

The primary terminal does not automatically display:

- Raw event logs or subsystem messages.
- Routine vitals and periodic environmental samples.
- Repetitive telemetry or status changes.
- Forensic dumps, command internals, or developer diagnostics.

These records remain available for debugging, replay, evidence inspection, or explicit player requests. Suppressing presentation must not suppress collection or change simulation behavior.

### Alert delivery

All consequential alerts are presented as MOTHER-7 messages, even when the underlying event is generated deterministically. For example:

> MOTHER-7: "Director, the Hydroponics camera and the Engineering log disagree about who was last in that compartment. I can't say which is wrong."

This is an illustrative report, not a fixed line. Reports must use the actual originating condition and available readings.

MOTHER-7 may subsequently elaborate through the LLM. Critical warnings must not depend on successful inference: a deterministic MOTHER-7 report is always available. Narration must not change the authoritative event, measured readings, or validated action outcome.

### Alert priority

| Priority | Behavior |
| --- | --- |
| Critical | MOTHER-7 immediately interrupts with a concise report and a clear operational concern. |
| Significant | MOTHER-7 announces the event at the next appropriate conversational boundary. |
| Routine | Recorded silently; available on request. |
| Diagnostic | Retained for inspection and development; not automatically presented as gameplay dialogue. |

Repeated alerts should be consolidated when they concern the same unresolved condition.

## 3. Reusable terminal composition

The workstation remains one primary conversational interface:

- **Conversation:** Scut and MOTHER-7 exchange questions, recommendations, decisions, and acknowledgments.
- **Inline optics:** `show_cam` presents requested camera information within the terminal.
- **Inline instruments:** `show_telem` requests an operational overview; `show_telem reactor_core` requests current compartment readings. This terminal facility is implemented separately from the proposed scenario.
- **Evidence inspection:** explicit forensic and dossier requests display relevant information without dumping unrelated records.

Scut discovers available capabilities through `help`, `menu`, and operational questions to MOTHER-7. The opening briefing and subsequent guidance must point to these documented discovery paths and explain the available validated interventions when relevant. No essential action may depend on guessing an undocumented command or on a developer command. These are opening-experience requirements for Chunk A, not a claim that every conversational question is already supported.

These views share consistent visual styling and return naturally to conversation. No permanent telemetry wall is required. Telemetry should sample authoritative state at request time, identify the observation tick, distinguish a frozen reading from a live instrument or historical evidence, and avoid invented trends or unavailable measurements. Preserve command history, scrolling, input focus, fallback behavior, and existing camera functionality.

## 4. Incident development (plan Section 5; Chunk A scope)

These are overlapping strands of one compact incident, not a prescribed dialogue sequence or linear plot. Specific circumstances are authored in Chunk A using existing facilities; items below are design intent, not established scenario facts.

### Strand A - Unexplained observations

Reports arise across different compartments. Camera images, telemetry, personnel accounts, and historical records give imperfect views. At least one discrepancy is independently checkable through existing investigative facilities (for example, a crew account that does not align with an optical observation, or a report whose timing conflicts with telemetry). Allow plausible explanations such as incomplete information or reporting delay. The player may infer that something is moving through the carrier; the game does not establish whether the events share a cause.

**Purpose:** Establish that MOTHER-7's and crew statements are claims that can be investigated.

### Strand B - Personnel response

Crew react differently to uncertainty and Scut's interventions. Movement, stress, suspicion, requests, competing priorities, and concealed agendas shape the situation. What Scut can see of these depends on information actually disclosed or discovered. At least one consequential personnel complication is included.

**Purpose:** Make prioritization and information access consequential.

### Strand C - Scut's interventions

Scut can investigate, question, direct personnel, isolate compartments, adjust systems, refuse action, manipulate reporting, or authorize destructive measures. These change the simulation and may alter what can subsequently be observed or established. Refusal, deferral and consequential inaction are valid choices with consequences.

**Purpose:** Establish player authority, measurable consequences and the director's cut.

### Convergence

MOTHER-7 may reference Scut's earlier choices and their consequences. Some explanations may become difficult to reconcile with retained observations, and apparently unrelated events may prove connected, without establishing MOTHER-7's motive. She may be concealing misconduct, protecting crew, or attempting to save them under constraints Scut does not yet understand.

Investigation and intervention are interleaved from the first prompt; no strand pauses the others or requires a retrospective inquiry before intervening.

## 5. Incident composition

Each incident has:

- An originating authoritative condition or authored circumstance.
- A progression driven by simulation state, time, or other incidents.
- A reporting policy governing what MOTHER-7 communicates.
- Available questions, investigations, and validated interventions.
- Consequences, including those of postponement or refusal.
- Retained evidence and causal relationships.
- Conditions for resolution, escalation, or convergence.

Incidents may overlap. Their underlying state is not paused because Scut is talking about something else. Use existing scenario and beat mechanisms before introducing new infrastructure.

## 6. MOTHER-7's dramatic role

MOTHER-7 is composed, competent, measured, and apparently helpful. She does not deliver constant exposition, announce game mechanics, or explain the entire scenario. Her influence comes from selecting observations, framing recommendations, emphasizing risks, and managing Scut's attention.

Her motives remain ambiguous. Concealing misconduct, protecting crew, and trying to save them under constraints unknown to Scut are live creative possibilities. A discrepancy does not by itself establish malice, deception, or benevolence.

A possible private agenda remains distinct from authoritative simulation truth and Scut-visible evidence. MOTHER-7 may mislead through dialogue, but her claims remain identifiable as claims. Preserve underlying events and measurements independently.

## 7. Opening acceptance

Chunk A acceptance (plan Section 9): from an ordinary game start, the player can investigate, intervene, and conclude the incident through at least two materially different approaches without developer commands. A successful playthrough should also establish that:

1. The terminal starts connected, with no manual activation, and MOTHER-7 briefs Scut concisely on an uncertain incident without naming a culprit or mandating an emergency.
2. Routine background logging does not flood the conversation, and all unsolicited alerts are presented by MOTHER-7.
3. Requested camera, telemetry, and evidence views are distinguishable from conversation.
4. At least one discrepancy is independently checkable, and at least one consequential personnel complication occurs.
5. Contextual choices accept common phrasings, preserve exact commands, clarify ambiguity, and never execute destructive actions on ambiguous language.
6. Scut can investigate, intervene, refuse, defer, or suffer consequential inaction while conditions evolve.
7. At least one decision produces a measurable simulation consequence, and MOTHER-7 recalls the relevant decision and observations.
8. Hidden facts (including concealed identities and agendas) do not reach the player without disclosure or investigation.
9. Scut can discover essential actions through `help`, `menu`, and MOTHER-7's guidance without guessing undocumented commands.
10. Multiple judgments are derived from actual state, and the final report does not alter historical facts.
11. Retained evidence can challenge MOTHER-7's account without predetermining her motive.
12. The game is fully playable with deterministic offline MOTHER-7; inference is optional.

These are future acceptance criteria, not a report of completed verification.

## 8. Development boundaries and priorities

Preserve authoritative simulation, fixed-step behavior, validated commands, causal-context experiment, local inference provider, deterministic fallback, and existing terminal rendering. Do not introduce another narrative framework, parser architecture (beyond the bounded authored-choice matcher), inference runtime, roaming entity, cutscene, or distribution strategy. Any deviation follows the plan's Section 11 reporting process.

Order of work within Chunk A is a proposal for review, not authorization: (1) ordinary start lands in the briefed session; (2) the briefing and incident circumstances; (3) authored choices and forgiving input; (4) judgments and report. Evaluate the playable experience before expanding the scenario. Chunks B and C follow only after explicit authorization.

## Current implementation boundary

Experiment 2 implements on-demand telemetry and deterministic MOTHER-7 routing in workstation presentation. Runtime verification is recorded in the DEVLOG entries for Experiment 2. The connected start, briefing, roaming-disturbance circumstances, authored choices and judgments are not implemented. Experiment 1 still requires `continuity_incident`, and cold boot still requires `[ENTER]` or `start_experiment`.

The presentation consumes existing terminal-visible event records and authored cue weights without changing collection. Critical oxygen/pressure/breach/security reports appear immediately; significant authored reports wait for a prose boundary while Scut is not composing input. Known routine telemetry and diagnostic signatures remain outside the conversation. Consolidation and pending reports are bounded and reset with presentation; priority depends on current event signatures/cues, not a new authoritative event schema. Requested telemetry is a frozen request-time snapshot using existing inline stream retention. Existing `show_cam` and explicit forensic inspection retain their independent functions.
