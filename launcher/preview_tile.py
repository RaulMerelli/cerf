from __future__ import annotations

import math
import tkinter as tk
from pathlib import Path
from typing import Callable, Optional

from device_state import (DeviceBundle, LIVE_STATE_SCREENSHOT_FILENAME,
                          SAVED_STATE_SCREENSHOT_FILENAME, running_status,
                          saved_state_info)
import ui_theme as theme

PLAY_RUNNING = "#3fb950"
BOX_BG = "#000000"


class PreviewTile:
    def __init__(self, parent: tk.Misc, devices_dir: Path, width: int,
                 height: int, glyph: int, bg: str, box_always: bool = False,
                 on_click: Optional[Callable[[], None]] = None):
        self._devices_dir = devices_dir
        self._w, self._h, self._glyph = width, height, glyph
        self._box_always = box_always
        self._device: Optional[DeviceBundle] = None
        self._sig = ""
        self._img: Optional[tk.PhotoImage] = None
        self.canvas = tk.Canvas(parent, width=width, height=height, bg=bg,
                                highlightthickness=0, bd=0, cursor="hand2")
        if on_click is not None:
            self.canvas.bind("<Button-1>", lambda _e: on_click())

    def set_device(self, d: Optional[DeviceBundle]) -> None:
        self._device = d
        self.refresh()

    def retheme(self, bg: str) -> None:
        self.canvas.config(bg=bg)
        self._sig = ""
        self.refresh()

    def refresh(self) -> None:
        d = self._device
        if d is None:
            return
        state = self._state(d)
        shot = {"running": LIVE_STATE_SCREENSHOT_FILENAME,
                "paused": SAVED_STATE_SCREENSHOT_FILENAME}.get(state)
        mtime = self._mtime(d, shot) if shot else 0.0
        sig = f"{d.name}:{state}:{mtime}"
        if sig == self._sig:
            return
        self._sig = sig
        cv = self.canvas
        cv.delete("all")
        self._img = None
        if state != "stopped" or self._box_always:
            cv.create_rectangle(-1, -1, self._w + 1, self._h + 1,
                                fill=BOX_BG, outline="")
        if shot is not None:
            img = self._load(d, shot)
            if img is not None:
                self._img = img
                cv.create_image(self._w // 2, self._h // 2, image=img,
                                anchor="center")
        cx, cy, s = self._w / 2, self._h / 2, self._glyph
        if state == "running":
            self._play(cv, cx, cy, s, PLAY_RUNNING)
        elif state == "paused":
            self._pause(cv, cx, cy, s, theme.UPDATE_LINK, "#101010")
        else:
            self._play(cv, cx, cy, s, theme.PREVIEW_STOPPED)

    def _state(self, d: DeviceBundle) -> str:
        dirpath = self._devices_dir / d.name
        if running_status(dirpath) is not None:
            return "running"
        if saved_state_info(dirpath) is not None:
            return "paused"
        return "stopped"

    def _mtime(self, d: DeviceBundle, filename: str) -> float:
        p = self._devices_dir / d.name / filename
        try:
            return p.stat().st_mtime
        except OSError:
            return 0.0

    def _load(self, d: DeviceBundle, filename: str) -> Optional[tk.PhotoImage]:
        p = self._devices_dir / d.name / filename
        try:
            img = tk.PhotoImage(file=str(p))
        except tk.TclError:
            return None
        if img.width() <= 0 or img.height() <= 0:
            return None
        return self._fit_contain(img)

    def _fit_contain(self, img: tk.PhotoImage) -> tk.PhotoImage:
        iw, ih = img.width(), img.height()
        scale = min(self._w / iw, self._h / ih)
        if scale >= 1.0:
            return img
        best: Optional[tuple] = None
        for a in range(1, 7):
            if iw * a * ih * a * 4 > 64 * 1024 * 1024:
                break
            b = max(1, math.ceil(a / scale))
            err = scale - a / b
            if best is None or err < best[0]:
                best = (err, a, b)
        if best is None:
            return img
        _, a, b = best
        out = img
        if a > 1:
            out = out.zoom(a, a)
        if b > 1:
            out = out.subsample(b, b)
        return out

    def _play(self, cv: tk.Canvas, cx: float, cy: float, s: float,
              fill: str, outline: str = "") -> None:
        cv.create_polygon(cx - s * 0.6, cy - s, cx - s * 0.6, cy + s,
                          cx + s, cy, fill=fill, outline=outline, width=1)

    def _pause(self, cv: tk.Canvas, cx: float, cy: float, s: float,
               fill: str, outline: str = "") -> None:
        bw, gap, bh = s * 0.42, s * 0.32, s * 1.7
        cv.create_rectangle(cx - gap - bw, cy - bh / 2, cx - gap, cy + bh / 2,
                            fill=fill, outline=outline)
        cv.create_rectangle(cx + gap, cy - bh / 2, cx + gap + bw, cy + bh / 2,
                            fill=fill, outline=outline)
