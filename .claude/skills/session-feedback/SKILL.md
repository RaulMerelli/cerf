---
name: session-feedback
description: Invoke at the end of a session (or when the user asks "anything to add to CLAUDE.md?", "session feedback", "learnings", "/session-feedback") to reflect on whether something learned this session MUST be promoted into CLAUDE.md, a reference page under agent_docs/, or user memory so that future no-context agents benefit. Proposes bullets for review first; only edits files after explicit user approval.
---

# Session Feedback

Reflect on this session and decide whether anything you learned clears the bar for promotion into the project's durable documentation (CLAUDE.md, any page under `agent_docs/`, or user memory).

**Be ruthless about the bar.** The existing docs are valuable precisely because every line earned its place. Adding noise degrades them for every future agent. When in doubt, don't add.

## What qualifies

An entry qualifies ONLY if it is all of the following:

- **Abstract and durable** — a rule, invariant, workflow, or navigation principle that will still be true months from now. Not a fact about one function, one bug, one commit, or today's state of the tree.
- **Non-obvious** — a future no-context agent would not derive it from reading existing docs + IDA + the code.
- **Materially time-saving** — knowing it would have saved you (or the next agent) real hours, not minutes.
- **Verified this session** — you actually hit the situation and confirmed the rule end-to-end. Not a hunch, not a pattern you noticed once.
- **Not already stated** — even approximately — anywhere in CLAUDE.md, the reference pages, or user memory.

## The highest-leverage category: subsystem map gaps

Before anything else, ask: **did `agent_docs/subsystems.md` mislead me, omit something, or cost me time locating a subsystem that should have been basic project knowledge?** This is the single most common source of wasted agent hours. Two concrete triggers — if either is true, the fix almost always qualifies and you should propose it:

- **You wasted time finding a subsystem that already existed.** If you grepped, read-around, or asked the user to locate something that a one-line pointer in `subsystems.md` would have resolved instantly, that pointer is missing and belongs there. The bar for this is lower than other categories: navigation aids for existing subsystems are nearly always worth adding.
- **You created (or substantially reshaped) a subsystem this session.** If a no-context agent opening the repo tomorrow would reasonably expect this subsystem to be part of basic project knowledge, but `subsystems.md` does not mention it, the entry is missing. Add it as a short "what it is / where it lives / what thunks it owns" line in the same style as neighboring entries.

Keep the entry abstract in voice (what the subsystem *is* and *where* it lives, not today's bug or today's diff). File paths and subsystem names ARE allowed here — they are the navigation target, not stale detail — but offsets, log lines, and function internals still are not.

## What does NOT qualify (hard rejects)

- Session-specific trivia: "today's bug in X was caused by Y", "function Z has a null check missing"
- Concrete references that will rot: IDA offsets, log excerpts, file:line pointers, function names, commit hashes, specific struct layouts. These belong inside code comments or investigation docs, not in the rules layer.
- Restatements of existing general rules ("verify in IDA", "read the docs", "no hacks")
- Speculation, opinion, or anything you'd hedge with "I think" / "probably" / "seems like"
- Padding produced because you feel you "should" contribute something

## Style requirement for proposed bullets

If you do propose a bullet, it MUST match the voice of the existing entries in CLAUDE.md and user memory: **abstract principles, not concrete case studies**.

The rule itself must carry the meaning. Do not tie it to the specific thing that triggered today's discovery.

- Wrong: "The tray subsystem is tech debt because it forwards `Shell_NotifyIcon` to the host shell instead of maintaining its own icon state."
- Right: "Functions that forward to the host OS instead of implementing CE behavior are tech debt and must be flagged."

- Wrong: "When `gwes!xxx_CreateWindowEx` at 0x41234 returns NULL, check that the atom table was initialised."
- Right: "A CE API returning NULL on a path that IDA shows as infallible usually means a prerequisite subsystem was not initialised before the call."

- Wrong: "cerf.log line 'MMU: remap failed for 0x40000000' means the L1 table is stale."
- Right: "MMU remap failures after device switches usually indicate a strategy object was not re-seated for the new VM layout."

No offsets. No log lines. No filenames in the bullet. No "today I found". The claim must read the same five years from now.

## Output protocol

Before touching any file:

1. **If nothing clears the bar**, respond with exactly:

   > Nothing to add this session.

   Then stop. This is the correct and expected answer most sessions.

2. **If something does clear the bar**, output each candidate as a single bullet in the existing memory/CLAUDE.md style — short title in bold, em-dash, one-sentence abstract rule. Example shape:

   - **Short principle name** — one-sentence abstract rule that will still be true and useful months from now.

   Propose at most what you can defend. One solid bullet beats three weak ones. Zero is fine.

3. For each proposed bullet, on the line below it, add a single short line prefixed with `  ↳ target:` naming where it would go (e.g. `agent_docs/rules.md § Mental Model`, `CLAUDE.md § MOST IMPORTANT RULES`, `user memory`). This line is for review only and will not be copied into the document.

4. Then STOP. Do not edit any file yet. Wait for the user.

## After user review

- If the user replies `yes` / `+` / `go` / `add` (or equivalent approval), edit the named target document(s), inserting each approved bullet in the matching existing style. Do not embellish, do not add the `↳ target` line to the file, do not attach offsets/logs/filenames to the bullet.
- If the user approves only some bullets, add only those. Drop the rest silently.
- If the user rejects or modifies a bullet, respect that exactly. Do not re-pitch.
- If the user moves on or says nothing conclusive, drop all of it.

## Commit offer

Once the approved edits have landed on disk, ask — in one short line — whether to commit them. Example: `Commit these doc updates? (yes/+/ok/go/do)`.

Then STOP again. Do not run any git command yet. Wait for the user's reply.

- If the user replies `yes` / `+` / `ok` / `go` / `do` (or equivalent approval), proceed with the commit under this strict protocol:
  1. Run `git status` and read the output. Confirm that the only modifications on disk are the files you just edited in the previous step. If any other file is modified or staged, STOP and report — do not guess, do not stage anything else, do not stash, do not touch other files. Ask the user.
  2. Stage ONLY the specific files you edited, by explicit path. Never use `git add -A`, `git add .`, or `git add -u`. Never use `-f` to force past `.gitignore`.
  3. Run `git status` again and verify the staged set is exactly the edited files and nothing else. If it is not, STOP and report.
  4. Run `git diff --cached` and sanity-check that the staged diff is only the approved bullets in the correct locations. If anything extra appears, STOP and report.
  5. Commit with a concise message that names the rule(s) added and the target document(s). Follow the repo's commit style (lowercase imperative subject, short body explaining the why). Include the standard `Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>` trailer via HEREDOC.
  6. Do NOT push. Do NOT amend. Do NOT skip hooks. If a pre-commit hook fails, stop and report — do not retry with `--no-verify`.
  7. After the commit lands, run `git status` once more and report the result (commit hash + clean tree).
- If the user declines or does not give an approval token, leave the edits uncommitted. They can commit later themselves.

Remember: "Nothing to add this session." is a successful outcome.
