#!/usr/bin/env python3
"""
PostToolUse hook for Write|Edit on C/C++ source. Fires when the agent
AUTHORS a comment, and demands the citation test on it.

Enforces the comment rule in agent_docs/code_style.md § Comments. The
emitted message carries the test the agent must apply.

Scope is the text the agent authored in this call:
  - Write: tool_input.content.
  - Edit:  tool_input.new_string, minus comments that already appear
           verbatim in tool_input.old_string, so a comment carried
           through an edit unchanged does not fire.

The scanner is string-literal aware, so "http://x" and '/' inside string
or char literals are not mistaken for comments.
"""
import json
import os
import sys

import _hookpath

SOURCE_EXTS = (".cpp", ".c", ".h", ".hpp", ".cc", ".cxx")


def scan_line(line, in_block):
    """Return (list of comment fragments on this line, new in_block state)."""
    parts = []
    i, n = 0, len(line)
    in_str = None
    while i < n:
        c = line[i]
        if in_block:
            j = line.find("*/", i)
            if j >= 0:
                parts.append(line[i:j])
                in_block = False
                i = j + 2
            else:
                parts.append(line[i:])
                i = n
            continue
        if in_str:
            if c == "\\":
                i += 2
                continue
            if c == in_str:
                in_str = None
            i += 1
            continue
        if c in ('"', "'"):
            in_str = c
            i += 1
            continue
        if c == "/" and i + 1 < n:
            if line[i + 1] == "/":
                parts.append(line[i + 2:])
                i = n
                continue
            if line[i + 1] == "*":
                in_block = True
                i += 2
                continue
        i += 1
    return parts, in_block


def extract_comments(src):
    """Return [(line_no, raw_line, comment_text), ...] for lines carrying
    comment content."""
    out = []
    in_block = False
    for idx, line in enumerate(src.splitlines(), start=1):
        parts, in_block = scan_line(line, in_block)
        text = " ".join(p.strip() for p in parts if p.strip())
        if text:
            out.append((idx, line.strip(), text))
    return out


def main() -> int:
    try:
        payload = json.loads(sys.stdin.buffer.read().decode("utf-8-sig"))
    except (json.JSONDecodeError, ValueError, UnicodeDecodeError):
        return 0

    tool_input = payload.get("tool_input") or {}
    file_path = _hookpath.normalize(tool_input.get("file_path", ""))
    if not file_path.lower().endswith(SOURCE_EXTS):
        return 0

    new_blob = tool_input.get("content")
    if not isinstance(new_blob, str):
        new_blob = tool_input.get("new_string")
    if not isinstance(new_blob, str) or not new_blob:
        return 0

    old_blob = tool_input.get("old_string")
    old_texts = set()
    if isinstance(old_blob, str) and old_blob:
        old_texts = {t for _, _, t in extract_comments(old_blob)}

    added = [c for c in extract_comments(new_blob) if c[2] not in old_texts]
    if not added:
        return 0

    try:
        rel = os.path.relpath(file_path).replace("\\", "/")
    except ValueError:
        rel = file_path.replace("\\", "/")

    sample = "\n".join(f"  {raw[:110]}" for _, raw, _ in added[:8])
    more = f"\n  ... and {len(added) - 8} more" if len(added) > 8 else ""

    msg = (
        f"COMMENTS-FORBIDDEN: this write/edit of {rel} authored "
        f"{len(added)} comment line(s):\n\n{sample}{more}\n\n"
        f"THE RULE (CLAUDE.md, agent_docs/code_style.md § Comments): a "
        f"comment is a CITATION, or it does not exist. There is no "
        f"third kind.\n\n"
        f"APPLY THE TEST TO EACH COMMENT ABOVE. It survives ONLY if it "
        f"is one of these two:\n"
        f"  1. HARDWARE CITATION - names the external source of truth: "
        f"chip datasheet section / table / figure, register name + "
        f"offset + bit field, CPU architecture reference manual "
        f"section, BSP source path, standard / RFC clause.\n"
        f"  2. REVERSE-ENGINEERING CITATION - names the decompiled "
        f"guest it is derived from: module + address / VA, function "
        f"name at an address, ROM offset.\n\n"
        f"IF IT IS NEITHER, DELETE IT. Not reword. Not shorten. Not "
        f"'distill'. DELETE.\n\n"
        f"Deleted by definition (non-exhaustive): what the code does, "
        f"why an approach was chosen, what was considered and "
        f"rejected, 'do not do X', design rationale, invariant "
        f"restatement, section banners, summaries of the function "
        f"below, narration of the edit, anything a reader could get "
        f"from the code itself.\n\n"
        f"A citation with no NAMED source is not a citation. Delete "
        f"it.\n\n"
        f"FALSE-POSITIVE GATE: if a listed line is not actually a "
        f"comment you authored (pre-existing text carried through the "
        f"edit unchanged), ignore that line."
    )

    out = {
        "hookSpecificOutput": {
            "hookEventName": "PostToolUse",
            "additionalContext": msg,
        },
        "systemMessage": (
            f"[CLAUDE.md hook] COMMENTS-FORBIDDEN: {len(added)} authored "
            f"comment line(s) in {rel}"
        ),
    }
    json.dump(out, sys.stdout)
    return 0


if __name__ == "__main__":
    sys.exit(main())
