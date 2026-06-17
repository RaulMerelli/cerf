"""Launch options: log/network toggles, guest additions, the resolution
override fields + preset slider, and cerf.exe argv assembly."""
from __future__ import annotations

import tkinter as tk
from tkinter import ttk
from typing import List, Optional

from device_state import DeviceBundle
from ui_dialogs import show_error, show_guest_additions_help
from ui_theme import BG_FIELD, FG_DIM


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


class LaunchOptionsPanel:
    def __init__(self, inner: ttk.Frame, parent_window: tk.Misc, row: int):
        self._window = parent_window
        self._device: Optional[DeviceBundle] = None

        opts = ttk.LabelFrame(inner, text="Launch options", padding=8)
        opts.grid(row=row, column=0, sticky="ew", pady=(0, 8))
        opts.columnconfigure(1, weight=1)
        self.frame = opts
        self.var_log_all   = tk.BooleanVar(value=False)
        self.var_flush     = tk.BooleanVar(value=False)
        self.var_flood     = tk.BooleanVar(value=False)
        self.var_no_net    = tk.BooleanVar(value=False)
        self.var_full_screen = tk.BooleanVar(value=False)
        self.var_guest_additions = tk.BooleanVar(value=False)
        ttk.Checkbutton(opts, text="Enable all log channels", variable=self.var_log_all).grid(row=0, column=0, columnspan=2, sticky="w")
        ttk.Checkbutton(opts, text="Flush logs immediately",
                        variable=self.var_flush).grid(row=1, column=0, columnspan=2, sticky="w")
        ttk.Checkbutton(opts, text="Allow stdout flood",
                        variable=self.var_flood).grid(row=2, column=0, columnspan=2, sticky="w")
        ttk.Checkbutton(opts, text="Disable network backend",
                        variable=self.var_no_net).grid(row=3, column=0, columnspan=2, sticky="w")
        ttk.Checkbutton(opts, text="Borderless full screen",
                        variable=self.var_full_screen).grid(row=4, column=0, columnspan=2, sticky="w")

        ttk.Separator(opts).grid(row=5, column=0, columnspan=3, sticky="ew", pady=6)

        guest = ttk.Frame(opts)
        guest.grid(row=6, column=0, columnspan=3, sticky="ew", pady=(0, 6))
        guest.columnconfigure(0, weight=1)
        ttk.Checkbutton(guest, text="Enable guest additions",
                        variable=self.var_guest_additions,
                        command=self.refresh_resolution_state,
                        style="Guest.TCheckbutton").grid(row=0, column=0, sticky="w")
        ttk.Button(guest, text="?", width=2, style="Help.TButton",
                   command=lambda: show_guest_additions_help(self._window)).grid(row=0, column=1, sticky="e")
        ttk.Label(guest, text="(might be unstable)",
                  style="Hint.TLabel").grid(row=1, column=0, columnspan=2, sticky="w")

        ttk.Separator(opts).grid(row=7, column=0, columnspan=3, sticky="ew", pady=(0, 6))

        self.res_note = ttk.Label(opts, text="Resolution override:")
        self.res_note.grid(row=8, column=0, columnspan=3, sticky="w")
        self.var_width  = tk.StringVar(value="240")
        self.var_height = tk.StringVar(value="320")
        numeric_vcmd = (parent_window.register(self._is_optional_uint), "%P")
        res_fields = ttk.Frame(opts)
        res_fields.grid(row=9, column=0, columnspan=3, sticky="ew", pady=(2, 0))
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

    def set_device(self, device: Optional[DeviceBundle]) -> None:
        self._device = device
        self.refresh_resolution_state()

    def collect_args(self, device: DeviceBundle) -> Optional[List[str]]:
        """Build the cerf.exe argument tail for the chosen options. Returns
        None (after showing an error) when the resolution fields are invalid."""
        argv: List[str] = [f"--device={device.name}"]
        if self.var_log_all.get(): argv.append("--log=ALL")
        if self.var_flush.get():  argv.append("--flush-outputs")
        if self.var_flood.get():  argv.append("--allow-flood")
        if self.var_no_net.get(): argv.append("--disable-network")
        if self.var_full_screen.get(): argv.append("--full-screen")
        guest_additions = self.var_guest_additions.get()
        if guest_additions:
            argv.append("--guest-additions")
        if guest_additions or device.screen_supported is not False:
            w = self._resolution_value(self.var_width, self.width_entry, "Width")
            if w is None:
                return None
            h = self._resolution_value(self.var_height, self.height_entry, "Height")
            if h is None:
                return None
            argv += [f"--screen-width={w}", f"--screen-height={h}"]
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

    def _apply_resolution_defaults(self, device: Optional[DeviceBundle]) -> None:
        if device is not None and device.default_screen_width:
            self.var_width.set(str(device.default_screen_width))
        elif not self.var_width.get().strip():
            self.var_width.set("240")

        if device is not None and device.default_screen_height:
            self.var_height.set(str(device.default_screen_height))
        elif not self.var_height.get().strip():
            self.var_height.set("320")

    def _set_resolution_fields_enabled(self, enabled: bool) -> None:
        state = "normal" if enabled else "disabled"
        self.width_entry.config(state=state)
        self.height_entry.config(state=state)
        self.res_slider.config(state=state)
        self.res_preset_label.config(foreground=FG_DIM if enabled else BG_FIELD)

    def refresh_resolution_state(self) -> None:
        device = self._device
        if self.var_guest_additions.get():
            self.res_note.config(text="CERF display driver resolution:")
            self._set_resolution_fields_enabled(True)
            self._apply_resolution_defaults(device)
            return

        if device is not None and device.screen_supported is False:
            self.res_note.config(text="Resolution changes unsupported.")
            self._set_resolution_fields_enabled(False)
            return

        self.res_note.config(text="Resolution override:")
        self._set_resolution_fields_enabled(True)
        self._apply_resolution_defaults(device)

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

    def _on_res_text_changed(self, *_args: object) -> None:
        if self._res_sync_guard:
            return
        self._sync_slider_to_text()

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
                self.res_preset_label.config(text=f"Custom — {w} × {h}")
        finally:
            self._res_sync_guard = False
