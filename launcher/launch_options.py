"""Launch options: log/network toggles, guest additions, the resolution
override fields + preset slider, and cerf.exe argv assembly."""
from __future__ import annotations

import tkinter as tk
from tkinter import ttk
from typing import List, Optional

from pathlib import Path

from cerf_user_json import read_persist_fields, write_persist_overrides
from device_state import DeviceBundle
from board_info import board_configurable_screen, board_features
from ui_dialogs import show_error, show_guest_additions_help, show_dpi_help
import ui_theme as theme


# Common display resolutions the override slider snaps through, ordered by
# pixel area (ascending) so the thumb moves monotonically small -> large,
# spanning PDA sizes up to 4K UHD. Both orientations of the everyday small
# sizes are included; the boxes stay free-form so any value off this list
# is still typable.
RES_PRESETS = [
    (240, 320),  (320, 240),    #  QVGA
    (320, 480),  (480, 320),    #  HVGA
    (640, 480),  (480, 640),    #  VGA
    (800, 480),  (480, 800),    #  WVGA
    (854, 480),                 #  FWVGA
    (800, 600),  (600, 800),    #  SVGA
    (1024, 600),                #  WSVGA
    (1024, 768), (768, 1024),   #  XGA
    (1280, 720),                #  HD 720p
    (1280, 800),                #  WXGA
    (1366, 768),                #  HD
    (1280, 1024),               #  SXGA
    (1440, 900),                #  WXGA+
    (1600, 900),                #  HD+
    (1680, 1050),               #  WSXGA+
    (1600, 1200),               #  UXGA
    (1920, 1080),               #  FHD 1080p
    (1920, 1200),               #  WUXGA
    (2560, 1440),               #  QHD 1440p
    (2560, 1600),               #  WQXGA
    (3440, 1440),               #  UW-QHD
    (3840, 2160),               #  4K UHD
]

# DPI override slider bounds. The entry stays free-form so any value (including
# extreme ones) is typable past the slider's range.
DPI_SLIDER_MIN = 48
DPI_SLIDER_MAX = 480

DEFAULT_SCREEN_WIDTH = 800
DEFAULT_SCREEN_HEIGHT = 600


class LaunchOptionsPanel:
    def __init__(self, inner: ttk.Frame, parent_window: tk.Misc,
                 devices_dir: Path, row: int):
        self._window = parent_window
        self._device: Optional[DeviceBundle] = None
        self._devices_dir = devices_dir
        self._device_dir: Optional[Path] = None
        self._baseline: dict = {}
        self._restoring = False
        self._guest_additions_available = True

        container = ttk.Frame(inner)
        container.grid(row=row, column=0, sticky="ew", pady=(0, 8))
        container.columnconfigure(0, weight=1)
        self.frame = container
        self.var_log_all   = tk.BooleanVar(value=False)
        self.var_no_net    = tk.BooleanVar(value=False)
        self.var_full_screen = tk.BooleanVar(value=False)
        self.var_guest_additions = tk.BooleanVar(value=False)

        cfg = ttk.LabelFrame(container, text="Configuration", padding=8)
        cfg.grid(row=0, column=0, sticky="ew", pady=(0, 8))
        cfg.columnconfigure(0, weight=1)

        guest = self.guest_block = ttk.Frame(cfg)
        guest.grid(row=0, column=0, sticky="ew")
        guest.columnconfigure(0, weight=1)
        ttk.Checkbutton(guest, text="Enable guest additions",
                        variable=self.var_guest_additions,
                        command=self._on_guest_additions_changed,
                        style="Guest.TCheckbutton").grid(row=0, column=0, sticky="w")
        ttk.Button(guest, text="?", width=2, style="Help.TButton",
                   command=lambda: show_guest_additions_help(self._window)).grid(row=0, column=1, sticky="e")
        ttk.Label(guest, text="(might be unstable)",
                  style="Hint.TLabel").grid(row=1, column=0, columnspan=2, sticky="w")

        self.guest_sep = ttk.Separator(cfg, orient="horizontal")
        self.guest_sep.grid(row=1, column=0, sticky="ew", pady=8)

        self.res_note = ttk.Label(cfg, text="Resolution override:")
        self.res_note.grid(row=2, column=0, sticky="w")
        self.var_width  = tk.StringVar(value=str(DEFAULT_SCREEN_WIDTH))
        self.var_height = tk.StringVar(value=str(DEFAULT_SCREEN_HEIGHT))
        numeric_vcmd = (parent_window.register(self._is_optional_uint), "%P")
        res_fields = self.res_fields = ttk.Frame(cfg)
        res_fields.grid(row=3, column=0, sticky="ew", pady=(2, 0))
        res_fields.columnconfigure(5, weight=1)
        self.width_entry = ttk.Entry(res_fields, textvariable=self.var_width, width=8,
                                     validate="key", validatecommand=numeric_vcmd)
        self.height_entry = ttk.Entry(res_fields, textvariable=self.var_height, width=8,
                                      validate="key", validatecommand=numeric_vcmd)
        self.width_unit  = ttk.Label(res_fields, text="px")
        self.height_unit = ttk.Label(res_fields, text="px")
        ttk.Label(res_fields, text="Width").grid(row=0, column=0, sticky="w")
        self.width_entry.grid(row=0, column=1, sticky="w", padx=(4, 4))
        self.width_unit.grid(row=0, column=2, sticky="w", padx=(0, 12))
        ttk.Label(res_fields, text="Height").grid(row=0, column=3, sticky="w")
        self.height_entry.grid(row=0, column=4, sticky="w", padx=(4, 4))
        self.height_unit.grid(row=0, column=5, sticky="w")

        self._res_sync_guard = False
        self.res_slider = ttk.Scale(res_fields, from_=0, to=len(RES_PRESETS) - 1,
                                    orient="horizontal", style="Res.Horizontal.TScale",
                                    command=self._on_res_slider)
        self.res_slider.grid(row=1, column=0, columnspan=6, sticky="ew", pady=(8, 0))
        self.res_preset_label = ttk.Label(res_fields, text="", style="Hint.TLabel")
        self.res_preset_label.grid(row=2, column=0, columnspan=6, sticky="w")
        self.var_width.trace_add("write", self._on_res_text_changed)
        self.var_height.trace_add("write", self._on_res_text_changed)
        self._sync_slider_to_text()

        self.res_sep = ttk.Separator(cfg, orient="horizontal")
        self.res_sep.grid(row=4, column=0, sticky="ew", pady=8)

        self.var_override_dpi = tk.BooleanVar(value=False)
        self.var_dpi = tk.StringVar(value="96")
        dpi_head = self.dpi_head = ttk.Frame(cfg)
        dpi_head.grid(row=5, column=0, sticky="ew")
        dpi_head.columnconfigure(0, weight=1)
        ttk.Checkbutton(dpi_head, text="Override DPI",
                        variable=self.var_override_dpi,
                        command=self._on_override_dpi_changed).grid(row=0, column=0, sticky="w")
        ttk.Button(dpi_head, text="?", width=2, style="Help.TButton",
                   command=lambda: show_dpi_help(self._window)).grid(row=0, column=1, sticky="e")

        dpi_fields = self.dpi_fields = ttk.Frame(cfg)
        dpi_fields.grid(row=6, column=0, sticky="ew", pady=(2, 0))
        dpi_fields.columnconfigure(3, weight=1)
        ttk.Label(dpi_fields, text="DPI").grid(row=0, column=0, sticky="w")
        self.dpi_entry = ttk.Entry(dpi_fields, textvariable=self.var_dpi, width=8,
                                   validate="key", validatecommand=numeric_vcmd)
        self.dpi_entry.grid(row=0, column=1, sticky="w", padx=(4, 12))
        self._dpi_sync_guard = False
        self.dpi_slider = ttk.Scale(dpi_fields, from_=DPI_SLIDER_MIN, to=DPI_SLIDER_MAX,
                                    orient="horizontal", style="Res.Horizontal.TScale",
                                    command=self._on_dpi_slider)
        self.dpi_slider.grid(row=1, column=0, columnspan=4, sticky="ew", pady=(8, 0))
        self.var_dpi.trace_add("write", self._on_dpi_text_changed)
        self._sync_dpi_slider_to_text()

        self.dpi_sep = ttk.Separator(cfg, orient="horizontal")
        self.dpi_sep.grid(row=7, column=0, sticky="ew", pady=8)

        ttk.Checkbutton(cfg, text="Borderless full screen",
                        variable=self.var_full_screen,
                        command=self._persist).grid(row=8, column=0, sticky="w")

        ttk.Separator(cfg, orient="horizontal").grid(row=9, column=0,
                                                     sticky="ew", pady=8)

        ttk.Checkbutton(cfg, text="Enable all log channels",
                        variable=self.var_log_all).grid(row=10, column=0, sticky="w")
        ttk.Checkbutton(cfg, text="Disable network backend",
                        variable=self.var_no_net,
                        command=self._persist).grid(row=11, column=0, sticky="w")
        self.refresh_resolution_state()

    def set_device(self, device: Optional[DeviceBundle]) -> None:
        self._device = device
        self._device_dir = None
        base: dict = {}
        override: dict = {}
        if device is not None and device.is_installed:
            self._device_dir = self._devices_dir / device.name
            base, override = read_persist_fields(self._device_dir)
        self._baseline = self._resolve_baseline(base, device)
        self._guest_additions_available = self._resolve_guest_additions_available(device)
        eff = dict(self._baseline)
        eff.update(override)
        self._restoring = True
        try:
            self.var_no_net.set(not eff["network_enabled"])
            self.var_guest_additions.set(eff["guest_additions"]
                                         and self._guest_additions_available)
            self.var_full_screen.set(eff["full_screen"])
            self.var_width.set(str(eff["width"]))
            self.var_height.set(str(eff["height"]))
            if "dpi" in eff:
                self.var_override_dpi.set(True)
                self.var_dpi.set(str(eff["dpi"]))
            else:
                self.var_override_dpi.set(False)
            self._sync_slider_to_text()
            self._sync_dpi_slider_to_text()
        finally:
            self._restoring = False
        self.refresh_resolution_state()

    def _resolve_baseline(self, base: dict,
                          device: Optional[DeviceBundle]) -> dict:
        b: dict = {}
        b["network_enabled"] = base.get("network_enabled", True)
        b["guest_additions"] = base.get("guest_additions", False)
        b["full_screen"] = base.get("full_screen", False)
        if "width" in base:
            b["width"] = base["width"]
        elif device is not None and device.default_screen_width:
            b["width"] = device.default_screen_width
        else:
            b["width"] = DEFAULT_SCREEN_WIDTH
        if "height" in base:
            b["height"] = base["height"]
        elif device is not None and device.default_screen_height:
            b["height"] = device.default_screen_height
        else:
            b["height"] = DEFAULT_SCREEN_HEIGHT
        if "dpi" in base:
            b["dpi"] = base["dpi"]
        return b

    def _resolve_guest_additions_available(self,
                                           device: Optional[DeviceBundle]) -> bool:
        """Guest additions are offered unless the board's feature map says the
        board cannot run them, or the ROM is a CE 1.x image (the guest driver
        needs APIs CE 1.x does not have)."""
        if device is None:
            return True
        if device.meta.os_ver_major == 1:
            return False
        return board_features(device.meta.board_id).get("guest_additions") is not False

    def _current_fields(self) -> dict:
        f: dict = {}
        f["network_enabled"] = not self.var_no_net.get()
        f["guest_additions"] = self.var_guest_additions.get()
        f["full_screen"] = self.var_full_screen.get()
        w = self._optional_uint(self.var_width)
        if w is not None:
            f["width"] = w
        h = self._optional_uint(self.var_height)
        if h is not None:
            f["height"] = h
        if self.var_guest_additions.get() and self.var_override_dpi.get():
            d = self._optional_uint(self.var_dpi)
            if d is not None:
                f["dpi"] = d
        return f

    def _optional_uint(self, var: tk.StringVar) -> Optional[int]:
        try:
            v = int(var.get().strip())
        except ValueError:
            return None
        return v if v > 0 else None

    def _persist(self) -> None:
        if self._restoring or self._device_dir is None:
            return
        cur = self._current_fields()
        override: dict = {}
        for k in ("network_enabled", "guest_additions", "full_screen",
                  "width", "height", "dpi"):
            if k in cur and cur[k] != self._baseline.get(k):
                override[k] = cur[k]
        write_persist_overrides(self._device_dir, override)

    def _on_guest_additions_changed(self) -> None:
        self.refresh_resolution_state()
        self._persist()

    def _on_override_dpi_changed(self) -> None:
        self._refresh_dpi_state()
        self._persist()

    def collect_args(self, device: DeviceBundle) -> Optional[List[str]]:
        """Build the cerf.exe argument tail for the chosen options. Returns
        None (after showing an error) when the resolution fields are invalid."""
        argv: List[str] = [f"--device={device.name}"]
        if self.var_log_all.get(): argv.append("--log=ALL")
        if self.var_no_net.get(): argv.append("--disable-network")
        if self.var_full_screen.get(): argv.append("--full-screen")
        guest_additions = self.var_guest_additions.get()
        if guest_additions:
            argv.append("--guest-additions")
        if guest_additions or board_configurable_screen(device.meta.board_id):
            w = self._resolution_value(self.var_width, self.width_entry, "Width")
            if w is None:
                return None
            h = self._resolution_value(self.var_height, self.height_entry, "Height")
            if h is None:
                return None
            argv += [f"--screen-width={w}", f"--screen-height={h}"]
        if guest_additions and self.var_override_dpi.get():
            dpi = self._dpi_value()
            if dpi is None:
                return None
            argv.append(f"--screen-dpi={dpi}")
        return argv

    def _is_optional_uint(self, value: str) -> bool:
        return value == "" or value.isdigit()

    def _resolution_value(self, var: tk.StringVar, entry: ttk.Entry,
                          label: str) -> Optional[int]:
        raw = var.get().strip()
        if not raw:
            show_error(self._window, "Invalid resolution",
                       f"{label} must be a positive whole-pixel value.")
            entry.focus_set()
            return None
        try:
            value = int(raw, 10)
        except ValueError:
            show_error(self._window, "Invalid resolution",
                       f"{label} must be a positive whole-pixel value.")
            entry.focus_set()
            return None
        if value < 1:
            show_error(self._window, "Invalid resolution",
                       f"{label} must be at least 1 px.")
            entry.focus_set()
            return None
        var.set(str(value))
        return value

    def _set_block_visible(self, visible: bool, *widgets: tk.Widget) -> None:
        for w in widgets:
            if visible:
                w.grid()
            else:
                w.grid_remove()

    def refresh_resolution_state(self) -> None:
        """Show only the blocks the selected device can actually use: a board
        whose resolution is fixed hides the resolution block, and DPI - which
        only the CERF display driver honours - is shown with guest additions
        on. A block that cannot apply is hidden, never shown disabled."""
        device = self._device
        guest_additions = self.var_guest_additions.get()

        self._set_block_visible(self._guest_additions_available,
                                self.guest_block, self.guest_sep)

        res_visible = guest_additions or (
            device is not None
            and board_configurable_screen(device.meta.board_id))
        self._set_block_visible(res_visible, self.res_note, self.res_fields,
                                self.res_sep)
        self.res_note.config(text="CERF display driver resolution:"
                             if guest_additions else "Resolution override:")
        self.res_preset_label.config(foreground=theme.FG_DIM)

        self._set_block_visible(guest_additions, self.dpi_head, self.dpi_fields,
                                self.dpi_sep)
        self._refresh_dpi_state()

    def _on_res_slider(self, value: str) -> None:
        if self._res_sync_guard:
            return
        index = max(0, min(len(RES_PRESETS) - 1, round(float(value))))
        # Snap the continuous Scale thumb onto the discrete preset stop.
        if abs(float(value) - index) > 1e-9:
            self.res_slider.set(index)
            return
        w, h = RES_PRESETS[index]
        self._res_sync_guard = True
        try:
            self.var_width.set(str(w))
            self.var_height.set(str(h))
        finally:
            self._res_sync_guard = False
        self.res_preset_label.config(text=f"{w} × {h}")
        self._persist()

    def _on_res_text_changed(self, *_args: object) -> None:
        if self._res_sync_guard:
            return
        self._sync_slider_to_text()
        self._persist()

    def _sync_slider_to_text(self) -> None:
        try:
            w = int(self.var_width.get().strip())
            h = int(self.var_height.get().strip())
        except ValueError:
            self.res_preset_label.config(text="Custom")
            return
        self._res_sync_guard = True
        try:
            if (w, h) in RES_PRESETS:
                self.res_slider.set(RES_PRESETS.index((w, h)))
                self.res_preset_label.config(text=f"{w} × {h}")
            else:
                # Off-list value: park the thumb on the nearest-area preset
                # for a sensible position without overwriting the typed size.
                area = w * h
                nearest = min(range(len(RES_PRESETS)),
                              key=lambda i: abs(RES_PRESETS[i][0] * RES_PRESETS[i][1] - area))
                self.res_slider.set(nearest)
                self.res_preset_label.config(text=f"Custom - {w} × {h}")
        finally:
            self._res_sync_guard = False

    def _refresh_dpi_state(self) -> None:
        fields = "normal" if self.var_override_dpi.get() else "disabled"
        self.dpi_entry.config(state=fields)
        self.dpi_slider.config(state=fields)

    def _dpi_value(self) -> Optional[int]:
        raw = self.var_dpi.get().strip()
        try:
            value = int(raw, 10)
        except ValueError:
            value = 0
        if value < 1:
            show_error(self._window, "Invalid DPI",
                       "DPI must be a positive whole number.")
            self.dpi_entry.focus_set()
            return None
        return value

    def _on_dpi_slider(self, value: str) -> None:
        if self._dpi_sync_guard:
            return
        self._dpi_sync_guard = True
        try:
            self.var_dpi.set(str(int(round(float(value)))))
        finally:
            self._dpi_sync_guard = False
        self._persist()

    def _on_dpi_text_changed(self, *_args: object) -> None:
        if self._dpi_sync_guard:
            return
        self._sync_dpi_slider_to_text()
        self._persist()

    def _sync_dpi_slider_to_text(self) -> None:
        try:
            dpi = int(self.var_dpi.get().strip())
        except ValueError:
            return
        self._dpi_sync_guard = True
        try:
            self.dpi_slider.set(max(DPI_SLIDER_MIN, min(DPI_SLIDER_MAX, dpi)))
        finally:
            self._dpi_sync_guard = False
