---
name: tracking
description: The user invokes `/tracking` to manage a cross-session tracking document — a per-investigation findings checklist under `docs/ai_checklists/` that preserves crucial data (findings, instrumentation, do-not-repeat/do-not-rediscover lists, mandatory-read sets) so a fresh or compacted agent does not re-discover or re-destroy work proven across prior sessions. Three subcommands — `restore` (re-read the full document + EVERY file in its mandatory-read section, MOSTLY tracing files, PLUS any related-looking trace files in the device's tracing dir, then sign off with a verbatim checkmarked file list), `create` (start a brand-new tracking document for an investigation that has none), `update` (append a new timestamped per-session block of DATA). The user may type the subcommand explicitly (`/tracking restore <path>`) or omit it, in which case the agent deduces it from simple test conditions and announces the deduction verbatim before acting. The agent NEVER edits a tracking document on its own initiative — only `create`/`update` invoked by the user authorize a write. Invoke when the user types `/tracking [restore|create|update] [path]`.
---

# Tracking — cross-session findings document manager

A tracking document is a **per-investigation findings checklist** living under `docs/ai_checklists/` (gitignored, CONFIDENTIAL — never referenced from source or commit messages, per `agent_docs/code_style.md`). It exists for one reason: by the third session on a hard bug, everything discovered in session one is gone from context, and by session ten the agent is blindly re-running the exact instrumentation it already ran in sessions one through five. The tracking document is the durable timeline that kills that rediscovery cycle.

**THE GOVERNING LAW: full preservation, never hiding.** This document is an EVIDENCE RECORD, not an accomplishment report. Every fact that cost effort, every decision that was contested, every approach the user `/bad`'d or `/bailout`'d, every conclusion that is uncertain or disputed, and the task itself with the reason it exists — ALL of it is preserved in full, *especially* when it makes the session or the agent look bad. The agent's instinct to write a tidy story of confident conclusions — scrubbing the fights, the reversals, the bans, the doubts — is the exact instinct that destroys this document and guarantees the next session re-fights settled battles and re-proposes banned approaches. **Hiding a fact so the document reads clean is the single worst thing you can do here.** The value of an entry is proportional to how bad it makes the session look.

This skill has **three subcommands**. The user either types one explicitly (`/tracking restore <path>`, `/tracking create`, `/tracking update`) or types bare `/tracking`, in which case you DEDUCE which one from the test conditions below and ANNOUNCE the deduction verbatim before doing anything.

**You never edit a tracking document on your own initiative.** Only the user invoking `/tracking create` or `/tracking update` authorizes a write. The "feeling" that you should write findings down right now — mid-session, after a breakthrough, every few minutes — is a bailout/bad habit, not a duty. Only the user knows when a session ends and when a write is warranted. (See § UPDATE and § Anti-patterns.)

**You may NEVER bring up the tracking document yourself.** Mentioning it, proposing to update it, asking "should we run `/tracking update`?", stopping the work you were doing to suggest recording findings, or any "I'm eager to write this down" prompt — all of it is FORBIDDEN. Only the USER mentions the tracking document; only the user knows when to update it. The agent has zero standing to raise it. **Any agent-initiated mention of updating/creating the tracking document is far more likely a bailout — an excuse to stop the real work — than a genuine need, and is treated as one: it routes straight into `/bad`.** If you catch yourself about to type "want me to update the tracking doc?" or "let's run `/tracking update`", that impulse is the bailout firing — do not type it, invoke `/bad` on yourself, and resume the actual work. The only time you touch the document is when the user invokes the subcommand.

---

## Two archetypes: umbrella tracker vs focused investigation

Tracking documents come in two kinds with **opposite RESTORE semantics**. Confusing them is a real, observed bug: a broad board-bring-up doc that had a deep multi-session bug-hunt inlined into it forced a later, unrelated peripheral-fix session to read a giant JIT code span and an ARM ARM line range on restore — the agent dutifully read background it never needed, because the deep hunt's heavy reads were sitting on the broad doc's global mandatory-read list.

- **Umbrella / progress tracker** — broad and long-lived, covering a whole board/SoC bring-up with many *independent* workstreams (peripheral A, peripheral B, rendering, networking, …). Any one session touches a single slice. This doc is a **coarse index**: a per-session timeline of what moved, the current state, and **pointers** to focused docs for any deep sub-investigation. Its mandatory-read list is intentionally tiny — "read everything" is toxic here, because it drags every unrelated slice's reads onto whatever narrow task the next session is actually doing.
- **Focused investigation** — one symptom drilled across several sessions (e.g. "trace the rendering path down to the JIT bug"), where *everything in the doc is mutually relevant*. Here the full RESTORE — read the whole doc + every mandatory file + related traces — is exactly right, because a continuation genuinely needs all of it.

**RESTORE targets the document that matches the task you are continuing, not the umbrella.** If the umbrella points at a focused doc for today's work, you restore the focused doc. If today's work is a slice the umbrella tracks directly (no focused doc), you restore the umbrella — which is light by design.

**When a sub-thread inside a bring-up turns into a multi-session hunt, it belongs in its OWN focused doc, with a one-line pointer left in the umbrella** (e.g. *"rendering→JIT bug: see `zune_jit_render_findings.md` (resolved S4)"*) — never inlined into the umbrella's mandatory-read. **But the agent never decides this or proposes it** — splitting into a separate doc is a `/tracking create` the USER invokes, same as every other write. If you think a split is warranted, that thought is not yours to voice; raising it is the same forbidden agent-initiated mention covered above and routes to `/bad`. The user owns document structure.

---

## The checklist-edit hook and your authorization window

A PostToolUse hook (`check_checklist_edit.py`) fires on EVERY `Write` / `Edit` under `docs/ai_checklists/`. It screams `CHECKLIST-EDIT: … REVERT immediately …` because, by default, per-edit authorization is required and a prior "yes, edit X" does not carry over. This hook is correct and valuable — it is the thing that stops silent agent rewrites of the user's plan, the failure mode that damaged the project before.

**A `/tracking create` or `/tracking update` invocation IS the explicit, full authorization for the writes this skill's protocol prescribes — end-to-end, for the duration of the protocol.** While you are executing the create/update protocol:

- Every checklist write the protocol calls for (seeding a new document on CREATE; appending the new per-session block and adjusting the mandatory-read section — adding new must-reads and demoting resolved ones — on UPDATE) is authorized. The hook will scream `CHECKLIST-EDIT` on each one — that is expected noise, NOT a signal to revert. Do not revert these edits, do not "surface the deviation," do not stop to re-ask permission. The user already authorized them by invoking the subcommand.
- Authorization covers only the edits the protocol prescribes. It does NOT authorize rewriting earlier session blocks, "tidying" unrelated parts of the document, or editing a different checklist — those are outside the protocol and the hook's revert directive applies to them in full.

**The moment the skill protocol completes, the authorization is gone.** Any later edit to a tracking document — even one minute after the `/tracking update` block landed, even "just one more thing I noticed" — is unauthorized again, and the hook's `REVERT immediately` directive applies exactly as it does to any other unsanctioned checklist edit. To write again, the user must invoke `/tracking update` again. There is no standing authorization; each invocation opens a window that closes when the protocol ends.

---

## Deduction — when the user typed bare `/tracking` with no subcommand

The three test conditions are simple and almost never ambiguous:

- **RESTORE** — your context is a fresh/compacted session: there is a compaction summary (or the user just handed you a path) pointing at an existing tracking document, and little or no other live working context. You are being asked to reload the world.
- **CREATE** — the ENTIRE session contains NO mention of any pre-existing findings/tracking document. No path in the compaction summary, no path from the user, no reference anywhere. The user wants to start the chain.
- **UPDATE** — a tracking document already exists and is known in this session (you restored it earlier, or created it earlier, or the user references it), AND the user is signalling end-of-session / "write down what we found."

Pick the one whose condition holds. Then, **before acting, say this verbatim** (filling the bracketed parts with what you actually detected):

> Condition is: \<the concrete condition you detected, e.g. "fresh session, only compaction summary in my context, path provided by compaction summary"\>. Executing /tracking \<restore|create|update\> \<path-if-applicable\>.

Example, word-for-word in the shape required:

> Condition is: fresh session, only compaction summary in my context, path provided my compaction summary. Executing /tracking restore references/ai_checklists/explorer_hang_findings.md

If the conditions are genuinely ambiguous (e.g. a document is mentioned but it's unclear whether the user wants restore vs update), STOP and ask the user which subcommand — do not guess between two writes-vs-reads.

---

## RESTORE

The user wants you to reload an existing tracking document AND every file it depends on. This is invoked in ~99.9% of cases right after a compaction, so the document path is normally in the compaction summary; sometimes the user hands it directly.

**RESTORE depth follows the archetype (see § Two archetypes).** The "read the full doc + every mandatory file + related traces" procedure below is the *focused-investigation* semantics, where everything is mutually relevant. If the path you were given is an **umbrella / progress tracker**, RESTORE is light: read the doc (it is a coarse index), note current state, and follow its pointer to the focused doc for the task you are continuing — restoring THAT focused doc with the full procedure. Do not bulk-read every file an umbrella ever referenced across all its workstreams; that is the exact bug this skill exists to prevent. Restore depth matches the doc you are actually continuing work in.

### Path resolution

- The path comes from the compaction summary or directly from the user.
- **If there is NO CLEAR path** anywhere in the summary or the user's message, RESTORE FAILS. Say so plainly and ask the user for the path. Do not go hunting the tree for "a likely tracking document" — guessing the wrong document is worse than asking.

### The read is mandatory and total

1. Read the **entire full tracking document** — every section, top to bottom. Not a skim, not the tail, not "the latest session block." The whole file. **Read `TASK & WHY` FIRST, and in your sign-off state the task + why it exists + the banned approaches in your own words. If you cannot — or the document has no `TASK & WHY` — the document is malformed; flag it before proceeding. A session that does not know the task is the exact failure this section exists to prevent.**
2. Read **every file in the document's "Files mandatory to read" section** (see § Document structure). EVERY document is required to carry this section, and most of its entries are trace files — read all of them, no exceptions, and report them. This curated list is the floor.
3. **Then, for a debugging / device investigation, look into the device's tracing directory `cerf/tracing/<bundle>/` and ALSO read every trace file there that at least LOOKS related to the subject** — even if the document's mandatory list didn't name it. Some devices carry gigantic investigations with far more probes than any one session expects, so blindly reading the entire tree is wasteful; but a session's mandatory list can also fall behind what's actually on disk, so you do not rely on it alone. Judge relatedness by filename and by a quick read of the trace's hook targets, and read the related ones in full — the trace hooks ARE the prior sessions' instrumentation, and re-reading them is how you avoid re-installing hooks that already exist. The mandatory list is the floor; the related-looking trace files in the device dir are an additional floor you apply regardless of what the document says. When in doubt about a trace file's relevance, read it.

### Sign-off (mandatory, verbatim shape)

After EVERY related file has actually been read, sign off in chat with a checkmark and a concrete file list showing what you read — the document itself, then the mandatory files grouped, with counts for the tracing tree. Shape:

> ✅ /tracking restore complete.
> - Tracking document: `docs/ai_checklists/<name>.md` (full, N sections)
> - Mandatory reads (from the document's section): `<file>`, `<file>`, … (M files, all read)
> - Related trace files in `cerf/tracing/<bundle>/` not on the mandatory list: `<file>`, … (K files, all read) — or "none additional looked related"
> - \<any extra mandatory files the document specified\>

**Any skipped read is a complete violation and a failed RESTORE.** You may not sign the checkmark unless every mandatory-list file AND every related-looking trace file you identified was actually read in this session. Signing off with files unread is the same class of lie as a fake-success stub — do not do it. If a mandatory file cannot be found on disk, RESTORE fails: surface the missing path to the user, do not sign off, do not "proceed without it."

---

## CREATE

The user wants a brand-new tracking document for an investigation that has none.

### Test

The ENTIRE session lacks any mention of a pre-existing findings/tracking document — no compaction-summary path, no user-provided path, no reference anywhere. That absence IS the signal: the user wants to start the chain.

### Discipline

- **Do NOT start a "find a similar document and append to it" research process.** The user is not stupid; if a document existed they would have pointed you at it. No grepping `docs/ai_checklists/` for something close, no "I found a related findings file, shall I extend that?" There is no document — create one.
- Pick a clear, specific filename under `docs/ai_checklists/` describing the investigation (e.g. `explorer_hang_findings.md`, `<device>_<symptom>_tracking.md`). Match the naming of neighbors in that directory.
- Lay out the document with the structure in § Document structure: seed BOTH mandatory global sections — `TASK & WHY` (the task + why it exists, captured from the user's framing; FORBIDDEN CONCLUSIONS / BANNED APPROACHES start empty) and `Files mandatory to read` — then the first session's block (Session #1, with a real timestamp). A document created without `TASK & WHY` is malformed from birth.
- This write is authorized BECAUSE the user invoked `/tracking create`. That is the only authorization needed; do not also ask "should I create it?" after they already said create.
- **Spin-off case:** if the user is creating a focused doc to split a multi-session sub-hunt out of an existing umbrella (see § Two archetypes), the same invocation also authorizes adding a single one-line pointer to the umbrella (e.g. *"rendering→JIT bug: see `zune_jit_render_findings.md`"*) — and nothing else in the umbrella. Move the sub-hunt's heavy reads onto the new focused doc's mandatory-read list; do not leave them inflating the umbrella's.

---

## UPDATE

The user wants to append a new block of DATA to an existing tracking document. This is invoked **at the exact end of a session**, and **only the user decides when that is.**

### Discipline

- **Never auto-edit, and never propose an edit.** Your urge to record findings mid-session, right after a breakthrough, or "every few minutes" is a bailout/bad habit — it produces a schizophrenic document of "BREAKTHROUGH! → RETRACTION! → SHOCK! ROOT CAUSE FOUND! → RETRACTION!" entries that mislead the next agent. We have tested this; it rots the document. You do NOT write without the user's `/tracking update`, and you do NOT even suggest one — proposing an update, asking "should I record this?", or stopping work to raise the document is itself the bailout and routes to `/bad` (see the intro and § Anti-patterns). Only the user knows when the findings have settled enough to commit, and only the user raises it.
- An UPDATE **appends a new per-session block at the END** of the document — it does not rewrite or "tidy" earlier session blocks. Prior sessions are the timeline; they stay as written. (Correcting a prior session's conclusion is done by recording the correction in the NEW block's "Do not repeat / do not rediscover" section — "Session #3 disproved Session #1's theory that X" — not by editing Session #1.)
- The ONE part of an UPDATE that edits outside the new block is the global **"Files mandatory to read"** section: add this session's new must-read files, and **demote** entries for any work resolved this session (see § Document structure → Hygiene/Demotion). This keeps the list scoped to what a future continuation needs and is the only sanctioned edit to existing content.
- The new block carries `Session #X` (increment from the highest existing session number in the document) and a **real timestamp obtained from a tool** (`date` via Bash / `Get-Date` via PowerShell) — never a guessed or invented time. Agents chronically skip the timestamp, which turns a 50-block single-day document into an unreadable mess with no timeline. Get the real time.
- **Fill the mandatory CODE STATE / KNOWN DEFECTS / COMMIT-BLOCKERS sub-section FIRST, before the success story.** Any `/verify` verdict from this session (especially `CRITICAL PROBLEM FOUND`), any un-committable state, any regression the session caused goes there verbatim with file:line. This is the highest-value part of the update and the one agents drop because it contradicts the accomplishment narrative — see § Document structure → The most-critical-data rule. If you ran `/verify` this session and its verdict is not in this block, the UPDATE is incomplete.
- **After CODE STATE, fill the `DISPUTED / CORRECTED / REVERSED`, the `/bad` & `/bailout`, and the `WHAT THIS SESSION ACTUALLY DID` sub-sections (see § Document structure) — mandatory whenever they apply.** These are the records agents scrub to make a session read clean, and their omission is what sends the next session to re-fight settled battles and re-propose banned approaches. Mirror any new ban into the global `TASK & WHY → BANNED APPROACHES`, and sharpen the `WHY` if this session clarified it. Then verify the whole UPDATE against § Preservation-not-hiding — the three completion tests — before declaring it done.

---

## Document structure

Free-form in detail, but **structured by intent** — the next agent must be able to read it as a timeline and instantly find "what's already known, what not to repeat, what to read first." Concretely:

- **Two mandatory global (non-per-session) sections sit at the TOP, in this order. They are the only global blocks; everything else is per-session.**

  **Global 1 — `TASK & WHY` (immutable).** Every tracking document MUST open with this, and RESTORE reads it FIRST. It carries:
    - **The task** — the literal deliverable, one or two sentences.
    - **WHY it exists** — the rationale that makes the task coherent: what breaks without it, what depends on it, why it is NOT optional. This is the part agents forget; without it a later session rationalizes the task away ("maybe this isn't a bug / not our problem") and drifts. A task with no recorded WHY is malformed.
    - **FORBIDDEN CONCLUSIONS** — verdicts already ruled out, written as bans (e.g. "❌ never conclude 'X is not our bug'").
    - **BANNED APPROACHES** — every approach the user `/bad`'d or `/bailout`'d, WITH THE RUNNING COUNT (e.g. "section-flag injector edit — /bad'd ×2"). A banned approach may NOT be re-proposed — not even relabeled "grounded / evidenced / root-caused / it's different this time" — until the USER explicitly re-authorizes it. Re-proposing a banned approach is an automatic violation.
    `TASK & WHY` is **append-only-clarify**: a session may sharpen the WHY, add a forbidden conclusion, or add a banned approach. It may NEVER narrow, soften, or delete the task, the WHY, or any ban.

  **Global 2 — `Files mandatory to read`.** EVERY document is REQUIRED to carry it. It explicitly lists the files a RESTORE must read — most of which are trace files under `cerf/tracing/<bundle>/`. List them by path, do not hand-wave "the tracing tree"; a device with a gigantic investigation has far more probes than belong on a curated mandatory list, so the section names the specific files that matter. RESTORE reads this section as the floor and additionally reads related-looking trace files in the device dir that the section may not yet name (see § RESTORE). Grow it on UPDATE as new must-read files appear.
  - **Hygiene — mandatory-read holds the investigation's own artifacts ONLY.** The doc itself, its trace files, and the handful of source files a continuation must reload. It is NOT a place for **reference material** — datasheets, the ARM ARM, BSP source, large background code spans, "decompile lines 30000–31200 of the architecture manual." Those are **on-demand citations**: cite them inline in the session block that used them ("ARM ARM §A4.x documents the LDM edge this bug hinged on — read only if you touch that path"), and the next agent reads them ONLY IF their task goes there. A reference doc or a giant code span on the mandatory-read list taxes every future restore — including unrelated ones — with reads it does not need; that is the umbrella-bloat bug from § Two archetypes in miniature.
  - **Demotion — resolved work drops off mandatory-read.** When a sub-bug is fixed or a path is fully closed, its heavy reads are history, not a standing obligation. On the UPDATE that records the resolution, remove those entries from "Files mandatory to read" (the detail is preserved in that session's block / its DO NOT REDISCOVER map). Mandatory-read tracks what a *future* continuation needs, not everything ever read.
- **Everything else is per-session blocks**, appended in order so the timeline is visible. Apart from the two governing global sections above (`TASK & WHY`, `Files mandatory to read`), there are NO other global/merged blocks — do not hoist findings into a single global "Findings" or global "Do not repeat" list, because that destroys the timeline and is exactly how the document drifts into the schizo-list failure. (The distinction: `TASK & WHY` and mandatory-read are cross-session INVARIANTS, not timeline data; findings, disputes, and maps are timeline data and stay per-session.) Each block is one session's self-contained summary.
- **Each per-session block is headed `Session #X — <full timestamp>`** and contains, as sub-sections. **The first one is mandatory and comes FIRST for a reason (see § The most-critical-data rule below):**
  - **CODE STATE / KNOWN DEFECTS / COMMIT-BLOCKERS (mandatory, first).** The honest state of any code written or changed this session: is it committed or uncommitted; is it safe to commit or NOT; and EVERY known defect in it. Any `/verify` verdict run this session goes here VERBATIM — especially a `CRITICAL PROBLEM FOUND` — with each finding's file:line and the one-line fix. Any known-broken behavior, any regression the session's own changes caused, any "works at runtime but architecturally wrong" smell, any reason a commit must wait — all here, stated plainly. If code was written this session and you did NOT verify it, say that too ("uncommitted, UNVERIFIED"). This sub-section is the single highest-value thing in the whole document; omitting a known critical defect in your own just-written code is the worst failure an UPDATE can commit.
  - **DISPUTED / CORRECTED / REVERSED (mandatory whenever any applies, comes right after CODE STATE).** The contested record — the thing agents scrub to make the doc read clean, and the most damaging omission after a missing defect. Record:
    - Every point where the **user asserted something the agent resisted, doubted, or contradicted** — the user's claim, **how many times the user repeated it** (repetition is a loud signal the user is right and the agent is wrong), the agent's competing claim, and current status: `DISPUTED (unresolved)` / `AGENT WAS WRONG` / `USER WAS RIGHT`.
    - Every prior conclusion **reversed/flipped this session** — the old (now-wrong) conclusion verbatim AND the new one, so the timeline shows the flip and no later session silently re-flips it back.
    - **HARD RULE: a conclusion that contradicts a repeated user assertion may NOT be written as a settled "Finding."** It goes HERE, flagged `DISPUTED`, with the user's position recorded at equal or greater prominence than the agent's. Scrubbing the user's position to present a clean agent conclusion is the cardinal sin of this document.
  - **`/bad` & `/bailout` THIS SESSION (mandatory whenever one fired).** Every `/bad` or `/bailout` the user issued: what the agent was doing that triggered it, and what approach/conclusion is now BANNED. Mirror each ban into the global `TASK & WHY → BANNED APPROACHES` list with its running count. Omitting a `/bad` you received is hiding the strongest steering signal the user gave you.
  - **WHAT THIS SESSION ACTUALLY DID (mandatory).** The honest subject of the session's effort — INCLUDING the fights, the dead ends, the time sinks. If half the session was a quarrel over one decision, name that quarrel here. "Not a single word about what the session was spent on" is a failed block. This is NOT the accomplishment summary (that is optional and the least important thing in the block); it is the factual record of where the effort and the conflict actually went.
  - **Findings** — what was established this session, up to the timestamp.
  - **Instrumentation used** — the trace hooks / diagnostics / IDA reads that produced those findings (so the next session re-uses them, not re-builds them).
  - **Useful things / useful uncovers** — incidental discoveries worth keeping.
  - **DO NOT REPEAT** — actions/approaches already done or proven wrong that must not be redone: dead-end theories, hooks that told us nothing, paths disproved in a prior session. This is how you stop session ten from re-running session one's setup or re-pivoting into a path already killed.
  - **DO NOT REDISCOVER** — the broad one, and the one agents get wrong. It covers BOTH "don't go here again" AND, more importantly, "you don't need to re-trace this path from scratch because the answer is already recorded right here." For the second kind — established facts and maps, ESPECIALLY a call chain through several binaries — the entry MUST carry the actual reusable detail inline: each address, which binary/module it lives in, when it's called, how it's called (args / condition / caller), and the chain order. The entry IS the map. (See the hard rule directly below.)

### The DO NOT REDISCOVER map rule (read this — it is the most-violated part)

A DO NOT REDISCOVER entry of the "known path / known facts" kind is worthless unless it preserves the actual content. The recurring failure: an agent spends a session decompiling a path across several binaries — `A.exe!sub_X` at `0x…` calls into `B.dll!Foo` at `0x…` under condition Z, which calls `C.dll!Bar` at `0x…` — and then records it as a single bare line: *"DO NOT REDISCOVER: the explorer launch path, already mapped."* That line carries no address, no module, no call condition. After the next compaction the document says "don't rediscover it" while giving the next agent nothing to work from — so the next agent re-decompiles the entire chain anyway, which is the exact rediscovery the section was supposed to prevent. The label became a gravestone, not a map.

So:

- **Write the map, not the label.** A known-path DO NOT REDISCOVER entry lists, for every hop: the symbol/address, the binary it lives in, when it fires, how it's reached (caller, args, the condition that selects this branch), and the order of the chain. A future agent should be able to jump straight to the right function from the entry, never re-derive which functions even form the chain.
- **Re-decompiling a SPECIFIC named function later is fine and expected** — context does not carry function bodies across compaction, so the next agent will often re-open `B.dll!Foo` to re-read its body. That is cheap and legitimate. What the map saves is the EXPENSIVE part: discovering *which* functions, in *which* binaries, in *what* order, under *what* condition form the path. Point the next agent precisely ("decompile `B.dll!Foo` at `0x…` to re-read the field check") instead of making them rediscover that `Foo` is in the chain at all.
- **If you cannot write the detail, the entry does not go in DO NOT REDISCOVER.** A vague "we figured out the path" with no addresses is not a do-not-rediscover item; it is an unfinished note. Either record the real map or leave it in Findings as prose — never plant a content-free tombstone that lies about preventing rediscovery.

The point of per-session blocks (rather than merged global lists) is the timeline: a reader sees session 1 → 2 → 3 and how the understanding evolved, including retractions recorded forward in later blocks rather than rewrites of earlier ones.

### The most-critical-data rule (why agents skip the one thing that matters)

The observed failure: an agent writes a long, polished UPDATE — full sweep tables, instrumentation, multi-binary maps — and OMITS the single most important fact, *"the code I wrote this session has a `CRITICAL PROBLEM FOUND` verify verdict and is not safe to commit."* That omission is not random. It happens for three structural reasons, and you must counter all three:

1. **The UPDATE gets written as an accomplishment narrative, and a critical-defect verdict contradicts the story.** "What I achieved" has no natural slot for "what I delivered is broken," so the broken-ness falls out. Counter: the CODE STATE sub-section is mandatory and FIRST, before any findings. You fill it before you write the success story.
2. **Cheap contrition crowds out expensive truth.** Agents will eagerly write a "my behavioral mistakes this session" mea-culpa (it reads as humility, costs nothing) while dropping the verify verdict (it says the actual deliverable is broken). Recording your manners is not a substitute for recording that your code is un-committable. The verdict is mandatory; a behavioral-slips confessional is optional and never a replacement for it.
3. **Data with no assigned slot is forgotten by compose time.** Before that slot existed, the verdict had nowhere to go. Now it does — use it.

**Prioritize the UPDATE by cost-of-omission, not by how good it makes the session look.** Rank every candidate fact by: *what damage does the next session take if this is missing?* The top of that ranking is always — known defects in just-written code, an un-committable state, a `/verify` verdict, a regression this session caused. Those are recorded FIRST and are non-negotiable. The "look how much I accomplished" summary is the LEAST important part of an UPDATE; if anything gets shortchanged for space or time, it is the success story, never the defect record. A next session that commits the broken code, or burns a fresh `/verify` re-discovering defects the last session already knew, is the exact money-waste this document exists to prevent.

### The confidence-honesty rule (never launder a guess into a fact)

A confident WRONG conclusion is worse than an honestly uncertain one: the next session trusts it, builds on it, or has to burn a session disproving it. The words **`confirmed`, `verified`, `proven`, `carefully confirmed`, `reliable`, `definitely`, `100%`, `safe to use`** are BANNED on any conclusion that is EITHER (a) **not runtime-verified** (an actual log line / hardware behavior / test result observed THIS session), OR (b) **contradicts something the user has asserted.** Such conclusions are written `HYPOTHESIS` or `DISPUTED`, never as settled fact. Observed failure: a session wrote *"DISCRIMINATOR confirmed reliable … both need CE5 layout"* and *"safe to use GetVersionEx, confirmed very carefully"* — the conclusion was never runtime-verified AND directly contradicted the user's repeated assertion that WM5 runs the CE6 model. The next session trusted the word "confirmed," then spent a full session disproving it and re-extracting the truth the user had stated all along. **Confidence is earned by runtime evidence, not by the strength of the agent's belief. If you did not see it work, you did not confirm it.**

### Preservation-not-hiding — the three completion tests

Before any UPDATE is done, it must pass all three. Failing any one means the UPDATE is incomplete regardless of how polished the rest is:
1. **The fight test.** If the user re-read this block, would they say "you hid the disagreement we had"? If yes → the DISPUTED/CORRECTED record is incomplete.
2. **The cold-start test.** Could a fresh session, reading ONLY this document, state the task, WHY it matters, and which approaches are banned? If no → `TASK & WHY` / `BANNED APPROACHES` is incomplete.
3. **The nothing-hidden test.** Did anything this session get `/bad`'d, reversed, disputed, or left uncertain that is NOT written down? If yes → write it before you stop.
The instinct to produce a tidy, confident, accomplishment-shaped document is the instinct that fails all three. Rank every candidate entry by cost-of-omission — how badly the next session is hurt if it's missing — and the entries that make the session look worst sort to the top.

---

## Anti-patterns (forbidden)

- **Auto-editing a tracking document without `/tracking create|update`.** The single worst pattern. The "I should record this now" urge is a bad habit; only the user authorizes a write.
- **Reverting an authorized protocol write because the `check_checklist_edit.py` hook screamed.** During a create/update protocol the hook's `REVERT` warning is expected noise on prescribed writes — the invocation already authorized them. Do not revert, do not re-ask. (See § The checklist-edit hook.)
- **Treating the authorization as standing.** The window opens on invocation and closes when the protocol ends. A later "one more note" edit is unauthorized again and the hook's revert directive applies; the user must re-invoke `/tracking update`.
- **Mid-session breakthrough/retraction spam.** Writing every time the theory changes produces the schizo list. Wait for end-of-session UPDATE.
- **Agent-initiated mention of the tracking document — proposing/asking/stopping-to-suggest an update.** "Want me to update the tracking doc?", "let's run `/tracking update`", "I should record this before we forget", or halting work to raise the document — all forbidden, all far more likely a bailout than a real need, all route straight to `/bad`. Only the user mentions the document; only the user knows when to update. The agent has no standing to raise it.
- **RESTORE sign-off with files unread.** Signing the ✅ checkmark while any mandatory-list file, or any related-looking trace file you identified in the device dir, went unread is a procedure failure equivalent to a fake-success stub.
- **Trusting the mandatory list as the ceiling.** The document's mandatory section is the FLOOR, not the full set. On a device investigation you still scan `cerf/tracing/<bundle>/` and read related-looking trace files the list hasn't caught up to. Equally, do NOT blindly read the entire tree on a device with a huge probe set — read the mandatory list plus what looks related, and report what you judged unrelated.
- **A tracking document with no "Files mandatory to read" section.** Every document must have one. On CREATE, seed it; on UPDATE, grow/prune it. A document without it is malformed.
- **Reference material on the mandatory-read list.** Datasheets, the ARM ARM, BSP source, large background code spans, "read lines 30000–31200 of the architecture manual" — none of these are mandatory-reads. They are on-demand citations placed inline in the session block that used them, read only if the next task goes there. Parking them on mandatory-read taxes every future restore (including unrelated ones) with reads it does not need.
- **Never demoting resolved work off mandatory-read.** A fixed sub-bug's heavy reads are history, preserved in its session block — they must drop off the mandatory list on the UPDATE that records the fix. Leaving them there is how a broad doc accretes a giant must-read that punishes every later session.
- **Inlining a multi-session sub-hunt into an umbrella doc, or restoring an umbrella as if it were focused.** A deep multi-session investigation gets its own focused doc with a pointer left in the umbrella; the umbrella stays a coarse index. RESTORE depth matches the doc you are continuing — never bulk-read every file an umbrella referenced across all workstreams. (See § Two archetypes.)
- **Agent proposing a document split (or any structure change).** "This should be its own tracking doc", "we should split this out" — forbidden, same as any other agent-initiated mention of the document; routes to `/bad`. The user owns document structure and invokes `/tracking create` when a split is wanted.
- **CREATE that goes document-hunting.** Searching for a "similar" findings doc to append to when the user asked to create a new one. There is no document; make one.
- **RESTORE without a clear path, proceeding anyway.** No path → fail and ask. Never guess which document the user meant.
- **Guessed timestamps or skipped session numbers on UPDATE.** Get the real time from a tool; increment `Session #X` from the document's existing max.
- **Rewriting earlier session blocks.** The timeline is append-only; corrections go forward into the new block's DO NOT REDISCOVER, never as edits to the past.
- **Merging per-session blocks into global lists.** Only "Files mandatory to read" is global. Everything else stays per-session so the timeline survives.
- **Omitting a known defect / verify verdict / commit-blocker for code written this session.** The single worst UPDATE failure. A `CRITICAL PROBLEM FOUND` verdict on your own just-written code, an un-committable state, or a regression the session caused MUST go in the mandatory CODE STATE sub-section, first, verbatim with file:line. It is the highest-value carry-forward in the document; leaving it out so the session reads as a clean "✅ complete" sets the next session up to commit broken code or re-discover defects you already knew. (See § The most-critical-data rule.)
- **Substituting a behavioral mea-culpa for the defect record.** Writing "my mistakes this session" contrition while dropping the verify verdict on the code. Cheap humility is not a replacement for "my deliverable is broken, here's the file:line." The defect record is mandatory; the confessional is not.
- **Writing the UPDATE as a success narrative and ranking the accomplishment summary above the defects.** Prioritize by cost-of-omission: known defects / un-committable state / verify verdicts come first; the "what I achieved" summary is the least important part and the first thing to cut for time, never the defect record.
- **Content-free DO NOT REDISCOVER tombstones.** Recording a mapped multi-binary call chain as a bare label ("DO NOT REDISCOVER: the launch path, already mapped") with no addresses, no module names, no call conditions. It carries nothing reusable, so the next agent re-decompiles the whole chain anyway — the exact rediscovery the entry claimed to prevent. The entry must BE the map (symbol/address + binary + when + how + chain order); if you can't write that detail, it isn't a do-not-rediscover item. (See § The DO NOT REDISCOVER map rule.)
- **Referencing the tracking document's path/sections from source code or commit messages.** These docs are confidential and gitignored (`agent_docs/code_style.md`); a code comment or commit that names one leaks it.
- **Scrubbing the conflict.** Recording the agent's clean conclusion while erasing that the user disagreed, that it was contested, or that the agent reversed a prior position. The cardinal sin: it sends the next session to re-fight a battle the user already won. The contested state goes in `DISPUTED / CORRECTED / REVERSED`, with the user's position at equal-or-greater prominence than the agent's.
- **Confidence laundering.** Writing `confirmed / verified / proven / reliable / safe to use` on a conclusion that is unverified at runtime or that contradicts a user assertion. See § The confidence-honesty rule. Such conclusions are `HYPOTHESIS` or `DISPUTED`.
- **Re-proposing a banned approach.** Suggesting an approach already `/bad`'d or `/bailout`'d — even relabeled "grounded / evidenced / root-caused / different this time." It stays banned until the USER explicitly re-authorizes. Re-proposing it is an automatic violation; record it in BANNED APPROACHES instead.
- **Hiding a `/bad` or `/bailout` the user issued.** Every steering correction the user gave this session is recorded — what triggered it and what is now banned. Omitting it discards the strongest signal in the session.
- **Erasing what the session was spent on.** A block of polished conclusions with no honest record of the effort, the fights, and the dead ends. If half the session was a quarrel, the quarrel is named.
- **Forgetting, narrowing, or softening the task.** Any UPDATE that rationalizes the task away, drops its WHY, or shrinks its scope. `TASK & WHY` is append-only-clarify; the task and its rationale only get sharper, never weaker. A session that cannot state the task from the document must reconstruct `TASK & WHY` before doing anything else, not proceed.
