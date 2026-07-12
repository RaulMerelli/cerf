from __future__ import annotations

import tkinter as tk
import webbrowser
from tkinter import ttk
from typing import Callable


ARTICLES_URL = "https://cerf.dz3n.net/articles/"


class Toolbar:
    def __init__(self, parent: tk.Misc, on_download: Callable[[], None],
                 on_refresh: Callable[[], None],
                 on_update_all: Callable[[], None],
                 on_update_selected: Callable[[], None],
                 on_remove_selected: Callable[[], None],
                 on_discard_selected: Callable[[], None]) -> None:
        bar = ttk.Frame(parent, padding=(8, 6))
        self.frame = bar
        self.btn_help = ttk.Button(bar, text="Help",
                                   command=lambda: webbrowser.open(ARTICLES_URL))
        self.btn_help.pack(side="right")
        self.btn_download = ttk.Button(bar, text="⬇  Download",
                                       style="Download.TButton",
                                       command=on_download)
        self.btn_download.pack(side="left")
        self.btn_refresh = ttk.Button(bar, text="Refresh", command=on_refresh)
        self.btn_refresh.pack(side="left", padx=(8, 0))
        self.btn_update_all = ttk.Button(bar, text="Update all",
                                         command=on_update_all)
        self.btn_update_all.pack(side="left", padx=(8, 0))
        ttk.Separator(bar, orient="vertical").pack(side="left", fill="y",
                                                   padx=8, pady=2)
        self.btn_update = ttk.Button(bar, text="Update", style="Accent.TButton",
                                     command=on_update_selected, state="disabled")
        self.btn_update.pack(side="left")
        self.btn_remove = ttk.Button(bar, text="Remove", style="Danger.TButton",
                                     command=on_remove_selected, state="disabled")
        self.btn_remove.pack(side="left", padx=(8, 0))
        self.btn_discard = ttk.Button(bar, text="Discard state",
                                      command=on_discard_selected,
                                      state="disabled")
        self.btn_discard.pack(side="left", padx=(8, 0))

    def set_busy(self, busy: bool) -> None:
        state = "disabled" if busy else "normal"
        for b in (self.btn_download, self.btn_refresh, self.btn_update_all,
                  self.btn_update, self.btn_remove, self.btn_discard):
            b.config(state=state)

    def set_selection_enabled(self, can_update: bool, can_remove: bool,
                              can_discard: bool) -> None:
        self.btn_update.config(state="normal" if can_update else "disabled")
        self.btn_remove.config(state="normal" if can_remove else "disabled")
        self.btn_discard.config(state="normal" if can_discard else "disabled")
