"""Dark theme: palette constants, ttk style setup, DPI awareness, and the
Win32 dark titlebar attribute."""
from __future__ import annotations

import ctypes
import sys
import tkinter as tk
from pathlib import Path
from tkinter import ttk
from typing import Dict, Optional

from device_state import (
    STATE_AVAILABLE,
    STATE_INSTALLED,
    STATE_UPDATE,
    STATE_USER,
)


BG          = "#1e1e1e"
BG_LIGHTER  = "#252526"
BG_FIELD    = "#2d2d30"
BG_HOVER    = "#3c3c3c"
BG_SELECTED = "#094771"
FG          = "#e0e0e0"
FG_DIM      = "#808080"
BORDER      = "#3f3f46"
UPDATE_LINK = "#e8c44a"  # yellow "update available" status-bar link
LINK_FG     = "#569cd6"  # community links in the status bar

STATE_TINT = {
    STATE_INSTALLED: "#1e3a1e",
    STATE_UPDATE:    "#3a2f12",
    STATE_AVAILABLE: BG_FIELD,
    STATE_USER:      "#3a1e3a",
}


def load_badge(icons_dir: Optional[Path], cpu: Optional[str],
               cache: Dict[str, Optional[tk.PhotoImage]]
               ) -> Optional[tk.PhotoImage]:
    """CPU-arch badge PNG (badge_<cpu>.png) at native size; cached in `cache`
    and kept referenced so Tk doesn't GC it. Returns None when unavailable."""
    if not cpu or icons_dir is None:
        return None
    key = cpu.lower()
    if key not in cache:
        try:
            cache[key] = tk.PhotoImage(file=str(icons_dir / f"badge_{key}.png"))
        except tk.TclError:
            cache[key] = None
    return cache[key]


def enable_dpi_awareness() -> None:
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


def enable_dark_titlebar(window: tk.Misc) -> None:
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


def apply_dark_theme(root: tk.Tk) -> None:
    root.configure(bg=BG)
    style = ttk.Style(root)
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

    # The boot-mode dropdown half of the launch split button: same green and
    # height as Launch.TButton, narrow so it reads as a chevron beside it.
    style.configure("LaunchArrow.TButton",
                    background="#107c10", foreground="#ffffff",
                    bordercolor="#0b5a0b", padding=(2, 8),
                    borderwidth=1, font=("Segoe UI", 10, "bold"))
    style.map("LaunchArrow.TButton",
              background=[("pressed",  "#0b5a0b"),
                          ("active",   "#168a16"),
                          ("disabled", BG_FIELD)],
              foreground=[("disabled", FG_DIM)],
              bordercolor=[("focus", "#168a16")])

    style.configure("Accent.TButton",
                    background="#0e639c", foreground="#ffffff",
                    bordercolor="#0a4d7a", padding=4, borderwidth=1)
    style.map("Accent.TButton",
              background=[("pressed",  BG_SELECTED),
                          ("active",   "#1177bb"),
                          ("disabled", BG_FIELD)],
              foreground=[("disabled", FG_DIM)],
              bordercolor=[("focus", "#1177bb")])

    style.configure("Danger.TButton",
                    background=BG_FIELD, foreground="#f48771",
                    bordercolor="#a1260d", padding=4, borderwidth=1)
    style.map("Danger.TButton",
              background=[("pressed", "#5a1d1d"),
                          ("active",  "#3a1e1e"),
                          ("disabled", BG)],
              foreground=[("disabled", FG_DIM)],
              bordercolor=[("disabled", BORDER),
                           ("focus", "#c42b1c")])

    style.configure("TCheckbutton",
                    background=BG, foreground=FG,
                    focuscolor=BG, indicatorcolor=BG_FIELD)
    style.map("TCheckbutton",
              background=[("active", BG)],
              foreground=[("disabled", FG_DIM)],
              indicatorcolor=[("selected", BG_SELECTED),
                              ("active",   BG_HOVER)])

    style.configure("TRadiobutton",
                    background=BG, foreground=FG,
                    focuscolor=BG, indicatorcolor=BG_FIELD)
    style.map("TRadiobutton",
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

    style.configure("Warn.TLabelframe", background=BG,
                    bordercolor="#9a6a00", lightcolor=BG, darkcolor=BG)
    style.configure("Warn.TLabelframe.Label", background=BG,
                    foreground="#ffb900", font=("Segoe UI", 9, "bold"))

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
