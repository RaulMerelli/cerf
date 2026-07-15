from __future__ import annotations

import tkinter as tk
from tkinter import ttk
from typing import Callable, List, Tuple

from bundle_repositories import (BundleRepository, read_repositories,
                                 write_repositories)
from ui_dialogs import ask_text, show_info
import ui_theme as theme


class RepositoryPanel:
    def __init__(self, parent: tk.Misc, on_changed: Callable[[], None]) -> None:
        self._on_changed = on_changed
        self._repos: List[BundleRepository] = read_repositories()
        self._enabled = True

        frame = ttk.LabelFrame(parent, text="Bundle repositories", padding=6)
        self.frame = frame
        frame.columnconfigure(0, weight=1)

        tree = ttk.Treeview(frame, columns=("url",), show="tree headings",
                            selectmode="browse", height=4)
        tree.heading("#0", text="On")
        tree.heading("url", text="Manifest URL")
        tree.column("#0", width=44, minwidth=44, anchor="center", stretch=False)
        tree.column("url", width=640, minwidth=200, anchor="w", stretch=True)
        tree.grid(row=0, column=0, sticky="nsew")
        vsb = ttk.Scrollbar(frame, orient="vertical", command=tree.yview)
        vsb.grid(row=0, column=1, sticky="ns")
        tree.configure(yscrollcommand=vsb.set)
        tree.tag_configure("main", foreground=theme.FG_DIM)
        tree.bind("<Button-1>", self._on_click)
        tree.bind("<<TreeviewSelect>>", lambda _e: self._sync_buttons())
        self.tree = tree

        btns = ttk.Frame(frame)
        btns.grid(row=1, column=0, columnspan=2, sticky="ew", pady=(6, 0))
        self.btn_add = ttk.Button(btns, text="Add…", command=self._add)
        self.btn_add.pack(side="left")
        self.btn_del = ttk.Button(btns, text="Delete", command=self._delete,
                                  state="disabled")
        self.btn_del.pack(side="left", padx=(6, 0))
        self.status = ttk.Label(btns, text="", foreground=theme.FG_DIM)
        self.status.pack(side="right")

        self._reload_table()

    def set_enabled(self, enabled: bool) -> None:
        self._enabled = enabled
        state = "normal" if enabled else "disabled"
        self.btn_add.config(state=state)
        self.status.config(text="" if enabled else "Refreshing…")
        self._sync_buttons()

    def show_errors(self, errors: List[Tuple[str, str]]) -> None:
        if errors:
            self.status.config(
                text=f"{len(errors)} repository(ies) unavailable")
        else:
            self.status.config(text="")

    def _reload_table(self) -> None:
        self.tree.delete(*self.tree.get_children())
        for r in self._repos:
            glyph = "☑" if r.enabled else "☐"
            label = r.url + ("  (main)" if r.main else "")
            self.tree.insert("", "end", iid=r.url, text=glyph,
                             values=(label,),
                             tags=(("main",) if r.main else ()))
        self._sync_buttons()

    def _sync_buttons(self) -> None:
        sel = self.tree.selection()
        url = sel[0] if sel else None
        is_main = any(r.url == url and r.main for r in self._repos)
        can_del = self._enabled and url is not None and not is_main
        self.btn_del.config(state="normal" if can_del else "disabled")

    def _on_click(self, event: tk.Event) -> str:
        if not self._enabled:
            return "break"
        region = self.tree.identify("region", event.x, event.y)
        iid = self.tree.identify_row(event.y)
        if not iid or region != "tree":
            return ""
        for r in self._repos:
            if r.url == iid:
                r.enabled = not r.enabled
                break
        self._commit()
        return "break"

    def _add(self) -> None:
        if not self._enabled:
            return
        top = self.frame.winfo_toplevel()
        url = ask_text(top, "Add bundle repository",
                       "Bundle repository URL (without /manifest.json):")
        if not url:
            return
        if not (url.startswith("http://") or url.startswith("https://")):
            show_info(top, "Invalid URL",
                      "Enter a full http:// or https:// repository URL.")
            return
        if any(r.url == url for r in self._repos):
            show_info(top, "Already added",
                      "That manifest URL is already in the list.")
            return
        self._repos.append(BundleRepository(url=url, enabled=True, main=False))
        self._commit()

    def _delete(self) -> None:
        if not self._enabled:
            return
        sel = self.tree.selection()
        if not sel:
            return
        url = sel[0]
        self._repos = [r for r in self._repos
                       if not (r.url == url and not r.main)]
        self._commit()

    def _commit(self) -> None:
        write_repositories(self._repos)
        self._reload_table()
        self._on_changed()
