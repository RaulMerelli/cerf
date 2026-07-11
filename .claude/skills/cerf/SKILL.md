---
name: cerf
description: The user invokes `/cerf` to see the project skill index plus a health check of the dev environment. It runs the repo's environment doctor (`setup.ps1 -Check`) to confirm git hooks are active via core.hooksPath, the .claude hooks are present and runnable, submodules are initialised and vcpkg is integrated. On a broken clone it leads with the diagnosis and the fix; on a healthy clone it prints the one-liner-per-skill menu. Invoke when the user types `/cerf`.
---

# /cerf - environment doctor + project skill index

Two jobs, in this order: check this clone's dev environment, then print the
skill index.

Git does not clone `.git/hooks/`, and `core.hooksPath` lives in local
`.git/config`, which is not cloned either - so a fresh clone runs no git hooks
at all until `setup.cmd` wires them up.

## Step 1 - run the doctor

From the repo root:

```
powershell -NoProfile -ExecutionPolicy Bypass -File setup.ps1 -Check
```

Read-only. Prints one tab-separated line per check - `STATUS<TAB>key<TAB>detail`,
where STATUS is `OK`, `WARN` or `FAIL` - and exits 1 if anything FAILed.

`setup.ps1` owns the check list; read its output rather than testing any of this
directly here.

What a FAIL means:

| key | consequence |
| --- | --- |
| `git-repo` | not a git repo - nothing else is meaningful |
| `git-hooks` | `core.hooksPath` is not `.githooks` -> `.githooks/pre-commit` never runs |
| `submodules` | a declared submodule is empty -> the build fails |
| `python` | `py -3` is not runnable -> every `.claude/hooks/*.py` hook is a silent no-op |
| `claude-hooks` | `.claude/settings.json` missing/unparseable, or a referenced hook script is absent or does not compile |
| `vcpkg` | `vcpkg integrate install` was never run -> `build.ps1` refuses to build |

`stale-local-hooks` is a WARN: leftovers in `.git/hooks/` are shadowed by
`core.hooksPath` and cannot fire, but they mislead whoever reads them. Mention
it, don't dramatise it.

## Step 2 - report

**All OK** (exit 0): print one short line - e.g. *"Environment: healthy - git
hooks and .claude hooks both active."* - then the index. Don't print the table.
Name any WARN lines in one line.

**Anything FAILed:** diagnosis first, index after.

1. Say what is broken in terms of its consequence, not its key name - read the
   consequence off the table above, and off `.githooks/pre-commit` itself for
   what that hook enforces.
2. Print the failing lines.
3. Give the fix. `git-hooks` and `submodules` are fixed by `setup.cmd`. `python`
   and `vcpkg` are outside the repo - install the Python launcher, or run
   `vcpkg integrate install` from `<VS install>\VC\vcpkg\vcpkg.exe`; `setup.cmd`
   reports these but cannot fix them.
4. **Offer to run `setup.cmd`** when the failures are the fixable kind - ask once
   in plain chat, run it if the user agrees. Never run it unprompted: it writes
   git config and may initialise submodules.

Then print the index.

## Step 3 - the skill index

Open with a brief welcome line - a one/two-line greeting to the CERF Claude
development environment. Keep it short and warm; don't pad it into a paragraph.

1. List the immediate subdirectories of the project's `.claude/skills/`
   directory (each subdirectory is one skill, containing a `SKILL.md`).
2. For each skill, read the `name` and `description` from its `SKILL.md` YAML
   frontmatter.
3. **Condense each `description` to a single short line** - the frontmatter
   descriptions are long; you distil one clause that says what the skill is
   for. Do not paste the whole description.
4. Print the result as a compact list, one skill per line:

   ```
   /<name> - <one-line explanation>
   ```

   **Ordering: `/start-board-implementation` is ALWAYS listed first** (it's the
   entry point for the project's core work - bringing up boards). Every other
   skill follows it, alphabetical by name. If `/start-board-implementation`
   isn't present in the directory, just list the rest alphabetically.

## Scope & rules

- **Project skills only.** Enumerate `.claude/skills/` in the repo root. Do not
  list built-in CLI commands (`/help`, `/clear`, …) or global/user-level skills
  that don't live in this project's `.claude/skills/`.
- **Read the directory live - never hardcode the list.** Skills are added and
  removed over time; a baked-in list rots. Always enumerate the directory in
  THIS run so the output reflects what's actually present.
- **One line each, genuinely one line.** If a description is a paragraph,
  summarise its purpose in a handful of words. Keep the column readable.
- If a subdirectory has no `SKILL.md` or no parseable frontmatter, list it by
  its directory name with `(no description)` rather than skipping it silently.
- No confirmation prompts and no follow-up questions beyond the single
  offer-to-run-setup above. Print, and stop.
