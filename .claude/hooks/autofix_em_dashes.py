#!/usr/bin/env python3
"""
PostToolUse hook for Write|Edit. Replaces non-ASCII Unicode dashes with
the ASCII hyphen '-' in scanned source files, in place, and notifies the
agent when it did.

Windows CE toolchains and the guest cannot handle non-ASCII dashes; they
are destructive in CE source. This hook strips them automatically on
every edit so they never reach a build.

Targets, written as backslash-u escapes so THIS file contains no literal
Unicode dash (a self-scan therefore finds nothing): em dash U+2014, en
dash U+2013, horizontal bar U+2015.

Scanned extensions: .py .cpp .c .h .hpp .ps1 .cmd .bat .md

The hook SKIPS ITS OWN FILE: rewriting the running hook is the "explode"
case. Combined with the ASCII-only source above, the hook can never
corrupt itself.

The in-place rewrite is a plain filesystem write from this subprocess; it
does NOT go through the Claude Code tool layer, so it does not re-trigger
PostToolUse (no loop).
"""
import json
import os
import sys

import _hookpath

SCAN_EXTS = (".py", ".cpp", ".c", ".h", ".hpp", ".ps1", ".cmd", ".bat", ".md")

# em dash, en dash, horizontal bar (chr() keeps this file ASCII-only).
DASHES = (chr(0x2014), chr(0x2013), chr(0x2015))


def main() -> int:
    try:
        payload = json.loads(sys.stdin.buffer.read().decode("utf-8-sig"))
    except (json.JSONDecodeError, ValueError, UnicodeDecodeError):
        return 0

    tool_input = payload.get("tool_input") or {}
    tool_response = payload.get("tool_response") or {}
    file_path = _hookpath.normalize(tool_response.get("filePath") or tool_input.get("file_path"))
    if not file_path:
        return 0
    if not file_path.lower().endswith(SCAN_EXTS):
        return 0

    # Skip our own file: rewriting the running hook is the explode case.
    try:
        if os.path.abspath(file_path) == os.path.abspath(__file__):
            return 0
    except OSError:
        pass

    if not os.path.isfile(file_path):
        return 0

    try:
        with open(file_path, "rb") as f:
            raw = f.read()
    except OSError:
        return 0

    # Decode preserving BOM + encoding, so the rewrite changes only dashes.
    if raw.startswith(b"\xef\xbb\xbf"):
        bom, enc, body = b"\xef\xbb\xbf", "utf-8", raw[3:]
    elif raw.startswith(b"\xff\xfe"):
        bom, enc, body = b"\xff\xfe", "utf-16-le", raw[2:]
    elif raw.startswith(b"\xfe\xff"):
        bom, enc, body = b"\xfe\xff", "utf-16-be", raw[2:]
    else:
        bom, enc, body = b"", "utf-8", raw

    try:
        text = body.decode(enc)
    except UnicodeDecodeError:
        return 0

    count = sum(text.count(d) for d in DASHES)
    if count == 0:
        return 0

    for d in DASHES:
        text = text.replace(d, "-")

    try:
        with open(file_path, "wb") as f:
            f.write(bom + text.encode(enc))
    except OSError:
        return 0

    try:
        rel = os.path.relpath(file_path).replace("\\", "/")
    except ValueError:
        rel = file_path.replace("\\", "/")

    msg = (
        f"EM-DASH-AUTOFIX: {count} non-ASCII dash(es) detected in {rel} "
        f"and replaced automatically with ASCII '-'. Unicode dashes are "
        f"destructive in Windows CE; they are stripped on every edit. No "
        f"action needed."
    )
    out = {
        "hookSpecificOutput": {
            "hookEventName": "PostToolUse",
            "additionalContext": msg,
        },
        "systemMessage": f"[CLAUDE.md hook] em dashes auto-replaced in {rel}",
    }
    json.dump(out, sys.stdout)
    return 0


if __name__ == "__main__":
    sys.exit(main())
