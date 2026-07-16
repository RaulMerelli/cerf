---
name: changelog-append
description: The user invokes `/changelog-append` to record a change just made (bug fix, feature, etc.) as a concise changelog entry. It appends one `<li>` to the top (latest) version block in `docs/changelog.html`, matching the surrounding style, then runs `./compile_readme.py` to regenerate `README.md`. Invoke when the user types `/changelog-append` or asks to add a changelog entry.
---

# changelog-append - record one change in the changelog

The user invoked `/changelog-append`. Add one concise entry for the change just made, then regenerate the README.

## Procedure

1. **Open `docs/changelog.html`.** The **latest version is the topmost `<tr>`** in `<tbody>` (currently `TBA` at the top). Find its `<td>` → `<ul>`.
2. **Append one `<li>` to the bottom of that `<ul>`.** Do not create a new version row. Do not touch older version rows.
3. **Run `python compile_readme.py`** (from repo root) to regenerate `README.md` from the updated changelog. Confirm it prints `README.md compiled (vX.Y)`.

## Writing the entry

- **A changelog line, not narration.** State what changed to the product, in the fewest words that are still clear. Smaller is better.
- **Match the surrounding examples** - read the existing `<li>`s in the top block and follow their voice, tense, and prefix convention (`Guest Additions:`, `Launcher:`, `Dev only:`, `Fixed …`, `<Board> support`, …).
- **One line per change.** If the work spans clearly separate user-visible changes, add one `<li>` each - but do not pad; prefer a single tight line.
- **No conversation echoes**, no session history, no model/agent references, no "as you asked".

Examples already in the file (copy this register):
- `Fixed NEC MP700 touch`
- `Guest Additions: Fixed IMGFS ROMs regression introduced in v6.0`
- `Launcher: added downloads count sort`
- `Sharp Mobilon HC-4100 support (Handheld PC, Windows CE 2.0)`

## Git

Do **not** run any git command unless the user explicitly asks (e.g. "stage your changes", "commit"). When they do, the changelog edit **and** the regenerated `README.md` are both part of the changeset - include both.
