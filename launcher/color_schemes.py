"""Guest-additions color-scheme override catalog: the (key, label) choices the
launch-options dropdown offers, chronological by era, and the key<->label maps.
The key is what cerf.exe receives as --ga-color-scheme."""
from __future__ import annotations

from board_catalog_schema import (HANDHELD_PC_PRO, HANDHELD_PC_2000,
                                  WINDOWS_CE_NET, WINDOWS_MOBILE_2003SE,
                                  WINDOWS_MOBILE_5, WINDOWS_MOBILE_6)

COLOR_SCHEMES = [
    ("",              "Do not override color scheme"),
    ("ce2_grayscale", "Windows CE 2.0 Grayscale"),
    ("hpc3",          HANDHELD_PC_PRO.name),
    ("hpc2000",       HANDHELD_PC_2000.name),
    ("ce4",           WINDOWS_CE_NET.name),
    ("xp",            "Windows XP Luna"),
    ("vista",         "Windows Vista"),
    ("wm5",           WINDOWS_MOBILE_5.name),
    ("wm6",           WINDOWS_MOBILE_6.name),
    ("wm6_green",     "Windows Mobile 6 Green"),
    ("wm6_guava",     "Windows Mobile 6 Guava Bubbles"),
    ("wm65",          "Windows Mobile 6.5"),
]
CS_KEY_TO_LABEL = {k: d for (k, d) in COLOR_SCHEMES}
CS_LABEL_TO_KEY = {d: k for (k, d) in COLOR_SCHEMES}

_COLOR_SCHEME_UNSUPPORTED_OS = {
    WINDOWS_MOBILE_2003SE.name, WINDOWS_MOBILE_5.name, WINDOWS_MOBILE_6.name,
}


def color_scheme_supported_for_os(os_name: str) -> bool:
    n = (os_name or "").strip()
    if n in _COLOR_SCHEME_UNSUPPORTED_OS:
        return False
    nl = n.casefold()
    return not (nl.startswith("windows mobile") or nl.startswith("wm "))
