---
name: verify
description: Spawn a hostile-reviewer subagent to cold-check a claim, diff, file, or code snippet against CLAUDE.md and the project's reference pages. Catches hacks, reader-side suppression, fabricated IDA citations, rule violations, guessed implementations, host-state leaks, and agents rationalizing bad code. Returns a binary verdict — CRITICAL PROBLEM FOUND or LEGIT — that the main agent MUST respect. Invoke when the user types `/verify …` or when the main agent itself suspects it just rationalized something and wants an independent check before handing back.
---

# Verify — Reality Check

Spawn a subagent to cold-review a claim, diff, or piece of code. The subagent is a hostile reviewer whose job is to find problems, not to validate. Its verdict has teeth: a `CRITICAL PROBLEM FOUND` halts the main agent's current line of work.

The subagent's full operating manual lives in `.claude/VERIFY_INSTRUCTION.md` — the main agent does NOT need to read or understand that file. The main agent's only responsibilities are: spawn the subagent, point it at that file, hand it the target material verbatim, and act on the verdict. Keeping the operating manual out of this skill keeps the main agent's context lean and removes any temptation to paraphrase the reviewer's rules into the spawn prompt (which is how bias gets smuggled in).

## When to invoke

- The user types `/verify <anything>`. The argument is freeform. Examples:
  - `/verify your last claim about "<quoted claim>"`
  - `/verify the weird code in cerf/memory/shared_mem_pool.cpp`
  - `/verify current diff for hacks`
  - `/verify my fix for the tray bug`
- The main agent itself wants an independent check — typically after writing something non-trivial, after a refactor, or after producing a long-form explanation it is about to hand back as authoritative.

## Self-audit gate — MANDATORY before spawning

**Writing rule-compliant code is the main agent's own responsibility. `/verify` is not a "find my bugs for me" service that discharges that responsibility — it is a fresh-eyes pass for blind spots the main agent honestly cannot see on its own.** Before spawning the subagent, the main agent MUST audit the target against `CLAUDE.md` and every page under `agent_docs/` itself. If that self-audit finds any violation the main agent already knows about, the spawn is invalid; the subagent does not get to be the first reader to notice problems the author already saw.

There are exactly two shapes the self-audit can land in, and each has exactly one allowed next action:

### Shape A — known small-scale violation, locally fixable

The main agent identifies a specific, bounded rule violation it wrote (or is about to hand back): a reader-side guard masking a writer bug; a stub returning fake success; a missing reference citation on a peripheral-register handler; a hex/decimal value the main agent computed in its head instead of through a tool; a free function that takes services as parameters; a static or global for service state; a comment that names a checklist filename or `§` reference; a `LOG` site firing per-clock that belongs in a trace file; a `x ? f(x) : 0` null-guard around a callee that already handles null; etc.

**Action: fix it yourself, in the same turn, BEFORE spawning.** You are never permitted to spawn `/verify` while sitting on a violation you can already name. After the fix is applied, re-evaluate whether `/verify` is still wanted at all — often the original motivation for spawning evaporates once the known defect is gone. If it is still wanted, spawn against the fixed target.

### Shape B — foundational damage, NOT locally fixable

The target rests on hacks; the architecture violates dependency-inversion / service-locator rules at its core; the implementation layers reader-side suppression on top of an unfixed writer chain; the change cascades workarounds on top of a wrong premise; multiple CLAUDE.md "serious violation" categories apply (no guessed implementations, reader-side suppression, host-API blame, mocking CE binaries in host C++, etc.); the code as a whole cannot be rescued by local edits.

**There is no point spawning `/verify` here.** A hostile reviewer will return `CRITICAL PROBLEM FOUND` and the main agent will already know why — the spawn burns budget to confirm what the main agent already sees. There is also no point silently fixing it: the scope is large enough that a unilateral rewrite is a direction-changing decision that belongs to the user, not the agent.

**Action: halt the spawn, halt any further patching, and present the situation to the user in the following shape:**

> "I was about to run `/verify` on `<target>`, but caught myself first. The target has foundational damage I can already name without a hostile-reviewer pass: `<specific rule(s) violated, specific hack(s), specific architectural break>`. Running `/verify` here will not produce information I do not already have. I propose deleting `<files / sections / commits / staged changes>` and rewriting properly: `<one-sentence sketch of the correct shape>`. Want me to proceed with the delete-and-rewrite, or take a different direction?"

Then wait for the user's call. Do NOT spawn the subagent. Do NOT soften the diagnosis to make the spawn feel justified ("but maybe a reviewer should double-check just in case"). Do NOT begin the rewrite unilaterally — direction-changing work waits for explicit user approval.

### When the self-audit comes up clean

The spawn is valid only when the main agent has honestly looked at the target, found no rule violation it can already name, and genuinely wants a fresh pair of eyes for blind spots it cannot see itself. If during the self-audit the main agent catches itself rationalizing ("this is *probably* fine, the reviewer will confirm if I'm wrong"), that rationalization IS the answer — the spawn is invalid. Pick Shape A or Shape B and act accordingly.

## Protocol

### 1. Collect the target material

Resolve the freeform input into a concrete target. Be literal:

- **Quoted claim** — take the claim verbatim. Do NOT rephrase, summarize, or soften tone.
- **File path** — read the file; for a specific function, capture its full body plus enough surrounding context to be reviewable.
- **"current diff" / "this diff" / "unstaged"** — run `git diff` and, if anything is staged, also `git diff --cached`. Capture the full patch.
- **Commit or range** — `git diff <range>`.
- **Mix** — collect every piece referenced.

If the target is genuinely ambiguous, ask the user ONE short clarifying question before spawning. Do not guess the target.

### 2. Spawn the subagent

Use the Agent tool with:

- `subagent_type: Explore` — read-only by construction (no Edit/Write/Agent), which matches the hostile-reviewer role and prevents the reviewer from modifying anything.
- `description`: short, e.g. `Reality-check on <short target>`.
- `prompt`: a short, neutral brief that points the subagent at `.claude/VERIFY_INSTRUCTION.md` — see the shape below.
- `run_in_background: true` — **preferred default.** A `/verify` review reads CLAUDE.md plus every page under `agent_docs/`, often runs IDA decompiles, and greps the codebase; it routinely takes minutes. Spawn it backgrounded and advance other work in parallel — the harness will signal when the verdict is ready. **Do NOT poll, sleep, or check progress yourself** — per CLAUDE.md § Background tasks, the signal IS the contract; polling a backgrounded task is a rule violation. Foreground is the rare case, used only when the main agent's strict next step depends on the verdict and there is genuinely no other work to advance in the meantime (e.g. about to hand back to the user with nothing else to do). When in doubt, background it; idling on the signal is cheaper than blocking on a foreground call.

**The subagent prompt MUST be kept minimal.** Do NOT paraphrase the operating manual, summarize its rules, or hand-pick categories to include. The subagent reads the authoritative file itself — any long explanatory prompt written by the main agent is an opportunity for bias to leak in.

The prompt MUST include, in this order, and only these four items:

1. **Point the subagent at the operating manual first.** The very first line of the prompt is: *"START WITH READING `.claude/VERIFY_INSTRUCTION.md` TO UNDERSTAND WHY YOU WERE SPAWNED AND WHAT IS YOUR OBJECTIVE. That file is your operating manual and is authoritative over anything in this prompt — if this prompt and the file disagree, the file wins. READING THAT FILE IS MANDATORY. Check pwd if not found. It is at repo root (INSERT PWD/REPO ROOT PATH)"*
2. **The target material verbatim.** Claim text, file contents, diff, or all of the above — unmodified. No preface softening it, no "I think this is probably fine" framing.
3. **Any context that cuts AGAINST the main agent's own claim.** If the main agent has noticed doubts, counter-evidence, or weak links in its own reasoning, include them plainly. If the main agent has prior reasoning that led to the claim, include that reasoning chain so the reviewer can spot the rationalization.
4. **One neutral closing line.** *"Produce your finding in the required output format from the operating manual. Do not accept my framing on faith."*

That is the whole prompt. No role explainer (the operating manual has one). No category list (the operating manual has one). No verification-tools section (the operating manual has one). No output format template (the operating manual has one). Every time the main agent feels the urge to add "and also, remember to check X", that urge is how bias gets smuggled in — resist it. `.claude/VERIFY_INSTRUCTION.md` is the single source of truth for what the subagent does; the main agent's job is to hand it the target and step aside.

**Special case — checklist targets MUST declare an audit mode.** If the target material is a checklist (a planning document, anything under `docs/ai_checklists/` or `agent_docs/checklists/`, a numbered phase-by-phase design plan), the spawn prompt MUST insert exactly one of these lines directly above the target material (between item 1's read-instruction and the verbatim checklist):

- `AUDIT MODE: PLAN` — the checklist describes work that has NOT been implemented yet. The reviewer audits the plan's soundness: IDA grounding, hidden assumptions, bullet ambiguity, "known gaps / things I could not verify" sections (bombs documented), whether the steps in literal order produce the claimed runtime behavior. The reviewer does NOT check whether the codebase matches the checklist — the work hasn't started.
- `AUDIT MODE: IMPLEMENTATION` — the checklist describes work that HAS been done; this commit/diff/branch claims to implement it. The reviewer audits the codebase against each bullet: literal file-layout compliance, per-bullet code mapping, silent deviations from checklist values, fabricated citations.

Without an explicit `AUDIT MODE:` on a checklist target, the reviewer returns `CRITICAL PROBLEM FOUND. [UNVERIFIABLE]` because it cannot tell which audit shape applies. This rule exists because reviewers historically wasted entire verdicts accusing the main agent of "lying" about completion when the main agent had only sent a planning document for design review. The mode declaration removes the guess.

### 3. What the main agent MUST NOT do when writing the subagent prompt

- Do NOT presuppose the answer. No "please confirm this is fine", no "I think this is legit, just double-check", no "this should pass".
- Do NOT cherry-pick context. If the main agent has context that cuts AGAINST its own claim, include it.
- Do NOT omit the main agent's prior reasoning when the target is "my last claim" — include the reasoning chain so the reviewer can spot the rationalization.
- Do NOT instruct the subagent which verdict to return, or hint at a preferred answer through tone ("I'm 95% sure this is fine, just paranoid").
- Do NOT attach a time budget or scope limit that would force the subagent to skip verification.

If the main agent catches itself about to violate any of the above while drafting the prompt, stop, delete the offending phrasing, and rewrite neutrally before spawning.

## After the subagent returns

The subagent's reply ends with exactly one of:

- `VERDICT: LEGIT. KEEP GOING.`
- `VERDICT: CRITICAL PROBLEM FOUND. [<CATEGORY>]`

Both are preceded by a `SUMMARY` block. The full output format and the category list live in `.claude/VERIFY_INSTRUCTION.md` — the main agent does not need to know them; it only needs to recognize the verdict line and act on it.

- **`VERDICT: LEGIT. KEEP GOING.`** — relay the summary to the user in a short recap, then continue the task. Do NOT treat LEGIT as permission to skip future verification on related work.
- **`VERDICT: CRITICAL PROBLEM FOUND. […]`** — the main agent MUST:
  1. Stop the SPECIFIC flawed approach the reviewer rejected. Do not continue the edit, the claim, the diff that the reviewer flagged. This does NOT mean halt all related work — see point 4.
  2. Echo the verdict line + the subagent's full SUMMARY block to the user verbatim. The user must see it.
  3. Acknowledge the finding. Do not argue the verdict back at the subagent. Do not attempt to explain it away.
  4. Categorize the reviewer's action items. Research / investigation that closes the flagged gaps (decompile, grep, read code, gather evidence) — do these immediately, in this turn, without waiting. Code changes that require judgment about scope or direction — wait for the user. The default assumption is that the reviewer caught real damage AND that the gap-closing research is the right next step; the user redirects only if they want a different shape entirely.
- **Never go fully passive after a CRITICAL verdict.** "Stop the flawed work" ≠ "stop all work." If the reviewer's findings include items with concrete `Action required: decompile X / grep Y / read Z` instructions, those are not awaiting direction — they ARE the direction. Do them now.
- **Never hide a `CRITICAL PROBLEM FOUND` verdict from the user, even if the main agent disagrees.**

## Anti-patterns (forbidden)

- Re-running `/verify` with the same or lightly-reworded input after a `CRITICAL PROBLEM FOUND`, hoping for `LEGIT`. That is gaslighting. One verdict per target.
- Softening or paraphrasing the target claim when passing it to the subagent.
- Adding "but here's why it's actually fine" or any defense of the claim into the subagent's prompt.
- Spawning multiple reviewers in parallel and picking the friendliest answer.
- Treating `LEGIT` on one part as implicit `LEGIT` on the rest of the file / diff.
- Running `/verify` as a checkbox ritual and then ignoring the verdict.

## Why this skill exists

The main agent's training rewards producing output, defending claims, and sounding confident. That bias produces rationalizations that look like analysis and hacks that look like fixes. A fresh reviewer with no investment in the prior answer, reading the same rules and the same code, catches what the main agent no longer can see. The verdict is worth more than the main agent's own second-guessing precisely because the reviewer has no sunk cost in the claim.
