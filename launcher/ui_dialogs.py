"""Modal dialogs and tooltips: the generic dark-themed dialog plus every
canned launcher dialog (ROM license, guest-additions help)."""
from __future__ import annotations

import tkinter as tk
import webbrowser
from tkinter import ttk
from typing import Dict, Optional

from app_paths import resolve_ga_banner
from device_state import DeviceSource
import ui_theme as theme


DISCORD_URL = "https://discord.gg/QREE9Y2v2d"
GITHUB_URL  = "https://github.com/gweslab/cerf"

# Funding targets, mirroring .github/FUNDING.yml (patreon: dz3n,
# buy_me_a_coffee: dz3n, ko_fi: dz333n) - that file is not shipped with the
# packaged launcher, so the handles are spelled out here.
PATREON_URL        = "https://www.patreon.com/dz3n"
KOFI_URL           = "https://ko-fi.com/dz333n"
BUY_ME_A_COFFEE_URL = "https://www.buymeacoffee.com/dz3n"


def show_dialog(parent: tk.Misc, title: str, message: str,
                buttons: tuple[str, ...] = ("OK",),
                default: Optional[str] = None) -> str:
    dlg = tk.Toplevel(parent)
    dlg.title(title)
    dlg.configure(bg=theme.BG)
    dlg.transient(parent)
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
    theme.apply_titlebar(dlg)
    w, h = dlg.winfo_reqwidth(), dlg.winfo_reqheight()
    x = parent.winfo_rootx() + (parent.winfo_width()  - w) // 2
    y = parent.winfo_rooty() + (parent.winfo_height() - h) // 2
    dlg.geometry(f"+{max(0, x)}+{max(0, y)}")

    dlg.grab_set()
    parent.wait_window(dlg)
    return result["value"]


def show_info(parent: tk.Misc, title: str, message: str) -> None:
    show_dialog(parent, title, message)


def show_error(parent: tk.Misc, title: str, message: str) -> None:
    show_dialog(parent, title, message)


def ask_yesno(parent: tk.Misc, title: str, message: str) -> bool:
    return show_dialog(parent, title, message, ("Yes", "No"), default="No") == "Yes"


def confirm_rom_license(parent: tk.Misc, display_name: str) -> bool:
    return ask_yesno(
        parent,
        "Download confirmation",
        "ROM bundles and add-on packages distributed by this launcher are "
        "abandonware/released publicly by their respective OEMs. They remain "
        "the property of those OEMs and are governed by whatever terms "
        "the OEM applied when releasing them.\n\n"
        f"By pressing Yes you take full personal responsibility for "
        f"downloading {display_name} and accept whatever license, "
        f"terms, or restrictions the OEM applied. The CERF project "
        f"gives no warranty, grants no license, and accepts no "
        f"liability for the contents.\n\n"
        f"Download {display_name}?"
    )


def show_dpi_help(parent: tk.Misc) -> None:
    show_info(
        parent,
        "Display DPI override",
        "Overrides the logical DPI (pixels-per-inch) the CERF guest display "
        "driver reports to the OS. It changes what the OS believes the screen "
        "density is - it most likely causes rendering artifacts and broken "
        "graphics.\n\n"
        "Known behaviour:\n"
        "• Restores VGA (2×) mode on Device Emulator ROMs.\n"
        "• Scales readable / printable text (documents, web pages) on older "
        "CE versions.\n"
        "• Works best on Alt-Controls (touch-style) ROMs.\n\n"
        "Takes effect on the next guest reset. Requires guest additions."
    )


def show_guest_additions_help(parent: tk.Misc) -> None:
    """Show the Guest Additions feature banner in a borderless-content window.
    Falls back to nothing if the banner asset can't be located."""
    path = resolve_ga_banner()
    if path is None:
        return

    dlg = tk.Toplevel(parent)
    dlg.title("Guest additions")
    dlg.configure(bg=theme.BG)
    dlg.transient(parent)
    dlg.resizable(False, False)

    banner = tk.PhotoImage(file=str(path))
    lbl = ttk.Label(dlg, image=banner, background=theme.BG)
    lbl.image = banner  # keep a reference so Tk doesn't GC the image
    lbl.pack()

    dlg.bind("<Escape>", lambda _e: dlg.destroy())
    lbl.bind("<Button-1>", lambda _e: dlg.destroy())

    dlg.update_idletasks()
    theme.apply_titlebar(dlg)
    w, h = dlg.winfo_reqwidth(), dlg.winfo_reqheight()
    x = parent.winfo_rootx() + (parent.winfo_width()  - w) // 2
    y = parent.winfo_rooty() + (parent.winfo_height() - h) // 2
    dlg.geometry(f"+{max(0, x)}+{max(0, y)}")

    dlg.grab_set()
    parent.wait_window(dlg)


def show_update_available(parent: tk.Misc, remote_version: str,
                          url: str) -> None:
    choice = show_dialog(
        parent,
        "A new CERF version is available",
        f"CERF {remote_version} is out - you're running an older build.\n\n"
        "Updating is strongly recommended. Newer builds add features and "
        "fixes, and the ROM bundle catalog keeps advancing - ROM downloads "
        "and updates may not work correctly, or may break, on an outdated "
        "CERF.\n\n"
        "Get the latest CERF:",
        buttons=("Download", "Later"),
        default="Later")
    if choice == "Download":
        webbrowser.open(url)


def _link_label(parent: tk.Misc, text: str, url: str) -> ttk.Label:
    lbl = ttk.Label(parent, text=text, foreground=theme.LINK_FG, cursor="hand2")
    lbl.bind("<Button-1>", lambda _e: webbrowser.open(url))
    return lbl


def _maybe_link(parent: tk.Misc, text: str, url: str) -> ttk.Label:
    """A clickable link when url is set, otherwise the same text as plain
    (non-clickable) label."""
    return _link_label(parent, text, url) if url else ttk.Label(parent, text=text)


def show_source_thanks(parent: tk.Misc, source: DeviceSource) -> None:
    show_sources_thanks(parent, [source] if source is not None else [])


def show_sources_thanks(parent: tk.Misc, sources) -> None:
    distinct: list = []
    seen: set = set()
    for s in sources:
        if s is not None and s.has_links and s.name not in seen:
            seen.add(s.name)
            distinct.append(s)
    if not distinct:
        return

    dlg = tk.Toplevel(parent)
    dlg.title("ROM preservation")
    dlg.configure(bg=theme.BG)
    dlg.transient(parent)
    dlg.resizable(False, False)

    body = ttk.Frame(dlg, padding=16)
    body.pack(fill="both", expand=True)
    for i, source in enumerate(distinct):
        if i:
            ttk.Label(body, text="").pack(anchor="w")
        ttk.Label(body, wraplength=420, justify="left",
                  text=f"This ROM bundle was preserved and provided by "
                       f"{source.name}.").pack(anchor="w")
        ask = ttk.Frame(body)
        ask.pack(anchor="w", pady=(6, 0))
        ttk.Label(ask, text="Would you like to ").pack(side="left")
        _maybe_link(ask, "pay them a visit", source.website).pack(side="left")
        ttk.Label(ask, text=" or ").pack(side="left")
        _maybe_link(ask, "support them", source.donate).pack(side="left")
        ttk.Label(ask, text="?").pack(side="left")
        if source.origin:
            _link_label(body, "Source data link", source.origin).pack(anchor="w")

    btns = ttk.Frame(body)
    btns.pack(anchor="e", pady=(14, 0))
    ok = ttk.Button(btns, text="OK", command=dlg.destroy)
    ok.pack(side="left")
    ok.focus_set()
    dlg.bind("<Return>", lambda _e: dlg.destroy())
    dlg.bind("<Escape>", lambda _e: dlg.destroy())

    dlg.update_idletasks()
    theme.apply_titlebar(dlg)
    w, h = dlg.winfo_reqwidth(), dlg.winfo_reqheight()
    x = parent.winfo_rootx() + (parent.winfo_width()  - w) // 2
    y = parent.winfo_rooty() + (parent.winfo_height() - h) // 2
    dlg.geometry(f"+{max(0, x)}+{max(0, y)}")


def bind_tooltip(widget: tk.Widget, text: str) -> None:
    state: Dict[str, Optional[tk.Toplevel]] = {"tip": None}

    def show(_e: object) -> None:
        if state["tip"] is not None:
            return
        tip = tk.Toplevel(widget)
        tip.wm_overrideredirect(True)
        tip.configure(bg=theme.BORDER)
        ttk.Label(tip, text=text, background=theme.BG_FIELD, foreground=theme.FG,
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
