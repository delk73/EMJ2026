# DIRECT OR SCUT - Opening Shift: Emergency Activation

**Status:** Proposed scenario and presentation specification (2026-11-15).  
**Objective:** Compose the existing simulation, terminal, and MOTHER-7 systems into one cohesive, playable emergency supervisory activation.

The filename `OPENING_SHIFT.md` is retained for continuity. Its intended experience is Scut being summoned into an emergency already underway.

## 1. Player experience

Director Ridley Scut is summoned remotely into an emergency already underway aboard THETA-9. Local attempts to resolve the situation have been insufficient. Through MOTHER-7's terminal, Scut must establish what happened, assess conflicting accounts, and authorize interventions while conditions continue evolving.

Reconstructing earlier events informs immediate operational decisions. Investigation and intervention remain interleaved throughout the emergency, rather than forming a separate retrospective phase.

The ship continues operating regardless of Scut's attention. Problems can emerge concurrently, interfere with each other, and develop while other matters are being investigated.

**MOTHER-7 reports -> Scut questions -> Scut investigates -> Scut decides -> the simulation responds -> MOTHER-7 interprets the result.**

The underlying mystery develops through discrepancies between MOTHER-7's account and independently observable conditions.

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

> MOTHER-7: "Director, reactor supply stability has fallen outside tolerance. Atmospheric reserves are currently sufficient, but I recommend reviewing the power distribution."

This is an illustrative report, not a fixed line or evidence that reserves are sufficient in a particular run. Reports must use the actual originating condition and available readings.

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

Scut discovers available capabilities through `help`, `menu`, and operational questions to MOTHER-7. The emergency briefing and subsequent guidance must point to these documented discovery paths and explain the available validated interventions when relevant. No essential action may depend on guessing an undocumented command. These are proposed opening-experience requirements, not a claim that every conversational question is already supported.

These views share consistent visual styling and return naturally to conversation. No permanent telemetry wall is required. Telemetry should sample authoritative state at request time, identify the observation tick, distinguish a frozen reading from a live instrument or historical evidence, and avoid invented trends or unavailable measurements. Preserve command history, scrolling, input focus, fallback behavior, and existing camera functionality.

## 4. Proposed emergency activation scenario

### Phase A - Emergency supervisory activation

The terminal connects and MOTHER-7 delivers a concise emergency briefing: the current operational concern, which local recovery attempts have been insufficient, the immediate constraints, and the decision or investigation that needs Scut's attention. Distinguish measured conditions, crew accounts, and unknowns. The briefing must be operationally credible and leave one or two subtle, checkable inconsistencies worth investigating.

Possible authored discrepancies include a recovery acknowledgment whose timing conflicts with available readings, or a crew account that does not align with an independent optical observation. These are design options, not established scenario facts. Select only discrepancies supported by authored circumstances and retained evidence; allow plausible explanations such as incomplete information or reporting delay.

MOTHER-7 identifies `help`, `menu`, and questions as ways to discover controls and evidence. Scut can begin acting or inspecting immediately while the emergency continues.

**Purpose:** Establish urgency, supervisory authority, an apparently competent working relationship, and a reason to verify the account.

### Phase B - Intervention in the ongoing supply emergency

A reactor supply interruption is one of the conditions already underway when Scut connects. MOTHER-7 reports available measurements, explains the limits of local recovery, and recommends an operational response. Scut may accept, investigate, refuse, or postpone while other conditions evolve.

The existing Experiment 1 supply interruption and causal continuity provide the technical starting point. The proposed opening must establish the ongoing condition and evidence of prior local attempts as scenario circumstances, without requiring the player to know `continuity_incident`. That automatic emergency setup is not implemented by Experiment 1 or Experiment 2.

**Purpose:** Establish player authority and measurable consequences.

### Phase C - Contradictory observation

While immediate operations continue, Scut compares an earlier crew-position report or operational account with another available source. MOTHER-7 supplies a plausible explanation. Scut may inspect optical or forensic information, challenge the explanation, or intervene in the supply problem without completing the inquiry first. Earlier events matter because they affect current decisions; the ship does not wait for a reconstruction to finish.

**Purpose:** Establish that MOTHER-7's statements are claims that can be investigated.

### Phase D - Competing demand

Another crew or compartment condition is already active or escalates while the supply incident and its investigation remain unresolved. Available options affect operational conditions, access, timing, or personnel. There is no requirement to resolve incidents in a particular order.

**Purpose:** Make prioritization consequential.

### Phase E - Convergence

MOTHER-7 references Scut's earlier choices and their consequences while operational decisions remain necessary. Some explanations become difficult to reconcile with retained observations. The player may discover that apparently unrelated events share a meaningful connection, without establishing MOTHER-7's motive. She may be concealing misconduct, protecting crew, or attempting to save them under constraints Scut does not yet understand.

**Purpose:** Deepen the mystery through emergency decisions and their consequences while keeping motives ambiguous.

These phases describe overlapping strands of the live emergency, not a prescribed dialogue sequence. Investigation of earlier events begins alongside immediate decisions from activation; no phase pauses other incidents or requires the player to finish a retrospective inquiry before intervening.

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

## 7. Emergency activation acceptance

A successful playthrough should establish that:

1. The terminal connects with Scut summoned into a credible emergency already underway after insufficient local recovery attempts; MOTHER-7 briefs him concisely and leaves one or two subtle inconsistencies to investigate.
2. Routine background logging no longer floods the conversation.
3. All unsolicited alerts are presented by MOTHER-7.
4. Requested camera, telemetry, and evidence views are distinguishable from conversation.
5. At least two incidents can be active concurrently.
6. Scut can investigate or postpone one incident while another evolves.
7. At least one decision produces a measurable simulation consequence.
8. MOTHER-7 recalls the relevant decision and subsequent observations.
9. The player can identify at least one discrepancy worth investigating.
10. The experience remains operational and understandable without knowledge of a hidden narrative premise.
11. Scut can discover essential actions through `help`, `menu`, and MOTHER-7's guidance without guessing undocumented commands.
12. Earlier events are investigated alongside immediate decisions while conditions continue evolving.
13. Retained evidence can challenge MOTHER-7's account without predetermining whether her motive is concealment, protection, or rescue under unknown constraints.

These are future acceptance criteria, not a report of completed verification.

## 8. Development boundaries and priorities

Preserve authoritative simulation, fixed-step behavior, validated commands, causal-context experiment, local inference provider, deterministic fallback, and existing terminal rendering. Do not introduce another narrative framework, parser architecture, inference runtime, or distribution strategy in this design pass.

1. Establish correct terminal message routing and MOTHER-7-mediated alerts.
2. Compose emergency terminal activation, a concise MOTHER-7 briefing, discoverable controls, and the already-underway supply incident using existing facilities.
3. Introduce one overlapping incident using existing simulation facilities.

Evaluate the resulting playable experience before expanding the scenario.

## Current implementation boundary

Experiment 2 implements on-demand telemetry and deterministic MOTHER-7 routing in workstation presentation. Runtime verification is recorded in the latest `DEVLOG.md` entry. The emergency activation and briefing, already-underway scenario setup with prior local attempts, overlapping incidents and convergence remain proposed. Experiment 1 still requires `continuity_incident`.

The presentation consumes existing terminal-visible event records and authored cue weights without changing collection. Critical oxygen/pressure/breach/security reports appear immediately; significant authored reports wait for a prose boundary while Scut is not composing input. Known routine telemetry and diagnostic signatures remain outside the conversation. Consolidation and pending reports are bounded and reset with presentation; priority depends on current event signatures/cues, not a new authoritative event schema. Requested telemetry is a frozen request-time snapshot using existing inline stream retention. Existing `show_cam` and explicit forensic inspection retain their independent functions.
