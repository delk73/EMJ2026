# DIRECTOR SCUT — Epic AI MegaJam 2026 After-Action Report

**Jam theme:** DIRECTOR'S CUT  
**Engine:** Unreal Engine 5.9  
**Platform:** Windows x64  
**Development window:** November 12–15, 2026  
**Human direction:** Solo human-led development, assisted by multiple AI tools  
**Development record:** [`DEVLOG.md`](https://github.com/delk73/EMJ2026/blob/main/DEVLOG.md) *(repository currently private; publish a public copy or an itch.io devlog before supplying this as a public submission link)*

## The game

**DIRECTOR SCUT** places the player at an authenticated remote supervisory terminal for the industrial carrier THETA-9. MOTHER-7, the ship's supervisory intelligence, presents reports, recommendations and consequential decisions. The player may follow her convenient account or investigate the underlying cameras, telemetry and records independently.

The central distinction is between **what happened** and **what the director chooses to put on record**. An official final disposition can be incomplete, provisional or contradicted by retained observations; filing it does not rewrite the simulation's history. The theme, DIRECTOR'S CUT, therefore operates in both senses: directing an unfolding situation and choosing the final account.

## What we set out to test

Could a short, offline-playable mystery give a player meaningful discretion without making a language model the authority over the simulation? We separated three responsibilities:

- **Authored scenario:** MOTHER-7's dialogue, disclosures, priorities, contextual choices, pacing and possible judgments.
- **Deterministic simulation:** the ship's measured state, operational validation and actual consequences, advancing at a fixed 30 Hz.
- **Retained records and official report:** observations, claims and actions remain distinct from Scut's submitted disposition.

The console is intentionally comprehensive, but it is not the primary narrative guide. MOTHER-7 makes ordinary participation accessible; deeper investigation through the console can expose information beyond her summaries. Neither trusting her nor questioning her is designated as automatically correct.

## Human direction and AI tools

The project was directed and reviewed by a human developer. AI systems supported coding, design discussion and iteration, but were not independent creative authorities or a substitute for validation.

| Tool | Role in the development workflow |
|---|---|
| **OpenAI ChatGPT** | Design and project-management discussion, specification and prompt drafting, implementation review, acceptance criteria, and evaluation of the emerging player experience. |
| **OpenAI Codex** | Repository implementation tasks, C++/Slate changes, test development, build/package orchestration, verification reports and development-log updates under human direction. |
| **GitHub Copilot** | Code and development assistance within the editor/repository workflow. |
| **Google Gemini** | Additional AI assistance during development; the specific contributions should be identified from its session records before making more detailed attribution claims. |
| **Ollama / local inference integration** | An optional experimental integration in the project. Core jam gameplay and automated verification operate offline without a running language model. |

**Human decisions mattered:** The human operator established the desired experience, approved changes, observed play in Unreal Editor, rejected interactions that were technically functional but experientially wrong, and determined what made the final cut.

## Four implementation stages

### A1 — Connected opening

Established the ordinary playable session: Director Scut begins already logged into the remote terminal, with a MOTHER-7 briefing. Historical experiments remained available for regression testing, but no longer dictated the opening game flow.

### A2 — Incident composition and retained evidence

Introduced overlapping reports, a disputed dispatch location, an independently retained optical observation and a personnel workload decision. Actions, claims and evidence were kept separate, allowing the player to challenge an account without altering the historical measurement.

An early implementation exposed a major problem: the event-menu command updated internal state, but the normal terminal had no visible menu surface. Correcting the shared menu presentation and testing actual rendered Slate interactions made the mechanic usable.

### A3 — MOTHER-7-mediated engagement

The next problem was more fundamental. Working commands did not amount to compelling gameplay. We redirected the experience around MOTHER-7's proactive communications and contextual responses, while keeping the console fully available for independent investigation.

One authored scenario beat deck became the local source for dialogue, timing, responses and disclosures. A later correction moved remaining hard-coded response meanings into typed, script-owned actions and named beat relationships. The engine continued to validate and execute outcomes; the authored script did not become an alternate simulation authority.

### A4 — Judgments and final report

Completed the opening-to-ending loop. Scut can file a disposition with actual prerequisites, review the resulting official account and inspect the retained underlying record. Guided and investigation-led playthroughs produce materially different reports. Selecting a report label does not invent a sealed room or an executed airlock purge: those outcomes must first exist in the authoritative simulation.

## The difficult parts

The largest source of friction was not compilation. It was the gap between **implemented mechanics** and **the intended experience**.

A technically functioning terminal initially became an overly command-driven, diagnostic-feeling game. MOTHER-7's dialogue was too explanatory and cautious. Menus surfaced raw operational statistics. Tests initially verified internal choice arrays without verifying whether the player could see or use them. These were caught through human play and corrected by tracing the actual player-facing path.

Other notable corrections included stale menu indices mapping to the wrong judgment after prerequisites changed, a legacy results overlay obscuring ordinary endgame controls, and scenario-specific handling embedded in code that was supposed to execute generic authored responses. Each led to focused regression coverage rather than abandoning the underlying system.

The process was exploratory rather than strictly linear. That produced rework, but also clarified the game's central mechanism: **the tension between a conveniently curated account and independently inspectable reality**.

## Verification and release

The A4 complete-game implementation reported 17 PIE tests passing. The subsequent verified presentation/audio release also reported **17 PIE tests passing with no failures or test warnings**. The Windows x64 Shipping package was built, archived and tested with offline startup; the ZIP contained 27 files and passed CRC and SHA-256 integrity checks. Separate guided and investigative automated playthroughs exercised the final-report logic.

These checks establish a working packaged game, not a universal quality guarantee. Clean-machine installation, all possible input/display combinations and the complete real-time player experience were not exhaustively validated. The development log retains commands, test reports, failed intermediate runs, corrections and packaging evidence.

**Latest visual/audio polish and gameplay trailer:** final particulars should be added only after the last build and media export have been verified.

## Pre-jam work and asset disclosure

The jam implementation reused technical infrastructure and experiments that existed before November 12, including portions of the simulation, surveillance/terminal and supporting development workflow. The opening incident, script-mediated engagement and final-report loop were built during the jam. Prior work is acknowledged here rather than represented as wholly new.

**Before submission, complete the asset inventory** using the repository and packaged build: identify any pre-jam code, shaders, content and third-party licensed materials that must be declared. A dependency warning mentioning Fab does not, by itself, establish which Fab assets were included.

## What remains beyond the jam

The shipped experience is a compact proof of the investigation-and-disposition structure, not a fully authored mystery campaign. The current incident is intentionally modest and in places too abstract. Later scenario work could make the central stakes more concrete and add optional discoveries such as raw hexadecimal communications, hidden supervisory records or conflicting directives. Possible MOTHER-7 motives are creative proposals, **not established facts of the shipped scenario**.

A future headless scenario laboratory could generate bounded script variants, execute them against the deterministic model and retain comparable playthrough logs. It was deliberately not added ahead of the jam submission.

## Closing assessment

DIRECTOR SCUT reached a verified, packaged playable loop in a short jam window. Its most valuable result is not the quantity of code produced with AI assistance, but the architecture and design lesson reinforced by repeated human review: **a persuasive report is not the same thing as a true event, and a passing subsystem test is not the same thing as a working player experience.**

The player gets to decide what goes into the official record. The ship keeps its own account.

---

## Suggested itch.io game-page summary

**DIRECTOR SCUT** is a remote-supervision mystery aboard the industrial carrier THETA-9. MOTHER-7 offers explanations, recommendations and decisions. Follow her guidance—or dig through the ship's cameras, telemetry and retained records to form your own view. What you discover and what you officially report need not be the same thing.

**Platform:** Windows 64-bit. **Engine:** Unreal Engine 5.9. **Play:** Mouse to select MOTHER-7's responses; keyboard terminal for contextual replies and operational commands. Type `help` to explore the console. The core game runs offline without Ollama.

## Suggested OpenAI Award response

OpenAI ChatGPT supported human-led design, scope management, authored task specifications, code/diff review and player-experience critique. OpenAI Codex supported implementation in Unreal Engine C++/Slate, test coverage, packaging and verification. We used the agents as directed development collaborators, not as gameplay authorities. The player's official judgments remain separate from deterministic simulated events and retained evidence. The released core game does not require an OpenAI or other LLM API at runtime.

## Suggested Tools Used field

Unreal Engine 5.9; OpenAI ChatGPT; OpenAI Codex; GitHub Copilot; Google Gemini; GitHub; optional/local Ollama integration. Only add other asset, capture or editing tools after verifying they were actually used.

## Public development-log link for jam submission

**Required action:** Publish this AAR as an itch.io developer log or another publicly accessible page. The source link `https://github.com/delk73/EMJ2026/blob/main/DEVLOG.md` currently points into a **private** repository and must not be supplied as the only Glass Box URL. Once the public post exists, paste its actual public URL into the jam's **Dev log link** field.
