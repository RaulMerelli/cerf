---
name: commit
description: The user invokes `/commit` to create a git commit. The skill exists as the rule-respect boundary for commit text — agents reflexively narrate the session, leak conversation context, and yap into the commit body. This skill forces a concise, self-contained message that describes the DIFF and nothing else: no narration, no conversation leaks, no model/agent references, no incident history, no "as you asked". Shorter is better. Invoke when the user types `/commit` or asks to commit changes.
---

# Commit — concise, leak-free git commit

The user invoked `/commit`. Create a git commit whose message describes the diff and nothing else.

## Procedure

1. **`git status` first**, then `git diff` (staged + unstaged) — read what actually changed. Per `agent_docs/rules.md`: never blindly stage; verify the changeset is exactly what you expect.
2. Stage the intended files (`git add <paths>`). If files are already staged and match intent, skip.
3. Commit with a message authored under the rules below.

## The message rules

- **Describe the diff, not the discussion.** Title + optional body cover what the change does to the project — never the conversation that produced it.
- **Shorter is better.** A one-line title is the default. Add body lines only when the *what* genuinely needs them. No padding.
- **Imperative title**, lowercase scope prefix matching repo convention (e.g. `jornada820: keyboard`, `host: compose window title from cerf.json device meta`). Match the style of recent commits.
- **End with the required trailer** from CLAUDE.md:

  ```
  Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
  ```

## Forbidden in the message (this is the whole point of the skill)

Per `agent_docs/code_style.md` § Comments and `agent_docs/rules.md` "Commit messages describe the diff, not the discussion":

- **Narration / yapping** — "this commit", "in this change we", multi-paragraph essays about a one-line edit.
- **Conversation echoes** — "as you asked", "per your feedback", "reverted per discussion", "you were right".
- **Model / agent references** in the body — "the previous agent", "a prior session" (the trailer is the only sanctioned model mention).
- **Incident history** — "this broke N times", "after the regression", "the cautionary tale".
- **Alternatives narrative** — "we chose X over Y", "originally tried Z".
- **Checklist / private-design leaks** — section numbers (`§3.1`), phase names, `docs/ai_checklists/` paths, any vocabulary from a confidential checklist.
- **Edit-process narration** — "reframed", "renamed section", removed-section names.

Test each line against the fresh-clone bar: a developer reading `git log` with zero knowledge of this session must understand the *what* from the message alone. If a line only makes sense to someone who watched this conversation, delete it.

## Hard stops

- **Never run git unless the user asked** — `/commit` IS that ask, scoped to this commit only. Do not `git push` (that needs separate explicit approval).
- **Never force past `.gitignore`** (`git add -f`). The ignore is a STOP signal.
- **If `git status` shows files you don't recognize or didn't intend**, stop and surface them — don't sweep them into the commit.
