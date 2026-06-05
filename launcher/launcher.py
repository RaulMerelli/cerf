#!/usr/bin/env python3
from __future__ import annotations

import argparse
import ctypes
import os
import queue
import re
import subprocess
import sys
import threading
import tkinter as tk
import traceback
import webbrowser
from concurrent.futures import Future
from pathlib import Path
from tkinter import ttk
from typing import Callable, Dict, List, Optional

_THIS_DIR = Path(__file__).resolve().parent
if str(_THIS_DIR) not in sys.path:
    sys.path.insert(0, str(_THIS_DIR))

from bundles import (
    BundleError,
    DeviceBundle,
    ManifestVersionError,
    RELEASE_TAG_URL,
    is_safe_bundle_name,
    parse_version_tuple,
)
from boards import board_support_state, board_extra_notes, board_features
from operations import BundleManager, CancelledError


def _exe_dir() -> Path:
    if getattr(sys, "frozen", False):
        return Path(sys.executable).resolve().parent
    return Path(__file__).resolve().parent


def _resolve_devices_dir() -> Path:
    exe_dir = _exe_dir()
    sibling = exe_dir / "devices"
    if sibling.is_dir():
        return sibling
    current = exe_dir
    for _ in range(6):
        candidate = current / "bundled" / "devices"
        if candidate.is_dir():
            return candidate
        if current.parent == current:
            break
        current = current.parent
    return sibling


def _resolve_cerf_exe() -> Optional[Path]:
    exe_dir = _exe_dir()
    candidate = exe_dir / "cerf.exe"
    if candidate.is_file():
        return candidate
    return None


def _resolve_icon() -> Optional[Path]:
    meipass = getattr(sys, "_MEIPASS", None)
    if meipass:
        candidate = Path(meipass) / "cerf.ico"
        if candidate.is_file():
            return candidate
    exe_dir = _exe_dir()
    candidate = exe_dir / "cerf.ico"
    if candidate.is_file():
        return candidate
    repo_candidate = exe_dir / ".." / "cerf" / "assets" / "cerf.ico"
    if repo_candidate.is_file():
        return repo_candidate.resolve()
    return None


def _resolve_icons_dir() -> Optional[Path]:
    meipass = getattr(sys, "_MEIPASS", None)
    candidates: List[Path] = []
    if meipass:
        candidates.append(Path(meipass) / "assets" / "icons")
    candidates.append(_exe_dir() / "assets" / "icons")
    candidates.append(Path(__file__).resolve().parent / "assets" / "icons")
    for path in candidates:
        if path.is_dir():
            return path
    return None


def _resolve_version() -> str:
    meipass = getattr(sys, "_MEIPASS", None)
    candidates: List[Path] = []
    if meipass:
        candidates.append(Path(meipass) / "version.h")
    exe_dir = _exe_dir()
    candidates.append(exe_dir / "version.h")
    candidates.append(exe_dir / ".." / "cerf" / "version.h")
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


# Side-panel feature icons, in display order. (boards.py key, icon file, tooltip).
FEATURE_SPECS = [
    ("display",  "display.png",  "Display"),
    ("sound",    "speaker.png",  "Sound"),
    ("touch",    "stylus.png",   "Touch"),
    ("keyboard", "keyboard.png", "Keyboard"),
    ("network",  "internet.png", "Network"),
]

STATE_INSTALLED = "Installed"
STATE_UPDATE    = "Update available"
STATE_AVAILABLE = "Available"
STATE_USER      = "User device"

BG          = "#1e1e1e"
BG_LIGHTER  = "#252526"
BG_FIELD    = "#2d2d30"
BG_HOVER    = "#3c3c3c"
BG_SELECTED = "#094771"
FG          = "#e0e0e0"
FG_DIM      = "#808080"
BORDER      = "#3f3f46"
UPDATE_LINK = "#e8c44a"  # yellow "update available" status-bar link

STATE_TINT = {
    STATE_INSTALLED: "#1e3a1e",
    STATE_UPDATE:    "#3a2f12",
    STATE_AVAILABLE: BG_FIELD,
    STATE_USER:      "#3a1e3a",
}

# Common display resolutions the override slider snaps through, ordered by
# pixel area (ascending) so the thumb moves monotonically small -> large,
# spanning PDA sizes up to 4K UHD. Both orientations of the everyday small
# sizes are included; the boxes stay free-form so any value off this list
# is still typable.
RES_PRESETS = [
    (240, 320),  (320, 240),    #  QVGA
    (320, 480),  (480, 320),    #  HVGA
    (640, 480),  (480, 640),    #  VGA
    (800, 480),  (480, 800),    #  WVGA
    (854, 480),                 #  FWVGA
    (800, 600),  (600, 800),    #  SVGA
    (1024, 600),                #  WSVGA
    (1024, 768), (768, 1024),   #  XGA
    (1280, 720),                #  HD 720p
    (1280, 800),                #  WXGA
    (1366, 768),                #  HD
    (1280, 1024),               #  SXGA
    (1440, 900),                #  WXGA+
    (1600, 900),                #  HD+
    (1680, 1050),               #  WSXGA+
    (1600, 1200),               #  UXGA
    (1920, 1080),               #  FHD 1080p
    (1920, 1200),               #  WUXGA
    (2560, 1440),               #  QHD 1440p
    (2560, 1600),               #  WQXGA
    (3440, 1440),               #  UW-QHD
    (3840, 2160),               #  4K UHD
]


def _enable_dpi_awareness() -> None:
    if sys.platform != "win32":
        return
    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(2)
        return
    except (OSError, AttributeError):
        pass
    try:
        ctypes.windll.user32.SetProcessDPIAware()
    except (OSError, AttributeError):
        pass


def _enable_dark_titlebar(window: tk.Misc) -> None:
    if sys.platform != "win32":
        return
    try:
        window.update_idletasks()
        hwnd = ctypes.windll.user32.GetParent(window.winfo_id())
        if hwnd == 0:
            hwnd = window.winfo_id()
        value = ctypes.c_int(1)
        ctypes.windll.dwmapi.DwmSetWindowAttribute(
            hwnd, 20, ctypes.byref(value), ctypes.sizeof(value))
    except (OSError, AttributeError):
        pass


def _sort_text(value: object) -> str:
    if not isinstance(value, str):
        return ""
    return " ".join(value.casefold().split())


def _sort_optional_text(value: object) -> tuple[bool, str]:
    text = _sort_text(value)
    return (not bool(text), text)


def _sort_optional_int(value: object, *, missing_when_zero: bool = True) -> tuple[bool, int]:
    number = value if isinstance(value, int) and not isinstance(value, bool) else 0
    missing = number == 0 if missing_when_zero else False
    return (missing, number)


def _device_sort_key(d: DeviceBundle) -> tuple:
    meta = d.meta
    version_missing = not (meta.os_ver_major or meta.os_ver_minor)
    return (
        _sort_text(meta.device_name or d.name),
        _sort_optional_text(meta.os_name),
        _sort_optional_int(meta.os_ver_major, missing_when_zero=version_missing),
        _sort_optional_int(meta.os_ver_minor, missing_when_zero=version_missing),
        _sort_optional_int(meta.device_year),
        _sort_text(d.name),
    )


def _int_metadata(value: object) -> int:
    return value if isinstance(value, int) and not isinstance(value, bool) else 0


def _os_name_has_version(name: str, major: int, minor: int) -> bool:
    if not name or not (major or minor):
        return False

    version_pattern = (
        rf"(?<![\d.]){major}\.{minor}(?:\.\d+)*(?!\d)"
    )
    if re.search(version_pattern, name):
        return True

    if minor == 0:
        major_pattern = rf"(?<![\d.]){major}(?![\d.])"
        return bool(re.search(major_pattern, name))

    return False


def _table_os_label(d: DeviceBundle) -> str:
    meta = d.meta
    name = meta.os_name.strip() if isinstance(meta.os_name, str) else ""
    major = _int_metadata(meta.os_ver_major)
    minor = _int_metadata(meta.os_ver_minor)
    has_version = bool(major or minor)
    if not has_version:
        return name
    if _os_name_has_version(name, major, minor):
        return name
    version = f"{major}.{minor}"
    return f"{name} ({version})" if name else f"Unknown OS ({version})"


class LauncherApp(tk.Tk):
    def __init__(self, manager: BundleManager, cerf_exe: Optional[Path]):
        super().__init__()
        self.manager = manager
        self.cerf_exe = cerf_exe
        version = _resolve_version()
        self.title(f"CERF {version} Launcher" if version else "CERF Launcher")

        try:
            dpi = float(self.winfo_fpixels("1i"))
            self.tk.call("tk", "scaling", dpi / 72.0)
        except tk.TclError:
            dpi = 96.0

        scale = max(1.0, dpi / 96.0)
        self.geometry(f"{int(1180 * scale)}x{int(620 * scale)}")
        self.minsize(int(1000 * scale), int(520 * scale))

        icon = _resolve_icon()
        if icon is not None:
            try:
                self.iconbitmap(str(icon))
            except tk.TclError:
                pass

        self._apply_dark_theme()

        self.progress_queue: "queue.Queue[tuple[str, int, Optional[int]]]" = queue.Queue()
        self.cancel_event = threading.Event()
        self.busy = False
        self.devices: List[DeviceBundle] = []
        self.selected_name: Optional[str] = None

        self._icons_dir = _resolve_icons_dir()
        self._icon_cache: Dict[tuple[str, bool], Optional[tk.PhotoImage]] = {}

        self._build_ui()
        _enable_dark_titlebar(self)
        self._pump_progress()
        self.after(50, self._refresh_manifest)
        self.after(50, self._start_update_check)

        self.protocol("WM_DELETE_WINDOW", self._on_close)

    def _apply_dark_theme(self) -> None:
        self.configure(bg=BG)
        style = ttk.Style(self)
        style.theme_use("clam")

        style.configure(".",
                        background=BG, foreground=FG,
                        fieldbackground=BG_FIELD, bordercolor=BORDER,
                        lightcolor=BG, darkcolor=BG,
                        troughcolor=BG_FIELD,
                        selectbackground=BG_SELECTED, selectforeground=FG,
                        insertcolor=FG, focuscolor=BG_SELECTED)

        style.configure("TFrame",       background=BG)
        style.configure("TLabel",       background=BG, foreground=FG)
        style.configure("TSeparator",   background=BORDER)
        style.configure("TLabelframe",  background=BG, foreground=FG,
                                       bordercolor=BORDER)
        style.configure("TLabelframe.Label", background=BG, foreground=FG)

        style.configure("TButton",
                        background=BG_FIELD, foreground=FG,
                        bordercolor=BORDER, padding=4, borderwidth=1)
        style.map("TButton",
                  background=[("pressed", BG_SELECTED),
                              ("active",  BG_HOVER),
                              ("disabled", BG)],
                  foreground=[("disabled", FG_DIM)],
                  bordercolor=[("focus", BG_SELECTED)])

        style.configure("Launch.TButton",
                        background="#107c10", foreground="#ffffff",
                        bordercolor="#0b5a0b", padding=(16, 8),
                        borderwidth=1, font=("Segoe UI", 10, "bold"))
        style.map("Launch.TButton",
                  background=[("pressed",  "#0b5a0b"),
                              ("active",   "#168a16"),
                              ("disabled", BG_FIELD)],
                  foreground=[("disabled", FG_DIM)],
                  bordercolor=[("focus", "#168a16")])

        style.configure("TCheckbutton",
                        background=BG, foreground=FG,
                        focuscolor=BG, indicatorcolor=BG_FIELD)
        style.map("TCheckbutton",
                  background=[("active", BG)],
                  foreground=[("disabled", FG_DIM)],
                  indicatorcolor=[("selected", BG_SELECTED),
                                  ("active",   BG_HOVER)])

        style.configure("Guest.TCheckbutton",
                        background=BG, foreground="#9cdcfe",
                        focuscolor=BG, indicatorcolor=BG_FIELD,
                        font=("Segoe UI", 10, "bold"))
        style.map("Guest.TCheckbutton",
                  background=[("active", BG)],
                  foreground=[("active", "#b5e4ff"),
                              ("disabled", FG_DIM)],
                  indicatorcolor=[("selected", "#107c10"),
                                  ("active",   BG_HOVER)])

        style.configure("Hint.TLabel", background=BG, foreground=FG_DIM)

        style.configure("Help.TButton", padding=(4, 1))

        style.configure("TEntry",
                        fieldbackground=BG_FIELD, foreground=FG,
                        bordercolor=BORDER, insertcolor=FG)

        style.configure("TScrollbar",
                        background=BG_FIELD, troughcolor=BG,
                        bordercolor=BORDER, arrowcolor=FG)
        style.map("TScrollbar", background=[("active", BG_HOVER)])

        style.configure("Horizontal.TProgressbar",
                        background=BG_SELECTED, troughcolor=BG_FIELD,
                        bordercolor=BORDER, lightcolor=BG_SELECTED,
                        darkcolor=BG_SELECTED)

        style.configure("Res.Horizontal.TScale",
                        background=BG, troughcolor=BG_FIELD,
                        bordercolor=BORDER, lightcolor=BG_SELECTED,
                        darkcolor=BG_SELECTED)
        style.map("Res.Horizontal.TScale",
                  background=[("active", BG_HOVER), ("disabled", BG)],
                  troughcolor=[("disabled", BG)])

        style.configure("Treeview",
                        background=BG_FIELD, foreground=FG,
                        fieldbackground=BG_FIELD, bordercolor=BORDER,
                        rowheight=24)
        style.map("Treeview",
                  background=[("selected", BG_SELECTED)],
                  foreground=[("selected", FG)])
        style.configure("Treeview.Heading",
                        background=BG_LIGHTER, foreground=FG,
                        bordercolor=BORDER, relief="flat")
        style.map("Treeview.Heading",
                  background=[("active", BG_HOVER)])

    def _build_ui(self) -> None:
        # Status bar is packed first (side=bottom) so it is always reserved at
        # the window's bottom edge; the expanding content fills the rest above.
        self._build_status(self)

        outer = ttk.Frame(self, padding=8)
        outer.pack(fill="both", expand=True)
        outer.columnconfigure(0, weight=1, minsize=650)
        outer.columnconfigure(1, weight=0, minsize=300)
        outer.rowconfigure(0, weight=1)

        self._build_left(outer)
        self._build_right(outer)

    def _build_left(self, parent: ttk.Frame) -> None:
        left = ttk.Frame(parent)
        left.grid(row=0, column=0, sticky="nsew", padx=(0, 8))
        left.rowconfigure(1, weight=1)
        left.columnconfigure(0, weight=1)

        filter_bar = ttk.Frame(left)
        filter_bar.grid(row=0, column=0, columnspan=2, sticky="ew", pady=(0, 6))
        self.var_hide_unsupported = tk.BooleanVar(value=True)
        ttk.Checkbutton(filter_bar, text="Hide unsupported boards",
                        variable=self.var_hide_unsupported,
                        command=self._reload_device_list).pack(side="left")

        columns = ("os", "year", "board", "soc", "status")
        tree = ttk.Treeview(left, columns=columns, show="tree headings", selectmode="browse")
        tree.heading("#0", text="Device")
        tree.heading("os", text="OS")
        tree.heading("year", text="Year")
        tree.heading("board", text="Board")
        tree.heading("soc", text="SoC")
        tree.heading("status", text="Status")
        tree.column("#0", width=200, minwidth=140, anchor="w", stretch=True)
        tree.column("os", width=260, minwidth=180, anchor="w", stretch=True)
        tree.column("year", width=55, minwidth=45, anchor="center", stretch=False)
        tree.column("board", width=140, minwidth=95, anchor="w", stretch=True)
        tree.column("soc", width=105, minwidth=80, anchor="w", stretch=True)
        tree.column("status", width=100, minwidth=90, anchor="w", stretch=False)
        tree.grid(row=1, column=0, sticky="nsew")
        vsb = ttk.Scrollbar(left, orient="vertical", command=tree.yview)
        vsb.grid(row=1, column=1, sticky="ns")
        tree.configure(yscrollcommand=vsb.set)
        tree.bind("<<TreeviewSelect>>", self._on_select_device)
        tree.bind("<Double-1>", lambda _e: self._launch())
        for state, tint in STATE_TINT.items():
            tree.tag_configure(state, background=tint, foreground=FG)
        self.tree = tree

        bottom = ttk.Frame(left)
        bottom.grid(row=2, column=0, columnspan=2, sticky="ew", pady=(8, 0))
        bottom.columnconfigure(0, weight=1)
        bottom.columnconfigure(1, weight=1)
        bottom.columnconfigure(2, weight=1)
        self.btn_refresh = ttk.Button(bottom, text="Refresh manifest", command=self._refresh_manifest)
        self.btn_refresh.grid(row=0, column=0, sticky="ew", padx=(0, 4))
        self.btn_update_all = ttk.Button(bottom, text="Update all", command=self._update_all)
        self.btn_update_all.grid(row=0, column=1, sticky="ew", padx=4)
        self.btn_pdbs_all = ttk.Button(bottom, text="PDBs for all", command=self._download_pdbs_all)
        self.btn_pdbs_all.grid(row=0, column=2, sticky="ew", padx=(4, 0))

    def _build_right(self, parent: ttk.Frame) -> None:
        right = ttk.Frame(parent)
        right.grid(row=0, column=1, sticky="nsew")
        right.columnconfigure(0, weight=1)
        right.rowconfigure(0, weight=1)  # scroll area expands; launch bar pinned

        canvas = tk.Canvas(right, bg=BG, highlightthickness=0, width=300)
        canvas.grid(row=0, column=0, sticky="nsew")
        rsb = ttk.Scrollbar(right, orient="vertical", command=canvas.yview)
        rsb.grid(row=0, column=1, sticky="ns")
        canvas.configure(yscrollcommand=rsb.set)
        self.right_canvas = canvas

        inner = ttk.Frame(canvas)
        inner_id = canvas.create_window((0, 0), window=inner, anchor="nw")
        inner.columnconfigure(0, weight=1)

        def _on_inner_config(_e: object) -> None:
            canvas.configure(scrollregion=canvas.bbox("all"))
        inner.bind("<Configure>", _on_inner_config)

        def _on_canvas_config(e: object) -> None:
            canvas.itemconfigure(inner_id, width=e.width)
            wrap = max(120, e.width - 24)
            if hasattr(self, "desc_label"):
                self.desc_label.config(wraplength=wrap)
                self.notes_label.config(wraplength=wrap)
        canvas.bind("<Configure>", _on_canvas_config)

        meta = ttk.LabelFrame(inner, text="Device", padding=8)
        meta.grid(row=0, column=0, sticky="ew", pady=(0, 8))
        meta.columnconfigure(1, weight=1)
        self.meta_vars: Dict[str, tk.StringVar] = {}
        rows = [("Display name", "device_name"),
                ("Board",        "board_name"),
                ("SoC family",   "soc_family"),
                ("OS",           "os_version"),
                ("Year",         "device_year"),
                ("State",        "state")]
        for i, (label, key) in enumerate(rows):
            ttk.Label(meta, text=label + ":").grid(row=i, column=0, sticky="w", padx=(0, 8))
            var = tk.StringVar(value="—")
            self.meta_vars[key] = var
            ttk.Label(meta, textvariable=var, wraplength=220,
                      justify="left").grid(row=i, column=1, sticky="w")

        self.features_frame = ttk.LabelFrame(inner, text="Features", padding=8)
        self.features_frame.grid(row=1, column=0, sticky="ew", pady=(0, 8))
        self.features_icons = ttk.Frame(self.features_frame)
        self.features_icons.pack(anchor="w")

        self.desc_frame = ttk.LabelFrame(inner, text="Description", padding=8)
        self.desc_frame.grid(row=2, column=0, sticky="ew", pady=(0, 8))
        self.desc_frame.columnconfigure(0, weight=1)
        self.desc_label = ttk.Label(self.desc_frame, text="", wraplength=260,
                                    justify="left")
        self.desc_label.grid(row=0, column=0, sticky="w")

        self.notes_frame = ttk.LabelFrame(inner, text="Notes & quirks", padding=8)
        self.notes_frame.grid(row=3, column=0, sticky="ew", pady=(0, 8))
        self.notes_frame.columnconfigure(0, weight=1)
        self.notes_label = ttk.Label(self.notes_frame, text="", wraplength=260,
                                     justify="left")
        self.notes_label.grid(row=0, column=0, sticky="w")
        self.features_frame.grid_remove()
        self.desc_frame.grid_remove()
        self.notes_frame.grid_remove()

        opts = ttk.LabelFrame(inner, text="Launch options", padding=8)
        opts.grid(row=4, column=0, sticky="ew", pady=(0, 8))
        opts.columnconfigure(1, weight=1)
        self.var_log_all   = tk.BooleanVar(value=False)
        self.var_flush     = tk.BooleanVar(value=False)
        self.var_flood     = tk.BooleanVar(value=False)
        self.var_no_net    = tk.BooleanVar(value=False)
        self.var_guest_additions = tk.BooleanVar(value=False)
        ttk.Checkbutton(opts, text="Enable all log channels", variable=self.var_log_all).grid(row=0, column=0, columnspan=2, sticky="w")
        ttk.Checkbutton(opts, text="Flush logs immediately",
                        variable=self.var_flush).grid(row=1, column=0, columnspan=2, sticky="w")
        ttk.Checkbutton(opts, text="Allow stdout flood",
                        variable=self.var_flood).grid(row=2, column=0, columnspan=2, sticky="w")
        ttk.Checkbutton(opts, text="Disable network backend",
                        variable=self.var_no_net).grid(row=3, column=0, columnspan=2, sticky="w")

        ttk.Separator(opts).grid(row=4, column=0, columnspan=3, sticky="ew", pady=6)

        guest = ttk.Frame(opts)
        guest.grid(row=5, column=0, columnspan=3, sticky="ew", pady=(0, 6))
        guest.columnconfigure(0, weight=1)
        ttk.Checkbutton(guest, text="Enable guest additions",
                        variable=self.var_guest_additions,
                        command=self._refresh_resolution_state,
                        style="Guest.TCheckbutton").grid(row=0, column=0, sticky="w")
        ttk.Button(guest, text="?", width=2, style="Help.TButton",
                   command=self._show_guest_additions_help).grid(row=0, column=1, sticky="e")
        ttk.Label(guest, text="(might be unstable)",
                  style="Hint.TLabel").grid(row=1, column=0, columnspan=2, sticky="w")

        ttk.Separator(opts).grid(row=6, column=0, columnspan=3, sticky="ew", pady=(0, 6))

        self.res_note = ttk.Label(opts, text="Resolution override:")
        self.res_note.grid(row=7, column=0, columnspan=3, sticky="w")
        self.var_width  = tk.StringVar(value="240")
        self.var_height = tk.StringVar(value="320")
        numeric_vcmd = (self.register(self._is_optional_uint), "%P")
        res_fields = ttk.Frame(opts)
        res_fields.grid(row=8, column=0, columnspan=3, sticky="ew", pady=(2, 0))
        res_fields.columnconfigure(5, weight=1)
        self.width_entry = ttk.Entry(res_fields, textvariable=self.var_width, width=8,
                                     validate="key", validatecommand=numeric_vcmd)
        self.height_entry = ttk.Entry(res_fields, textvariable=self.var_height, width=8,
                                      validate="key", validatecommand=numeric_vcmd)
        self.width_unit  = ttk.Label(res_fields, text="px")
        self.height_unit = ttk.Label(res_fields, text="px")
        ttk.Label(res_fields, text="Width").grid(row=0, column=0, sticky="w")
        self.width_entry.grid(row=0, column=1, sticky="w", padx=(4, 4))
        self.width_unit.grid(row=0, column=2, sticky="w", padx=(0, 12))
        ttk.Label(res_fields, text="Height").grid(row=0, column=3, sticky="w")
        self.height_entry.grid(row=0, column=4, sticky="w", padx=(4, 4))
        self.height_unit.grid(row=0, column=5, sticky="w")

        self._res_sync_guard = False
        self.res_slider = ttk.Scale(res_fields, from_=0, to=len(RES_PRESETS) - 1,
                                    orient="horizontal", style="Res.Horizontal.TScale",
                                    command=self._on_res_slider)
        self.res_slider.grid(row=1, column=0, columnspan=6, sticky="ew", pady=(8, 0))
        self.res_preset_label = ttk.Label(res_fields, text="", style="Hint.TLabel")
        self.res_preset_label.grid(row=2, column=0, columnspan=6, sticky="w")
        self.var_width.trace_add("write", self._on_res_text_changed)
        self.var_height.trace_add("write", self._on_res_text_changed)
        self._sync_slider_to_text()

        actions = ttk.LabelFrame(inner, text="Bundle actions", padding=8)
        actions.grid(row=5, column=0, sticky="ew", pady=(0, 8))
        actions.columnconfigure(0, weight=1)
        actions.columnconfigure(1, weight=1)
        actions.columnconfigure(2, weight=1)
        self.btn_download = ttk.Button(actions, text="Download", command=self._download_selected)
        self.btn_update   = ttk.Button(actions, text="Update",   command=self._update_selected)
        self.btn_delete   = ttk.Button(actions, text="Delete",   command=self._delete_selected)
        self.btn_pdbs     = ttk.Button(actions, text="Download PDBs", command=self._pdbs_selected)
        self.btn_download.grid(row=0, column=0, sticky="ew", padx=(0, 4))
        self.btn_update.grid  (row=0, column=1, sticky="ew", padx=4)
        self.btn_delete.grid  (row=0, column=2, sticky="ew", padx=(4, 0))
        self.btn_pdbs.grid    (row=1, column=0, columnspan=3, sticky="ew", pady=(4, 0))

        # Launch bar is pinned below the scroll area (right row 1), so it stays
        # visible no matter how far the options scroll or how short the window.
        launch_bar = ttk.Frame(right)
        launch_bar.grid(row=1, column=0, columnspan=2, sticky="ew", pady=(8, 0))
        self.btn_launch = ttk.Button(launch_bar, text="Launch CERF",
                                     command=self._launch,
                                     style="Launch.TButton")
        self.btn_launch.pack(side="right")

        self._bind_right_wheel(canvas)
        self._bind_right_wheel(inner)

    def _on_right_wheel(self, event: object) -> str:
        self.right_canvas.yview_scroll(int(-event.delta / 120), "units")
        return "break"

    def _bind_right_wheel(self, widget: tk.Misc) -> None:
        widget.bind("<MouseWheel>", self._on_right_wheel)
        for child in widget.winfo_children():
            self._bind_right_wheel(child)

    def _build_status(self, root: tk.Misc) -> None:
        bar = ttk.Frame(root, padding=(8, 4))
        bar.pack(fill="x", side="bottom")
        bar.columnconfigure(0, weight=1)

        self.update_var = tk.StringVar(value="")
        self._update_url: Optional[str] = None
        self.update_link = ttk.Label(bar, textvariable=self.update_var, anchor="w")
        self.update_link.grid(row=0, column=0, sticky="w")
        self.update_link.bind("<Button-1>", self._on_update_link_click)

        self.status_var = tk.StringVar(value="Ready.")
        ttk.Label(bar, textvariable=self.status_var, anchor="e").grid(
            row=0, column=1, sticky="e", padx=(8, 8))
        self.progress = ttk.Progressbar(bar, orient="horizontal", length=220, mode="determinate")
        self.progress.grid(row=0, column=2, sticky="e")

    def _is_optional_uint(self, value: str) -> bool:
        return value == "" or value.isdigit()

    def _resolution_value(self, var: tk.StringVar, entry: ttk.Entry,
                          label: str) -> Optional[int]:
        raw = var.get().strip()
        if not raw:
            self._error("Invalid resolution",
                        f"{label} must be a positive whole-pixel value.")
            entry.focus_set()
            return None
        try:
            value = int(raw, 10)
        except ValueError:
            self._error("Invalid resolution",
                        f"{label} must be a positive whole-pixel value.")
            entry.focus_set()
            return None
        if value < 1:
            self._error("Invalid resolution",
                        f"{label} must be at least 1 px.")
            entry.focus_set()
            return None
        var.set(str(value))
        return value

    def _dialog(self, title: str, message: str,
                buttons: tuple[str, ...] = ("OK",),
                default: Optional[str] = None) -> str:
        dlg = tk.Toplevel(self)
        dlg.title(title)
        dlg.configure(bg=BG)
        dlg.transient(self)
        dlg.resizable(False, False)
        result = {"value": default if default is not None else buttons[-1]}

        body = ttk.Frame(dlg, padding=16)
        body.pack(fill="both", expand=True)
        ttk.Label(body, text=message, wraplength=420, justify="left").pack(
            anchor="w", pady=(0, 14))

        btns = ttk.Frame(body)
        btns.pack(anchor="e")
        for i, label in enumerate(buttons):
            def click(l=label):
                result["value"] = l
                dlg.destroy()
            b = ttk.Button(btns, text=label, command=click)
            b.pack(side="left", padx=(6, 0))
            if i == 0:
                b.focus_set()
            dlg.bind("<Return>", lambda _e, l=label: click(l)) if i == 0 else None
        dlg.bind("<Escape>", lambda _e: dlg.destroy())

        dlg.update_idletasks()
        _enable_dark_titlebar(dlg)
        w, h = dlg.winfo_reqwidth(), dlg.winfo_reqheight()
        x = self.winfo_rootx() + (self.winfo_width()  - w) // 2
        y = self.winfo_rooty() + (self.winfo_height() - h) // 2
        dlg.geometry(f"+{max(0, x)}+{max(0, y)}")

        dlg.grab_set()
        self.wait_window(dlg)
        return result["value"]

    def _info(self, title: str, message: str) -> None:
        self._dialog(title, message)

    def _error(self, title: str, message: str) -> None:
        self._dialog(title, message)

    def _yesno(self, title: str, message: str) -> bool:
        return self._dialog(title, message, ("Yes", "No"), default="No") == "Yes"

    def _show_guest_additions_help(self) -> None:
        self._info(
            "Guest additions",
            "CERF injects own ARM library into XIP/IMGFS and replaces stock video driver "
            "with own OS version-agnostic driver. It fully replaces original video driver "
            "and operates OS APIs to orchestrate the entire stack of features.\n\n"
            "⚠️ Guest additions are experimental and unstable. Consider booting stock "
            "ROM in case if there are any glitches/instabilities.\n\n"
            "Supported features:\n"
            "- 32bpp custom big resolution (boot CE3 into 4K!)\n"
            "- Host-accelerated blitting - the driver routes blits to host which performs the "
            "full set of graphical operations in native code\n"
            "- Dynamic screen resolution (CE 4+)\n"
            "- Shared storage with host"
            "- Mouse pointer driver:\n"
            "  - required to avoid stock touch limitations on custom res\n"
            "  - guest OS cursor shape translated directly into host graphics\n"
            "  - scroll wheel support for newer CE\n"
            "  - use runtime switcher if you need to go back to stock touch"
        )

    def _confirm_rom_license(self, display_name: str) -> bool:
        return self._yesno(
            "Download confirmation",
            "ROM bundles distributed by this launcher are abandonware/"
            "released publicly by their respective OEMs. They remain the "
            "property of those OEMs and are governed by whatever terms "
            "the OEM applied when releasing them.\n\n"
            f"By pressing Yes you take full personal responsibility for "
            f"downloading {display_name} and accept whatever license, "
            f"terms, or restrictions the OEM applied. The CERF project "
            f"gives no warranty, grants no license, and accepts no "
            f"liability for the ROM contents.\n\n"
            f"Download {display_name}?"
        )

    def _set_busy(self, busy: bool, label: str = "") -> None:
        self.busy = busy
        state = "disabled" if busy else "normal"
        for b in (self.btn_refresh, self.btn_update_all, self.btn_pdbs_all,
                  self.btn_download, self.btn_update, self.btn_delete,
                  self.btn_pdbs, self.btn_launch):
            b.config(state=state)
        if busy:
            self.status_var.set(label or "Working…")
        else:
            self.status_var.set("Ready.")
            self.progress.config(value=0, mode="determinate")
        self._refresh_selection_state()

    def _await_future(self, future: Future, done: Callable[[Optional[BaseException]], None]) -> None:
        def poll() -> None:
            if future.done():
                exc = future.exception()
                done(exc)
            else:
                self.after(50, poll)
        self.after(50, poll)

    def _progress_cb(self, label: str, done: int, total: Optional[int]) -> None:
        self.progress_queue.put((label, done, total))

    def _pump_progress(self) -> None:
        try:
            while True:
                label, done, total = self.progress_queue.get_nowait()
                self.status_var.set(label)
                if total:
                    self.progress.config(mode="determinate", maximum=total, value=done)
                else:
                    if str(self.progress.cget("mode")) != "indeterminate":
                        self.progress.config(mode="indeterminate")
                        self.progress.start(80)
        except queue.Empty:
            pass
        self.after(50, self._pump_progress)

    def _refresh_manifest(self) -> None:
        if self.busy:
            return
        self._set_busy(True, "Fetching manifest…")
        future = self.manager.submit_refresh()
        def done(exc: Optional[BaseException]) -> None:
            self._set_busy(False)
            if isinstance(exc, ManifestVersionError):
                self._show_manifest_version_error(exc)
            elif exc is not None:
                self._error(
                    "Remote manifest unavailable",
                    f"{exc}\n\n"
                    f"Local devices remain available to launch. Download / "
                    f"update / PDB fetch require a reachable remote manifest — "
                    f"try again later or check your network."
                )
            self._reload_device_list()
        self._await_future(future, done)

    def _show_manifest_version_error(self, exc: ManifestVersionError) -> None:
        if exc.remote_is_newer:
            self._info(
                "A newer CERF build is required",
                f"The bundle catalog on the server uses a newer format "
                f"(manifest version {exc.remote_version}) than this CERF build "
                f"understands (version {exc.supported_version}).\n\n"
                f"Download a newer CERF build to fetch or update ROM bundles:\n"
                f"https://github.com/gweslab/cerf\n"
                f"  • Releases — latest stable build\n"
                f"  • Actions artifacts — newest CI build\n\n"
                f"Your already-installed devices remain available to launch."
            )
        else:
            self._error(
                "Remote manifest outdated",
                f"The server's bundle catalog (manifest version "
                f"{exc.remote_version}) is older than this CERF build expects "
                f"(version {exc.supported_version}). This is usually "
                f"temporary — try again later.\n\n"
                f"Your already-installed devices remain available to launch."
            )

    def _start_update_check(self) -> None:
        self._set_update_status("Checking updates…", FG_DIM, link=False)
        future = self.manager.submit_version_check()
        def done(exc: Optional[BaseException]) -> None:
            remote: Optional[str] = None
            if exc is None:
                try:
                    remote = future.result()
                except Exception:
                    remote = None
            self._apply_update_check(remote)
        self._await_future(future, done)

    def _apply_update_check(self, remote: Optional[str]) -> None:
        current = parse_version_tuple(_resolve_version())
        remote_tuple = parse_version_tuple(remote) if remote else None
        # Stay silent when either side is unknown (offline, missing file,
        # unparseable) rather than asserting a state we cannot verify.
        if remote_tuple is None or current is None:
            self._update_url = None
            self._set_update_status("", FG_DIM, link=False)
            return
        if remote_tuple > current:
            self._update_url = RELEASE_TAG_URL.format(tag=remote)
            self._set_update_status(f"Download CERF v{remote}", UPDATE_LINK, link=True)
        else:
            self._update_url = None
            self._set_update_status("No new releases of CERF available", FG_DIM, link=False)

    def _set_update_status(self, text: str, color: str, link: bool) -> None:
        self.update_var.set(text)
        self.update_link.config(foreground=color, cursor=("hand2" if link else ""))

    def _on_update_link_click(self, _event: object) -> None:
        if self._update_url:
            webbrowser.open(self._update_url)

    def _reload_device_list(self) -> None:
        previous = self.selected_name
        self.devices = sorted(self.manager.list_devices(), key=_device_sort_key)
        hide = self.var_hide_unsupported.get()
        self.tree.delete(*self.tree.get_children())
        for d in self.devices:
            # Only an explicit supported:False board is hidden. Unknown boards
            # (no board_name, or not in boards.py) are always shown.
            if hide and board_support_state(d.meta.board_name) is False:
                continue
            state = self._state_label(d)
            year = str(d.meta.device_year) if d.meta.device_year else ""
            os_label = _table_os_label(d)
            board = d.meta.board_name or ""
            soc = d.meta.soc_family or ""
            display = d.meta.device_name or d.name
            self.tree.insert("", "end", iid=d.name,
                             text=display if display != d.name else d.name,
                             values=(os_label, year, board, soc, state),
                             tags=(state,))
        visible = self.tree.get_children()
        if previous and previous in visible:
            self.tree.selection_set(previous)
            self.tree.see(previous)
        elif visible:
            self.tree.selection_set(visible[0])
            self.tree.see(visible[0])

    def _state_label(self, d: DeviceBundle) -> str:
        if d.is_user_device:
            return STATE_USER
        if d.has_update:
            return STATE_UPDATE
        if d.is_installed:
            return STATE_INSTALLED
        return STATE_AVAILABLE

    def _apply_resolution_defaults(self, device: Optional[DeviceBundle]) -> None:
        if device is not None and device.default_screen_width:
            self.var_width.set(str(device.default_screen_width))
        elif not self.var_width.get().strip():
            self.var_width.set("240")

        if device is not None and device.default_screen_height:
            self.var_height.set(str(device.default_screen_height))
        elif not self.var_height.get().strip():
            self.var_height.set("320")

    def _set_resolution_fields_enabled(self, enabled: bool) -> None:
        state = "normal" if enabled else "disabled"
        self.width_entry.config(state=state)
        self.height_entry.config(state=state)
        self.res_slider.config(state=state)
        self.res_preset_label.config(foreground=FG_DIM if enabled else BG_FIELD)

    def _on_res_slider(self, value: str) -> None:
        if self._res_sync_guard:
            return
        index = max(0, min(len(RES_PRESETS) - 1, round(float(value))))
        # Snap the continuous Scale thumb onto the discrete preset stop.
        if abs(float(value) - index) > 1e-9:
            self.res_slider.set(index)
            return
        w, h = RES_PRESETS[index]
        self._res_sync_guard = True
        try:
            self.var_width.set(str(w))
            self.var_height.set(str(h))
        finally:
            self._res_sync_guard = False
        self.res_preset_label.config(text=f"{w} × {h}")

    def _on_res_text_changed(self, *_args: object) -> None:
        if self._res_sync_guard:
            return
        self._sync_slider_to_text()

    def _sync_slider_to_text(self) -> None:
        try:
            w = int(self.var_width.get().strip())
            h = int(self.var_height.get().strip())
        except ValueError:
            self.res_preset_label.config(text="Custom")
            return
        self._res_sync_guard = True
        try:
            if (w, h) in RES_PRESETS:
                self.res_slider.set(RES_PRESETS.index((w, h)))
                self.res_preset_label.config(text=f"{w} × {h}")
            else:
                # Off-list value: park the thumb on the nearest-area preset
                # for a sensible position without overwriting the typed size.
                area = w * h
                nearest = min(range(len(RES_PRESETS)),
                              key=lambda i: abs(RES_PRESETS[i][0] * RES_PRESETS[i][1] - area))
                self.res_slider.set(nearest)
                self.res_preset_label.config(text=f"Custom — {w} × {h}")
        finally:
            self._res_sync_guard = False

    def _refresh_resolution_state(self) -> None:
        device = self._selected_device()
        if self.var_guest_additions.get():
            self.res_note.config(text="CERF display driver resolution:")
            self._set_resolution_fields_enabled(True)
            self._apply_resolution_defaults(device)
            return

        if device is not None and device.screen_supported is False:
            self.res_note.config(text="Resolution changes unsupported.")
            self._set_resolution_fields_enabled(False)
            return

        self.res_note.config(text="Resolution override:")
        self._set_resolution_fields_enabled(True)
        self._apply_resolution_defaults(device)

    def _on_select_device(self, _event: object) -> None:
        sel = self.tree.selection()
        if not sel:
            return
        self.selected_name = sel[0]
        device = self._selected_device()
        if device is None:
            return
        self.meta_vars["device_name"].set(device.meta.device_name or device.name)
        self.meta_vars["board_name"] .set(device.meta.board_name or "—")
        self.meta_vars["soc_family"] .set(device.meta.soc_family or "—")
        self.meta_vars["os_version"] .set(device.meta.os_version or "—")
        self.meta_vars["device_year"].set(str(device.meta.device_year) if device.meta.device_year else "—")
        self.meta_vars["state"].set(self._state_label(device))
        self._update_info_panels(device)
        self._refresh_resolution_state()
        self._refresh_selection_state()

    def _update_info_panels(self, device: DeviceBundle) -> None:
        self._update_features(device)

        description = device.meta.description.strip()
        if description:
            self.desc_label.config(text=description)
            self.desc_frame.grid()
        else:
            self.desc_frame.grid_remove()

        # ROM-specific notes first, then board-wide quirks from boards.py.
        notes: List[str] = list(device.meta.notes)
        notes += board_extra_notes(device.meta.board_name)
        if notes:
            self.notes_label.config(text="\n".join(f"• {n}" for n in notes))
            self.notes_frame.grid()
        else:
            self.notes_frame.grid_remove()

    def _update_features(self, device: DeviceBundle) -> None:
        for child in self.features_icons.winfo_children():
            child.destroy()
        features = board_features(device.meta.board_name)
        shown = 0
        for key, filename, label in FEATURE_SPECS:
            if key not in features:  # absent -> board has no such hardware
                continue
            supported = features[key]
            icon = self._feature_icon(filename, gray=not supported)
            if icon is None:
                continue
            lbl = ttk.Label(self.features_icons, image=icon)
            lbl.image = icon  # keep a ref so Tk doesn't GC it
            lbl.pack(side="left", padx=(0, 8))
            tip = label if supported else f"{label} (unsupported)"
            self._bind_tooltip(lbl, tip)
            shown += 1
        self._bind_right_wheel(self.features_icons)
        if shown:
            self.features_frame.grid()
        else:
            self.features_frame.grid_remove()

    def _feature_icon(self, filename: str, gray: bool) -> Optional[tk.PhotoImage]:
        cache_key = (filename, gray)
        if cache_key in self._icon_cache:
            return self._icon_cache[cache_key]
        icon: Optional[tk.PhotoImage] = None
        if self._icons_dir is not None:
            path = self._icons_dir / filename
            try:
                base = tk.PhotoImage(file=str(path))
                if base.width() > 24:  # source icons are 32px; show ~16px
                    base = base.subsample(2, 2)
                icon = self._grayscale_image(base) if gray else base
            except tk.TclError:
                icon = None
        self._icon_cache[cache_key] = icon
        return icon

    def _grayscale_image(self, img: tk.PhotoImage) -> tk.PhotoImage:
        # Desaturate in-place on a copy, preserving per-pixel transparency.
        w, h = img.width(), img.height()
        gray = img.copy()
        for y in range(h):
            for x in range(w):
                if self.tk.call(img, "transparency", "get", x, y):
                    continue
                r, g, b = img.get(x, y)
                lum = (r * 299 + g * 587 + b * 114) // 1000
                gray.put(f"#{lum:02x}{lum:02x}{lum:02x}", to=(x, y))
        return gray

    def _bind_tooltip(self, widget: tk.Widget, text: str) -> None:
        state: Dict[str, Optional[tk.Toplevel]] = {"tip": None}

        def show(_e: object) -> None:
            if state["tip"] is not None:
                return
            tip = tk.Toplevel(widget)
            tip.wm_overrideredirect(True)
            tip.configure(bg=BORDER)
            ttk.Label(tip, text=text, background=BG_FIELD, foreground=FG,
                      padding=(6, 2)).pack(padx=1, pady=1)
            x = widget.winfo_rootx()
            y = widget.winfo_rooty() + widget.winfo_height() + 2
            tip.wm_geometry(f"+{x}+{y}")
            state["tip"] = tip

        def hide(_e: object) -> None:
            if state["tip"] is not None:
                state["tip"].destroy()
                state["tip"] = None

        widget.bind("<Enter>", show)
        widget.bind("<Leave>", hide)

    def _selected_device(self) -> Optional[DeviceBundle]:
        if not self.selected_name:
            return None
        for d in self.devices:
            if d.name == self.selected_name:
                return d
        return None

    def _refresh_selection_state(self) -> None:
        d = self._selected_device()
        if d is None:
            for b in (self.btn_download, self.btn_update, self.btn_delete, self.btn_pdbs, self.btn_launch):
                b.config(state="disabled")
            return
        if self.busy:
            return
        self.btn_download.config(state=("normal" if (not d.is_installed and d.remote) else "disabled"))
        self.btn_update  .config(state=("normal" if d.has_update else "disabled"))
        self.btn_delete  .config(state=("normal" if d.is_installed else "disabled"))
        pdbs_available = bool(d.remote and d.remote.pdbs_url and not d.has_pdbs and d.is_installed)
        self.btn_pdbs    .config(state=("normal" if pdbs_available else "disabled"))
        self.btn_launch  .config(state=("normal" if self.cerf_exe else "disabled"))

    def _download_selected(self) -> None:
        d = self._selected_device()
        if d is None or self.busy or d.remote is None:
            return
        if not self._confirm_rom_license(d.meta.device_name or d.name):
            return
        self._set_busy(True, f"Downloading {d.name}…")
        f = self.manager.submit_install(d.name, with_pdbs=False, progress=self._progress_cb,
                                        cancel_event=self.cancel_event)
        self._await_future(f, lambda exc: self._after_op(exc, f"Downloaded {d.name}"))

    def _update_selected(self) -> None:
        self._download_selected()

    def _delete_selected(self) -> None:
        d = self._selected_device()
        if d is None or self.busy:
            return
        if not self._yesno("Delete device",
                           f"Remove devices/{d.name}/ and its files?\nThis cannot be undone."):
            return
        self._set_busy(True, f"Deleting {d.name}…")
        f = self.manager.submit_delete(d.name)
        self._await_future(f, lambda exc: self._after_op(exc, f"Deleted {d.name}"))

    def _pdbs_selected(self) -> None:
        d = self._selected_device()
        if d is None or self.busy or d.remote is None or not d.remote.pdbs_url:
            return
        self._set_busy(True, f"Downloading PDBs for {d.name}…")
        f = self.manager.submit_install_pdbs(d.name, self._progress_cb, self.cancel_event)
        self._await_future(f, lambda exc: self._after_op(exc, f"PDBs installed for {d.name}"))

    def _update_all(self) -> None:
        if self.busy:
            return
        targets = [d for d in self.devices if d.has_update]
        if not targets:
            self._info("Update all", "All installed bundles are up to date.")
            return
        if not self._confirm_rom_license(f"{len(targets)} bundle(s)"):
            return
        self._set_busy(True, f"Updating {len(targets)} bundle(s)…")
        names = [d.name for d in targets]
        self._run_sequence(names, "install", "Updated all")

    def _download_pdbs_all(self) -> None:
        if self.busy:
            return
        targets = [d for d in self.devices
                   if d.is_installed and d.remote and d.remote.pdbs_url and not d.has_pdbs]
        if not targets:
            self._info("Download PDBs", "No installed bundles are missing PDBs.")
            return
        self._set_busy(True, f"Downloading PDBs for {len(targets)} bundle(s)…")
        names = [d.name for d in targets]
        self._run_sequence(names, "pdbs", "PDBs installed")

    def _run_sequence(self, names: List[str], op: str, done_label: str) -> None:
        errors: List[tuple[str, BaseException]] = []
        def step(idx: int) -> None:
            if idx >= len(names):
                self._set_busy(False)
                if errors:
                    summary = "\n".join(f"{n}: {e}" for n, e in errors)
                    self._error("Sequence completed with errors", summary)
                self._reload_device_list()
                return
            name = names[idx]
            self.status_var.set(f"[{idx+1}/{len(names)}] {name}…")
            if op == "install":
                f = self.manager.submit_install(name, with_pdbs=False,
                                                progress=self._progress_cb,
                                                cancel_event=self.cancel_event)
            else:
                f = self.manager.submit_install_pdbs(name, self._progress_cb,
                                                    self.cancel_event)
            def finished(exc: Optional[BaseException]) -> None:
                if exc is not None and not isinstance(exc, CancelledError):
                    errors.append((name, exc))
                step(idx + 1)
            self._await_future(f, finished)
        step(0)

    def _after_op(self, exc: Optional[BaseException], success_msg: str) -> None:
        self._set_busy(False)
        if exc is not None:
            if isinstance(exc, CancelledError):
                self.status_var.set("Cancelled.")
            else:
                self._error("Operation failed", str(exc))
                self.status_var.set("Error.")
        else:
            self.status_var.set(success_msg)
        self._reload_device_list()

    def _launch(self) -> None:
        d = self._selected_device()
        if d is None or self.busy:
            return
        if self.cerf_exe is None:
            self._error("Cannot launch",
                        "cerf.exe not found next to launcher.exe.")
            return
        if not d.is_installed:
            if d.remote is None:
                self._error("Cannot launch",
                            f"{d.name} is not installed and no remote bundle "
                            f"is available to download.")
                return
            if not self._confirm_rom_license(d.meta.device_name or d.name):
                return
            name = d.name
            self._set_busy(True, f"Downloading {name}…")
            f = self.manager.submit_install(name, with_pdbs=False,
                                            progress=self._progress_cb,
                                            cancel_event=self.cancel_event)
            self._await_future(f, lambda exc: self._after_download_for_launch(name, exc))
            return
        self._spawn_cerf(d)

    def _after_download_for_launch(self, name: str,
                                   exc: Optional[BaseException]) -> None:
        self._set_busy(False)
        if exc is not None:
            if isinstance(exc, CancelledError):
                self.status_var.set("Cancelled.")
            else:
                self._error("Download failed", str(exc))
                self.status_var.set("Error.")
            self._reload_device_list()
            return
        self._reload_device_list()
        fresh = next((x for x in self.devices if x.name == name), None)
        if fresh is None or not fresh.is_installed:
            self._error("Launch failed",
                        f"download of {name} reported success but the "
                        f"device is not marked installed.")
            return
        self.status_var.set(f"Downloaded {name}; launching…")
        self._spawn_cerf(fresh)

    def _spawn_cerf(self, d: DeviceBundle) -> None:
        argv: List[str] = [str(self.cerf_exe), f"--device={d.name}"]
        if self.var_log_all.get(): argv.append("--log=ALL")
        if self.var_flush.get():  argv.append("--flush-outputs")
        if self.var_flood.get():  argv.append("--allow-flood")
        if self.var_no_net.get(): argv.append("--disable-network")
        guest_additions = self.var_guest_additions.get()
        if guest_additions:
            argv.append("--guest-additions")
        if guest_additions or d.screen_supported is not False:
            w = self._resolution_value(self.var_width, self.width_entry, "Width")
            if w is None:
                return
            h = self._resolution_value(self.var_height, self.height_entry, "Height")
            if h is None:
                return
            argv += [f"--screen-width={w}", f"--screen-height={h}"]
        try:
            subprocess.Popen(argv, cwd=str(self.cerf_exe.parent),
                             creationflags=getattr(subprocess, "DETACHED_PROCESS", 0))
            self.status_var.set(f"Launched cerf.exe for {d.name}.")
        except OSError as exc:
            self._error("Launch failed", str(exc))

    def _on_close(self) -> None:
        self.cancel_event.set()
        self.manager.shutdown()
        self.destroy()


def _cli_progress(label: str, done: int, total: Optional[int]) -> None:
    if total:
        pct = int(done * 100 / total)
        print(f"\r{label}: {pct}%  ({done:,} / {total:,})", end="", flush=True)
    else:
        print(f"\r{label}: {done:,} bytes", end="", flush=True)


def _print_rom_license_notice(target: str) -> None:
    print(
        f"NOTICE: ROM bundles distributed by this launcher are abandonware/"
        f"released publicly by their respective OEMs and remain their "
        f"property. By downloading {target} you take full personal "
        f"responsibility and accept whatever license or terms the OEM "
        f"applied when releasing it. The CERF project gives no warranty, "
        f"grants no license, and accepts no liability for the ROM contents.\n"
    )


def _run_in_cerf(bundle: str) -> None:
    cerf = _resolve_cerf_exe()
    if cerf is None:
        print("ERROR: cerf.exe not found next to launcher.exe; cannot "
              "run-in-cerf.", file=sys.stderr)
        return
    try:
        subprocess.Popen([str(cerf), f"--device={bundle}"],
                         cwd=str(cerf.parent),
                         creationflags=getattr(subprocess, "DETACHED_PROCESS", 0))
        print(f"Launching cerf.exe for {bundle}...")
    except OSError as exc:
        print(f"ERROR: failed to launch cerf.exe: {exc}", file=sys.stderr)


def _cli_run(devices_dir: Path, argv: List[str]) -> int:
    parser = argparse.ArgumentParser(prog="launcher.exe sync")
    parser.add_argument("command", choices=(
        "list", "download", "update", "delete",
        "update-all", "download-pdbs", "download-pdbs-all"))
    parser.add_argument("bundle", nargs="?")
    parser.add_argument("--run-in-cerf", action="store_true",
                        help="after a successful download/update, launch "
                             "cerf.exe --device=<bundle> and exit")
    args = parser.parse_args(argv)

    manager = BundleManager(devices_dir)
    try:
        manager.submit_refresh().result()
    except BundleError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1

    cancel = threading.Event()
    try:
        if args.command == "list":
            for d in manager.list_devices():
                state = "installed"
                if d.has_update:    state = "update available"
                elif not d.is_installed: state = "available"
                elif d.is_user_device:   state = "user device"
                line = f"{d.name:32} {state:18}"
                if d.remote:
                    line += f" {d.remote.updated_at}"
                print(line)
            return 0
        if args.command == "update-all":
            targets = [d for d in manager.list_devices() if d.has_update]
            if targets:
                _print_rom_license_notice(f"{len(targets)} bundle(s)")
            for d in targets:
                print(f"Updating {d.name}...")
                manager.submit_install(d.name, False, _cli_progress, cancel).result()
                print()
            return 0
        if args.command == "download-pdbs-all":
            for d in manager.list_devices():
                if d.is_installed and d.remote and d.remote.pdbs_url and not d.has_pdbs:
                    print(f"PDBs {d.name}...")
                    manager.submit_install_pdbs(d.name, _cli_progress, cancel).result()
                    print()
            return 0
        if not args.bundle:
            print(f"ERROR: {args.command} requires a bundle name", file=sys.stderr)
            return 1
        if args.command in ("download", "update"):
            _print_rom_license_notice(args.bundle)
            manager.submit_install(args.bundle, False, _cli_progress, cancel).result()
            if args.run_in_cerf:
                print()
                _run_in_cerf(args.bundle)
        elif args.command == "delete":
            manager.submit_delete(args.bundle).result()
        elif args.command == "download-pdbs":
            manager.submit_install_pdbs(args.bundle, _cli_progress, cancel).result()
        print()
        return 0
    except BundleError as e:
        print(f"\nERROR: {e}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        cancel.set()
        print("\nCancelled.", file=sys.stderr)
        return 130
    finally:
        manager.shutdown()


def main(argv: List[str]) -> int:
    devices_dir = _resolve_devices_dir()
    if not devices_dir.exists():
        try:
            devices_dir.mkdir(parents=True, exist_ok=True)
        except OSError as exc:
            print(f"ERROR: cannot create {devices_dir}: {exc}", file=sys.stderr)
            return 1

    if argv and argv[0] == "sync":
        return _cli_run(devices_dir, argv[1:])

    _enable_dpi_awareness()

    manager = BundleManager(devices_dir)
    cerf_exe = _resolve_cerf_exe()
    app = LauncherApp(manager, cerf_exe)
    try:
        app.mainloop()
    except Exception:
        traceback.print_exc()
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
