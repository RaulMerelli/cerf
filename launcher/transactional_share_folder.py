from __future__ import annotations

import tkinter as tk
from tkinter import ttk
from typing import Optional

from persisted_options import persist_subset
from share_folder_block import ShareFolderBlock
import ui_theme as theme

SHARE_FOLDER_KEYS = ("share_folder",)


class _ShareFolderDialog:
    def __init__(self, ctx, _query: dict):
        self._ctx = ctx
        self._accepted = False

        dlg = tk.Toplevel(ctx.root)
        dlg.withdraw()
        dlg.title("Share Folder - CE Runtime Foundation")
        dlg.configure(bg=theme.BG)
        dlg.resizable(False, False)
        self._dlg = dlg

        body = ttk.Frame(dlg, padding=14)
        body.pack(fill="both", expand=True)
        body.columnconfigure(0, weight=1)

        holder = ttk.Frame(body)
        holder.grid(row=0, column=0, sticky="ew")
        holder.columnconfigure(0, weight=1)
        self.block = ShareFolderBlock(holder, dlg, self._on_change, row=0,
                                      pick_label="Browse…", entry_width=54)
        self.block.restore(ctx.effective)
        self.block.refresh_state(False)

        buttons = ttk.Frame(body)
        buttons.grid(row=1, column=0, sticky="e", pady=(16, 0))
        ok = ttk.Button(buttons, text="OK", command=self._on_ok)
        ok.pack(side="left", padx=(6, 0))
        ttk.Button(buttons, text="Cancel",
                   command=self._on_cancel).pack(side="left", padx=(6, 0))
        ok.focus_set()

        dlg.bind("<Escape>", lambda _e: self._on_cancel())
        dlg.protocol("WM_DELETE_WINDOW", self._on_cancel)

    def run(self) -> Optional[dict]:
        self._ctx.present(self._dlg)
        return None

    def _on_change(self) -> None:
        return

    def _on_ok(self) -> None:
        fields: dict = {}
        self.block.collect(fields)
        persist_subset(self._ctx.device_dir, self._ctx.baseline,
                       SHARE_FOLDER_KEYS, fields)
        self._accepted = True
        self._dlg.destroy()

    def _on_cancel(self) -> None:
        self._accepted = False
        self._dlg.destroy()


def run_share_folder(ctx, query: dict) -> Optional[dict]:
    return _ShareFolderDialog(ctx, query).run()
