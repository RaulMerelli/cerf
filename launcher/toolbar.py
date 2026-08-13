from __future__ import annotations

import tkinter as tk
from pathlib import Path
from tkinter import ttk
from typing import Callable, Dict, Optional

from launch_button import LaunchSplitButton
from toolbar_overflow import OverflowBar


UPDATE_TEXT_ALL = "Update bundles"
UPDATE_TEXT_ONE = "Update bundle"
ISSUES_TEXT = "Bugs&Requests"


class Toolbar:
    def __init__(self, parent: tk.Misc, icons_dir: Optional[Path],
                 devices_dir: Path,
                 on_new: Callable[[], None],
                 on_refresh: Callable[[], None],
                 on_update: Callable[[], None],
                 on_remove_selected: Callable[[], None],
                 on_discard_selected: Callable[[], None],
                 on_launch: Callable[[Optional[str]], None],
                 on_settings: Callable[[], None],
                 on_about: Callable[[], None],
                 on_issues: Callable[[], None]) -> None:
        self._icons_dir = icons_dir
        self._icons: Dict[str, object] = {}

        self._bar = OverflowBar(parent)
        self.frame = self._bar.frame

        self.btn_new = self._button("New", "new_device", on_new)
        self.btn_remove = self._button("Remove", "delete_device",
                                       on_remove_selected, state="disabled")
        self.btn_discard = self._button("Discard state", "discard_state",
                                        on_discard_selected, state="disabled")
        self._bar.add_separator()
        self.start = LaunchSplitButton(self._bar.frame, devices_dir, on_launch,
                                       icon=self._icon("start_device"),
                                       on_resize=self._bar.refresh)
        self._bar.add(self.start.frame, entries=self.start.menu_entries)
        self._bar.add_separator()
        self.btn_refresh = self._button("Refresh bundles", "refresh_remote",
                                        on_refresh)
        self.btn_update = self._button(UPDATE_TEXT_ALL, "update_from_remote",
                                       on_update, state="disabled")
        self.btn_settings = self._button("Settings", "settings", on_settings,
                                         side="right")
        self.btn_issues = self._button(ISSUES_TEXT, "feedback", on_issues,
                                       side="right")
        self.btn_about = self._button("About", "help", on_about, side="right")
        self._bar.finish()

    def _button(self, text: str, stem: str, command: Callable[[], None],
                side: str = "left", state: str = "normal") -> ttk.Button:
        btn = ttk.Button(self._bar.frame, text=text, image=self._icon(stem),
                         compound="top", command=command, state=state)
        self._bar.add(btn, label=lambda b=btn: str(b.cget("text")),
                      command=command, side=side,
                      enabled=lambda b=btn: str(b.cget("state")) != "disabled")
        return btn

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

    def retheme(self) -> None:
        self._bar.retheme()

    def set_busy(self, busy: bool) -> None:
        state = "disabled" if busy else "normal"
        for b in (self.btn_new, self.btn_refresh, self.btn_update,
                  self.btn_remove, self.btn_discard):
            b.config(state=state)

    def set_catalog_loading(self, loading: bool) -> None:
        self.btn_refresh.config(state="disabled" if loading else "normal")

    def set_selection_enabled(self, selected_has_update: bool,
                              any_updateable: bool, can_remove: bool,
                              can_discard: bool) -> None:
        text = UPDATE_TEXT_ONE if selected_has_update else UPDATE_TEXT_ALL
        changed = str(self.btn_update.cget("text")) != text
        self.btn_update.config(
            state="normal" if any_updateable else "disabled", text=text)
        self.btn_remove.config(state="normal" if can_remove else "disabled")
        self.btn_discard.config(state="normal" if can_discard else "disabled")
        if changed:
            self._bar.refresh()
