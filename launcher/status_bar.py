"""Bottom status bar: community links, the release-update link, the status
text, and the progress bar."""
from __future__ import annotations

import tkinter as tk
import webbrowser
from tkinter import ttk
from typing import Callable, Optional

from ui_dialogs import (
    BUY_ME_A_COFFEE_URL,
    DISCORD_URL,
    PATREON_URL,
    WEBSITE_URL,
)
import ui_theme as theme


class StatusBar:
    def __init__(self, root: tk.Misc):
        bar = ttk.Frame(root, padding=(8, 4))
        bar.pack(fill="x", side="bottom")
        bar.columnconfigure(1, weight=1)

        links = (
            ("Discord",         DISCORD_URL),
            ("Website",         WEBSITE_URL),
            ("Patreon",         PATREON_URL),
            ("Buy me a coffee", BUY_ME_A_COFFEE_URL),
        )
        link_bar = ttk.Frame(bar)
        link_bar.grid(row=0, column=0, sticky="w")
        self._link_labels: list[ttk.Label] = []
        for col, (text, url) in enumerate(links):
            label = ttk.Label(link_bar, text=text, foreground=theme.LINK_FG,
                              cursor="hand2")
            label.grid(row=0, column=col, sticky="w", padx=(0, 12))
            label.bind("<Button-1>",
                       lambda _e, u=url: webbrowser.open(u))
            self._link_labels.append(label)

        self.update_var = tk.StringVar(value="")
        self._update_click: Optional[Callable[[], None]] = None
        self._update_is_link = False
        self.update_link = ttk.Label(bar, textvariable=self.update_var, anchor="w")
        self.update_link.grid(row=0, column=1, sticky="w")
        self.update_link.bind("<Button-1>", self._on_update_link_click)

        self.status_var = tk.StringVar(value="Ready.")
        ttk.Label(bar, textvariable=self.status_var, anchor="e").grid(
            row=0, column=2, sticky="e", padx=(8, 8))
        self.progress = ttk.Progressbar(bar, orient="horizontal", length=220,
                                        mode="determinate")
        self.progress.grid(row=0, column=3, sticky="e")

    def set_status(self, text: str) -> None:
        self.status_var.set(text)

    def set_update_status(self, text: str, color: str, link: bool,
                          on_click: Optional[Callable[[], None]] = None) -> None:
        self._update_click = on_click
        self._update_is_link = link
        self.update_var.set(text)
        self.update_link.config(foreground=color, cursor=("hand2" if link else ""))

    def retheme(self) -> None:
        for label in self._link_labels:
            label.config(foreground=theme.LINK_FG)
        self.update_link.config(
            foreground=theme.UPDATE_LINK if self._update_is_link
            else theme.FG_DIM)

    def _on_update_link_click(self, _event: object) -> None:
        if self._update_click is not None:
            self._update_click()

    def show_progress(self, done: int, total: Optional[int]) -> None:
        if total:
            self.progress.config(mode="determinate", maximum=total, value=done)
        else:
            if str(self.progress.cget("mode")) != "indeterminate":
                self.progress.config(mode="indeterminate")
                self.progress.start(80)

    def reset_progress(self) -> None:
        self.progress.config(value=0, mode="determinate")
