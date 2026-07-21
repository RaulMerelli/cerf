from __future__ import annotations

import subprocess
import webbrowser
from typing import List, Optional

from device_state import DeviceBundle
from running_state import show_running_window
from ui_dialogs import show_error, WEBSITE_URL


class SpawnMixin:
    def _launch(self, boot: Optional[str] = None) -> None:
        sel = self.tree_panel.selection()
        d = sel.device
        if d is None or self.busy:
            return
        if self.cerf_exe is None:
            show_error(self, "Cannot launch", "cerf.exe not found next to launcher.exe.")
            return
        if not d.is_installed:
            return
        status = self._running_status_for(d)
        if status is not None:
            if not show_running_window(status):
                show_error(self, "Show CERF",
                           f"{d.name} is running but its window could not be focused.")
            return
        self._spawn_cerf(d, boot)

    def _open_about(self) -> None:
        if self.cerf_exe is None:
            webbrowser.open(WEBSITE_URL)
            return
        try:
            subprocess.Popen([str(self.cerf_exe), "--about"],
                             cwd=str(self.cerf_exe.parent),
                             creationflags=getattr(subprocess, "DETACHED_PROCESS", 0))
        except OSError as exc:
            show_error(self, "Cannot show About", str(exc))

    def _spawn_cerf(self, d: DeviceBundle, boot: Optional[str] = None) -> None:
        tail = self.launch_options.collect_args(d)
        if tail is None:
            return
        tail.append(f"--boot={boot or 'resume'}")
        argv: List[str] = [str(self.cerf_exe)] + tail
        try:
            subprocess.Popen(argv, cwd=str(self.cerf_exe.parent),
                             creationflags=getattr(subprocess, "DETACHED_PROCESS", 0))
            self.status_bar.set_status(f"Launched cerf.exe for {d.name}.")
        except OSError as exc:
            show_error(self, "Launch failed", str(exc))
