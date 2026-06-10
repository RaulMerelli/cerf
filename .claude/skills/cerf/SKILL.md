---
name: cerf
description: The user invokes `/cerf` to list every skill available in this project with a one-line explanation of each. A simple discovery/index command — it reads the project's `.claude/skills/` directory, pulls each skill's name + description, and prints a compact one-liner-per-skill menu so the user can see what's on hand. Invoke when the user types `/cerf`.
---

# /cerf — project skill index

Print a short welcome banner, then list the skills available in THIS project,
each with a one-line explanation.

## What to do

0. **Open with a brief welcome line** — a one/two-line greeting to the CERF
   Claude development environment, e.g. *"👋 Welcome to the CERF Claude
   development environment — the available project skills are below."* Keep it
   short and warm; don't pad it into a paragraph. Then print the index.
1. List the immediate subdirectories of the project's `.claude/skills/`
   directory (each subdirectory is one skill, containing a `SKILL.md`).
2. For each skill, read the `name` and `description` from its `SKILL.md` YAML
   frontmatter.
3. **Condense each `description` to a single short line** — the frontmatter
   descriptions are long; you distil one clause that says what the skill is
   for. Do not paste the whole description.
4. Print the result as a compact list, one skill per line:

   ```
   /<name> — <one-line explanation>
   ```

   **Ordering: `/start-board-implementation` is ALWAYS listed first** (it's the
   entry point for the project's core work — bringing up boards). Every other
   skill follows it, alphabetical by name. If `/start-board-implementation`
   isn't present in the directory, just list the rest alphabetically.

That's the whole job. No confirmation prompt, no follow-up question — just
print the index and stop.

## Scope & rules

- **Project skills only.** Enumerate `.claude/skills/` in the repo root. Do not
  list built-in CLI commands (`/help`, `/clear`, …) or global/user-level skills
  that don't live in this project's `.claude/skills/`.
- **Read the directory live — never hardcode the list.** Skills are added and
  removed over time; a baked-in list rots. Always enumerate the directory in
  THIS run so the output reflects what's actually present.
- **One line each, genuinely one line.** If a description is a paragraph,
  summarise its purpose in a handful of words. Keep the column readable.
- If a subdirectory has no `SKILL.md` or no parseable frontmatter, list it by
  its directory name with `(no description)` rather than skipping it silently.
