"""Local, developer-editable knowledge about device boards.

This list is the launcher's own opinion about which boards cerf.exe can
actually run, keyed on the ``meta.board_name`` value a bundle's cerf.json
carries. Edit it by hand when a new board lands in cerf.exe's BoardDetector
(see ``cerf/boards/board_detector.h``) or when a board's quirks change.

Semantics (matched case-insensitively on ``name``):
  * ``supported: True``  -> cerf.exe has a real BoardDetector for this board.
  * ``supported: False`` -> known board cerf.exe cannot run yet; hidden by
                            the "Hide unsupported boards" filter.
  * board not listed here, or a bundle with no board_name -> UNKNOWN. The
    launcher passes no judgement: such bundles are always shown.

``notes`` here EXTEND (do not replace) the per-ROM ``meta.notes`` shown in
the side panel. Use them for board-wide quirks that apply to every ROM on
the board.

``features`` is an optional dict of capability -> bool the side panel shows
as icons. Three states per capability:
  * True  -> hardware present and working (colour icon)
  * False -> hardware present but unsupported in CERF (greyed icon)
  * key absent -> the board has no such hardware (icon hidden entirely)
Recognised keys: display, sound, touch, keyboard, network.
"""

from __future__ import annotations

from typing import List, Optional

CE3_SHARED_STORAGE_PROBLEM = "Guest additions shared storage misbehaves on CE3"

BOARDS_INFORMATION = [
    {
        "name": "Device Emulator",
        "supported": True,
        "features": {
            "display": True,
            "sound": True,
            "touch": True,
            "keyboard": True,
            "network": True,
        },
        "notes": [
            "Stock video: don't exceed 640x480 on Windows Mobile 6.5 or newer.",
            "Stock video: don't exceed 800x600 on any OS.",
            "Keyboard is misbehaving on Windows Mobile Smartphones ROMs.",
            "Internet is supported but doesn't work",
            "Guest additions will break boot on WM version > 6.0.",
            "Guest additions cause visual artifacts in smartphone ROMs and WM2003SE",
        ],
    },
    {
        "name": "SMDK2410",
        "supported": False,
        "notes": [],
    },
    {
        "name": "PC3xx",
        "supported": True,
        "features": {"display": True, "sound": False, "touch": True, "keyboard": False},
        "notes": [
            "Stock video adapter shows severe artifacts - enable guest "
            "additions for a usable display.",
            "Audio is currently crippled.",
            "Guest additions shared storage broken on CE 4",
        ],
    },
    {
        "name": "iPAQ 3600",
        "supported": True,
        "features": {"display": True, "sound": True, "touch": True},
        "notes": [
            "Occasional random freezes (suspected OS-timer issue).",
            "Non-critical PCMCIA errors may appear on screen.",
            "Calibration only responds to the stock stylus, not the mouse "
            "pointer; switch input method if a screen needs the stylus.",
            "OS seems to really disrespect the guest additions pointer: "
            "once you see any stalls/OS doesnt register click/OS registers click "
            "in a weird way - try switching to stock input and back, use both",
            "Guest additions break multi-XIP PPC2002 ROM",
            CE3_SHARED_STORAGE_PROBLEM,
        ],
    },
    {
        "name": "ODO/Poseidon",
        "supported": True,
        "features": {"display": True, "sound": True, "touch": True, "keyboard": True},
        "notes": [
            "Audio is currently crippled.",
            CE3_SHARED_STORAGE_PROBLEM,
            "Guest additions are NOT supported on CE 2.11",
        ],
    },
    {
        "name": "OMAP 3530 EVM",
        "supported": True,
        "features": {"display": True, "sound": False, "touch": True},
        "notes": [
            "Don't open the XAML keyboard or Internet Explorer - they "
            "render blank and hurt performance.",
            "ROMs with Compositor are slower than regular",
        ],
    },
    {
        "name": "Keel",
        "supported": True,
        "features": {"display": True, "sound": False, "keyboard": True},
        "notes": [
            "Guest additions break the ROM - don't use them",
            "Main input is the keyboard: use arrows, enter, backspace, space",
            "CERF auto-generates HDD on first boot if there was no (hdd.img in device dir)",
        ],
    },
    {
        "name": "Asus A6x6",
        "supported": False,
        "notes": [],
    },
    {
        "name": "Draco",
        "supported": False,
        "notes": [],
    },
    {
        "name": "Pavo",
        "supported": False,
        "notes": [],
    },
    {
        "name": "Scorpius",
        "supported": False,
        "notes": [],
    },
]


def _board_entry(board_name: str) -> Optional[dict]:
    if not board_name:
        return None
    key = board_name.strip().casefold()
    if not key:
        return None
    for entry in BOARDS_INFORMATION:
        name = entry.get("name")
        if isinstance(name, str) and name.strip().casefold() == key:
            return entry
    return None


def board_support_state(board_name: str) -> Optional[bool]:
    """True/False if the board is a known supported/unsupported board;
    None when the launcher has no opinion (unknown board, no board_name)."""
    entry = _board_entry(board_name)
    if entry is None:
        return None
    return bool(entry.get("supported", False))


def board_extra_notes(board_name: str) -> List[str]:
    """Board-wide quirk notes that extend a ROM's own meta.notes."""
    entry = _board_entry(board_name)
    if entry is None:
        return []
    notes = entry.get("notes")
    if not isinstance(notes, list):
        return []
    return [n for n in notes if isinstance(n, str) and n.strip()]


def board_features(board_name: str) -> dict:
    """Capability -> bool map for the board; empty when the launcher has no
    feature data for it (unknown board, or no features declared)."""
    entry = _board_entry(board_name)
    if entry is None:
        return {}
    features = entry.get("features")
    if not isinstance(features, dict):
        return {}
    return {k: bool(v) for k, v in features.items() if isinstance(k, str)}
