"""Right-side info panels: device metadata, feature icons, description and
notes - plus the additional-package details view shown when a package row
is selected in the device tree."""
from __future__ import annotations

import tkinter as tk
import webbrowser
from pathlib import Path
from tkinter import ttk
from typing import Callable, Dict, List, Optional

from device_state import (
    DeviceBundle,
    PackageStatus,
    format_size,
)
from supported_devices import (
    FEATURE_SPECS,
    board_extra_notes,
    board_features,
    dynamic_extra_notes,
)
from ui_dialogs import bind_tooltip
import ui_theme as theme


class DetailsPanel:
    def __init__(self, inner: ttk.Frame, icons_dir: Optional[Path],
                 devices_dir: Path,
                 bind_wheel: Callable[[tk.Misc], None],
                 on_package_action: Optional[Callable] = None):
        self._inner = inner
        self._icons_dir = icons_dir
        self._devices_dir = devices_dir
        self._bind_wheel = bind_wheel
        self._on_package_action = on_package_action
        self._icon_cache: Dict[tuple[str, bool], Optional[tk.PhotoImage]] = {}

        meta = ttk.LabelFrame(inner, text="Description", padding=8)
        meta.grid(row=0, column=0, sticky="ew", pady=(0, 8))
        meta.columnconfigure(1, weight=1)
        self.meta_frame = meta

        self.desc_label = ttk.Label(meta, text="", wraplength=220,
                                    justify="left")
        self.desc_label.grid(row=0, column=0, columnspan=2, sticky="w")

        self.source_caption = ttk.Label(meta, text="Source:")
        self.source_caption.grid(row=1, column=0, sticky="w", padx=(0, 8),
                                 pady=(8, 0))
        self.source_value = ttk.Label(meta, wraplength=220, justify="left")
        self.source_value.grid(row=1, column=1, sticky="w", pady=(8, 0))
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

        self.notes_frame = ttk.LabelFrame(inner, text="⚠ Notes & quirks",
                                          style="Warn.TLabelframe", padding=8)
        self.notes_frame.grid(row=5, column=0, sticky="ew", pady=(0, 8))
        self.notes_frame.columnconfigure(0, weight=1)
        self.notes_label = ttk.Label(self.notes_frame, text="", wraplength=260,
                                     justify="left")
        self.notes_label.grid(row=0, column=0, sticky="w")

        self.addons_frame = ttk.LabelFrame(inner, text="Add-ons", padding=8)
        self.addons_frame.grid(row=6, column=0, sticky="ew", pady=(0, 8))
        self.addons_frame.columnconfigure(0, weight=1)
        self.addons_body = ttk.Frame(self.addons_frame)
        self.addons_body.grid(row=0, column=0, sticky="ew")
        self.addons_body.columnconfigure(0, weight=1)

        self.package_frame.grid_remove()
        self.features_frame.grid_remove()
        self.desc_label.grid_remove()
        self.notes_frame.grid_remove()
        self.addons_frame.grid_remove()

    def set_wraplength(self, wrap: int) -> None:
        self.desc_label.config(wraplength=wrap)
        self.notes_label.config(wraplength=wrap)

    def show_device(self, device: DeviceBundle) -> None:
        self.package_frame.grid_remove()
        self._update_source(device)
        self._update_description(device)
        has_desc = bool(device.meta.description.strip())
        has_source = (device.meta.source is not None
                      and bool(device.meta.source.name))
        if has_desc or has_source:
            self.meta_frame.grid()
        else:
            self.meta_frame.grid_remove()
        self._update_features(device)
        self._update_notes(device)
        self._update_packages(device)

    def _update_packages(self, device: DeviceBundle) -> None:
        for child in self.addons_body.winfo_children():
            child.destroy()
        if not device.packages:
            self.addons_frame.grid_remove()
            return
        self.addons_frame.grid()
        last_cat = None
        r = 0
        for ps in device.packages:
            if ps.category_label != last_cat:
                ttk.Label(self.addons_body, text=ps.category_label,
                          foreground=theme.FG).grid(row=r, column=0, columnspan=2,
                                                    sticky="w", pady=(4, 0))
                last_cat = ps.category_label
                r += 1
            row = ttk.Frame(self.addons_body)
            row.grid(row=r, column=0, columnspan=2, sticky="ew")
            row.columnconfigure(0, weight=1)
            ttk.Label(row, text=ps.remote.name, wraplength=170,
                      justify="left").grid(row=0, column=0, sticky="w")
            if ps.has_update or not ps.installed:
                ttk.Button(row, text="Update" if ps.has_update else "Get",
                           width=7,
                           command=lambda p=ps: self._package_action(device, p, "install")
                           ).grid(row=0, column=1, sticky="e", padx=(4, 0))
            elif ps.installed:
                ttk.Button(row, text="Delete", width=7, style="Danger.TButton",
                           command=lambda p=ps: self._package_action(device, p, "delete")
                           ).grid(row=0, column=1, sticky="e", padx=(4, 0))
            r += 1
        self._bind_wheel(self.addons_body)

    def _package_action(self, device: DeviceBundle, ps, action: str) -> None:
        if self._on_package_action is not None:
            self._on_package_action(device, ps, action)

    def show_package(self, device: DeviceBundle, ps: PackageStatus) -> None:
        self.meta_frame.grid_remove()
        self.features_frame.grid_remove()
        self.notes_frame.grid_remove()
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
            self.source_value.config(text=src.name, foreground=theme.LINK_FG,
                                     cursor="hand2")
        else:
            self.source_value.config(text=src.name, foreground=theme.FG,
                                     cursor="")

    def _on_source_click(self, _event: object) -> None:
        if self._source_url:
            webbrowser.open(self._source_url)

    def _update_description(self, device: DeviceBundle) -> None:
        description = device.meta.description.strip()
        if description:
            self.desc_label.config(text=description)
            self.desc_label.grid()
        else:
            self.desc_label.grid_remove()

    def _update_notes(self, device: DeviceBundle) -> None:
        # ROM-specific notes first, then board-wide quirks, then
        # predicate-gated dynamic notes - both from supported_devices.py.
        notes: List[str] = list(device.meta.notes)
        notes += board_extra_notes(device.meta.board_id)
        notes += dynamic_extra_notes(device.meta.os_name,
                                     device.meta.os_ver_major,
                                     device.meta.os_ver_minor,
                                     device.meta.board_id)
        if notes:
            self.notes_label.config(text="\n".join(f"• {n}" for n in notes))
            self.notes_frame.grid()
        else:
            self.notes_frame.grid_remove()

    def _update_features(self, device: DeviceBundle) -> None:
        for child in self.features_icons.winfo_children():
            child.destroy()
        features = board_features(device.meta.board_id)
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
