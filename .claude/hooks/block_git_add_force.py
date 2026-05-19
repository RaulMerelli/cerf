#!/usr/bin/env python3
"""
PreToolUse hook for Bash. HARD-BLOCKS `git add -f` / `git add --force`.

The whole point of .gitignore is to keep specific paths out of git history.
Force-adding ignored paths is the exact bypass route for confidentiality
boundaries — checklists under docs/ai_checklists/, build artifacts, the
references/ tree, secrets, local-only configs. Per user memory § "Never
force past gitignore": .gitignore is a STOP signal; pause and recall rules,
never `git add -f` past it.

There is NO adequate agent-side situation where this is correct. If a path
genuinely belongs in git history, edit .gitignore to un-ignore it, then
`git add` it normally. Never bypass the ignore itself.

Returns permissionDecision: "deny" on match. The Bash tool call is then
blocked before it runs.
"""
import json
import re
import sys

# Match `git add` only at command position — very start of the command
# string, or after an inline shell separator (`;`, `&`, `|`). NOT after
# a bare newline (MULTILINE is intentionally OFF) because heredoc bodies,
# quoted multi-line strings, and commit-message text contain newlines
# without those being command separators in the shell-parse sense.
# Trade-off: contrived multi-line scripts that bury `git add -f` after
# a literal newline (no `;&|`) won't be caught — acceptable because the
# real failure mode is an agent running `git add -f` as a single
# explicit command, which this still catches.
# Then require the flag form: -f / -Af / -fA / -<chars>f<chars> / --force.
GIT_ADD_FORCE_RE = re.compile(
    r"(?:^|[;&|]\s*)git\s+add\b(?:\s+\S+)*?\s(?:-[a-zA-Z]*f[a-zA-Z]*|--force)\b"
)


def main() -> int:
    try:
        payload = json.load(sys.stdin)
    except (json.JSONDecodeError, ValueError):
        return 0

    cmd = (payload.get("tool_input") or {}).get("command", "")
    if not cmd:
        return 0

    if not GIT_ADD_FORCE_RE.search(cmd):
        return 0

    reason = (
        "BLOCKED: `git add -f` / `--force` bypasses .gitignore. There is no "
        "adequate situation where an agent should force-add an ignored path. "
        ".gitignore exists specifically to keep paths out of history — "
        "confidential checklists under docs/ai_checklists/, build artifacts, "
        "references/, secrets, local configs. Per user memory § 'Never "
        "force past gitignore': .gitignore is a STOP signal. If a path "
        "genuinely belongs in git history, edit .gitignore to un-ignore it, "
        "then `git add` it normally — never bypass the ignore itself."
    )

    out = {
        "hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "permissionDecision": "deny",
            "permissionDecisionReason": reason,
        },
        "systemMessage": "[CLAUDE.md hook] BLOCKED: git add -f",
    }
    json.dump(out, sys.stdout)
    return 0


if __name__ == "__main__":
    sys.exit(main())
