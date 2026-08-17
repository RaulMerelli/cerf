"""The colour-depth (bpp) override sub-panel of the launch options: its
slider, the Auto stop, and its persisted field."""
from __future__ import annotations

import tkinter as tk
from tkinter import ttk
from typing import Callable, List, Optional

from ui_dialogs import show_bpp_help

BPP_STOPS = (0, 8, 16, 24, 32)


def bpp_label(value: int) -> str:
    if value == 0:
        return "Auto"
    if value in BPP_STOPS:
        return "{} bpp".format(value)
    return "Custom - {} bpp".format(value)


def nearest_stop_index(value: int) -> int:
    if value in BPP_STOPS:
        return BPP_STOPS.index(value)
    return min(range(1, len(BPP_STOPS)),
               key=lambda i: abs(BPP_STOPS[i] - value))


class BppOptionBlock:
    def __init__(self, cfg: ttk.Frame, window: tk.Misc,
                 on_change: Callable[[], None],
                 row_head: int, row_fields: int, row_sep: int):
        self._window = window
        self._on_change = on_change
        self._sync_guard = False
        self._value = 0

        self.head = ttk.Frame(cfg)
        self.head.grid(row=row_head, column=0, sticky="ew")
        self.head.columnconfigure(0, weight=1)
        ttk.Label(self.head, text="Color depth:").grid(row=0, column=0, sticky="w")
        self.help = ttk.Button(self.head, text="?", width=2, style="Help.TButton",
                               command=lambda: show_bpp_help(self._window))
        self.help.grid(row=0, column=1, sticky="e")

        self.fields = ttk.Frame(cfg)
        self.fields.grid(row=row_fields, column=0, sticky="ew", pady=(2, 0))
        self.fields.columnconfigure(0, weight=1)
        self.slider = ttk.Scale(self.fields, from_=0, to=len(BPP_STOPS) - 1,
                                orient="horizontal", style="Res.Horizontal.TScale",
                                command=self._on_slider)
        self.slider.grid(row=0, column=0, sticky="ew", pady=(6, 0))
        self.label = ttk.Label(self.fields, text=bpp_label(0), style="Hint.TLabel")
        self.label.grid(row=1, column=0, sticky="w")

        self.sep = ttk.Separator(cfg, orient="horizontal")
        self.sep.grid(row=row_sep, column=0, sticky="ew", pady=8)

    def lockables(self) -> List[tk.Widget]:
        return [self.help, self.slider]

    def blocks(self) -> List[tk.Widget]:
        return [self.head, self.fields, self.sep]

    def restore(self, eff: dict) -> None:
        value = eff.get("bpp", 0)
        if not isinstance(value, int) or value < 0:
            value = 0
        self._set_value(value)

    def optional_value(self) -> Optional[int]:
        return self._value if self._value != 0 else None

    def refresh_state(self, locked: bool) -> None:
        self.slider.config(state="disabled" if locked else "normal")

    def _set_value(self, value: int) -> None:
        self._value = value
        self._sync_guard = True
        try:
            self.slider.set(nearest_stop_index(value))
        finally:
            self._sync_guard = False
        self.label.config(text=bpp_label(value))

    def _on_slider(self, raw: str) -> None:
        if self._sync_guard:
            return
        index = max(0, min(len(BPP_STOPS) - 1, int(round(float(raw)))))
        if abs(float(raw) - index) > 1e-9:
            self.slider.set(index)
            return
        value = BPP_STOPS[index]
        if value == self._value:
            return
        self._set_value(value)
        self._on_change()
