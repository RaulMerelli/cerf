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


def _version_header_text() -> str:
    meipass = getattr(sys, "_MEIPASS", None)
    candidates: List[Path] = []
    if meipass:
        candidates.append(Path(meipass) / "version.h")
    candidates.append(exe_dir() / "version.h")
    candidates.append(exe_dir() / ".." / "cerf" / "version.h")
    for path in candidates:
        if path.is_file():
            return path.read_text(encoding="utf-8", errors="ignore")
    return ""


def _int_define(text: str, name: str) -> Optional[int]:
    match = re.search(r"#define\s+" + name + r"\s+(\d+)", text)
    return int(match.group(1)) if match else None


def _str_define(text: str, name: str) -> str:
    match = re.search(r'#define\s+' + name + r'\s+"([^"]*)"', text)
    return match.group(1) if match else ""


def resolve_version_tuple() -> Optional[tuple]:
    text = _version_header_text()
    major = _int_define(text, "CERF_VERSION_MAJOR")
    minor = _int_define(text, "CERF_VERSION_MINOR")
    if major is None or minor is None:
        return None
    return (major, minor,
            _int_define(text, "CERF_VERSION_PATCH") or 0,
            _int_define(text, "CERF_VERSION_BUILD") or 0)


def resolve_version() -> str:
    text = _version_header_text()
    major = _int_define(text, "CERF_VERSION_MAJOR")
    minor = _int_define(text, "CERF_VERSION_MINOR")
    if major is None or minor is None:
        return ""
    patch = _int_define(text, "CERF_VERSION_PATCH") or 0
    version = "{}.{}".format(major, minor)
    if patch:
        version += ".{}".format(patch)
    build = _int_define(text, "CERF_VERSION_BUILD") or 0
    if not build:
        return version
    detail = [part for part in ("build {}".format(build),
                                _str_define(text, "CERF_VERSION_DATE"),
                                _str_define(text, "CERF_VERSION_SHA")) if part]
    return "{} ({})".format(version, ", ".join(detail))
