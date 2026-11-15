# DIRECTOR SCUT — Canonical Game Plan

**Status:** Approved design and execution contract (2026-11-15). This is the authoritative reference for creative direction, intended player experience, implementation sequence, and acceptance criteria.

This is a plan, not a status report. Nothing in Sections 2–9 below is claimed as implemented unless a [DEVLOG.md](../DEVLOG.md) entry records its implementation and verification. Implementation is authorized one chunk at a time (Section 9).

Related documents: [AGENTS.md](../AGENTS.md) (agent contract), [DIRECT_OR_SCUT.md](../DIRECT_OR_SCUT.md) (architecture, status, historical evidence), [OPENING_SHIFT.md](../OPENING_SHIFT.md) (opening experience), [DEVLOG.md](../DEVLOG.md) (implementation and verification history).

## 1. Identity and premise

**Title:** DIRECTOR SCUT

**Genre:** Darkly comic, systems-driven, choose-your-own-adventure mystery.

**Setting:** A remote supervisory terminal connected to industrial carrier THETA-9.

**Player:** Director Scut, a corporate problem solver with a reputation for *venting problems*.

Something has happened aboard THETA-9.

Nobody can provide a satisfactory explanation. Reports conflict. Unusual circumstances arise in different compartments. Personnel disagree about what they have experienced. The apparent disturbance seems to move, although its nature, origin, and even its existence as a single phenomenon remain uncertain.

Scut has been called upon to resolve the matter.

He is not aboard the carrier. He operates remotely through the existing terminal, receiving information from MOTHER-7, instrumentation, cameras, recorded evidence, and personnel communications.

Scut possesses extensive authority but limited direct knowledge.

His responsibility is to determine what constitutes the problem, decide how to respond, and eventually render judgment.

Success has many forms. Scut may restore operations, establish an explanation, protect personnel, isolate a perceived threat, conceal inconvenient facts, decline to certify an account, or destroy the supposed problem through disproportionate intervention.

Not every conclusion must be beneficial, morally defensible, or even sensible.

The game must recognize consequences rather than impose a single correct solution.

### Character and humor

Scut's reputation precedes him. He is associated with decisive, sometimes excessive solutions.

The player determines whether Scut lives up to that reputation.

The tone combines supervisory competence, bureaucratic absurdity, ambiguity, and dark comedy.

MOTHER-7 speaks professionally and with dry understatement. Humor should emerge naturally from actual decisions and their consequences.

For example, after catastrophic venting, MOTHER-7 might report:

"The reported disturbances have ceased. Personnel complaints have also fallen to zero."

Such observations must be conditional on authoritative simulation state.

Do not turn MOTHER-7 into a constant joke machine or predetermine her as an antagonist.

## 2. Starting experience

The game begins with Scut **already authenticated and connected to the existing remote supervisory terminal**.

The terminal is established working infrastructure, not a feature to rebuild.

MOTHER-7 delivers a concise opening incident briefing through the existing interface.

Scut does not yet know what the problem is.

The briefing introduces uncertainty and provides immediately actionable investigative approaches without announcing a culprit, supernatural phenomenon, or mandatory emergency.

The player may immediately:

- Question MOTHER-7.
- Request available reports and records.
- Inspect cameras and telemetry.
- Review forensic information.
- Contact or direct personnel through supported facilities.
- Authorize operational actions.
- Refuse recommendations or defer intervention.

Normal play must not depend on discovering developer commands or manually activating an experiment.

A cinematic introduction may eventually precede this experience, but no cutscene, authentication sequence, login animation, or terminal redesign is required.

Preserve the terminal's established input, navigation, display, and diagnostic behavior.

## 3. Core gameplay

The central loop is:

**Observe → Interpret → Direct → Experience consequences → Render judgment**

The experience combines authored narrative decisions with an authoritative ship simulation.

The player can investigate conflicting accounts, exercise operational control, decline to intervene, manipulate information, and eventually conclude the incident.

Actions affect ship conditions, personnel, evidence availability, and subsequent choices.

Inaction and delay may also have consequences.

The player should encounter difficult tradeoffs. Better information does not guarantee a favorable outcome, and apparently decisive solutions may cause disproportionate harm.

Do not construct a predetermined unwinnable scenario. Let uncertainty and competing consequences create the tension.

### The Director's Cut

Scut can literally cut off systems, access, communications, or atmosphere.

He can also determine which interpretation of the incident becomes the official account.

The final incident disposition is the player's **director's cut** of events.

The authoritative record independently preserves what happened.

Scut's judgment may be consistent with the record, incomplete, self-serving, unsupported, or contradicted by observed outcomes.

This distinction is fundamental to the game.

## 4. MOTHER-7 and information boundaries

MOTHER-7 is the player's intermediary.

She appears competent, helpful, and composed. Her observations and interpretations may be incomplete, mistaken, selective, or influenced by interests the player does not understand.

Her motivations remain ambiguous.

Maintain a strict distinction between:

1. Authoritative simulation facts.
2. MOTHER-7's reports and interpretations.
3. Information available to Scut.

The player must not automatically receive knowledge that exists only in simulation state.

Crew members are introduced through names, assignments, reported histories, and observable behavior.

Concealed identities, characteristics, and agendas remain concealed until appropriately disclosed or discovered.

A person may be an android without Scut knowing.

Do not label personnel by hidden identity in ordinary introductory text or interface elements.

Ambiguous evidence is acceptable. Arbitrary contradictions that cannot be reconciled with the game's information model are not.

## 5. Scenario — The roaming disturbance

The incident is an uncertain pattern of observations and developments across the vessel.

Reported disturbances may appear in different compartments, involve different personnel, and affect different operational systems.

The player may infer that something is moving through the carrier.

The game does not initially establish whether the events share a cause.

Use existing simulation, crew, environmental, surveillance, reporting, and causal-record facilities to construct this experience.

Do not implement a new roaming creature, supernatural simulation, or predetermined culprit.

The initial incident is not automatically an emergency. Severity and urgency should emerge from actual conditions, competing demands, and player decisions.

### Three overlapping scenario strands

**A. Unexplained observations**

Reports arise across different compartments. Camera images, telemetry, personnel accounts, and historical records provide imperfect views of events.

At least one discrepancy must be independently checkable through existing investigative facilities.

**B. Personnel response**

Crew members react differently to uncertainty and Scut's interventions.

Movement, stress, suspicion, requests, competing priorities, and concealed agendas contribute to the unfolding situation.

The player's access to those details depends on available information.

**C. Scut's interventions**

The director can investigate, question, direct personnel, isolate compartments, adjust systems, refuse action, manipulate reporting, or authorize destructive measures.

Those decisions change the simulation and may alter what can subsequently be observed or established.

The strands overlap. They are not three independent campaigns or a mandatory linear plot.

Implement a compact set of coherent circumstances with meaningful variation rather than an extensive scripted narrative.

## 6. Authored choices and forgiving terminal input

The primary narrative interaction model uses authored, context-dependent choices.

Each decision situation provides understandable approaches with explicit prerequisites and consequences.

The player expresses a response through the existing terminal rather than being required to select a numbered dialogue option.

A bounded deterministic matcher recognizes common phrasings for the currently available authored choices.

Requirements:

- Preserve existing exact commands and give them precedence.
- Accept common natural variations of available choices.
- Keep choices grounded in current scenario state.
- Clarify ambiguous input rather than guessing.
- Provide useful guidance for unrecognized input.
- Validate consequential actions through existing authoritative mechanisms.
- Never execute a destructive action merely because ambiguous language resembles one.
- Preserve terminal-first keyboard behavior, including the absence of global printable-character shortcuts.

Do not build a generalized natural-language parser, a new dialogue framework, or an LLM-dependent decision system.

### Optional inference

The game must be fully playable through deterministic offline MOTHER-7.

Preserve the existing optional local inference adapter where practical.

Qwen, llama.cpp, configurable inference endpoints, bundled models, and richer generated conversation are stretch opportunities only.

Inference must not invent authoritative consequences, bypass validated actions, or become a prerequisite for completing the story.

## 7. Judgments and endings

The final disposition evaluates what Scut chose to conclude against what actually happened.

Possible categories include:

- **Resolved:** Operational conditions stabilize, potentially without a complete explanation.
- **Explained:** Available observations support a defensible account.
- **Contained:** A perceived threat is isolated, possibly at significant cost.
- **Reclassified:** The official account becomes administratively convenient.
- **Insufficient evidence:** Scut declines to endorse unsupported conclusions.
- **Vented:** A literal and potentially catastrophic application of his reputation.

These are compositional outcome categories, not six separate campaigns or a requirement for six independent ending implementations.

The final report should incorporate relevant authoritative facts, personnel conditions, actions taken, evidence encountered, operational consequences, and remaining uncertainty.

The report must not change historical simulation facts to validate Scut's chosen interpretation.

MOTHER-7 may provide a brief, dry assessment appropriate to the outcome.

The game recognizes multiple forms of success without asserting that every decision was wise.

## 8. Presentation and sound

Preserve the existing CRT terminal and procedural audio infrastructure.

The intended presentation is restrained, atmospheric, and legible.

Use sound and silence to distinguish significant messages, discoveries, choices, operational changes, and conclusions.

Develop MOTHER-7's personality through concise writing, timing, and appropriate feedback.

Remove prominent developer-experiment terminology from normal gameplay.

Avoid extensive interface reconstruction, decorative systems, or unnecessary dependencies.

A complete session should feel like a composed interactive narrative rather than a test harness.

## 9. Implementation sequence

Development proceeds through three separately reviewed chunks.

### Chunk A — Playable incident and branching judgments

Build the complete normal-play experience using established systems.

Scope:

- Begin at the existing terminal with an immediate MOTHER-7 incident briefing.
- Introduce a compact, evolving pattern of unexplained observations.
- Make at least one discrepancy independently checkable.
- Include a consequential personnel complication.
- Present contextual authored approaches with forgiving input.
- Preserve existing investigation and operational commands.
- Support refusal, intervention, and consequential inaction.
- Produce multiple incident dispositions derived from actual state.
- Preserve existing experimental and regression functionality where necessary.

**Acceptance:** From an ordinary game start, the player can investigate, intervene, and conclude the incident through at least two materially different approaches without developer commands.

### Chunk B — Sound, character, and presentation

Build atmosphere and narrative cohesion on existing infrastructure.

Scope:

- Procedural atmosphere and meaningful sound cues.
- Appropriate use of silence.
- Distinct feedback for important discoveries, decisions, and outcomes.
- MOTHER-7's dry personality and situational humor.
- Clear incident reporting and available approaches.
- Removal of developer-facing language from normal gameplay.
- Consistent remote-supervision framing.
- Limited CRT polish where it improves comprehension.

**Acceptance:** A complete session communicates atmosphere, consequence, and character without abandoning the established terminal experience.

### Chunk C — Playtesting and stabilization

Verify the completed player experience and correct defects.

Scope:

- Investigation-first playthrough.
- Intervention-first playthrough.
- Extreme containment or venting path.
- Hidden-information checks.
- Claims and evidence checked against authoritative state.
- Contextual input and existing command regression checks.
- Multiple judgments and state-grounded reports.
- Sound and presentation checks.
- Focused and established PIE regression testing.
- Review of changes and preparation for an authorized commit.

**Acceptance:** One coherent, complete incident experience with materially different conclusions and no known critical gameplay regression.

Each chunk must be reviewed and explicitly authorized before development proceeds to the next.

## 10. Engineering and scope boundaries

Preserve:

- Unreal Engine 5.9 project structure.
- Existing `DirectOrScut` source identifiers and repository paths.
- Deterministic 30 Hz simulation.
- Simulation-owned operational authority.
- Validated actions and fixed-tick execution.
- Retained evidence, causal records, and replay.
- Camera, telemetry, and forensic inspection.
- Existing terminal input and presentation.
- Existing procedural audio component.
- Offline deterministic MOTHER-7.
- Optional inference adapter.
- Existing tests and verified engineering history.

Do not introduce a new simulation architecture, general-purpose parser, terminal framework, mandatory LLM, new roaming entity, or cutscene implementation.

Distribution and packaging work are outside this gameplay implementation sequence.

If scope must be reduced, cut optional inference work, additional discrepancies, extra roaming beats, sophisticated sound synthesis, expanded synonyms, and decorative effects before cutting the playable opening, meaningful choices, consequences, sound feedback, or ending.

## 11. Development governance

This plan is the authoritative approved direction.

Development proceeds one reviewed chunk at a time.

Distinguish existing functionality from intended work. Do not reimplement established capabilities without a demonstrated requirement.

Any proposed deviation must identify:

1. The affected approved requirement.
2. The proposed change.
3. The implementation reason.
4. The smallest workable alternative.
5. The effect on the player experience and scope.

Do not silently adopt deviations.

Maintain evidence of what was implemented and tested.
