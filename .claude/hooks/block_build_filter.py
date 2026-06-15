#!/usr/bin/env python3
"""
PreToolUse hook for Bash AND PowerShell. Requires build.ps1 to be the
LAST thing that runs in the call, with only output redirection permitted
after it; any other trailing or wrapping construct is blocked.

build.ps1's exit status reaches the caller only if nothing runs after it
and nothing wraps it. Redirection spawns no command and preserves the
exit code, so it is the sole permitted trailing construct; everything
else is blocked by default.

What masks the exit code (all FORBIDDEN after build.ps1):
  - `;`  / newline   — statement separator; the next command's exit
                       becomes the call's. `build.ps1 ...; echo $?` makes
                       a failed build report 0.
  - `|`  / `||`      — pipe / or-chain; the filter or RHS sets the exit.
                       Also blinds the output (filters drop the error).
  - `&&`             — even though `&&` propagates failure, a trailing
                       command is still forbidden: keep build.ps1 in its
                       own call so output and exit are unambiguous.
  - `$( )` / backtick — command substitution running build inside another
                       command, whose exit wins.

The ONLY two sanctioned forms:
  1. build.ps1 raw (no trailing anything) — full output reaches the tool
     result, exit code intact.
  2. build.ps1 > build.log 2>&1  (redirection ALONE, nothing after) —
     exit code intact; Read build.log in a SEPARATE call afterwards.

run_in_background note: a background build's completion notification
reports the call's exit status. That is build.ps1's true status ONLY
because this hook guarantees nothing trails it. Never infer build success
from a notification on a call that had anything after build.ps1 — this
hook blocks that shape outright.

Leading setup before build.ps1 (`cd /z && build.ps1`, redirections) is
fine: what runs BEFORE build.ps1 cannot mask its exit when build.ps1 is
last. Only the tail after the last build.ps1 token is checked.
"""
import json
import re
import sys

BUILD_RE = re.compile(r"\bbuild\.ps1\b", re.IGNORECASE)

# Command substitution wrapping build.ps1 swallows its exit code (the
# outer command's status wins). There is no legitimate reason to run
# build.ps1 inside `$( )` or backticks, so either anywhere in the
# command is a block — the masker here sits BEFORE build.ps1, so a
# tail-only check would miss it.
SUBST_RE = re.compile(r"\$\(|`")

# Tokens that introduce a new command / pipeline stage AFTER build.ps1.
# Output redirection (`>`, `>>`, `2>&1`, `1>&2`) is deliberately NOT
# here — it spawns no command and preserves the exit code. The `&` of
# `2>&1` is excluded via the `(?<!>)` lookbehind; a bare background `&`
# (not preceded by `>`, not part of `&&`, not followed by a redirection
# digit) IS matched because it detaches build and returns 0 immediately.
FORBIDDEN_TAIL_RE = re.compile(r"[;|\n]|&&|(?<!>)&(?![&\d])")


def main() -> int:
    try:
        payload = json.load(sys.stdin)
    except (json.JSONDecodeError, ValueError):
        return 0

    cmd = (payload.get("tool_input") or {}).get("command", "")
    if not cmd:
        return 0

    matches = list(BUILD_RE.finditer(cmd))
    if not matches:
        return 0

    # Substitution-wrapping can mask from before build.ps1; check globally.
    subst = SUBST_RE.search(cmd)
    # Only the tail after the LAST build.ps1 occurrence can mask via a
    # trailing command.
    tail = cmd[matches[-1].end():]
    tail_hit = FORBIDDEN_TAIL_RE.search(tail)

    if not subst and not tail_hit:
        return 0

    bad = (subst or tail_hit).group(0).replace("\n", "\\n")
    reason = (
        f"BLOCKED: something runs after build.ps1 in this call "
        f"(found `{bad}` after the build.ps1 token). build.ps1 must be "
        f"the LAST thing that runs — anything chained after it masks "
        f"its exit code, so a failed build reports success.\n\n"
        f"Only output redirection may follow build.ps1. A trailing "
        f"command, pipe, `&&`/`||`/`;`/newline, `echo $?`, command "
        f"substitution (`$( )` / backticks), or another program after "
        f"build is blocked — each makes the exit status come from the "
        f"trailing construct instead of build, or hides build's "
        f"output.\n\n"
        f"Use exactly one of these two forms:\n"
        f"  1. Run build.ps1 with nothing after it — full output and "
        f"exit code reach you directly.\n"
        f"  2. `build.ps1 > build.log 2>&1` with nothing after the "
        f"redirection, then Read build.log in a SEPARATE call.\n\n"
        f"Running the build and inspecting its result are TWO separate "
        f"calls. If you run the build in the background, the same rule "
        f"holds: nothing may follow build.ps1, or the completion "
        f"notification's exit code is the trailing command's, not the "
        f"build's."
    )

    out = {
        "hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "permissionDecision": "deny",
            "permissionDecisionReason": reason,
        },
        "systemMessage": (
            f"[CLAUDE.md hook] BLOCKED: command runs after build.ps1 "
            f"(masks exit code)"
        ),
    }
    json.dump(out, sys.stdout)
    return 0


if __name__ == "__main__":
    sys.exit(main())
