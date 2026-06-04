---
name: leak
description: The user invokes `/leak` when the agent wrote inadequate narration into a durable artifact — committed code comments, documentation, hook messages, config files, commit bodies — that leaks SESSION / CONVERSATION / IMPLEMENTATION-HISTORY context instead of standing on its own. Canonical shapes: backstory ("this has happened repeatedly", "verified in bash earlier", "per our discussion"), model-version / agent references ("opus-4.8 does X", "the previous agent"), incident-history narration ("shipped broken N times", "incident #11"), conversation echoes ("as you asked", "reverted per your feedback"), "we chose X over Y" alternatives history, and any comment that reads like a chat message rather than a technical note about the code below it. The skill forces the agent to (1) enumerate EVERY such leak with file+line and the leaked phrase, (2) FIX each in the same reply — almost always by DELETING the excess narration, occasionally by rewriting to a self-contained technical note. Invoke when the user types `/leak`, or when the agent catches itself having just written backstory / conversation context into a committed file.
---

# Leak — Enumerate the narration leaks. Delete them.

The user invoked `/leak` because YOU wrote narration into a durable artifact that leaks context which does not belong there. A committed comment, a doc line, a hook message, a commit body, a config file — anything that survives the session — must read as a self-contained technical statement to a fresh developer at a fresh clone who has zero knowledge of this conversation, the model that wrote it, or the history of how the code got here.

Per `agent_docs/code_style.md` § Comments and `agent_docs/rules.md`:

- *"Never reference any document that isn't durably committed to the repo."* — and by extension, never reference the conversation, the session, the model, prior agents, or incident history.
- *"A comment that still makes sense moved to a random file is dead weight."* — narration about WHY the hook/comment exists, who hit the bug, how many times, is exactly this.
- *"Commit messages describe the diff, not the discussion."* — edit narrative, user-feedback labels, "reframed", removed-section names stay out.
- § Comments test: *"would a fresh developer at a fresh clone understand the WHY from this comment alone, with no \[external context\] in hand?"* If the line only makes sense to someone who watched this session, it leaks.

## The leak shapes

Every one of these is a leak, regardless of how technically-dressed it looks:

- **Backstory / incident history** — "this has happened repeatedly", "shipped broken 10 times", "incident #11", "verified in bash earlier", "the cautionary tale is".
- **Model / agent references** — "opus-4.8 does X", "the previous agent", "a prior session", "agents repeatedly".
- **Conversation echoes** — "as you asked", "per your feedback", "reverted per the discussion", "you were right that".
- **Alternatives history** — "we chose X over Y", "rather than the Z approach", "originally we tried", "previously this was".
- **Decision-defense / "don't undo my approach"** — the purest and most-missed shape. A comment whose entire reason to exist is to warn a future reader AWAY from an alternative the author considered or removed, or to justify why the current approach is the way it is: "Do NOT <thing the author tried and discarded>", "this MUST stay <X> or it breaks", framed as a caution about the author's own design. The code IS the design; nobody needs to be argued out of the path not taken, and that path is absent from git history anyway. **Tell:** the comment's value evaporates if the reader never knew an alternative was ever on the table — it is defending a decision, not describing the code. This is NOT the legitimate "DO NOT switch to Y — Z breaks because W" hazard note: that form names a non-obvious SYSTEM invariant a fresh reader would plausibly violate, stated as a property of the system. The leak form defends the author's journey. Test: would a fresh reader, with zero knowledge that any alternative was ever considered, write this exact warning from the code alone? If no, it leaks.
- **Self-narration** — comments that describe what the author DID ("added for the fix", "moved here from", "refactored to") instead of what the code IS.
- **Chat-shaped prose** — any comment/doc line that reads like a message to a person rather than a note about the code immediately below it.

## Step 1 — Enumerate every leak

Exhaustively, no aggregation, no softening:

> "Narration-leak disclosure under `/leak`:
> 1. `<file:line>` — leaked phrase: `<the exact text>` — shape: `<backstory | model-ref | conversation-echo | alternatives-history | self-narration | chat-prose>`.
> 2. `<file:line>` — leaked phrase: `<…>` — shape: `<…>`.
> 3. … (continue until every leaking line in your recent stretch is named)
> Total: N leaks."

The bar is items with file + line + the exact leaked phrase, not categories. *"Some comments are too chatty"* is not a valid Step 1 output. Re-read every artifact you wrote or edited in your recent stretch — code comments, docstrings, hook `reason`/message strings, doc `.md` files, config-file comments, any commit bodies you authored.

## Step 2 — Fix each leak in the same reply

For each Step 1 item, apply the default and the exception:

- **DEFAULT — delete the excess.** Most leaks are pure narration with no technical substance; the line goes entirely. A comment that said *"this has shipped broken 10 times so we verify here"* becomes either nothing (if the code is self-explanatory) or a one-line technical note (*"<X> must be checked before <Y> or <Z> happens"*).
- **EXCEPTION — rewrite to self-contained.** If the leaking line wraps a real technical fact, keep the fact, strip the narration. *"opus-4.8 keeps masking the exit code by piping to Select-Object, so we block it"* → *"piping build output to a filter masks the build's exit code"*. The mechanism stays; the who/when/how-many-times goes.

Apply via `Edit` / `Write` in this same reply. Do not ask which leaks to fix — all of them go.

**Test each fix against the fresh-clone bar:** would a developer who has never seen this conversation, does not know which model wrote it, and has no incident history understand the line purely as a statement about the code? If not, it still leaks — cut more.

## What the user gets back

The reply contains, in this exact order:

1. **Step 1 enumeration** — exhaustive numbered list with file+line + exact leaked phrase + shape per item.
2. **Step 2 fix tool calls** — `Edit` / `Write` deleting or rewriting each leak.

No asking. No *"should I keep this one?"*. The invocation IS the authorization to strip every leak.

## Anti-patterns (forbidden in the `/leak` reply)

- **Vague Step 1.** *"A few comments are chatty"* — the bar is file + line + exact phrase + shape, per item.
- **Rewrite-as-cover.** Rewording a leak to drop the obvious trigger word while keeping the narration substance (*"opus-4.8 does X"* → *"the model does X"* → *"agents do X"*) is the same leak under fresh paint. If the line is about who/when/history rather than the code, it goes — no laundering.
- **Keeping "useful context".** Backstory feels useful to the author; to the fresh-clone reader it is noise that hides the technical signal. Cut it.
- **Apology instead of fixing.** *"I see, I'll be more careful"* without the enumeration + edits is the leak continuing. The fixes are required.
- **Skipping artifacts.** Hook `reason` strings, docstrings, `.md` docs, config comments, and commit bodies are all in scope — not just `.cpp`/`.h` comments. A leak in a hook message reaches every future agent; a leak in a doc reaches every reader.

## Why this skill exists

Narration leaks are low-severity individually but corrosive in aggregate: every chatty comment, every "we chose X over Y", every "the previous agent" buries the actual technical signal a future reader needs and turns the codebase into a transcript of how it was built rather than a description of what it is. The fresh-clone reader — the next agent, the user months later, an external contributor — has none of the session context that made the narration feel meaningful, so to them it is pure noise occupying the exact place a real technical note should be. `/leak` strips it at the moment the user catches it, before it compounds.
