"""One-time-per-device notice that editing launch options while a saved state
exists will not fully apply until the state is discarded (a cold boot)."""
from __future__ import annotations

import tkinter as tk
from pathlib import Path
from typing import Optional

from device_state import saved_state_info
from ui_dialogs import show_info


class SavedStateEditWarning:
    def __init__(self, window: tk.Misc) -> None:
        self._window = window
        self._warned: set[str] = set()

    def maybe_warn(self, device_dir: Optional[Path]) -> None:
        if device_dir is None:
            return
        key = str(device_dir)
        if key in self._warned:
            return
        if saved_state_info(device_dir) is None:
            return
        self._warned.add(key)
        self._window.after_idle(self._show)

    def _show(self) -> None:
        show_info(
            self._window, "Saved state present",
            "This device has a saved state.\n\n"
            "Most changes you make here take effect only after a cold boot - "
            "discard the saved state to apply them. Some changes might instead "
            "take effect on a soft reboot inside your saved state.")
