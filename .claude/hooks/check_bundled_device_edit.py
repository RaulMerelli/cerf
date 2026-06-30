#!/usr/bin/env python3
"""
PostToolUse hook for Write|Edit. Fires when the edited file lives inside
a downloaded ROM bundle directory under bundled/devices/<dir>/.

These directories are populated by the CERF launcher (launcher.exe,
sources in launcher/), which pulls device bundles from an upstream ROMs
repository. They are NOT CERF built-in source - CERF itself never
produces these files, they originate from the online ROM bundle the user
sync'd locally. Any local edit will be OVERWRITTEN the next time the
user re-syncs that device.

If the agent needs a change inside a bundled device (cerf.json default,
a registry tweak, a TOC fix, etc.), the FIX BELONGS UPSTREAM in the
ROMs repository the launcher pulls from. The agent must tell the user
WHAT to change upstream - silently patching the local copy just defers
the same problem until the next sync.

Advisory - sometimes a local edit IS appropriate (one-shot debugging
diagnostic that the user explicitly directed, scratch experiment, etc.).
The hook can't tell the two apart; it flags every edit and lets the
agent self-check.
"""
import json
import os
import re
import sys

import _hookpath


# Match `bundled/devices/<dir>/<anything>` - requires the third segment
# to be followed by a slash, so the orchestration files directly under
# bundled/devices/ (sync_bundles.py, manifest.json) are NOT matched.
BUNDLED_DEVICE_RE = re.compile(r"^bundled/devices/[^/]+/", re.IGNORECASE)


def main() -> int:
    try:
        # BOM-tolerant: some sessions pipe the payload as UTF-8-with-BOM, which
        # json.load(sys.stdin) rejects (JSONDecodeError at char 0) -> silent no-op.
        payload = json.loads(sys.stdin.buffer.read().decode("utf-8-sig"))
    except (json.JSONDecodeError, ValueError, UnicodeDecodeError):
        return 0

    tool_input = payload.get("tool_input") or {}
    tool_response = payload.get("tool_response") or {}
    file_path = _hookpath.normalize(tool_response.get("filePath") or tool_input.get("file_path"))
    if not file_path:
        return 0

    try:
        rel = os.path.relpath(file_path).replace("\\", "/")
    except ValueError:
        rel = file_path.replace("\\", "/")

    if not BUNDLED_DEVICE_RE.match(rel):
        return 0

    # Pull the device directory name for a sharper warning headline.
    device = rel.split("/", 3)[2] if rel.count("/") >= 2 else "<dir>"

    msg = (
        f"BUNDLED-DEVICE-EDIT: you just modified {rel}. This path is "
        f"inside the downloaded ROM bundle for device '{device}' - it "
        f"is NOT CERF built-in source. The directory was populated by "
        f"the CERF launcher (launcher.exe) from the upstream ROMs "
        f"repository.\n\n"
        f"Your local edit will be OVERWRITTEN the next time the user "
        f"re-syncs this device (the launcher pulls the bundle fresh "
        f"from the manifest and overwrites the local copy). "
        f"Even if the user never re-syncs, anyone else with the same "
        f"device locally will not see your change - there is no "
        f"propagation path from this checkout back to other users.\n\n"
        f"If the change is genuinely needed, the FIX BELONGS UPSTREAM "
        f"in the ROMs repository the launcher pulls from. Tell the "
        f"user explicitly:\n"
        f"  1. WHICH file in the upstream bundle to change.\n"
        f"  2. WHAT the change is (concrete diff or new content).\n"
        f"  3. WHY - what CERF behaviour depends on it.\n"
        f"The user can then update the upstream ROMs repo, republish, "
        f"and re-sync - at which point every checkout reflects the "
        f"fix.\n\n"
        f"If this IS a one-shot local diagnostic / scratch edit and "
        f"the user explicitly directed it, ignore this and proceed."
    )

    out = {
        "hookSpecificOutput": {
            "hookEventName": "PostToolUse",
            "additionalContext": msg,
        },
        "systemMessage": f"[CLAUDE.md hook] bundled device edited: {rel}",
    }
    json.dump(out, sys.stdout)
    return 0


if __name__ == "__main__":
    sys.exit(main())
