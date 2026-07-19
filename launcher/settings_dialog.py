from __future__ import annotations

import tkinter as tk
from tkinter import ttk

from app_settings import (read_discord_rich_presence,
                          write_discord_rich_presence)
from screen_geometry import fit_geometry
from ui_dialogs import show_info
import ui_theme as theme


class SettingsDialog:
    def __init__(self, parent: tk.Misc) -> None:
        self._parent = parent
        dlg = tk.Toplevel(parent)
        self._dlg = dlg
        dlg.title("Settings")
        dlg.configure(bg=theme.BG)
        dlg.transient(parent)
        dlg.resizable(False, False)

        body = ttk.Frame(dlg, padding=16)
        body.pack(fill="both", expand=True)

        self.var_drp = tk.BooleanVar(value=read_discord_rich_presence())
        ttk.Checkbutton(body, text="Discord Rich Presence",
                        variable=self.var_drp).pack(anchor="w")
        ttk.Label(body, style="Hint.TLabel", wraplength=380, justify="left",
                  text="Shows the current device and OS version in your "
                       "Discord profile as an activity.").pack(
            anchor="w", padx=(22, 0), pady=(2, 0))

        actions = ttk.Frame(body)
        actions.pack(anchor="e", pady=(18, 0))
        ttk.Button(actions, text="Cancel", command=dlg.destroy).pack(
            side="left", padx=(0, 6))
        ttk.Button(actions, text="OK", command=self._ok).pack(side="left")

        dlg.update_idletasks()
        theme.apply_titlebar(dlg)
        fit_geometry(dlg, 440, 190, parent=parent)
        dlg.grab_set()

    def _ok(self) -> None:
        write_discord_rich_presence(self.var_drp.get())
        self._dlg.destroy()
        show_info(self._parent, "Settings saved",
                  "Emulator settings take effect only in newly launched "
                  "instances.")
