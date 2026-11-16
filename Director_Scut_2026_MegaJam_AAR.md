# DIRECTOR SCUT - Final After-Action Report

**Project:** Epic AI MegaJam 2026 - **Theme:** DIRECTOR'S CUT
**Engine/platform:** Unreal Engine 5.9 / Windows x64
**Recorded development:** November 12-15, 2026 - **Documentation closure:** November 16, 2026

## 1. Premise and exploratory intent

Director Scut was an intentionally immersive, exploratory jam project. The theme, constraints and deadline created a space to investigate systems, visual techniques, storytelling and human-AI collaboration. Efficient feature production and commercial polish were not its primary measures of success. Experiments and changes of direction belong in its achievements even when their implementations did not enter the final game.

The resulting game places Director Scut at an already-connected supervisory terminal for industrial carrier THETA-9. MOTHER-7 supplies reports and recommendations; the player can accept them, investigate independently, direct personnel and operations, or withhold agreement. Scut's final disposition is his director's cut. The authoritative record independently preserves what happened. This became the project's strongest relationship between theme, mechanics and narrative.

The central retrospective question is how to preserve the freedom and value of that exploration while reducing the friction that repeatedly interrupted it. The record supports neither a simple productivity success story nor a blanket indictment of AI tools. It shows valuable discoveries, substantial technical support, and repeated human work to reconnect functional systems with the desired experience.

### Evidence and limits

This review uses [DEVLOG.md](DEVLOG.md), [the timestamped summary](DirectorScut_Devlog_Timeline.md), [the canonical plan](Docs/DIRECTOR_SCUT_GAME_PLAN.md), the architecture/opening/agent documents, previously recorded commit checkpoints through `6aad53e`, and selected retained release JSON. This polish pass checked those documents and JSON artifacts without accessing Git history. All chronology times are Pacific Standard Time (UTC-08:00). Commit times are inherited checkpoints, not task start or test completion times; identifiers and present reachability were not verified through Git. Implementation chronology was checked against dated development entries, which do not independently corroborate every identifier. The development log mixes dated sections and later appended corrections; its physical order is not a reliable chronology.

Implementation and verification entries are agent-reported development records unless corroborated here by an inspected artifact. The missing event menu and swallowed input have explicit human-play evidence. The developer's interaction and resource experiences are testimony supplied for this retrospective, not telemetry extracted from Git. Interpretations and recommendations below are retrospective conclusions from those records. No new build, PIE run, packaged playthrough, public-release check or service-usage audit was performed for closure.

## 2. Development chronology and creative evolution

### November 12: attraction, liquid fields and cutting

The repository begins at **08:23 PST on November 12**, commit `2593509`. The 09:01 baseline entry describes an otherwise empty runtime module and empty Content directory. This corrects the previous AAR's unsupported claim that the ship simulation and terminal were pre-jam source reuse. The recorded baseline and implementation entries place their development during the jam; they do not establish every file's provenance.

The first direction explored attraction and spring networks at 120 Hz, drop/slice/reset interaction, liquid heightfields, archetypes and growth. A CPU bitmap presentation gave way to analytical GPU fields and gradients (`dc1d16f`, 10:23 commit). The log reports 16,000 numerical derivative comparisons, while explicitly leaving appearance and performance for viewport review. Driver/Buffer/Anchor interactions then gave way to authorization fields, Builders, trimming, patch movement and grafting. Bilinear reward sampling and target hysteresis reached a checkpoint at `20a4d0b`, 15:23.

These changes investigated different meanings of directing and cutting: manipulating forces, authorizing local growth, severing connections and preserving connected structures. The record does not explain the precise creative decision that ended this direction, so it would be misleading to invent one. It does show technical discoveries in field rendering, graph continuity and interaction. Retained unused texture/tray materials document displacement rather than a failed obligation to ship every experiment. Repeated camera and gesture corrections, however, show that interaction quality could not be inferred from mathematical and wiring checks.

### November 13: ship supervision and systemic consequences

By `e66470a` at 06:59, work had shifted to a clinical spacecraft blockout and nine-door topology. `bd65e1c` at 08:22 records the 30 Hz deterministic ship model, navigation and vacuum ejection. CCTV, console controls, biometrics, scenario beats and crew-follow controls followed. Later checkpoints added experiment endings, an eight-camera wall above a CRT desk, an activation briefing, surveillance suspicion, camera sabotage, crew staging and private agendas.

This was a rich systemic direction: watching could affect behavior; interrupted surveillance could change opportunities; pressure, power, oxygen, movement and manipulation could have consequences. It also exposed integration faults. The architecture record identifies globally closed startup doors, staging/navigation mismatches and agenda routing replacing player orders. Corrections restored traversable interiors, synchronized movement and gave accepted orders priority. These were failures of integrated behavior, not reasons to discard the experiment's premise.

The November 13 consolidation (`6db3a9b`, 21:43) moved numbered implementation units into domain-owned classes and consolidated tests. Explicit ownership helped preserve what had been learned. The earlier covert-experiment lifecycle, suspicion systems and endings remained useful technical history but ceased to define the ordinary final incident.

### November 14: the terminal becomes the experience

The 08:20 documentation pivot (`d231163`) made a unified terminal the active direction. The camera wall became an optional diagnostic view; optics moved into requested inline evidence. Successive CRT iterations explored paneled layouts, full-height teletype, comparative telemetry, prompt completion and a short read-only forensic scrub. The repeated presentation work had creative value: it tested how remote supervision should feel. Its cost rose when each revision left input or menu behavior needing reconstruction.

The typed hybrid director (`0253ecc`, 15:05) separated generated expression from physical authority. The optional local Ollama adapter (`92a8834`, 18:11) explored live inference; the map-first evidence view and six-message transcript followed (`712171a`, 20:05). These were genuine experiments, not prerequisites for offline play. The late architecture alignment (`f915138`, 22:58) explicitly recognized that system events, causal context, MOTHER-7's interpretation and player decisions still needed stronger narrative linkage. A technically capable workstation had not yet become a composed investigation.

### November 15: from experiments to an ordinary playable incident

The continuity experiment checkpoint (`e101337`, 00:03) retained one bounded operational episode beyond the chat window. Its first live test repeated stale readings and invented a notification. Current-state projection, provenance and stricter assertions followed. Selected later live responses distinguished acceptance/restoration from refusal using actual power and oxygen. That is valuable evidence about contextual grounding, not a guarantee of arbitrary generated truth or general narrative memory.

On-demand telemetry and deterministic MOTHER-7 alert routing followed (`230ab00`, 03:07). Human PIE exposed printable shortcuts consuming normal terminal characters. Removing global printable-key operations and testing actual key-down/character/key-up events corrected an interruption to the game's central activity: typing.

The canonical plan crystallized at `f47d92e`, **10:32**. It committed to the connected opening, uncertain incident, bounded contextual responses, independent evidence and state-grounded judgments. This was the point where emerging ideas became an explicit delivery contract; earlier exploration should not be retroactively judged as deviation from it.

A1 (`2e85c5e`, 12:11) removed ordinary activation-command dependence. A2 (`897aa2b`, 13:36) composed reports, a fixed dispatch-location claim, an independently retained optical observation and ORTIZ's workload request. Crucially, the game did not move a crew member merely to manufacture contradiction. User-operated PIE then found that `menu events` changed internal menu state but showed no choices: the unified terminal had no mounted menu surface. A shared visible surface and rendered interaction tests corrected the integration omission.

A3 (`0183398`, 14:58) made MOTHER-7 proactively offer authored responses, including acceptance, investigation, deferral and personnel decisions. It reduced raw numerical/menu instruction copy in ordinary dialogue. A further correction moved response meanings from positional assumptions and hard-coded handling into typed actions and named beat relationships. One local script could then own the account, aliases and disclosure without becoming another physical authority or a general dialogue engine.

A4 (`52d4c04`, 15:51) connected the incident to a filed report and separate retained records. Automated guided and investigation-led paths produced different accounts. Tests caught an old dossier obscuring closure controls and a stale filtered option index selecting another judgment. Label-based submission with execution-time validation corrected the latter. Vented and Contained required actual prior operational outcomes; choosing a label could not invent them.

### Packaging, audiovisual finishing and the submission boundary

Shipping packaging is recorded at `1142eb3` (16:06); bounded presentation, cue and stabilization work at `78b1d95` (16:31). Communication framing, distinct record/report typography and procedural cues helped distinguish MOTHER-7, instruments and the filed account. Silence was deliberately retained at that stage.

The final pass (`6aad53e`, **17:36**) added uneven warm practical lighting, restrained surveillance degradation, improved crew silhouettes and a quiet industrial ventilation/motor bed. These changes gave the same systems a more situated atmosphere. They also exposed problems absent from subsystem tests: an underexposed/silent capture, show-flag/exposure configuration and background-volume muting. Corrected capture and encoding evidence is retained. The trailer used an actual rendered development session and engine audio, with ordinary scripted terminal commands, rather than a fabricated interface. Viewport acquisition was 10 fps and output 30 fps; it is an edited demonstration, not an uninterrupted human playtest.

The release record ends with a verified local package and trailer prepared for the jam. **It does not establish public download availability or a confirmed jam entry.** Release-related log entries explicitly state no upload/submission was performed in those sessions. Public project-page or development-log visibility was not checked; even a visible page would not establish an available build or completed submission. No jam-entry URL or submission receipt was found in the inspected records.

## 3. Experiments, discoveries and technical accomplishments

The deterministic model, subsystem orchestration and validated next-fixed-tick actions gave consequences a stable basis. Replay consumed resolved actions without re-querying inference. This made it possible to test whether an interpretation, a request and a committed action really differed. The same distinction supported the final game's theme rather than merely adding engineering machinery.

Bounded causal retention showed that a refusal matters narratively even when it makes no physical mutation. Pinned optical evidence could survive eviction from the short forensic ring; a final report could omit a discovery while the audit preserved it. These are specific delivered capabilities. They are not persistent saves, full video recordings, complete knowledge ledgers or universal causal memory.

Optional inference was valuable as an investigation of expression, context selection, stale readings and validation. Keeping deterministic MOTHER-7 playable protected the creative experiment from service availability and model installation. Bundled inference, voiced dialogue, directional acoustics and broader authored mysteries remained possibilities rather than delivered features.

Technical exploration also revealed when generalization was premature. Numbered runtime units needed consolidation; response meanings needed a single authored home; provisional layouts exposed disconnected interaction paths. Conversely, the final response action contract was a useful small abstraction because it solved observed reordering and ownership problems. The lesson is to generalize demonstrated integration needs, while letting speculative narrative directions stay cheap.

### Loom provenance: consultation, implementation and limits

Loom was a real development reference, not merely an asset dependency later removed. The retained P2-00 audit documents 421 temporary Loom packages and successful inspection of `MF_Loom_LineMin` and `MF_Loom_RadialSegments_Filled`. This establishes temporary inclusion and reference consultation, not their use in the final material. The current generator builds its own graph and records reference metadata separately; its HLSL is present verbatim in `M_DirectOrScut_Terminal.uasset`. Retained dependency reports and current package-path scans support removal of Loom dependencies, which alone says nothing about source authorship.

Comparison with embedded HLSL in the local Loom originals and retained audit supports **conceptual influence** for line/radial presentation; no recognizable copied or adapted Loom implementation was established in the terminal. Native Slate arcs replace the angular-distance shader, while station contours, ECG traces and explicit phosphor keys provide evidence of separate project implementations. The rounded-box aperture shares ordinary SDF mathematics with `MF_Loom_RectMin`; that similarity does not establish derivation. For the older `TrayGlyphSDF.hlsl` and `LiquidMetaballs.hlsl`, the origin of shared segment-distance and smooth-min primitives remains **undetermined**, rather than proven independent or copied.

No connected binary graph was reconstructed in this audit. The current external radial asset differs from retained historical fragments, so it cannot substitute for an exact historical revision. Complete upstream authorship and applicable terms remain unresolved; Loom's IQ/MIT comments are attribution leads, not verified permission. Human review of those sources and any applicable notices is needed before treating uncertain material as original MIT-covered work. No license or redistribution decision is changed. [The architecture provenance section](DIRECT_OR_SCUT.md#loom-provenance-audit---2026-11-16) records locations, comparisons and limits. None of this establishes pre-jam reuse of the ship simulation or terminal subsystem.

## 4. The resulting game and the evolving vision

The final compact incident supports observing, interpreting, directing, experiencing consequences and rendering judgment. A player can follow MOTHER-7's convenient account or inspect retained optics, respond differently to workload, and file a qualified conclusion. The official account and underlying record remain separate. That is a coherent realization of the theme.

Its mystery is narrower than the broader vision of a roaming disturbance with rich overlapping discoveries and ambiguous supervisory motives. Explained qualifies a location account; it does not solve an authored common cause. No hidden motive or culprit should be supplied retrospectively. Numerous covert simulation systems and diagnostic operations survived outside the main authored route, while the permanent camera wall, activation gate and old timed endings were displaced from ordinary play.

Functional completeness diverged from creative intent when command availability was treated as engagement, cautious explanatory copy substituted for character, and numerical diagnostics crowded ordinary choices. The A3 redirection and late audiovisual pass addressed parts of that gap.

The developer reports that it plays and seems to work, describing the experience as "very meta chaos." That is a human assessment of an open-ended exploratory game, not an independently verified claim about every route or condition. A single "full playthrough" is not an appropriate overall completion benchmark. The record does not establish every intended quality of tension, humor, pacing or immersion; that limit does not negate the reported playability or the discoveries made while pursuing it.

## 5. AI-assisted development and collaboration

ChatGPT supported design discussion, specifications, prompts and review; Codex supported implementation, tests, documentation and release orchestration. GitHub Copilot and Gemini were also used. The inspected history does not support a precise per-feature attribution or comparative effectiveness judgment for the latter two. Commit authorship and agent-written logs are not a complete tool-session ledger.

AI assistance supported exploration across unusually different technical and creative areas. It could turn a bounded question into executable code, supply regression coverage and preserve intermediate failure evidence. Human direction remained essential: the developer established the desired experience, rejected technically working but unsuitable interactions and supplied observations automation had missed.

Continuity was uneven. Repeated corrections around opening intent, ordinary typing, visible choices and script ownership show that established decisions were not always carried through the whole implementation. Responsibilities blurred where scenario copy lived partly in the script and partly in generic execution, and where subsystem menu success was mistaken for presentation success. Those are observable coordination failures; the record cannot attribute each to a specific model or count all corrective prompts.

The canonical plan and ownership boundaries improved continuity once they existed. Yet long status documents also accumulated contradictions: before closure reconciliation, the architecture introduction described the scenario as unimplemented despite later A4 entries, and agent guidance described B/C as not started after release work. Documentation sometimes repaired drift afterward instead of preventing it. The five-part deviation rule and repeated stop-for-review wording protected scope, but their application could make small authorized corrections excessively procedural. The durable value was preserving intent and authority boundaries, not maximizing approval exchanges.

For a future exploratory effort, concise instructions should retain the current player experience, important decisions, ownership and present experiment. A short distinction between an idea, a working experiment, an approved commitment and a deferred possibility would reduce accidental promises. It should remain editable as discoveries change the direction. Improvement would be visible when an agent can continue the next task without reconstructing the opening or asking again about an already-authorized correction.

## 6. Productive exploration and unproductive friction

The attraction-to-ship-to-terminal progression expanded the design space. Liquid gradients, systemic surveillance, local inference and forensic evidence each produced inspectable discoveries. Their worth is not proportional to their final screen time. A pivot becomes wasteful when the same decision must be rediscovered or integration breaks without producing new understanding.

The developer reports sustained interaction drag: unnecessary corrections, repeated supervision, quota-reset prompts, unexpected usage depletion and a perception of pressure to spend more. These experiences reduced continuity and creative momentum by making attention return to managing the collaboration and its availability. The repository does not quantify those interruptions, establish billing or quota mechanics, or support claims about provider intent. The reported spending pressure is preserved as a perception, without assigning an unverified cause.

A concise resumable handoff could preserve the interrupted task and settled decisions while noting repeated corrections or resource interruptions. Improvement would mean fewer repeated instructions and fewer lost exploration sessions; more generated code alone would not demonstrate it.

## 7. Practices worth preserving

Preserve the freedom to build a working experiment before deciding whether it belongs in the final game. Preserve human authorship of tone and stakes, and the ability to reject a functional result on experiential grounds. Retaining the initial growth experiments and superseded surveillance history makes their discoveries available without claiming they shipped.

Preserve deterministic authority, typed actions, commit-time validation and provenance. These were both safety boundaries for inference and creative enablers for a game about disputed accounts. Preserve the offline route and bounded records: they kept the incident playable without making a model or a save-system project mandatory.

Preserve focused regression checks, real Slate input/rendered-control coverage, explicit skip reporting and intermediate failures. Preserve packaging manifests, offline configuration checks and the correction of the ineffective Shipping INI override. These practices made stronger claims possible while also making their limits visible.

## 8. Lessons and practical improvements

**Experience the representative interaction sooner.** Internal menu arrays passed while the player saw no menu; accelerated reveal and stepping did not establish pacing. This interrupted exploration by making the developer repeatedly discover basic access problems. Next time, pair an important new mechanic with a short ordinary session: read a report, type, inspect evidence, choose and hear the result. Keep technical assertions separate from a human observation of atmosphere and feel. Improvement is a usable, representative interaction appearing alongside the experiment, with fewer late interface surprises, rather than an early frozen design.

**Keep discoveries and decisions in one understandable place.** The opening changed from experiment activation to an already-connected incident; stale descriptions survived. This forced supervision and obscured what was still open to exploration. Retain a concise current direction and link to historical evidence instead of duplicating status across many documents. Improvement is agreement between the next task, the current game and its introduction, while old discoveries remain accessible as history.

**Use contracts at real integration boundaries.** Staging, agendas and player orders conflicted; later, response indices acquired story meaning. These problems consumed correction time without opening new creative territory. Preserve physical ownership and stable response identities, and exercise the complete input-to-consequence path when a boundary changes. Improvement is that reordering authored choices or changing presentation does not silently change an action, and a player order retains its documented priority. No additional framework is needed to achieve that.

**Make supervision proportional to uncertainty.** Explicit approvals helped distinguish a proposal from implemented scope, but repeated handoffs and reconstruction competed with immersion. Carry forward existing authorization and ask only when a material new decision is required. A useful handoff states what was learned, what changed and what remains uncertain. Improvement is less corrective prompting while the developer still controls creative commitments.

**Treat audiovisual experiments as evidence about the game.** Lighting, silhouettes and ambience changed the felt setting late; capture faults required direct viewing and audio inspection. Try a representative image, communication rhythm and sound bed while the direction is still exploratory, then evaluate them in ordinary play. Improvement is earlier evidence about mood and intelligibility, rather than a final capture revealing that the experience is dark or silent. This is an experiential probe, not a demand for early polish.

**Mark the transition to release stabilization deliberately.** The explicit plan arrived on the final day, and opening-to-ending composition, packaging and atmosphere followed within hours. This concentrated integration and acceptance pressure. When a representative experience becomes worth delivering, name what will make the cut and park remaining experiments with their discoveries intact. Improvement is a known local release candidate whose verification can finish without repeatedly changing the premise. Deadline delivery need not invalidate the preceding open exploration.

**Keep evidence claims narrow.** A4's full run was 16 successes plus one warning, followed by focused corrections; the later presentation suite was 17 clean passes; the final combined run was 17 successes and one legacy failure. Collapsing those into "all tests pass" loses useful information. Likewise, ZIP integrity, offline startup, human play and public submission answer different questions. Improvement is that every closure claim names the kind of evidence it actually has.

## 9. Final state, limitations and closure

The final local Windows x64 Shipping package and authentic gameplay trailer are documented at `6aad53e`. Inspected `Saved/Releases/FinalEvidence/ArchiveVerification.json` records **27 files**, **383,365,896 archive bytes**, CRC PASS and all-entry SHA-256 agreement with the source distribution. Its archive SHA-256 is `69e74fa17321e2f01160fc452709bdb565ff528ecc6d8be39f27561eaaed8487`. The retained JSON was inspected during this polish pass; the archive was not rehashed. Trailer verification records approximately **61.8 seconds**, 1920x1080 H.264, AAC stereo and a successful decode.

The inspected final atmosphere summary records **17 succeeded, 0 succeededWithWarnings, 1 failed, 0 notRun**. All current PIE tests passed; legacy `DirectOrScut.Chunk2.SimulationPIE` failed assertions concerning map topology, projection, routing, pressure and crew behavior. The development log identifies these as legacy expectations; this review does not independently diagnose each failure. Earlier baseline reproductions of legacy failures do not prove that every final assertion was reproduced separately. The combined final run is not all green.

Preflight and Shipping success, offline opening/camera captures and a responsive 30-second normal launch are documented. They do not establish clean-machine installation or exhaustive route, display and input coverage. The developer reports that the open-ended game plays and seems to work; no specific packaged test session or route coverage is asserted by that observation. Generated release artifacts are documented under ignored `Saved/`, outside the source distribution; tracking status was not rechecked during this pass. Public availability, final submission status and a complete asset/provenance inventory remain unresolved in the inspected record. The previous unsupported pre-jam reuse assertion is withdrawn.

Director Scut is closed as a jam-scale exploratory project with a composed offline investigation-and-disposition loop, substantial retained experiments, documented release artifacts and explicit limitations. Learning and creative discovery were intentional outcomes. The most consequential process lesson is to make technical rigor serve direct exploration of the experience, while reducing repeated context repair and supervision. Historical plans and deferred possibilities remain records, not an active backlog. This closure initiates no further development, publication or submission.
