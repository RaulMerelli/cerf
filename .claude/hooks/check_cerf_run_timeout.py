#!/usr/bin/env python3
"""
PostToolUse hook for Bash. Warns when `cerf.exe` is invoked without GNU
`timeout` ahead of it.

Per CLAUDE.md: "Always use GNU timeout for cerf.exe — prefer optimal
timeout looking at logs, unless user has different purposes of this run."

A bare `cerf.exe` run can hang on a boot regression or runaway loop and
burn wall-clock that's hard to recover. The rule does carve out
"different purposes" (long-term stability runs, interactive debugging,
perf bench), so this is ADVISORY, not a hard block.

Detection: match `cerf.exe` only at command position — start of the
command, or after an inline separator (`;`, `&&`, `||`, `|`),
optionally preceded by a path prefix (`./`, `build/Release/x64/`,
etc.). The anchored shape means the regex CANNOT match if `timeout`
sits between the separator and `cerf.exe` — so a successful match
is itself proof that `cerf.exe` runs unprotected.

Misses by design: prefix-command forms like `sudo ./cerf.exe` /
`wine cerf.exe` (no false-negative warning), and the literal token
inside a quoted echo (no false-positive warning). Both trade-offs
favor low noise; the common in-repo forms are caught.
"""
import json
import re
import sys

CERF_AT_COMMAND_RE = re.compile(
    r"(?:^|[;&|]\s*)(?:\S*/)?cerf\.exe\b",
    re.IGNORECASE,
)


def main() -> int:
    try:
        # BOM-tolerant: some sessions pipe the payload as UTF-8-with-BOM, which
        # json.load(sys.stdin) rejects (JSONDecodeError at char 0) -> silent no-op.
        payload = json.loads(sys.stdin.buffer.read().decode("utf-8-sig"))
    except (json.JSONDecodeError, ValueError, UnicodeDecodeError):
        return 0

    cmd = (payload.get("tool_input") or {}).get("command", "")
    if not cmd:
        return 0

    if not CERF_AT_COMMAND_RE.search(cmd):
        return 0

    msg = (
        "MISSING-TIMEOUT: cerf.exe was invoked without GNU `timeout` "
        "ahead of it. Per CLAUDE.md: 'Always use GNU timeout for "
        "cerf.exe — prefer optimal timeout looking at logs, unless "
        "user has different purposes of this run.' A bare cerf.exe "
        "run can hang on a boot regression or runaway loop and burn "
        "wall-clock that's hard to recover. If the user explicitly "
        "asked for a no-timeout run (long-term stability test, "
        "interactive debugging, perf bench), ignore this. Otherwise: "
        "prepend `timeout Xs` where X is chosen from observed boot "
        "time in cerf.log."
    )

    out = {
        "hookSpecificOutput": {
            "hookEventName": "PostToolUse",
            "additionalContext": msg,
        },
        "systemMessage": "[CLAUDE.md hook] cerf.exe ran without timeout",
    }
    json.dump(out, sys.stdout)
    return 0


if __name__ == "__main__":
    sys.exit(main())
