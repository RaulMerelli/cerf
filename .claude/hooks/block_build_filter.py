#!/usr/bin/env python3
"""
PreToolUse hook for Bash AND PowerShell. HARD-BLOCKS any call that runs
build.ps1 AND also invokes an output-filtering / line-slicing command
ANYWHERE in the same call, regardless of separator (pipe, newline,
semicolon, &&).

Two tool surfaces, two filter vocabularies:

  Bash:        head, tail, grep
  PowerShell:  Select-String (grep), Select-Object -First/-Last (head/
               tail), Where-Object / `?` (grep), `| select -First/-Last`

Both failure modes apply on both surfaces:

  1. EXIT-CODE MASKING — in a compound command the last command's exit
     status becomes the whole call's, so a successful filter after a
     failed build (`build.ps1 > log 2>&1; tail log`, or `... |
     Select-Object -Last 5`) reports success. PowerShell `$?` behaves
     the same when the last pipeline / chain element is a successful
     cmdlet.

  2. OUTPUT BLINDING — filtering build output to a regex / last-N-lines
     hides the actual compiler error, so the caller can't see why the
     build failed and re-runs the whole build with a different filter.

Raw build output goes directly to the agent. Pure redirection IS still
allowed (`build.ps1 > build.log 2>&1` ALONE, no follow-up filter in the
same call): redirection preserves build's exit code, so failure stays
visible — the agent just Reads build.log afterwards. Pairing the
redirect with a filter in the SAME call is what re-introduces masking
and blinding.
"""
import json
import re
import sys

BUILD_RE = re.compile(r"\bbuild\.ps1\b", re.IGNORECASE)

# Bash filters and PowerShell filters in one alternation. PowerShell
# cmdlets and their common aliases:
#   Select-String  (alias: sls)   — grep
#   Select-Object  (alias: select) with -First/-Last/-Skip — head/tail
#   Where-Object   (aliases: where, ?) — grep
#   more / out-host -paging        — pager
FILTER_RE = re.compile(
    r"\b(head|tail|grep)\b"                       # bash
    r"|\bSelect-String\b|\bsls\b"                 # ps grep
    r"|\bSelect-Object\b|\bselect\b"              # ps head/tail/proj
    r"|\bWhere-Object\b|\bwhere\b"                # ps filter
    r"|\|\s*\?\s"                                 # ps `?` filter alias
    r"|\bmore\b|\bout-host\b",                    # pagers
    re.IGNORECASE,
)


def main() -> int:
    try:
        payload = json.load(sys.stdin)
    except (json.JSONDecodeError, ValueError):
        return 0

    cmd = (payload.get("tool_input") or {}).get("command", "")
    if not cmd:
        return 0

    if not BUILD_RE.search(cmd):
        return 0

    m = FILTER_RE.search(cmd)
    if not m:
        return 0

    filter_token = m.group(0).strip().strip("|").strip()
    reason = (
        f"BLOCKED: build.ps1 is piped/chained into an output filter "
        f"(`{filter_token}`) in the same call. Two reasons this is "
        f"forbidden:\n\n"
        f"  1. Exit-code masking — the last command in a chain sets "
        f"the call's exit status, so the filter's success hides "
        f"build.ps1's failure. A failed build reads as succeeded.\n"
        f"  2. Output blinding — the filter drops the compiler error "
        f"lines, so you cannot see why the build failed and end up "
        f"re-running the whole build (minutes) to try another filter.\n\n"
        f"Do one of:\n"
        f"  - Run build.ps1 with no pipe/filter at all — full output "
        f"and exit code reach you intact.\n"
        f"  - Redirect alone: `build.ps1 > build.log 2>&1` (nothing "
        f"after it in the same call), then Read build.log in a "
        f"separate call.\n\n"
        f"Running the build and inspecting its output must be two "
        f"separate calls."
    )

    out = {
        "hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "permissionDecision": "deny",
            "permissionDecisionReason": reason,
        },
        "systemMessage": (
            f"[CLAUDE.md hook] BLOCKED: build.ps1 + filter "
            f"('{filter_token}')"
        ),
    }
    json.dump(out, sys.stdout)
    return 0


if __name__ == "__main__":
    sys.exit(main())
