"""Resolution of on-disk locations: the exe directory, the devices tree,
cerf.exe, the app icon, the feature-icon assets, and the CERF version."""
from __future__ import annotations

import re
import sys
from pathlib import Path
from typing import List, Optional


def exe_dir() -> Path:
    if getattr(sys, "frozen", False):
        return Path(sys.executable).resolve().parent
    return Path(__file__).resolve().parent


def resolve_devices_dir() -> Path:
    # The launcher and cerf.exe are co-located; cerf.exe reads/writes its ROMs
    # and state.img under "<exe dir>/devices", so the launcher uses the same tree.
    return exe_dir() / "devices"


def resolve_cerf_exe() -> Optional[Path]:
    candidate = exe_dir() / "cerf.exe"
    if candidate.is_file():
        return candidate
    return None


def resolve_icon() -> Optional[Path]:
    meipass = getattr(sys, "_MEIPASS", None)
    if meipass:
        candidate = Path(meipass) / "launcher.ico"
        if candidate.is_file():
            return candidate
    candidate = exe_dir() / "launcher.ico"
    if candidate.is_file():
        return candidate
    repo_candidate = exe_dir() / ".." / "cerf" / "assets" / "launcher.ico"
    if repo_candidate.is_file():
        return repo_candidate.resolve()
    return None


def resolve_icons_dir() -> Optional[Path]:
    meipass = getattr(sys, "_MEIPASS", None)
    candidates: List[Path] = []
    if meipass:
        candidates.append(Path(meipass) / "assets" / "icons")
    candidates.append(exe_dir() / "assets" / "icons")
    candidates.append(Path(__file__).resolve().parent / "assets" / "icons")
    for path in candidates:
        if path.is_dir():
            return path
    return None


def resolve_logo() -> Optional[Path]:
    meipass = getattr(sys, "_MEIPASS", None)
    candidates: List[Path] = []
    if meipass:
        candidates.append(Path(meipass) / "assets" / "gweslab.png")
    candidates.append(exe_dir() / "assets" / "gweslab.png")
    candidates.append(Path(__file__).resolve().parent.parent / "gweslab.png")
    for path in candidates:
        if path.is_file():
            return path
    return None


def resolve_version() -> str:
    meipass = getattr(sys, "_MEIPASS", None)
    candidates: List[Path] = []
    if meipass:
        candidates.append(Path(meipass) / "version.h")
    candidates.append(exe_dir() / "version.h")
    candidates.append(exe_dir() / ".." / "cerf" / "version.h")
    for path in candidates:
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        # CI (build.yml) rewrites CERF_VERSION_DISPLAY_STR to the full stamped
        # string ("3.0.0 (<UTC timestamp>, <short SHA>)"). Local builds leave
        # it as a bare macro reference, which this quoted-literal regex misses,
        # so we fall back to assembling the clean semver below.
        display = re.search(r'#define\s+CERF_VERSION_DISPLAY_STR\s+"([^"]+)"', text)
        if display:
            return display.group(1)
        major = re.search(r"#define\s+CERF_VERSION_MAJOR\s+(\d+)", text)
        minor = re.search(r"#define\s+CERF_VERSION_MINOR\s+(\d+)", text)
        patch = re.search(r"#define\s+CERF_VERSION_PATCH\s+(\d+)", text)
        if major and minor and patch:
            return f"{major.group(1)}.{minor.group(1)}.{patch.group(1)}"
    return ""
