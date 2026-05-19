#!/usr/bin/env python3
from __future__ import annotations

import argparse
import ctypes
import os
import queue
import subprocess
import sys
import threading
import tkinter as tk
import traceback
from concurrent.futures import Future
from pathlib import Path
from tkinter import ttk
from typing import Callable, Dict, List, Optional

_THIS_DIR = Path(__file__).resolve().parent
if str(_THIS_DIR) not in sys.path:
    sys.path.insert(0, str(_THIS_DIR))

from bundles import (
    BundleError,
    DeviceBundle,
    is_safe_bundle_name,
)
from operations import BundleManager, CancelledError


def _exe_dir() -> Path:
    if getattr(sys, "frozen", False):
        return Path(sys.executable).resolve().parent
    return Path(__file__).resolve().parent


def _resolve_devices_dir() -> Path:
    exe_dir = _exe_dir()
    sibling = exe_dir / "devices"
    if sibling.is_dir():
        return sibling
    current = exe_dir
    for _ in range(6):
        candidate = current / "bundled" / "devices"
        if candidate.is_dir():
            return candidate
        if current.parent == current:
            break
        current = current.parent
    return sibling


def _resolve_cerf_exe() -> Optional[Path]:
    exe_dir = _exe_dir()
    candidate = exe_dir / "cerf.exe"
    if candidate.is_file():
        return candidate
    return None


def _resolve_icon() -> Optional[Path]:
    meipass = getattr(sys, "_MEIPASS", None)
    if meipass:
        candidate = Path(meipass) / "cerf.ico"
        if candidate.is_file():
            return candidate
    exe_dir = _exe_dir()
    candidate = exe_dir / "cerf.ico"
    if candidate.is_file():
        return candidate
    repo_candidate = exe_dir / ".." / "cerf" / "cerf.ico"
    if repo_candidate.is_file():
        return repo_candidate.resolve()
    return None


STATE_INSTALLED = "Installed"
STATE_UPDATE    = "Update available"
STATE_AVAILABLE = "Available"
STATE_USER      = "User device"

BG          = "#1e1e1e"
BG_LIGHTER  = "#252526"
BG_FIELD    = "#2d2d30"
BG_HOVER    = "#3c3c3c"
BG_SELECTED = "#094771"
FG          = "#e0e0e0"
FG_DIM      = "#808080"
BORDER      = "#3f3f46"

STATE_TINT = {
    STATE_INSTALLED: "#1e3a1e",
    STATE_UPDATE:    "#3a2f12",
    STATE_AVAILABLE: BG_FIELD,
    STATE_USER:      "#3a1e3a",
}


def _enable_dpi_awareness() -> None:
    if sys.platform != "win32":
        return
    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(2)
        return
    except (OSError, AttributeError):
        pass
    try:
        ctypes.windll.user32.SetProcessDPIAware()
    except (OSError, AttributeError):
        pass


def _enable_dark_titlebar(window: tk.Misc) -> None:
    if sys.platform != "win32":
        return
    try:
        window.update_idletasks()
        hwnd = ctypes.windll.user32.GetParent(window.winfo_id())
        if hwnd == 0:
            hwnd = window.winfo_id()
        value = ctypes.c_int(1)
        ctypes.windll.dwmapi.DwmSetWindowAttribute(
            hwnd, 20, ctypes.byref(value), ctypes.sizeof(value))
    except (OSError, AttributeError):
        pass


class LauncherApp(tk.Tk):
    def __init__(self, manager: BundleManager, cerf_exe: Optional[Path]):
        super().__init__()
        self.manager = manager
        self.cerf_exe = cerf_exe
        self.title("CERF Launcher")

        try:
            dpi = float(self.winfo_fpixels("1i"))
            self.tk.call("tk", "scaling", dpi / 72.0)
        except tk.TclError:
            dpi = 96.0

        scale = max(1.0, dpi / 96.0)
        self.geometry(f"{int(980 * scale)}x{int(620 * scale)}")
        self.minsize(int(860 * scale), int(520 * scale))

        icon = _resolve_icon()
        if icon is not None:
            try:
                self.iconbitmap(str(icon))
            except tk.TclError:
                pass

        self._apply_dark_theme()

        self.progress_queue: "queue.Queue[tuple[str, int, Optional[int]]]" = queue.Queue()
        self.cancel_event = threading.Event()
        self.busy = False
        self.devices: List[DeviceBundle] = []
        self.selected_name: Optional[str] = None

        self._build_ui()
        _enable_dark_titlebar(self)
        self._pump_progress()
        self.after(50, self._refresh_manifest)

        self.protocol("WM_DELETE_WINDOW", self._on_close)

    def _apply_dark_theme(self) -> None:
        self.configure(bg=BG)
        style = ttk.Style(self)
        style.theme_use("clam")

        style.configure(".",
                        background=BG, foreground=FG,
                        fieldbackground=BG_FIELD, bordercolor=BORDER,
                        lightcolor=BG, darkcolor=BG,
                        troughcolor=BG_FIELD,
                        selectbackground=BG_SELECTED, selectforeground=FG,
                        insertcolor=FG, focuscolor=BG_SELECTED)

        style.configure("TFrame",       background=BG)
        style.configure("TLabel",       background=BG, foreground=FG)
        style.configure("TSeparator",   background=BORDER)
        style.configure("TLabelframe",  background=BG, foreground=FG,
                                       bordercolor=BORDER)
        style.configure("TLabelframe.Label", background=BG, foreground=FG)

        style.configure("TButton",
                        background=BG_FIELD, foreground=FG,
                        bordercolor=BORDER, padding=4, borderwidth=1)
        style.map("TButton",
                  background=[("pressed", BG_SELECTED),
                              ("active",  BG_HOVER),
                              ("disabled", BG)],
                  foreground=[("disabled", FG_DIM)],
                  bordercolor=[("focus", BG_SELECTED)])

        style.configure("TCheckbutton",
                        background=BG, foreground=FG,
                        focuscolor=BG, indicatorcolor=BG_FIELD)
        style.map("TCheckbutton",
                  background=[("active", BG)],
                  foreground=[("disabled", FG_DIM)],
                  indicatorcolor=[("selected", BG_SELECTED),
                                  ("active",   BG_HOVER)])

        style.configure("TEntry",
                        fieldbackground=BG_FIELD, foreground=FG,
                        bordercolor=BORDER, insertcolor=FG)

        style.configure("TScrollbar",
                        background=BG_FIELD, troughcolor=BG,
                        bordercolor=BORDER, arrowcolor=FG)
        style.map("TScrollbar", background=[("active", BG_HOVER)])

        style.configure("Horizontal.TProgressbar",
                        background=BG_SELECTED, troughcolor=BG_FIELD,
                        bordercolor=BORDER, lightcolor=BG_SELECTED,
                        darkcolor=BG_SELECTED)

        style.configure("Treeview",
                        background=BG_FIELD, foreground=FG,
                        fieldbackground=BG_FIELD, bordercolor=BORDER,
                        rowheight=24)
        style.map("Treeview",
                  background=[("selected", BG_SELECTED)],
                  foreground=[("selected", FG)])
        style.configure("Treeview.Heading",
                        background=BG_LIGHTER, foreground=FG,
                        bordercolor=BORDER, relief="flat")
        style.map("Treeview.Heading",
                  background=[("active", BG_HOVER)])

    def _build_ui(self) -> None:
        outer = ttk.Frame(self, padding=8)
        outer.pack(fill="both", expand=True)
        outer.columnconfigure(0, weight=1, minsize=380)
        outer.columnconfigure(1, weight=2, minsize=420)
        outer.rowconfigure(0, weight=1)

        self._build_left(outer)
        self._build_right(outer)
        self._build_status(self)

    def _build_left(self, parent: ttk.Frame) -> None:
        left = ttk.Frame(parent)
        left.grid(row=0, column=0, sticky="nsew", padx=(0, 8))
        left.rowconfigure(0, weight=1)
        left.columnconfigure(0, weight=1)

        columns = ("status", "year", "os")
        tree = ttk.Treeview(left, columns=columns, show="tree headings", selectmode="browse")
        tree.heading("#0", text="Device")
        tree.heading("status", text="Status")
        tree.heading("year", text="Year")
        tree.heading("os", text="OS")
        tree.column("#0", width=190, anchor="w")
        tree.column("status", width=110, anchor="w")
        tree.column("year", width=50, anchor="center")
        tree.column("os", width=130, anchor="w")
        tree.grid(row=0, column=0, sticky="nsew")
        vsb = ttk.Scrollbar(left, orient="vertical", command=tree.yview)
        vsb.grid(row=0, column=1, sticky="ns")
        tree.configure(yscrollcommand=vsb.set)
        tree.bind("<<TreeviewSelect>>", self._on_select_device)
        tree.bind("<Double-1>", lambda _e: self._launch())
        for state, tint in STATE_TINT.items():
            tree.tag_configure(state, background=tint, foreground=FG)
        self.tree = tree

        bottom = ttk.Frame(left)
        bottom.grid(row=1, column=0, columnspan=2, sticky="ew", pady=(8, 0))
        bottom.columnconfigure(0, weight=1)
        bottom.columnconfigure(1, weight=1)
        bottom.columnconfigure(2, weight=1)
        self.btn_refresh = ttk.Button(bottom, text="Refresh manifest", command=self._refresh_manifest)
        self.btn_refresh.grid(row=0, column=0, sticky="ew", padx=(0, 4))
        self.btn_update_all = ttk.Button(bottom, text="Update all", command=self._update_all)
        self.btn_update_all.grid(row=0, column=1, sticky="ew", padx=4)
        self.btn_pdbs_all = ttk.Button(bottom, text="PDBs for all", command=self._download_pdbs_all)
        self.btn_pdbs_all.grid(row=0, column=2, sticky="ew", padx=(4, 0))

    def _build_right(self, parent: ttk.Frame) -> None:
        right = ttk.Frame(parent)
        right.grid(row=0, column=1, sticky="nsew")
        right.columnconfigure(0, weight=1)
        right.rowconfigure(2, weight=1)

        meta = ttk.LabelFrame(right, text="Device", padding=8)
        meta.grid(row=0, column=0, sticky="ew", pady=(0, 8))
        meta.columnconfigure(1, weight=1)
        self.meta_vars: Dict[str, tk.StringVar] = {}
        rows = [("Display name", "device_name"),
                ("Board",        "board_name"),
                ("SoC family",   "soc_family"),
                ("OS",           "os_version"),
                ("Year",         "device_year"),
                ("State",        "state")]
        for i, (label, key) in enumerate(rows):
            ttk.Label(meta, text=label + ":").grid(row=i, column=0, sticky="w", padx=(0, 8))
            var = tk.StringVar(value="—")
            self.meta_vars[key] = var
            ttk.Label(meta, textvariable=var).grid(row=i, column=1, sticky="w")

        opts = ttk.LabelFrame(right, text="Launch options", padding=8)
        opts.grid(row=1, column=0, sticky="ew", pady=(0, 8))
        opts.columnconfigure(1, weight=1)
        self.var_quiet     = tk.BooleanVar(value=False)
        self.var_flush     = tk.BooleanVar(value=False)
        self.var_flood     = tk.BooleanVar(value=False)
        self.var_no_net    = tk.BooleanVar(value=False)
        ttk.Checkbutton(opts, text="Quiet logs", variable=self.var_quiet).grid(row=0, column=0, columnspan=2, sticky="w")
        ttk.Checkbutton(opts, text="Flush log file after every write (extremely slow; survives crash truncation)",
                        variable=self.var_flush).grid(row=1, column=0, columnspan=2, sticky="w")
        ttk.Checkbutton(opts, text="Allow stdout flood (disables anti-flood throttling)",
                        variable=self.var_flood).grid(row=2, column=0, columnspan=2, sticky="w")
        ttk.Checkbutton(opts, text="Disable network backend (overrides device cerf.json)",
                        variable=self.var_no_net).grid(row=3, column=0, columnspan=2, sticky="w")

        ttk.Separator(opts).grid(row=4, column=0, columnspan=2, sticky="ew", pady=6)

        self.res_note = ttk.Label(opts, text="Resolution override (honoured only by boards that support it):")
        self.res_note.grid(row=5, column=0, columnspan=3, sticky="w")
        self.var_width  = tk.IntVar(value=240)
        self.var_height = tk.IntVar(value=320)
        self.width_label  = ttk.Label(opts, text="240 px")
        self.height_label = ttk.Label(opts, text="320 px")
        scale_opts = dict(
            from_=160, to=1024, orient="horizontal", showvalue=False,
            bg=BG, fg=FG, troughcolor=BG_FIELD,
            highlightthickness=0, highlightbackground=BG,
            activebackground=BG_SELECTED, sliderrelief="flat",
            borderwidth=0,
        )
        self.scale_width = tk.Scale(opts, variable=self.var_width, **scale_opts,
            command=lambda v: self.width_label.config(text=f"{int(float(v))} px"))
        self.scale_height = tk.Scale(opts, variable=self.var_height, **scale_opts,
            command=lambda v: self.height_label.config(text=f"{int(float(v))} px"))
        ttk.Label(opts, text="Width").grid(row=6, column=0, sticky="w")
        self.scale_width.grid(row=6, column=1, sticky="ew")
        self.width_label.grid(row=6, column=2, sticky="w", padx=(4, 0))
        ttk.Label(opts, text="Height").grid(row=7, column=0, sticky="w")
        self.scale_height.grid(row=7, column=1, sticky="ew")
        self.height_label.grid(row=7, column=2, sticky="w", padx=(4, 0))

        actions = ttk.LabelFrame(right, text="Bundle actions", padding=8)
        actions.grid(row=2, column=0, sticky="nsew", pady=(0, 8))
        actions.columnconfigure(0, weight=1)
        actions.columnconfigure(1, weight=1)
        actions.columnconfigure(2, weight=1)
        self.btn_download = ttk.Button(actions, text="Download", command=self._download_selected)
        self.btn_update   = ttk.Button(actions, text="Update",   command=self._update_selected)
        self.btn_delete   = ttk.Button(actions, text="Delete",   command=self._delete_selected)
        self.btn_pdbs     = ttk.Button(actions, text="Download PDBs", command=self._pdbs_selected)
        self.btn_download.grid(row=0, column=0, sticky="ew", padx=(0, 4))
        self.btn_update.grid  (row=0, column=1, sticky="ew", padx=4)
        self.btn_delete.grid  (row=0, column=2, sticky="ew", padx=(4, 0))
        self.btn_pdbs.grid    (row=1, column=0, columnspan=3, sticky="ew", pady=(4, 0))

        launch_bar = ttk.Frame(right)
        launch_bar.grid(row=3, column=0, sticky="e")
        self.btn_launch = ttk.Button(launch_bar, text="Launch CERF", command=self._launch)
        self.btn_launch.grid(row=0, column=0, padx=(0, 0))

    def _build_status(self, root: tk.Misc) -> None:
        bar = ttk.Frame(root, padding=(8, 4))
        bar.pack(fill="x", side="bottom")
        bar.columnconfigure(0, weight=1)
        self.status_var = tk.StringVar(value="Ready.")
        ttk.Label(bar, textvariable=self.status_var, anchor="w").grid(row=0, column=0, sticky="ew")
        self.progress = ttk.Progressbar(bar, orient="horizontal", length=220, mode="determinate")
        self.progress.grid(row=0, column=1, sticky="e")

    def _dialog(self, title: str, message: str,
                buttons: tuple[str, ...] = ("OK",),
                default: Optional[str] = None) -> str:
        dlg = tk.Toplevel(self)
        dlg.title(title)
        dlg.configure(bg=BG)
        dlg.transient(self)
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
        _enable_dark_titlebar(dlg)
        w, h = dlg.winfo_reqwidth(), dlg.winfo_reqheight()
        x = self.winfo_rootx() + (self.winfo_width()  - w) // 2
        y = self.winfo_rooty() + (self.winfo_height() - h) // 2
        dlg.geometry(f"+{max(0, x)}+{max(0, y)}")

        dlg.grab_set()
        self.wait_window(dlg)
        return result["value"]

    def _info(self, title: str, message: str) -> None:
        self._dialog(title, message)

    def _error(self, title: str, message: str) -> None:
        self._dialog(title, message)

    def _yesno(self, title: str, message: str) -> bool:
        return self._dialog(title, message, ("Yes", "No"), default="No") == "Yes"

    def _set_busy(self, busy: bool, label: str = "") -> None:
        self.busy = busy
        state = "disabled" if busy else "normal"
        for b in (self.btn_refresh, self.btn_update_all, self.btn_pdbs_all,
                  self.btn_download, self.btn_update, self.btn_delete,
                  self.btn_pdbs, self.btn_launch):
            b.config(state=state)
        if busy:
            self.status_var.set(label or "Working…")
        else:
            self.status_var.set("Ready.")
            self.progress.config(value=0, mode="determinate")
        self._refresh_selection_state()

    def _await_future(self, future: Future, done: Callable[[Optional[BaseException]], None]) -> None:
        def poll() -> None:
            if future.done():
                exc = future.exception()
                done(exc)
            else:
                self.after(50, poll)
        self.after(50, poll)

    def _progress_cb(self, label: str, done: int, total: Optional[int]) -> None:
        self.progress_queue.put((label, done, total))

    def _pump_progress(self) -> None:
        try:
            while True:
                label, done, total = self.progress_queue.get_nowait()
                self.status_var.set(label)
                if total:
                    self.progress.config(mode="determinate", maximum=total, value=done)
                else:
                    if str(self.progress.cget("mode")) != "indeterminate":
                        self.progress.config(mode="indeterminate")
                        self.progress.start(80)
        except queue.Empty:
            pass
        self.after(50, self._pump_progress)

    def _refresh_manifest(self) -> None:
        if self.busy:
            return
        self._set_busy(True, "Fetching manifest…")
        future = self.manager.submit_refresh()
        def done(exc: Optional[BaseException]) -> None:
            self._set_busy(False)
            if exc is not None:
                self._error(
                    "Remote manifest unavailable",
                    f"{exc}\n\n"
                    f"Local devices remain available to launch. Download / "
                    f"update / PDB fetch require a reachable remote manifest "
                    f"in a supported version — try again later, check your "
                    f"network, or update the launcher if the manifest schema "
                    f"has moved on."
                )
            self._reload_device_list()
        self._await_future(future, done)

    def _reload_device_list(self) -> None:
        previous = self.selected_name
        self.devices = self.manager.list_devices()
        self.tree.delete(*self.tree.get_children())
        for d in self.devices:
            state = self._state_label(d)
            year = str(d.meta.device_year) if d.meta.device_year else ""
            os_label = d.meta.os_version or ""
            display = d.meta.device_name or d.name
            self.tree.insert("", "end", iid=d.name,
                             text=display if display != d.name else d.name,
                             values=(state, year, os_label),
                             tags=(state,))
        if previous and previous in (d.name for d in self.devices):
            self.tree.selection_set(previous)
            self.tree.see(previous)
        elif self.devices:
            self.tree.selection_set(self.devices[0].name)
            self.tree.see(self.devices[0].name)

    def _state_label(self, d: DeviceBundle) -> str:
        if d.is_user_device:
            return STATE_USER
        if d.has_update:
            return STATE_UPDATE
        if d.is_installed:
            return STATE_INSTALLED
        return STATE_AVAILABLE

    def _on_select_device(self, _event: object) -> None:
        sel = self.tree.selection()
        if not sel:
            return
        self.selected_name = sel[0]
        device = self._selected_device()
        if device is None:
            return
        self.meta_vars["device_name"].set(device.meta.device_name or device.name)
        self.meta_vars["board_name"] .set(device.meta.board_name or "—")
        self.meta_vars["soc_family"] .set(device.meta.soc_family or "—")
        self.meta_vars["os_version"] .set(device.meta.os_version or "—")
        self.meta_vars["device_year"].set(str(device.meta.device_year) if device.meta.device_year else "—")
        self.meta_vars["state"].set(self._state_label(device))
        if device.screen_supported is False:
            self.res_note.config(text="This ROM does not support screen resolution changes.")
            self.scale_width.config(state="disabled")
            self.scale_height.config(state="disabled")
            self.width_label.config(text="—")
            self.height_label.config(text="—")
        else:
            self.res_note.config(text="Resolution override (honoured only by boards that support it):")
            self.scale_width.config(state="normal")
            self.scale_height.config(state="normal")
            if device.default_screen_width:
                self.var_width.set(device.default_screen_width)
            if device.default_screen_height:
                self.var_height.set(device.default_screen_height)
            self.width_label.config(text=f"{self.var_width.get()} px")
            self.height_label.config(text=f"{self.var_height.get()} px")
        self._refresh_selection_state()

    def _selected_device(self) -> Optional[DeviceBundle]:
        if not self.selected_name:
            return None
        for d in self.devices:
            if d.name == self.selected_name:
                return d
        return None

    def _refresh_selection_state(self) -> None:
        d = self._selected_device()
        if d is None:
            for b in (self.btn_download, self.btn_update, self.btn_delete, self.btn_pdbs, self.btn_launch):
                b.config(state="disabled")
            return
        if self.busy:
            return
        self.btn_download.config(state=("normal" if (not d.is_installed and d.remote) else "disabled"))
        self.btn_update  .config(state=("normal" if d.has_update else "disabled"))
        self.btn_delete  .config(state=("normal" if d.is_installed else "disabled"))
        pdbs_available = bool(d.remote and d.remote.pdbs_url and not d.has_pdbs and d.is_installed)
        self.btn_pdbs    .config(state=("normal" if pdbs_available else "disabled"))
        self.btn_launch  .config(state=("normal" if (d.is_installed and self.cerf_exe) else "disabled"))

    def _download_selected(self) -> None:
        d = self._selected_device()
        if d is None or self.busy or d.remote is None:
            return
        self._set_busy(True, f"Downloading {d.name}…")
        f = self.manager.submit_install(d.name, with_pdbs=False, progress=self._progress_cb,
                                        cancel_event=self.cancel_event)
        self._await_future(f, lambda exc: self._after_op(exc, f"Downloaded {d.name}"))

    def _update_selected(self) -> None:
        self._download_selected()

    def _delete_selected(self) -> None:
        d = self._selected_device()
        if d is None or self.busy:
            return
        if not self._yesno("Delete device",
                           f"Remove devices/{d.name}/ and its files?\nThis cannot be undone."):
            return
        self._set_busy(True, f"Deleting {d.name}…")
        f = self.manager.submit_delete(d.name)
        self._await_future(f, lambda exc: self._after_op(exc, f"Deleted {d.name}"))

    def _pdbs_selected(self) -> None:
        d = self._selected_device()
        if d is None or self.busy or d.remote is None or not d.remote.pdbs_url:
            return
        self._set_busy(True, f"Downloading PDBs for {d.name}…")
        f = self.manager.submit_install_pdbs(d.name, self._progress_cb, self.cancel_event)
        self._await_future(f, lambda exc: self._after_op(exc, f"PDBs installed for {d.name}"))

    def _update_all(self) -> None:
        if self.busy:
            return
        targets = [d for d in self.devices if d.has_update]
        if not targets:
            self._info("Update all", "All installed bundles are up to date.")
            return
        self._set_busy(True, f"Updating {len(targets)} bundle(s)…")
        names = [d.name for d in targets]
        self._run_sequence(names, "install", "Updated all")

    def _download_pdbs_all(self) -> None:
        if self.busy:
            return
        targets = [d for d in self.devices
                   if d.is_installed and d.remote and d.remote.pdbs_url and not d.has_pdbs]
        if not targets:
            self._info("Download PDBs", "No installed bundles are missing PDBs.")
            return
        self._set_busy(True, f"Downloading PDBs for {len(targets)} bundle(s)…")
        names = [d.name for d in targets]
        self._run_sequence(names, "pdbs", "PDBs installed")

    def _run_sequence(self, names: List[str], op: str, done_label: str) -> None:
        errors: List[tuple[str, BaseException]] = []
        def step(idx: int) -> None:
            if idx >= len(names):
                self._set_busy(False)
                if errors:
                    summary = "\n".join(f"{n}: {e}" for n, e in errors)
                    self._error("Sequence completed with errors", summary)
                self._reload_device_list()
                return
            name = names[idx]
            self.status_var.set(f"[{idx+1}/{len(names)}] {name}…")
            if op == "install":
                f = self.manager.submit_install(name, with_pdbs=False,
                                                progress=self._progress_cb,
                                                cancel_event=self.cancel_event)
            else:
                f = self.manager.submit_install_pdbs(name, self._progress_cb,
                                                    self.cancel_event)
            def finished(exc: Optional[BaseException]) -> None:
                if exc is not None and not isinstance(exc, CancelledError):
                    errors.append((name, exc))
                step(idx + 1)
            self._await_future(f, finished)
        step(0)

    def _after_op(self, exc: Optional[BaseException], success_msg: str) -> None:
        self._set_busy(False)
        if exc is not None:
            if isinstance(exc, CancelledError):
                self.status_var.set("Cancelled.")
            else:
                self._error("Operation failed", str(exc))
                self.status_var.set("Error.")
        else:
            self.status_var.set(success_msg)
        self._reload_device_list()

    def _launch(self) -> None:
        d = self._selected_device()
        if d is None or self.busy:
            return
        if not d.is_installed:
            self._error("Cannot launch", f"{d.name} is not installed.")
            return
        if self.cerf_exe is None:
            self._error("Cannot launch",
                        "cerf.exe not found next to launcher.exe.")
            return
        argv: List[str] = [str(self.cerf_exe), f"--device={d.name}"]
        if self.var_quiet.get():  argv.append("--quiet")
        if self.var_flush.get():  argv.append("--flush-outputs")
        if self.var_flood.get():  argv.append("--allow-flood")
        if self.var_no_net.get(): argv.append("--disable-network")
        if d.screen_supported is not False:
            w = int(self.var_width.get())
            h = int(self.var_height.get())
            argv += [f"--screen-width={w}", f"--screen-height={h}"]
        try:
            subprocess.Popen(argv, cwd=str(self.cerf_exe.parent),
                             creationflags=getattr(subprocess, "DETACHED_PROCESS", 0))
            self.status_var.set(f"Launched cerf.exe for {d.name}.")
        except OSError as exc:
            self._error("Launch failed", str(exc))

    def _on_close(self) -> None:
        self.cancel_event.set()
        self.manager.shutdown()
        self.destroy()


def _cli_progress(label: str, done: int, total: Optional[int]) -> None:
    if total:
        pct = int(done * 100 / total)
        print(f"\r{label}: {pct}%  ({done:,} / {total:,})", end="", flush=True)
    else:
        print(f"\r{label}: {done:,} bytes", end="", flush=True)


def _cli_run(devices_dir: Path, argv: List[str]) -> int:
    parser = argparse.ArgumentParser(prog="launcher.exe sync")
    parser.add_argument("command", choices=(
        "list", "download", "update", "delete",
        "update-all", "download-pdbs", "download-pdbs-all"))
    parser.add_argument("bundle", nargs="?")
    args = parser.parse_args(argv)

    manager = BundleManager(devices_dir)
    try:
        manager.submit_refresh().result()
    except BundleError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1

    cancel = threading.Event()
    try:
        if args.command == "list":
            for d in manager.list_devices():
                state = "installed"
                if d.has_update:    state = "update available"
                elif not d.is_installed: state = "available"
                elif d.is_user_device:   state = "user device"
                line = f"{d.name:32} {state:18}"
                if d.remote:
                    line += f" {d.remote.updated_at}"
                print(line)
            return 0
        if args.command == "update-all":
            for d in manager.list_devices():
                if d.has_update:
                    print(f"Updating {d.name}...")
                    manager.submit_install(d.name, False, _cli_progress, cancel).result()
                    print()
            return 0
        if args.command == "download-pdbs-all":
            for d in manager.list_devices():
                if d.is_installed and d.remote and d.remote.pdbs_url and not d.has_pdbs:
                    print(f"PDBs {d.name}...")
                    manager.submit_install_pdbs(d.name, _cli_progress, cancel).result()
                    print()
            return 0
        if not args.bundle:
            print(f"ERROR: {args.command} requires a bundle name", file=sys.stderr)
            return 1
        if args.command in ("download", "update"):
            manager.submit_install(args.bundle, False, _cli_progress, cancel).result()
        elif args.command == "delete":
            manager.submit_delete(args.bundle).result()
        elif args.command == "download-pdbs":
            manager.submit_install_pdbs(args.bundle, _cli_progress, cancel).result()
        print()
        return 0
    except BundleError as e:
        print(f"\nERROR: {e}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        cancel.set()
        print("\nCancelled.", file=sys.stderr)
        return 130
    finally:
        manager.shutdown()


def main(argv: List[str]) -> int:
    devices_dir = _resolve_devices_dir()
    if not devices_dir.exists():
        try:
            devices_dir.mkdir(parents=True, exist_ok=True)
        except OSError as exc:
            print(f"ERROR: cannot create {devices_dir}: {exc}", file=sys.stderr)
            return 1

    if argv and argv[0] == "sync":
        return _cli_run(devices_dir, argv[1:])

    _enable_dpi_awareness()

    manager = BundleManager(devices_dir)
    cerf_exe = _resolve_cerf_exe()
    app = LauncherApp(manager, cerf_exe)
    try:
        app.mainloop()
    except Exception:
        traceback.print_exc()
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
