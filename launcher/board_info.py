"""Queries over the ``supported_devices.py`` board-knowledge base: board
sort order, per-board capability lookups, and the predicate-gated dynamic
notes."""
from __future__ import annotations

from typing import List, Optional

from board_catalog_schema import RomContext, STORAGE_FLAT, sort_text
from supported_devices import BOARDS_INFORMATION, DYNAMIC_NOTES


def board_sort_key(board_name: object) -> tuple[int, str]:
    """Board display order shared by the launcher tree and the README table.

    Three tiers: unknown boards (no cerf.json / no board meta) first, the
    unusual real boards alphabetically in the middle, and the massive
    official "Device Emulator" group pinned last (board names compare
    case-insensitively, same as the entry matching here).
    """
    board = sort_text(board_name)
    if not board:
        tier = 0
    elif board == "device emulator":
        tier = 2
    else:
        tier = 1
    return (tier, board)


def _board_entry(board_id: str) -> Optional[dict]:
    if not isinstance(board_id, str) or not board_id.strip():
        return None
    key = board_id.strip()
    for entry in BOARDS_INFORMATION:
        if entry.get("board_id") == key:
            return entry
    return None


def board_support_state(board_id: str) -> Optional[bool]:
    """True/False for a known board_id; None when no entry matches (board_id
    absent or not in this list -> unsupported)."""
    entry = _board_entry(board_id)
    if entry is None:
        return None
    return bool(entry.get("supported", False))


def board_extra_notes(board_id: str) -> List[str]:
    """Board-wide quirk notes that extend a ROM's own meta.notes."""
    entry = _board_entry(board_id)
    if entry is None:
        return []
    notes = entry.get("notes")
    if not isinstance(notes, list):
        return []
    return [n for n in notes if isinstance(n, str) and n.strip()]


def board_soc_cpu(board_id: str) -> Optional[str]:
    """CPU instruction-set family (e.g. "ARM" / "MIPS") for the board's SoC;
    None when the launcher has no SoC data for it."""
    entry = _board_entry(board_id)
    if entry is None:
        return None
    soc = entry.get("soc")
    if soc is None:
        return None
    return soc.cpu


def board_storage_type(board_id: str) -> str:
    """The ROM input kind the board boots from; STORAGE_FLAT when the entry
    declares none (or the board_id is unknown)."""
    entry = _board_entry(board_id)
    if entry is None:
        return STORAGE_FLAT
    storage = entry.get("storage")
    return storage if isinstance(storage, str) and storage else STORAGE_FLAT


def board_display_name(board_id: str) -> str:
    """The board's display name ("HP Jornada 820"); "" for unknown ids."""
    entry = _board_entry(board_id)
    if entry is None:
        return ""
    name = entry.get("name")
    return name if isinstance(name, str) else ""


def board_soc_label(board_id: str) -> str:
    """Display label of the board's SoC ("Samsung S3C2410 (ARM920T)"), the
    same shape a remote bundle's meta.soc_family carries; "" when the launcher
    has no SoC data for the board_id."""
    entry = _board_entry(board_id)
    if entry is None:
        return ""
    soc = entry.get("soc")
    if soc is None:
        return ""
    return f"{soc.family} ({soc.arch})"


def board_configurable_screen(board_id: str) -> bool:
    """True when the board's OAL accepts a configurable stock-video screen
    size; False for fixed-LCD boards and unknown board_ids."""
    entry = _board_entry(board_id)
    return bool(entry.get("configurable_screen", False)) if entry else False


def supported_boards() -> List[dict]:
    """The user-ready board entries, in list order - the New-device wizard's
    board choices."""
    return [e for e in BOARDS_INFORMATION if e.get("supported") is True]


def board_features(board_id: str) -> dict:
    """Capability -> bool map for the board; empty when the launcher has no
    feature data for it (unknown board_id, or no features declared)."""
    entry = _board_entry(board_id)
    if entry is None:
        return {}
    features = entry.get("features")
    if not isinstance(features, dict):
        return {}
    return {k: bool(v) for k, v in features.items() if isinstance(k, str)}


def dynamic_extra_notes(
    os_name: str,
    os_ver_major: int,
    os_ver_minor: int,
    board_id: str,
) -> List[str]:
    """Predicate-gated additional notes for one selected ROM; extend the
    ROM's meta.notes and board_extra_notes() in the side panel."""
    rom = RomContext(
        os_name=os_name,
        os_ver_major=os_ver_major,
        os_ver_minor=os_ver_minor,
        board_id=board_id,
        features=board_features(board_id),
        cpu=board_soc_cpu(board_id) or "",
    )
    return [entry.note for entry in DYNAMIC_NOTES if entry.applies(rom)]
