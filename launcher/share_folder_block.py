from __future__ import annotations

import tkinter as tk
from tkinter import filedialog, ttk
from typing import Callable, List


class ShareFolderBlock:
    def __init__(self, parent: tk.Misc, window: tk.Misc,
                 on_change: Callable[[], None], row: int = 0,
                 pick_label: str = "Pick", entry_width: int = 0):
        self._window = window
        self._on_change = on_change
        self._restoring = False

        frame = ttk.Frame(parent)
        frame.grid(row=row, column=0, sticky="ew")
        frame.columnconfigure(0, weight=1)
        self.frame = frame

        self.var_enabled = tk.BooleanVar(value=False)
        self.var_path = tk.StringVar(value="")

        self.check = ttk.Checkbutton(frame, text="Share folder with guest",
                                     variable=self.var_enabled,
                                     command=self._on_enabled_changed)
        self.check.grid(row=0, column=0, sticky="w")

        fields = ttk.Frame(frame)
        fields.grid(row=1, column=0, sticky="ew", pady=(4, 0))
        fields.columnconfigure(0, weight=1)
        if entry_width:
            self.entry = ttk.Entry(fields, textvariable=self.var_path,
                                   width=entry_width)
        else:
            self.entry = ttk.Entry(fields, textvariable=self.var_path)
        self.entry.grid(row=0, column=0, sticky="ew")
        self.pick = ttk.Button(fields, text=pick_label,
                               width=max(6, len(pick_label) + 2),
                               command=self._on_pick)
        self.pick.grid(row=0, column=1, sticky="e", padx=(6, 0))
        self.var_path.trace_add("write", self._on_path_changed)

        self._refresh_entry_state(False)

    def lockables(self) -> List[tk.Widget]:
        return [self.check, self.entry, self.pick]

    def restore(self, eff: dict) -> None:
        path = eff.get("share_folder", "")
        self._restoring = True
        try:
            self.var_enabled.set(bool(path))
            self.var_path.set(path)
        finally:
            self._restoring = False
        self._scroll_to_end()
        self._refresh_entry_state(False)

    def collect(self, out: dict) -> None:
        path = self.var_path.get().strip()
        if self.var_enabled.get() and path:
            out["share_folder"] = path

    def refresh_state(self, locked: bool) -> None:
        self.check.config(state="disabled" if locked else "normal")
        self._refresh_entry_state(locked)

    def _refresh_entry_state(self, locked: bool) -> None:
        on = self.var_enabled.get() and not locked
        state = "normal" if on else "disabled"
        self.entry.config(state=state)
        self.pick.config(state=state)

    def _scroll_to_end(self) -> None:
        try:
            self.entry.xview_moveto(1.0)
        except tk.TclError:
            pass

    def _on_enabled_changed(self) -> None:
        self._refresh_entry_state(False)
        if self.var_enabled.get() and not self.var_path.get().strip():
            self._on_pick()
            return
        self._on_change()

    def _on_path_changed(self, *_args: object) -> None:
        if self._restoring:
            return
        self._on_change()

    def _on_pick(self) -> None:
        options = {"parent": self._window, "mustexist": True,
                   "title": "Choose a host folder to share with the guest"}
        initial = self.var_path.get().strip()
        if initial:
            options["initialdir"] = initial
        picked = filedialog.askdirectory(**options)
        if not picked:
            if not self.var_path.get().strip():
                self.var_enabled.set(False)
                self._refresh_entry_state(False)
            return
        self.var_path.set(picked.replace("/", "\\"))
        self._scroll_to_end()
