from __future__ import annotations

import tkinter as tk
import webbrowser
from pathlib import Path
from tkinter import ttk
from typing import Callable, Dict, Optional

from launch_button import LaunchSplitButton


ARTICLES_URL = "https://cerf.cx/articles/"

UPDATE_TEXT_ALL = "Update bundles"
UPDATE_TEXT_ONE = "Update bundle"


class Toolbar:
    def __init__(self, parent: tk.Misc, icons_dir: Optional[Path],
                 devices_dir: Path,
                 on_new: Callable[[], None],
                 on_refresh: Callable[[], None],
                 on_update: Callable[[], None],
                 on_remove_selected: Callable[[], None],
                 on_discard_selected: Callable[[], None],
                 on_launch: Callable[[Optional[str]], None]) -> None:
        self._icons_dir = icons_dir
        self._icons: Dict[str, object] = {}

        bar = ttk.Frame(parent, padding=(8, 6))
        self.frame = bar

        self.btn_help = ttk.Button(bar, text="Help", image=self._icon("help"),
                                   compound="top",
                                   command=lambda: webbrowser.open(ARTICLES_URL))
        self.btn_help.pack(side="right")

        self.btn_new = ttk.Button(bar, text="New",
                                  image=self._icon("new_device"), compound="top",
                                  command=on_new)
        self.btn_new.pack(side="left")
        self.btn_remove = ttk.Button(bar, text="Remove",
                                     image=self._icon("delete_device"),
                                     compound="top",
                                     command=on_remove_selected, state="disabled")
        self.btn_remove.pack(side="left", padx=(8, 0))
        self.btn_discard = ttk.Button(bar, text="Discard state",
                                      image=self._icon("discard_state"),
                                      compound="top",
                                      command=on_discard_selected,
                                      state="disabled")
        self.btn_discard.pack(side="left", padx=(8, 0))
        ttk.Separator(bar, orient="vertical").pack(side="left", fill="y",
                                                   padx=8, pady=2)
        self.start = LaunchSplitButton(bar, devices_dir, on_launch,
                                       icon=self._icon("start_device"))
        self.start.frame.pack(side="left")
        ttk.Separator(bar, orient="vertical").pack(side="left", fill="y",
                                                   padx=8, pady=2)
        self.btn_refresh = ttk.Button(bar, text="Refresh bundles",
                                      image=self._icon("refresh_remote"),
                                      compound="top", command=on_refresh)
        self.btn_refresh.pack(side="left")
        self.btn_update = ttk.Button(bar, text=UPDATE_TEXT_ALL,
                                     image=self._icon("update_from_remote"),
                                     compound="top",
                                     command=on_update, state="disabled")
        self.btn_update.pack(side="left", padx=(8, 0))

    def _icon(self, stem: str) -> object:
        if self._icons_dir is None:
            return ""
        if stem not in self._icons:
            try:
                self._icons[stem] = tk.PhotoImage(
                    file=str(self._icons_dir / f"{stem}.png"))
            except tk.TclError:
                self._icons[stem] = ""
        return self._icons[stem]

    def set_busy(self, busy: bool) -> None:
        state = "disabled" if busy else "normal"
        for b in (self.btn_new, self.btn_refresh, self.btn_update,
                  self.btn_remove, self.btn_discard):
            b.config(state=state)

    def set_selection_enabled(self, selected_has_update: bool,
                              any_updateable: bool, can_remove: bool,
                              can_discard: bool) -> None:
        self.btn_update.config(
            state="normal" if any_updateable else "disabled",
            text=UPDATE_TEXT_ONE if selected_has_update else UPDATE_TEXT_ALL)
        self.btn_remove.config(state="normal" if can_remove else "disabled")
        self.btn_discard.config(state="normal" if can_discard else "disabled")
