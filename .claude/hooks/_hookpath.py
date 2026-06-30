"""Shared path helper for Claude Code hooks on Windows.

Claude Code forwards the Write/Edit `file_path` into the hook payload verbatim,
in whatever notation the tool received it. Sessions that drive the tools with
Git-Bash / MSYS paths (`/z/tmp/foo.cpp`, lowercase drive, forward slashes) hand
the hook a path that native Windows Python's os.path.isfile / open cannot
resolve, so every file-touching hook silently no-ops. normalize() converts the
MSYS drive form back to a Windows drive path so the stat/open succeeds; it
leaves already-Windows paths and genuine POSIX paths (e.g. /usr/...) untouched.
"""
import re

# /<single-letter>(/rest)?  ->  the next char after the drive letter must be a
# slash or end-of-string, so /usr, /tmp, /home never match (only /z, /c/...).
_MSYS_DRIVE_RE = re.compile(r"^/([A-Za-z])(/.*)?$")


def normalize(path: str) -> str:
    if not path:
        return path
    m = _MSYS_DRIVE_RE.match(path)
    if not m:
        return path
    return m.group(1).upper() + ":" + (m.group(2) or "/")
