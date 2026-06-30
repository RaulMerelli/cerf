#!/usr/bin/env python3
"""
PreToolUse hook for Write|Edit on C/C++ source. HARD-BLOCKS any attempt
to introduce a `#pragma comment(lib, ...)` directive.

Library link dependencies belong in the .vcxproj - concretely in
cerf/cerf.vcxproj for CERF. Scattering lib pragmas across source makes
the link surface invisible to the build system, routes dependency
changes through source edits rather than project metadata, and bypasses
the per-config / per-platform link selection the .vcxproj provides.

Returns permissionDecision: "deny" on match. Only inspects writes to
C/C++ source files - documentation files that mention the pragma as an
example are not affected.
"""
import json
import re
import sys

import _hookpath

PRAGMA_LIB_RE = re.compile(r"#\s*pragma\s+comment\s*\(\s*lib\b", re.IGNORECASE)

SOURCE_EXTS = (".cpp", ".h", ".hpp", ".cc", ".cxx", ".c")


def main() -> int:
    try:
        # BOM-tolerant: some sessions pipe the payload as UTF-8-with-BOM, which
        # json.load(sys.stdin) rejects (JSONDecodeError at char 0) -> silent no-op.
        payload = json.loads(sys.stdin.buffer.read().decode("utf-8-sig"))
    except (json.JSONDecodeError, ValueError, UnicodeDecodeError):
        return 0

    tool_input = payload.get("tool_input") or {}
    file_path = _hookpath.normalize(tool_input.get("file_path", ""))
    if not file_path.lower().endswith(SOURCE_EXTS):
        return 0

    # Write tool carries the full content; Edit tool carries new_string
    # as the addition being introduced. Either path can introduce the
    # forbidden pragma.
    blobs = []
    if isinstance(tool_input.get("content"), str):
        blobs.append(tool_input["content"])
    if isinstance(tool_input.get("new_string"), str):
        blobs.append(tool_input["new_string"])
    if not blobs:
        return 0

    for blob in blobs:
        if PRAGMA_LIB_RE.search(blob):
            reason = (
                "BLOCKED: '#pragma comment(lib, ...)' is forbidden in "
                "CERF source. Library link dependencies belong in "
                "cerf/cerf.vcxproj, not as in-source pragmas. Scattering "
                "lib pragmas across source files makes the link surface "
                "invisible to the build system, routes dependency changes "
                "through source edits rather than project metadata, and "
                "bypasses per-config / per-platform link selection. Add "
                "the library to cerf/cerf.vcxproj under <AdditionalDependencies>"
                " (or the matching ItemGroup) instead."
            )
            out = {
                "hookSpecificOutput": {
                    "hookEventName": "PreToolUse",
                    "permissionDecision": "deny",
                    "permissionDecisionReason": reason,
                },
                "systemMessage": "[CLAUDE.md hook] BLOCKED: #pragma comment(lib, ...)",
            }
            json.dump(out, sys.stdout)
            return 0

    return 0


if __name__ == "__main__":
    sys.exit(main())
