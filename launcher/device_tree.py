"""Left-side device tree: board groups → devices → additional-package
category rows → package rows, plus the bulk-action buttons under it."""
from __future__ import annotations

import re
import tkinter as tk
from dataclasses import dataclass
from tkinter import ttk
from typing import Callable, Dict, List, Optional

from device_state import DeviceBundle, PackageStatus
from supported_devices import (board_soc_cpu, board_sort_key,
                               board_support_state, sort_text)
from ui_theme import BG_LIGHTER, FG, STATE_TINT


# Board-group / package row iids contain ':' which is invalid in Windows
# directory names, so they can never collide with a bundle directory name
# used as a device iid.
GROUP_IID_PREFIX    = "board-group::"
CATEGORY_IID_PREFIX = "pkgcat::"
PACKAGE_IID_PREFIX  = "pkg::"
OSNOTE_IID_PREFIX   = "osnote::"
UNKNOWN_BOARD_LABEL = "Unknown board"


@dataclass
class TreeSelection:
    kind: str  # "none" | "group" | "device" | "category" | "package"
    device: Optional[DeviceBundle] = None
    package: Optional[PackageStatus] = None


def _sort_optional_text(value: object) -> tuple[bool, str]:
    text = sort_text(value)
    return (not bool(text), text)


def _sort_optional_int(value: object, *, missing_when_zero: bool = True) -> tuple[bool, int]:
    number = value if isinstance(value, int) and not isinstance(value, bool) else 0
    missing = number == 0 if missing_when_zero else False
    return (missing, number)


def _board_group_key(d: DeviceBundle) -> tuple[int, str]:
    return board_sort_key(d.meta.board_name)


def _device_sort_key(d: DeviceBundle) -> tuple:
    meta = d.meta
    version_missing = not (meta.os_ver_major or meta.os_ver_minor)
    return (
        sort_text(meta.device_name or d.name),
        _sort_optional_text(meta.os_name),
        _sort_optional_int(meta.os_ver_major, missing_when_zero=version_missing),
        _sort_optional_int(meta.os_ver_minor, missing_when_zero=version_missing),
        _sort_optional_int(meta.device_year),
        sort_text(d.name),
    )


def _int_metadata(value: object) -> int:
    return value if isinstance(value, int) and not isinstance(value, bool) else 0


def _os_name_has_version(name: str, major: int, minor: int) -> bool:
    if not name or not (major or minor):
        return False

    version_pattern = (
        rf"(?<![\d.]){major}\.{minor}(?:\.\d+)*(?!\d)"
    )
    if re.search(version_pattern, name):
        return True

    if minor == 0:
        major_pattern = rf"(?<![\d.]){major}(?![\d.])"
        return bool(re.search(major_pattern, name))

    return False


def _table_os_label(d: DeviceBundle) -> str:
    meta = d.meta
    name = meta.os_name.strip() if isinstance(meta.os_name, str) else ""
    major = _int_metadata(meta.os_ver_major)
    minor = _int_metadata(meta.os_ver_minor)
    year = _int_metadata(meta.os_year)
    # Parenthetical gathers CE version then OS year: "(CE 4.2, 2003)",
    # "(CE 4.2)", or "(2003)". The CE version is dropped when the name
    # already spells it out (e.g. "Windows CE 4.2").
    paren: List[str] = []
    if (major or minor) and not _os_name_has_version(name, major, minor):
        paren.append(f"CE {major}.{minor}")
    if year:
        paren.append(str(year))
    base = name or ("Unknown OS" if paren else "")
    return f"{base} ({', '.join(paren)})" if paren else base


def _table_device_label(d: DeviceBundle) -> str:
    display = d.meta.device_name or d.name
    return f"{display} ({d.meta.device_year})" if d.meta.device_year else display


def _device_search_haystack(d: DeviceBundle) -> str:
    parts: List[str] = [
        _table_device_label(d),
        _table_os_label(d),
        d.meta.board_name or "",
        d.meta.soc_family or "",
        d.state_label,
        d.name,
    ]
    parts.extend(d.meta.os_notes or [])
    parts.extend(ps.remote.name for ps in d.packages)
    return "\n".join(parts).lower()


class DeviceTreePanel:
    def __init__(self, parent: ttk.Frame,
                 on_select: Callable[[TreeSelection], None],
                 on_activate: Callable[[TreeSelection], None],
                 on_refresh: Callable[[], None],
                 on_update_all: Callable[[], None]):
        self._on_select = on_select
        self._on_activate = on_activate
        self.devices: List[DeviceBundle] = []
        self._payload: Dict[str, TreeSelection] = {}

        frame = ttk.Frame(parent)
        frame.grid(row=0, column=0, sticky="nsew", padx=(0, 8))
        frame.rowconfigure(1, weight=1)
        frame.columnconfigure(0, weight=1)
        self.frame = frame

        filter_bar = ttk.Frame(frame)
        filter_bar.grid(row=0, column=0, columnspan=2, sticky="ew", pady=(0, 6))
        self.var_filter_mode = tk.StringVar(value="hide_unsupported")
        self.var_hide_no_metadata = tk.BooleanVar(value=True)
        ttk.Radiobutton(filter_bar, text="Hide unsupported",
                        variable=self.var_filter_mode,
                        value="hide_unsupported",
                        command=self._filter_mode_changed).pack(side="left")
        self.chk_hide_no_metadata = ttk.Checkbutton(
            filter_bar, text="Hide without metadata",
            variable=self.var_hide_no_metadata,
            command=self._refill)
        self.chk_hide_no_metadata.pack(side="left", padx=(8, 0))
        ttk.Radiobutton(filter_bar, text="Show all",
                        variable=self.var_filter_mode,
                        value="show_all",
                        command=self._filter_mode_changed).pack(side="left",
                                                                padx=(8, 0))
        self.var_search = tk.StringVar(value="")
        search_entry = ttk.Entry(filter_bar, textvariable=self.var_search,
                                 width=22)
        search_entry.pack(side="right")
        ttk.Label(filter_bar, text="Search:").pack(side="right", padx=(0, 4))
        self.var_search.trace_add("write", lambda *_: self._refill())

        columns = ("os", "board", "soc", "status")
        tree = ttk.Treeview(frame, columns=columns, show="tree headings",
                            selectmode="browse")
        tree.heading("#0", text="Device")
        tree.heading("os", text="OS")
        tree.heading("board", text="Board")
        tree.heading("soc", text="SoC")
        tree.heading("status", text="Status")
        tree.column("#0", width=220, minwidth=140, anchor="w", stretch=True)
        tree.column("os", width=260, minwidth=180, anchor="w", stretch=True)
        tree.column("board", width=140, minwidth=95, anchor="w", stretch=True)
        tree.column("soc", width=105, minwidth=80, anchor="w", stretch=True)
        tree.column("status", width=100, minwidth=90, anchor="w", stretch=False)
        tree.grid(row=1, column=0, sticky="nsew")
        vsb = ttk.Scrollbar(frame, orient="vertical", command=tree.yview)
        vsb.grid(row=1, column=1, sticky="ns")
        tree.configure(yscrollcommand=vsb.set)
        tree.bind("<<TreeviewSelect>>", self._handle_select)
        tree.bind("<Double-1>", self._handle_double_click)
        for state, tint in STATE_TINT.items():
            tree.tag_configure(state, background=tint, foreground=FG)
        tree.tag_configure("group", background=BG_LIGHTER, foreground=FG)
        self.tree = tree

        bottom = ttk.Frame(frame)
        bottom.grid(row=2, column=0, columnspan=2, sticky="ew", pady=(8, 0))
        bottom.columnconfigure(0, weight=1)
        bottom.columnconfigure(1, weight=1)
        self.btn_refresh = ttk.Button(bottom, text="Refresh manifest",
                                      command=on_refresh)
        self.btn_refresh.grid(row=0, column=0, sticky="ew", padx=(0, 4))
        self.btn_update_all = ttk.Button(bottom, text="Update all",
                                         command=on_update_all)
        self.btn_update_all.grid(row=0, column=1, sticky="ew", padx=(4, 0))

    def _filter_mode_changed(self) -> None:
        strict_available = self.var_filter_mode.get() == "hide_unsupported"
        self.chk_hide_no_metadata.config(
            state="normal" if strict_available else "disabled")
        self._refill()

    def set_busy(self, busy: bool) -> None:
        state = "disabled" if busy else "normal"
        self.btn_refresh.config(state=state)
        self.btn_update_all.config(state=state)

    def selection(self) -> TreeSelection:
        sel = self.tree.selection()
        if not sel:
            return TreeSelection(kind="none")
        return self._payload.get(sel[0], TreeSelection(kind="none"))

    def reload(self, devices: List[DeviceBundle]) -> None:
        self.devices = sorted(devices,
                              key=lambda d: (_board_group_key(d), _device_sort_key(d)))
        self._refill()

    def _refill(self) -> None:
        tree = self.tree
        previous = tree.selection()
        previous_iid = previous[0] if previous else None
        # The user's expand/collapse toggles survive a reload; rows seen for
        # the first time fall back to the defaults below.
        prior_open: Dict[str, bool] = {
            iid: bool(tree.item(iid, "open"))
            for iid, payload in self._payload.items()
            if payload.kind in ("device", "category") and tree.exists(iid)}

        tree.delete(*tree.get_children())
        self._payload.clear()
        hide = self.var_filter_mode.get() == "hide_unsupported"
        hide_no_metadata = hide and self.var_hide_no_metadata.get()
        query = self.var_search.get().strip().lower()
        group_iids: Dict[str, str] = {}
        device_iids: List[str] = []
        for d in self.devices:
            # "Hide unsupported" drops explicit supported:False boards; with
            # "Hide without metadata" also checked, unknown boards (no
            # board_name, or not in supported_devices.py -> state None) are
            # dropped too, leaving only supported:True.
            if hide:
                state_flag = board_support_state(d.meta.board_name,
                                                 d.meta.board_prev_names)
                if state_flag is False:
                    continue
                if hide_no_metadata and state_flag is None:
                    continue
            if query and query not in _device_search_haystack(d):
                continue
            board = d.meta.board_name or ""
            group_iid = group_iids.get(board)
            if group_iid is None:
                group_iid = GROUP_IID_PREFIX + board
                tree.insert("", "end", iid=group_iid,
                            text=board or UNKNOWN_BOARD_LABEL,
                            open=True, tags=("group",))
                self._payload[group_iid] = TreeSelection(kind="group")
                group_iids[board] = group_iid
            state = d.state_label
            os_label = _table_os_label(d)
            soc = d.meta.soc_family or ""
            cpu = board_soc_cpu(d.meta.board_name, d.meta.board_prev_names)
            if soc and cpu:
                soc = f"{soc} ({cpu})"
            tree.insert(group_iid, "end", iid=d.name,
                        text=_table_device_label(d),
                        values=(os_label, board, soc, state),
                        open=prior_open.get(d.name,
                                            bool(d.packages) or bool(d.meta.os_notes)),
                        tags=(state,))
            self._payload[d.name] = TreeSelection(kind="device", device=d)
            device_iids.append(d.name)
            self._insert_os_note_rows(d)
            self._insert_package_rows(d, prior_open)

        if previous_iid and tree.exists(previous_iid):
            tree.selection_set(previous_iid)
            tree.see(previous_iid)
        elif device_iids:
            tree.selection_set(device_iids[0])
            tree.see(device_iids[0])

    def _insert_os_note_rows(self, d: DeviceBundle) -> None:
        # meta.os.notes as indented child rows in the OS column, each led by
        # the "↳" branch arrow pointing down-then-right from the OS name: <= 3
        # notes collapse into one comma-separated row; > 3 get one row each.
        # Selecting a note row resolves to the owning device.
        notes = d.meta.os_notes
        if not notes:
            return
        if len(notes) <= 3:
            rows = [("all", "↳ " + ", ".join(notes))]
        else:
            rows = [(str(i), f"↳ {note}") for i, note in enumerate(notes)]
        for suffix, text in rows:
            note_iid = f"{OSNOTE_IID_PREFIX}{d.name}::{suffix}"
            self.tree.insert(d.name, "end", iid=note_iid, text="",
                             values=(text, "", "", ""),
                             tags=("osnote",))
            self._payload[note_iid] = TreeSelection(kind="device", device=d)

    def _insert_package_rows(self, d: DeviceBundle,
                             prior_open: Dict[str, bool]) -> None:
        categories: Dict[str, str] = {}
        for ps in d.packages:
            cat_iid = categories.get(ps.remote.category)
            if cat_iid is None:
                cat_iid = f"{CATEGORY_IID_PREFIX}{d.name}::{ps.remote.category}"
                # Developer-only PDBs stay collapsed; everything else a user
                # actually plays with (CF cards, ...) starts expanded.
                default_open = ps.remote.category != "pdbs"
                self.tree.insert(d.name, "end", iid=cat_iid,
                                 text=ps.category_label,
                                 open=prior_open.get(cat_iid, default_open),
                                 tags=("group",))
                self._payload[cat_iid] = TreeSelection(kind="category", device=d)
                categories[ps.remote.category] = cat_iid
            state = ps.state_label
            pkg_iid = (f"{PACKAGE_IID_PREFIX}{d.name}::"
                       f"{ps.remote.category}::{ps.remote.key}")
            self.tree.insert(cat_iid, "end", iid=pkg_iid,
                             text=ps.remote.name,
                             values=("", "", "", state),
                             tags=(state,))
            self._payload[pkg_iid] = TreeSelection(kind="package", device=d,
                                                   package=ps)

    def _handle_select(self, _event: object) -> None:
        self._on_select(self.selection())

    def _handle_double_click(self, _event: object) -> None:
        self._on_activate(self.selection())
