"""Right-side info panels: device metadata, feature icons, description and
notes - plus the additional-package details view shown when a package row
is selected in the device tree."""
from __future__ import annotations

import tkinter as tk
import webbrowser
from datetime import datetime
from pathlib import Path
from tkinter import ttk
from typing import Callable, Dict, List, Optional

from device_state import (
    STATE_IMAGE_FILENAME,
    DeviceBundle,
    PackageStatus,
    format_size,
    saved_state_info,
)
from supported_devices import (
    FEATURE_SPECS,
    board_extra_notes,
    board_features,
    dynamic_extra_notes,
)
from ui_dialogs import ask_yesno, bind_tooltip, show_error
from ui_theme import FG, LINK_FG


class DetailsPanel:
    def __init__(self, inner: ttk.Frame, icons_dir: Optional[Path],
                 devices_dir: Path,
                 bind_wheel: Callable[[tk.Misc], None],
                 on_state_changed: Optional[Callable[[], None]] = None):
        self._inner = inner
        self._icons_dir = icons_dir
        self._devices_dir = devices_dir
        self._bind_wheel = bind_wheel
        self._on_state_changed = on_state_changed
        self._icon_cache: Dict[tuple[str, bool], Optional[tk.PhotoImage]] = {}
        self._state_device: Optional[DeviceBundle] = None

        meta = ttk.LabelFrame(inner, text="Device", padding=8)
        meta.grid(row=0, column=0, sticky="ew", pady=(0, 8))
        meta.columnconfigure(1, weight=1)
        self.meta_frame = meta
        self.meta_vars: Dict[str, tk.StringVar] = {}
        rows = [("Display name", "device_name"),
                ("Board",        "board_name"),
                ("SoC family",   "soc_family"),
                ("OS",           "os_version"),
                ("Year",         "device_year"),
                ("Size",         "size"),
                ("Download",     "download_size"),
                ("State",        "state")]
        for i, (label, key) in enumerate(rows):
            ttk.Label(meta, text=label + ":").grid(row=i, column=0, sticky="w", padx=(0, 8))
            var = tk.StringVar(value="-")
            self.meta_vars[key] = var
            ttk.Label(meta, textvariable=var, wraplength=220,
                      justify="left").grid(row=i, column=1, sticky="w")

        # Source credit row: shown only when a source name exists; the name
        # becomes a clickable link when the source carries a URL.
        src_row = len(rows)
        self.source_caption = ttk.Label(meta, text="Source:")
        self.source_caption.grid(row=src_row, column=0, sticky="w", padx=(0, 8))
        self.source_value = ttk.Label(meta, wraplength=220, justify="left")
        self.source_value.grid(row=src_row, column=1, sticky="w")
        self._source_url: Optional[str] = None
        self.source_value.bind("<Button-1>", self._on_source_click)

        package = ttk.LabelFrame(inner, text="Package", padding=8)
        package.grid(row=1, column=0, sticky="ew", pady=(0, 8))
        package.columnconfigure(1, weight=1)
        self.package_frame = package
        self.package_vars: Dict[str, tk.StringVar] = {}
        pkg_rows = [("Name",     "name"),
                    ("Device",   "device"),
                    ("Category", "category"),
                    ("Size",     "size"),
                    ("Download", "download_size"),
                    ("State",    "state")]
        for i, (label, key) in enumerate(pkg_rows):
            ttk.Label(package, text=label + ":").grid(row=i, column=0, sticky="w", padx=(0, 8))
            var = tk.StringVar(value="-")
            self.package_vars[key] = var
            ttk.Label(package, textvariable=var, wraplength=220,
                      justify="left").grid(row=i, column=1, sticky="w")

        self.features_frame = ttk.LabelFrame(inner, text="Features", padding=8)
        self.features_frame.grid(row=2, column=0, sticky="ew", pady=(0, 8))
        self.features_icons = ttk.Frame(self.features_frame)
        self.features_icons.pack(anchor="w")

        self.desc_frame = ttk.LabelFrame(inner, text="Description", padding=8)
        self.desc_frame.grid(row=4, column=0, sticky="ew", pady=(0, 8))
        self.desc_frame.columnconfigure(0, weight=1)
        self.desc_label = ttk.Label(self.desc_frame, text="", wraplength=260,
                                    justify="left")
        self.desc_label.grid(row=0, column=0, sticky="w")

        self.notes_frame = ttk.LabelFrame(inner, text="⚠ Notes & quirks",
                                          style="Warn.TLabelframe", padding=8)
        self.notes_frame.grid(row=5, column=0, sticky="ew", pady=(0, 8))
        self.notes_frame.columnconfigure(0, weight=1)
        self.notes_label = ttk.Label(self.notes_frame, text="", wraplength=260,
                                     justify="left")
        self.notes_label.grid(row=0, column=0, sticky="w")

        # Saved-state section, pinned at the bottom of the details column.
        self.state_frame = ttk.LabelFrame(inner, text="Saved state", padding=8)
        self.state_frame.grid(row=7, column=0, sticky="ew", pady=(0, 8))
        self.state_frame.columnconfigure(0, weight=1)
        self.state_when = ttk.Label(self.state_frame, text="", wraplength=260,
                                    justify="left")
        self.state_when.grid(row=0, column=0, columnspan=2, sticky="w")
        self.state_size = ttk.Label(self.state_frame, text="")
        self.state_size.grid(row=1, column=0, sticky="w", pady=(6, 0))
        self.state_delete = ttk.Button(self.state_frame, text="Delete",
                                       style="Danger.TButton",
                                       command=self._on_delete_state)
        self.state_delete.grid(row=1, column=1, sticky="e", pady=(6, 0))

        self.package_frame.grid_remove()
        self.features_frame.grid_remove()
        self.desc_frame.grid_remove()
        self.notes_frame.grid_remove()
        self.state_frame.grid_remove()

        # Refresh the saved-state section when the launcher window regains focus
        # (e.g. after closing cerf.exe with a save), so a new snapshot shows up
        # without re-selecting the device.
        inner.winfo_toplevel().bind("<FocusIn>",
                                    lambda _e: self.refresh_saved_state(),
                                    add="+")

    def set_wraplength(self, wrap: int) -> None:
        self.desc_label.config(wraplength=wrap)
        self.notes_label.config(wraplength=wrap)

    def show_device(self, device: DeviceBundle) -> None:
        self.package_frame.grid_remove()
        self.meta_frame.grid()
        self.meta_vars["device_name"].set(device.meta.device_name or device.name)
        self.meta_vars["board_name"] .set(device.meta.board_name or "-")
        self.meta_vars["soc_family"] .set(device.meta.soc_family or "-")
        self.meta_vars["os_version"] .set(device.meta.os_version or "-")
        self.meta_vars["device_year"].set(str(device.meta.device_year) if device.meta.device_year else "-")
        remote = device.remote
        self.meta_vars["size"].set(
            (format_size(remote.unpacked_size) if remote else "") or "-")
        self.meta_vars["download_size"].set(
            (format_size(remote.archive_size) if remote else "") or "-")
        self.meta_vars["state"].set(device.state_label)
        self._update_source(device)
        self._update_features(device)
        self._update_description(device)
        self._update_notes(device)
        self._update_saved_state(device)

    def show_package(self, device: DeviceBundle, ps: PackageStatus) -> None:
        self.meta_frame.grid_remove()
        self.features_frame.grid_remove()
        self.desc_frame.grid_remove()
        self.notes_frame.grid_remove()
        self._state_device = None
        self.state_frame.grid_remove()
        self.package_frame.grid()
        self.package_vars["name"].set(ps.remote.name)
        self.package_vars["device"].set(device.meta.device_name or device.name)
        self.package_vars["category"].set(ps.category_label)
        self.package_vars["size"].set(format_size(ps.remote.unpacked_size) or "-")
        self.package_vars["download_size"].set(
            format_size(ps.remote.archive_size) or "-")
        state = ps.state_label
        if not device.is_installed:
            state += " (install device first)"
        self.package_vars["state"].set(state)

    def _update_source(self, device: DeviceBundle) -> None:
        src = device.meta.source
        if src is None or not src.name:
            self.source_caption.grid_remove()
            self.source_value.grid_remove()
            self._source_url = None
            return
        self.source_caption.grid()
        self.source_value.grid()
        self._source_url = src.website or src.origin or None
        if self._source_url:
            self.source_value.config(text=src.name, foreground=LINK_FG,
                                     cursor="hand2")
        else:
            self.source_value.config(text=src.name, foreground=FG, cursor="")

    def _on_source_click(self, _event: object) -> None:
        if self._source_url:
            webbrowser.open(self._source_url)

    def _update_description(self, device: DeviceBundle) -> None:
        description = device.meta.description.strip()
        if description:
            self.desc_label.config(text=description)
            self.desc_frame.grid()
        else:
            self.desc_frame.grid_remove()

    def _update_notes(self, device: DeviceBundle) -> None:
        # ROM-specific notes first, then board-wide quirks, then
        # predicate-gated dynamic notes - both from supported_devices.py.
        notes: List[str] = list(device.meta.notes)
        notes += board_extra_notes(device.meta.board_name,
                                   device.meta.board_prev_names)
        notes += dynamic_extra_notes(device.meta.os_name,
                                     device.meta.os_ver_major,
                                     device.meta.os_ver_minor,
                                     device.meta.board_name,
                                     device.meta.board_prev_names)
        if notes:
            self.notes_label.config(text="\n".join(f"• {n}" for n in notes))
            self.notes_frame.grid()
        else:
            self.notes_frame.grid_remove()

    def _update_saved_state(self, device: DeviceBundle) -> None:
        self._state_device = device
        info = saved_state_info(self._devices_dir / device.name)
        if info is None:
            self.state_frame.grid_remove()
            return
        when = datetime.fromtimestamp(info.saved_at).strftime("%d.%m.%y %H:%M")
        self.state_when.config(text=f"State was saved {when}")
        self.state_size.config(text=format_size(info.size) or "-")
        self.state_frame.grid()

    def refresh_saved_state(self) -> None:
        if self._state_device is not None:
            self._update_saved_state(self._state_device)

    def _on_delete_state(self) -> None:
        device = self._state_device
        if device is None:
            return
        parent = self._inner.winfo_toplevel()
        name = device.meta.device_name or device.name
        if not ask_yesno(parent, "Delete saved state",
                         f"Delete the saved state for {name}?\n"
                         f"This cannot be undone."):
            return
        path = self._devices_dir / device.name / STATE_IMAGE_FILENAME
        try:
            path.unlink(missing_ok=True)
        except OSError as exc:
            show_error(parent, "Delete failed",
                       f"Could not delete the saved state:\n{exc}")
        self._update_saved_state(device)
        if self._on_state_changed is not None:
            self._on_state_changed()

    def _update_features(self, device: DeviceBundle) -> None:
        for child in self.features_icons.winfo_children():
            child.destroy()
        features = board_features(device.meta.board_name,
                                  device.meta.board_prev_names)
        shown = 0
        for key, filename, label in FEATURE_SPECS:
            if key not in features:  # absent -> board has no such hardware
                continue
            supported = features[key]
            icon = self._feature_icon(filename, gray=not supported)
            if icon is None:
                continue
            lbl = ttk.Label(self.features_icons, image=icon)
            lbl.image = icon  # keep a ref so Tk doesn't GC it
            lbl.pack(side="left", padx=(0, 8))
            tip = label if supported else f"{label} (unsupported)"
            bind_tooltip(lbl, tip)
            shown += 1
        self._bind_wheel(self.features_icons)
        if shown:
            self.features_frame.grid()
        else:
            self.features_frame.grid_remove()

    def _feature_icon(self, filename: str, gray: bool) -> Optional[tk.PhotoImage]:
        cache_key = (filename, gray)
        if cache_key in self._icon_cache:
            return self._icon_cache[cache_key]
        icon: Optional[tk.PhotoImage] = None
        if self._icons_dir is not None:
            path = self._icons_dir / filename
            try:
                base = tk.PhotoImage(file=str(path))
                if base.width() > 24:  # source icons are 32px; show ~16px
                    base = base.subsample(2, 2)
                icon = self._grayscale_image(base) if gray else base
            except tk.TclError:
                icon = None
        self._icon_cache[cache_key] = icon
        return icon

    def _grayscale_image(self, img: tk.PhotoImage) -> tk.PhotoImage:
        # Desaturate in-place on a copy, preserving per-pixel transparency.
        w, h = img.width(), img.height()
        gray = img.copy()
        tk_interp = img.tk
        for y in range(h):
            for x in range(w):
                if tk_interp.call(img, "transparency", "get", x, y):
                    continue
                r, g, b = img.get(x, y)
                lum = (r * 299 + g * 587 + b * 114) // 1000
                gray.put(f"#{lum:02x}{lum:02x}{lum:02x}", to=(x, y))
        return gray
