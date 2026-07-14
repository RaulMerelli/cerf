"""Local, developer-editable knowledge about device boards.

This list is the launcher's own opinion about which boards cerf.exe can
actually run, keyed on the ``board.id`` a bundle's cerf.json carries. Edit it
by hand when a new board lands in cerf.exe's BoardContext (see
``cerf/boards/board_context.h``) or when a board's quirks change.
compile_readme.py renders the same data into README.md's "Supported boards"
table, so the launcher and the README never drift apart.

Semantics (matched on ``board_id``):
  * ``supported: True``  -> cerf.exe runs this board; shown by default.
  * ``supported: False`` -> early WIP: cerf.exe has the board_id but it is
                            not user-ready; hidden by the "Hide unsupported"
                            filter and absent from the README.
  * a board.id with no entry here -> unsupported; hidden by "Hide unsupported".

``notes`` here EXTEND (do not replace) the per-ROM ``meta.notes`` shown in
the side panel. Use them for board-wide quirks that apply to every ROM on
the board.

``DYNAMIC_NOTES`` (bottom of this file) extends the side panel further with
predicate-gated additional notes: each entry's lambda runs against the
selected ROM's cerf.json metadata plus the board's feature map, so a note
can target e.g. "any networked board running Windows Mobile 4+". Add an
entry there when a note applies to a ROM-metadata predicate rather than one
whole board; grow ``RomContext`` with more metadata fields as new
predicates need them.

``features`` is an optional dict of capability -> bool the side panel shows
as icons. Three states per capability:
  * True  -> hardware present and working (colour icon)
  * False -> hardware present but unsupported in CERF (greyed icon)
  * key absent -> the board has no such hardware (icon hidden entirely)
Recognised keys are the first column of ``FEATURE_SPECS``.

Supported entries also carry the board's silicon + OS coverage, shown in
the README table (and available to the launcher):
  * ``soc``               -> a ``Soc`` constant (family + microarchitecture).
  * ``operating_systems`` -> list of ``OperatingSystem`` constants the board
                             boots in this cerf version.
"""

from __future__ import annotations

from typing import Callable, Dict, List, NamedTuple, Optional


class OperatingSystem(NamedTuple):
    name: str  # full display name


class Soc(NamedTuple):
    family: str  # e.g. "Intel SA-1110"
    arch: str  # e.g. "StrongARM"
    cpu: str  # CPU instruction-set family, e.g. "ARM" / "MIPS"


HANDHELD_PC_2000 = OperatingSystem("Handheld PC 2000")
POCKET_PC_2000 = OperatingSystem("Pocket PC 2000")
POCKET_PC_2002 = OperatingSystem("Pocket PC 2002")
WINDOWS_CE_1 = OperatingSystem("Windows CE 1.0")
WINDOWS_CE_2 = OperatingSystem("Windows CE 2.0")
PALM_SIZE_PC = OperatingSystem("Palm-size PC")
WINDOWS_CE_211 = OperatingSystem("Windows CE 2.11")
HANDHELD_PC_PRO = OperatingSystem("Handheld PC 3.0 Professional")
WINDOWS_CE_3 = OperatingSystem("Windows CE 3")
WINDOWS_CE_NET = OperatingSystem("Windows CE .NET")
WINDOWS_CE_5 = OperatingSystem("Windows CE 5")
WINDOWS_CE_6 = OperatingSystem("Windows CE 6")
WINDOWS_CE_7 = OperatingSystem("Windows CE 7")
WINDOWS_MOBILE_2003SE = OperatingSystem("WM 2003 SE")
WINDOWS_MOBILE_5 = OperatingSystem("Windows Mobile 5")
WINDOWS_MOBILE_6 = OperatingSystem("Windows Mobile 6")
ZUNE_OS_5 = OperatingSystem("Windows CE 5")

SOC_SA1100 = Soc("Intel SA-1100", "StrongARM", "ARM")
SOC_SA1110 = Soc("Intel SA-1110", "StrongARM", "ARM")
SOC_PXA255 = Soc("Intel XScale PXA255", "ARMv5TE", "ARM")
SOC_ODO = Soc("ARM720T", "ARMv4T", "ARM")
SOC_OMAP3530 = Soc("TI OMAP 3530", "Cortex-A8", "ARM")
SOC_IMX31L = Soc("Freescale i.MX31L", "ARM1136", "ARM")
SOC_IMX51 = Soc("Freescale i.MX51", "Cortex-A8", "ARM")
SOC_S3C2410 = Soc("Samsung S3C2410", "ARM920T", "ARM")
SOC_VR5500 = Soc("NEC VR5500", "MIPS IV", "MIPS")
SOC_VR4102 = Soc("NEC VR4102", "MIPS III", "MIPS")
SOC_PR31700 = Soc("Philips PR31700", "MIPS I", "MIPS")
SOC_PR31500 = Soc("Philips PR31500", "MIPS I", "MIPS")

# Feature icons in display order, shared by the launcher side panel and
# compile_readme.py. (features key, icon stem, label). The stem names an SVG
# source under cerf/assets/icons_sources/; the launcher loads <stem>.png under
# assets/icons (emitted by tools/make_icons.py), the README/website use the SVG.
FEATURE_SPECS = [
    ("display", "display", "Display"),
    ("touch", "stylus", "Touch"),
    ("mouse", "cursor", "Mouse"),
    ("keyboard", "keyboard", "Keyboard"),
    ("suspend", "suspend", "Suspend / Resume"),
    ("guest_additions", "ga_autoresize", "Guest Additions"),
    ("sound", "speaker_active", "Sound"),
    ("mic", "microphone", "Microphone"),
    ("pcmcia", "pcmcia_enabled", "PCMCIA"),
    ("network", "internet", "Network"),
    ("battery", "battery", "Battery"),
    ("serial", "serial_com", "Serial Port"),
]

AUDIO_ARTIFACTS = "Audio has artifacts/glitches"
CE3_SHARED_STORAGE_PROBLEM = "Guest additions shared storage misbehaves on CE3"
CE4_SHARED_STORAGE_BROKEN = "Guest additions shared storage is broken on CE4"
GUEST_ADDITIONS_BREAK_ROM = "Do NOT use guest additions - they break the ROM"
GUEST_ADDITIONS_POINTER_WARN = "Guest additions mouse does not work in some apps (e.g. calibration) - switch to stock input"

BOARDS_INFORMATION = [
    {
        "name": "Device Emulator",
        "board_id": "devemu",
        "supported": True,
        "soc": SOC_S3C2410,
        "operating_systems": [
            WINDOWS_CE_6,
            WINDOWS_MOBILE_5,
            WINDOWS_MOBILE_6,
            WINDOWS_MOBILE_2003SE,
            WINDOWS_CE_5,
        ],
        "features": {
            "display": True,
            "sound": True,
            "touch": True,
            "keyboard": True,
            "network": True,
            "pcmcia": True,
            "battery": True,
            "suspend": True,
            "guest_additions": True,
        },
        "notes": [
            "Stock video: don't exceed 640x480 on Windows Mobile 6.5 or newer.",
            "Stock video: don't exceed 800x600 on any OS.",
            "Guest additions cause visual artifacts on WM2003SE",
        ],
    },
    {
        "name": "Falcon 4220",
        "board_id": "falcon_4220",
        "supported": True,
        "soc": SOC_PXA255,
        "operating_systems": [WINDOWS_CE_NET],
        "features": {
            "display": True,
            "sound": True,
            "touch": True,
            "keyboard": False,
            "network": True,
            "pcmcia": True,
            "battery": True,
            "suspend": True,
            "guest_additions": True,
        },
        "notes": [
            "CERF badly emulates stock video, it renders with artifacts. You can use it with Guest Additions "
            "insetad",
            AUDIO_ARTIFACTS,
        ],
    },
    {
        "name": "iPAQ H3100/H3600/H3700",
        "board_id": "ipaq_gen1",
        "supported": True,
        "soc": SOC_SA1110,
        "operating_systems": [POCKET_PC_2000, POCKET_PC_2002],
        "features": {
            "display": True,
            "sound": True,
            "touch": True,
            "keyboard": False,
            "network": True,
            "pcmcia": True,
            "battery": False,
            "suspend": True,
            "guest_additions": True,
            "mic": True,
        },
        "notes": [GUEST_ADDITIONS_POINTER_WARN],
    },
    {
        "name": "HP Jornada 820",
        "board_id": "jornada_820",
        "supported": True,
        "soc": SOC_SA1100,
        "operating_systems": [HANDHELD_PC_PRO],
        "features": {
            "display": True,
            "sound": True,
            "keyboard": True,
            "pcmcia": True,
            "mouse": True,
            "network": True,
            "battery": True,
            "suspend": True,
            "guest_additions": True,
            "mic": False,
            "serial": False,
        },
    },
    {
        "name": "HP Jornada 720",
        "board_id": "jornada_720",
        "supported": True,
        "soc": SOC_SA1110,
        "operating_systems": [HANDHELD_PC_2000],
        "features": {
            "display": True,
            "sound": True,
            "touch": True,
            "keyboard": True,
            "network": True,
            "pcmcia": True,
            "battery": True,
            "suspend": True,
            "guest_additions": True,
            "mic": False,
            "serial": False,
        },
        "notes": [
            "Guest additions mouse is behaving weird",
            GUEST_ADDITIONS_POINTER_WARN,
            "The guest Fn key is mapped to host F10 (e.g. hold F10 + P = '{'). "
            "The keys widget menu lists the common Fn symbols.",
        ],
    },
    {
        "name": "Microsoft Windows CE Hardware Reference Platform",
        "board_id": "odo",
        "supported": True,
        "soc": SOC_ODO,
        "operating_systems": [WINDOWS_CE_211, WINDOWS_CE_3],
        "features": {
            "display": True,
            "sound": True,
            "touch": True,
            "keyboard": True,
            "battery": False,
            "guest_additions": True,
        },
        "notes": [
            "Audio is currently crippled.",
            "Guest additions keyboard causes OS to HANG - switch to stock!",
        ],
    },
    {
        "name": "OMAP 3530 EVM",
        "board_id": "omap_3530_evm",
        "supported": True,
        "soc": SOC_OMAP3530,
        "operating_systems": [WINDOWS_CE_7],
        "features": {
            "display": True,
            "sound": True,
            "touch": True,
            "suspend": False,
            "guest_additions": True,
        },
        "notes": [
            "Don't open the XAML keyboard or Internet Explorer - they "
            "render blank and hurt performance.",
            "ROMs with Compositor are slower than regular",
        ],
    },
    {
        "name": "Zune 30",
        "board_id": "zune_30",
        "supported": True,
        "soc": SOC_IMX31L,
        "operating_systems": [ZUNE_OS_5],
        "features": {
            "display": True,
            "sound": True,
            "keyboard": True,
            "guest_additions": True,
            "battery": False,
        },
        "notes": [
            "Main input is the keyboard: use arrows, enter, backspace, space",
            "CERF auto-generates HDD on first boot if there was no (hdd.img in device dir)",
            "Guest additions: You can open apps through shared storage + task manager, "
            "only extremely simple apps will run (like literally blank Win32 skeletons)",
            "Guest additions task manager wont poll process list (but run works)",
        ],
    },
    {
        "name": "Siemens SIMpad SL4",
        "board_id": "simpad_sl4",
        "supported": True,
        "soc": SOC_SA1110,
        "operating_systems": [HANDHELD_PC_2000, WINDOWS_CE_NET],
        "features": {
            "display": True,
            "sound": True,
            "touch": True,
            "pcmcia": True,
            "network": True,
            "guest_additions": True,
            "battery": True,
            "mic": False,
        },
        "notes": [
            GUEST_ADDITIONS_POINTER_WARN,
        ],
    },
    {
        "name": "NEC MobilePro 900",
        "board_id": "nec_mobilepro_900",
        "supported": True,
        "soc": SOC_PXA255,
        "operating_systems": [HANDHELD_PC_2000, WINDOWS_CE_NET],
        "features": {
            "display": True,
            "sound": True,
            "touch": True,
            "keyboard": True,
            "pcmcia": True,
            "network": True,
            "guest_additions": True,
            "battery": False,
            "mic": False,
            "suspend": False,
        },
        "notes": [
            GUEST_ADDITIONS_POINTER_WARN,
            "Emulated with lags / audio / visual issues",
        ],
    },
    {
        "name": "NEC MobilePro 700",
        "board_id": "nec_mobilepro_700",
        "supported": True,
        "soc": SOC_VR4102,
        "operating_systems": [WINDOWS_CE_2],
        "features": {
            "display": True,
            "touch": True,
            "keyboard": True,
            "sound": False,
            "network": True,
            "pcmcia": True,
            "serial": True,
            "battery": False,
            "guest_additions": True,
            "suspend": True,
        },
    },
    {
        "name": "NEC Rockhopper SG2_VR5500",
        "board_id": "nec_rockhopper",
        "supported": True,
        "soc": SOC_VR5500,
        "operating_systems": [WINDOWS_CE_6],
        "features": {
            "display": True,
            "mouse": True,
            "keyboard": True,
            "suspend": False,
            "sound": False,
            "guest_additions": True,
            "pcmcia": True,
            "network": True,
        },
        "notes": [
            "MIPS IV ROM is incompatible with MIPS II apps. Prefer using MIPS II ROM."
        ],
    },
    {
        "name": "Philips Nino 300",
        "board_id": "philips_nino_300",
        "supported": True,
        "soc": SOC_PR31700,
        "operating_systems": [PALM_SIZE_PC],
        "features": {
            "display": True,
            "touch": True,
            "keyboard": True,
            "battery": True,
            "suspend": True,
            "guest_additions": True,
            "pcmcia": True,
            "sound": True,
            "serial": True,
        },
    },
    {
        "name": "Philips Velo 1",
        "board_id": "philips_velo_1",
        "supported": True,
        "soc": SOC_PR31500,
        "operating_systems": [WINDOWS_CE_1],
        "features": {
            "display": True,
            "touch": True,
            "keyboard": True,
            "battery": True,
            "suspend": True,
            "guest_additions": False,
            "pcmcia": True,
            "network": True,
            "sound": True,
            "serial": True,
        },
    },
    {
        "name": "Siemens P177",
        "board_id": "siemens_p177",
        "supported": True,
        "soc": SOC_S3C2410,
        "operating_systems": [WINDOWS_CE_5],
        "features": {
            "display": True,
            "touch": True,
            "network": False,
            "guest_additions": True,
        },
        "notes": [],
    },
    {
        "name": "SmartBook G138",
        "board_id": "smartbook_g138",
        "supported": True,
        "soc": SOC_SA1110,
        "operating_systems": [WINDOWS_CE_NET],
        "features": {
            "display": True,
            "touch": True,
            "keyboard": True,
            "pcmcia": True,
            "network": True,
            "guest_additions": True,
            "battery": False,
            "mic": False,
            "suspend": True,
        },
    },
    {
        "name": "Ford SYNC 2",
        "board_id": "ford_sync_2",
        "supported": True,
        "soc": SOC_IMX51,
        "operating_systems": [WINDOWS_CE_6],
        "features": {
            "display": True,
            "touch": True,
            "guest_additions": False,
            "sound": False,
        },
        "notes": [
            "THIS BOARD IS NOT EMULATED WELL AT ALL! All it can do is a boot to a glitching home screen and that's it",
            "On the first boot, guest flashes ~2GB NAND inside device dir from .sec file. It will take some time and x2 disk space. After first boot, you can remove .sec file if you are not going to re-flash.",
            "GPU has severe visual artifacts.",
            "A click on screen will likely crash CERF due to missing GPU implementation",
            "Guest additions are launching if re-run with complete nand.img, but somehow causing stock GPU crash",
        ],
    },
]


def sort_text(value: object) -> str:
    """Casefolded, whitespace-collapsed text for ordering/matching."""
    if not isinstance(value, str):
        return ""
    return " ".join(value.casefold().split())


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


class RomContext(NamedTuple):
    """What a DYNAMIC_NOTES predicate gets to look at: the selected ROM's
    cerf.json metadata plus the board's feature map (same shape as
    ``board_features()``)."""

    os_name: str
    os_ver_major: int
    os_ver_minor: int
    board_id: str
    features: Dict[str, bool]
    cpu: str  # CPU instruction-set family, e.g. "ARM" / "MIPS"; "" when unknown

    def os_contains(self, fragment: str) -> bool:
        return sort_text(fragment) in sort_text(self.os_name)

    def board_is(self, board_id: str) -> bool:
        return self.board_id == board_id

    def cpu_is(self, name: str) -> bool:
        return sort_text(name) == sort_text(self.cpu)

    def has_feature(self, key: str) -> bool:
        # True only when present AND working; False = present-but-unsupported.
        return self.features.get(key) is True


class DynamicNote(NamedTuple):
    applies: Callable[[RomContext], bool]
    note: str


_NE2000_TOGGLE = "To enable internet access through NE2000, toggle on the card"

DYNAMIC_NOTES = [
    DynamicNote(
        applies=lambda rom: rom.has_feature("network")
        and (
            rom.os_contains("Pocket PC 2002")
            or (rom.os_contains("Windows Mobile") and rom.os_ver_major >= 4)
        ),
        note=_NE2000_TOGGLE
        + ", then go to Settings > Connections (tab) > Connections (icon) -> "
        'set "My network card connects to" to "The Internet". In newer '
        "versions, after opening Connections (icon) go to Advanced > "
        'Select Networks > change everything to "My Work Network".',
    ),
    DynamicNote(
        applies=lambda rom: rom.has_feature("network")
        and not rom.os_contains("Pocket PC 2002")
        and not rom.os_contains("Windows Mobile"),
        note=_NE2000_TOGGLE + ".",
    ),
    DynamicNote(
        applies=lambda rom: rom.os_ver_major == 3,
        note=CE3_SHARED_STORAGE_PROBLEM,
    ),
    DynamicNote(
        applies=lambda rom: rom.os_ver_major == 4,
        note=CE4_SHARED_STORAGE_BROKEN,
    ),
    DynamicNote(
        applies=lambda rom: rom.board_is("devemu")
        and rom.os_contains("Windows Mobile")
        and rom.os_ver_major >= 6,
        note="This ROM is most likely IMGFS, guest additions will break the boot.",
    ),
    DynamicNote(
        applies=lambda rom: rom.board_is("devemu") and rom.os_contains("Smartphone"),
        note="Keyboard is misbehaving on Smartphone ROMs.",
    ),
    DynamicNote(
        applies=lambda rom: rom.board_is("devemu") and rom.os_contains("Smartphone"),
        note="Guest additions break or cause visual artifacts on Smartphone OS.",
    ),
    DynamicNote(
        applies=lambda rom: rom.cpu_is("MIPS"),
        note="MIPS support is bare-bones and slow - expect the unexpected.",
    ),
]


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
