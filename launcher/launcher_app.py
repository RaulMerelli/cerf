from __future__ import annotations

import ctypes
import os
import queue
import subprocess
import sys
import threading
import tkinter as tk
from concurrent.futures import Future
from ctypes import wintypes
from pathlib import Path
from tkinter import ttk
from typing import Callable, List, Optional, Tuple

from app_paths import resolve_icon, resolve_icons_dir, resolve_version
from device_card_list import DeviceCardList
from device_state import (DeviceBundle, SAVED_STATE_SCREENSHOT_FILENAME,
                          STATE_IMAGE_FILENAME, running_status, saved_state_info)
from device_model import TreeSelection
from download_window import DownloadWindow
from running_state import show_running_window
from toolbar import Toolbar
from details_panel import DetailsPanel
from launch_button import LaunchSplitButton
from launch_options import LaunchOptionsPanel
from launcher_operations import OperationsMixin
from launcher_refresh import RefreshMixin
from operations import BundleManager
from screen_geometry import fit_geometry
from preview_tile import PreviewTile
from status_bar import StatusBar
from ui_dialogs import (ask_yesno, confirm_rom_license, show_error, show_info,
                        show_sources_thanks)
from ui_large_download import gate_large_bundle
from ui_scroll import ScrollColumn
from update_check import UpdateCheck
import ui_theme as theme


_WM_SETTINGCHANGE = 0x001A
_GWLP_WNDPROC = -4


class LauncherApp(OperationsMixin, RefreshMixin, tk.Tk):
    def __init__(self, manager: BundleManager, cerf_exe: Optional[Path],
                 upgraded: bool = False):
        super().__init__()
        self.manager = manager
        self.cerf_exe = cerf_exe
        self.update_check = UpdateCheck(self)
        version = resolve_version()
        self.title(f"CE Runtime Foundation {version} Launcher" if version else "CE Runtime Foundation Launcher")

        try:
            dpi = float(self.winfo_fpixels("1i"))
            self.tk.call("tk", "scaling", dpi / 72.0)
        except tk.TclError:
            dpi = 96.0

        scale = max(1.0, dpi / 96.0)
        self.minsize(int(500 * scale), int(300 * scale))

        icon = resolve_icon()
        if icon is not None:
            try:
                self.iconbitmap(str(icon))
            except tk.TclError:
                pass

        theme.apply_theme(self)

        self.progress_queue: "queue.Queue[tuple[str, int, Optional[int]]]" = queue.Queue()
        self.cancel_event = threading.Event()
        self.busy = False

        self._build_ui()
        theme.apply_titlebar(self)
        fit_geometry(self, int(1100 * scale), int(640 * scale))
        self._install_theme_listener()
        self._pump_progress()
        self.after(50, self._refresh_manifest)
        self.after(50, self.update_check.start)
        self.after(50, self._poll_runtime)
        if upgraded:
            self.after(200, lambda: show_info(
                self, "Upgrade complete",
                f"CERF has been upgraded to {version}." if version
                else "CERF has been upgraded."))

        self.protocol("WM_DELETE_WINDOW", self._on_close)

    def _build_ui(self) -> None:
        self.status_bar = StatusBar(self)

        self.toolbar = Toolbar(self, on_download=self._open_download_window,
                               on_refresh=self._refresh_manifest,
                               on_update_all=self._update_all,
                               on_update_selected=self._update_selected,
                               on_remove_selected=self._delete_selected,
                               on_discard_selected=self._discard_state)
        self.toolbar.frame.pack(fill="x", side="top")

        outer = ttk.Frame(self, padding=8)
        outer.pack(fill="both", expand=True)

        try:
            pscale = max(1.0, float(self.winfo_fpixels("1i")) / 96.0)
        except tk.TclError:
            pscale = 1.0

        paned = tk.PanedWindow(outer, orient="horizontal", bg=theme.BORDER, bd=0,
                               sashwidth=int(5 * pscale), sashrelief="flat",
                               showhandle=False, opaqueresize=True)
        paned.pack(fill="both", expand=True)
        self.paned = paned

        left_pane = ttk.Frame(paned)
        left_pane.rowconfigure(0, weight=1)
        left_pane.columnconfigure(0, weight=1)
        self.tree_panel = DeviceCardList(
            left_pane,
            on_select=self._on_tree_select,
            on_activate=self._on_tree_activate,
            devices_dir=self.manager.devices_dir,
            icons_dir=resolve_icons_dir(),
            on_context=self._on_right_click)

        right = ttk.Frame(paned, padding=(8, 0, 0, 0))
        right.columnconfigure(0, weight=1)
        right.rowconfigure(1, weight=1)

        paned.add(left_pane, minsize=int(420 * pscale), stretch="always")
        paned.add(right, minsize=int(300 * pscale), width=int(384 * pscale),
                  stretch="never")

        self.preview = PreviewTile(right, self.manager.devices_dir,
                                   int(300 * pscale), int(188 * pscale),
                                   int(24 * pscale), theme.BG, box_always=True,
                                   on_click=self._launch)
        self.preview.canvas.grid(row=0, column=0, sticky="n", pady=(0, 8))

        def on_width(width: int) -> None:
            self.details.set_wraplength(max(120, width - 24))
        self.scroll = ScrollColumn(right, width=int(340 * pscale),
                                   on_width_changed=on_width)
        self.scroll.grid(row=1, column=0, sticky="nsew")

        inner = self.scroll.inner
        self.details = DetailsPanel(inner, resolve_icons_dir(), self.manager.devices_dir,
                                    bind_wheel=self.scroll.bind_wheel,
                                    on_package_action=self._package_action)

        self.launch_options = LaunchOptionsPanel(inner, self, self.manager.devices_dir, row=7)

        launch_bar = ttk.Frame(right)
        launch_bar.grid(row=2, column=0, columnspan=2, sticky="ew", pady=(8, 0))
        self.launch_bar = launch_bar
        self.split = LaunchSplitButton(launch_bar, self.manager.devices_dir, self._launch)
        self.split.frame.pack(side="right")

        self.scroll.bind_wheel(inner)

        self.empty_frame = ttk.Frame(outer)
        empty_center = ttk.Frame(self.empty_frame)
        empty_center.place(relx=0.5, rely=0.5, anchor="center")
        ttk.Label(empty_center, text="No ROMs yet",
                  font=("Segoe UI", 16, "bold")).pack()
        ttk.Button(empty_center, text="⬇  Download", style="Download.TButton",
                   command=self._open_download_window).pack(pady=(14, 0))

    def _update_empty_state(self) -> None:
        if any(d.is_installed for d in self.tree_panel.devices):
            self.empty_frame.pack_forget()
            self.paned.pack(fill="both", expand=True)
        else:
            self.paned.pack_forget()
            self.empty_frame.pack(fill="both", expand=True)

    def _set_busy(self, busy: bool, label: str = "") -> None:
        self.busy = busy
        self.tree_panel.set_busy(busy)
        self.toolbar.set_busy(busy)
        self.split.set_enabled(not busy)
        if busy:
            self.status_bar.set_status(label or "Working…")
        else:
            self.status_bar.set_status("Ready.")
            self.status_bar.reset_progress()
        self._refresh_selection_state()

    def _await_future(self, future: Future, done: Callable[[Optional[BaseException]], None]) -> None:
        def poll() -> None:
            if future.done():
                done(future.exception())
            else:
                self.after(50, poll)
        self.after(50, poll)

    def _progress_cb(self, label: str, done: int, total: Optional[int]) -> None:
        self.progress_queue.put((label, done, total))

    def _pump_progress(self) -> None:
        try:
            while True:
                label, done, total = self.progress_queue.get_nowait()
                self.status_bar.set_status(label)
                self.status_bar.show_progress(done, total)
        except queue.Empty:
            pass
        self.after(50, self._pump_progress)

    def _reload_device_list(self) -> None:
        self.tree_panel.reload(self.manager.list_devices())
        self._update_empty_state()
        self._on_tree_select(self.tree_panel.selection())

    def _running_status_for(self, d: Optional[DeviceBundle]):
        if d is None or not d.is_installed:
            return None
        return running_status(self.manager.devices_dir / d.name)

    def _poll_runtime(self) -> None:
        self.tree_panel.update_runtime()
        sel = self.tree_panel.selection()
        self.split.set_running(self._running_status_for(sel.device) is not None)
        self.preview.refresh()
        self._refresh_selection_state()
        self.after(2500, self._poll_runtime)

    def _install_theme_listener(self) -> None:
        self._old_wndproc = None
        if sys.platform != "win32":
            return
        try:
            self.update_idletasks()
            user32 = ctypes.windll.user32
            hwnd = user32.GetParent(self.winfo_id()) or self.winfo_id()
            self._theme_hwnd = hwnd
            lresult = ctypes.c_ssize_t
            self._wndproc_type = ctypes.WINFUNCTYPE(
                lresult, wintypes.HWND, wintypes.UINT,
                ctypes.c_size_t, ctypes.c_ssize_t)
            self._wndproc = self._wndproc_type(self._theme_wndproc)
            self._call_wndproc = user32.CallWindowProcW
            self._call_wndproc.restype = lresult
            self._call_wndproc.argtypes = [
                ctypes.c_void_p, wintypes.HWND, wintypes.UINT,
                ctypes.c_size_t, ctypes.c_ssize_t]
            set_long = user32.SetWindowLongPtrW
            set_long.restype = ctypes.c_void_p
            set_long.argtypes = [wintypes.HWND, ctypes.c_int, ctypes.c_void_p]
            self._old_wndproc = set_long(
                hwnd, _GWLP_WNDPROC,
                ctypes.cast(self._wndproc, ctypes.c_void_p))
        except (OSError, AttributeError):
            self._old_wndproc = None

    def _theme_wndproc(self, hwnd, msg, wparam, lparam):
        if msg == _WM_SETTINGCHANGE:
            self.after_idle(self._maybe_retheme)
        if not self._old_wndproc:
            return ctypes.windll.user32.DefWindowProcW(hwnd, msg, wparam, lparam)
        return self._call_wndproc(self._old_wndproc, hwnd, msg, wparam, lparam)

    def _maybe_retheme(self) -> None:
        if theme.refresh_palette():
            self._retheme()

    def _retheme(self) -> None:
        theme.apply_theme(self)
        theme.apply_titlebar(self)
        self.paned.config(bg=theme.BORDER)
        self.scroll.retheme()
        self.split.retheme()
        self.status_bar.retheme()
        self.preview.retheme(theme.BG)
        self.tree_panel.retheme()
        self.launch_options.refresh_resolution_state()

    def _open_download_window(self) -> None:
        if self.busy:
            return
        DownloadWindow(self, self.tree_panel.devices, self._download_queue,
                       resolve_icons_dir(), reload_fn=self._reload_download_sources,
                       download_places=self.manager.download_places)

    def _download_queue(self, names: List[str]) -> None:
        if self.busy or not names:
            return
        by_name = {d.name: d for d in self.tree_panel.devices}
        targets = [by_name[n] for n in names
                   if n in by_name and not by_name[n].is_installed]
        if not targets:
            return
        label = ((targets[0].meta.device_name or targets[0].name)
                 if len(targets) == 1 else f"{len(targets)} ROMs")
        if not confirm_rom_license(self, label):
            return
        show_sources_thanks(self, [d.meta.source for d in targets])
        stream = [d for d in targets if gate_large_bundle(self, d)]
        if not stream:
            self._reload_device_list()
            return
        self._set_busy(True, f"Downloading {len(stream)} item(s)…")
        work: List[Tuple[str, Callable[[], Future]]] = [
            (d.name, lambda name=d.name: self.manager.submit_install(
                name, progress=self._progress_cb, cancel_event=self.cancel_event))
            for d in stream]
        self._run_sequence(work)

    def _package_action(self, d: DeviceBundle, ps, action: str) -> None:
        if self.busy:
            return
        if action == "install":
            self._download_package(d, ps)
        elif action == "delete":
            self._delete_package(d, ps)

    def _discard_state(self) -> None:
        sel = self.tree_panel.selection()
        d = sel.device
        if self.busy or d is None or not d.is_installed:
            return
        device_dir = self.manager.devices_dir / d.name
        if saved_state_info(device_dir) is None:
            return
        name = d.meta.device_name or d.name
        if not ask_yesno(self, "Delete saved state",
                         f"Delete the saved state for {name}?\n"
                         f"This cannot be undone."):
            return
        try:
            (device_dir / STATE_IMAGE_FILENAME).unlink(missing_ok=True)
            (device_dir / SAVED_STATE_SCREENSHOT_FILENAME).unlink(missing_ok=True)
        except OSError as exc:
            show_error(self, "Delete failed",
                       f"Could not delete the saved state:\n{exc}")
        self._reload_device_list()

    def _open_device_folder(self, d: DeviceBundle) -> None:
        try:
            os.startfile(str(self.manager.devices_dir / d.name))
        except OSError as exc:
            show_error(self, "Open folder", str(exc))

    def _on_right_click(self, event: tk.Event) -> None:
        if self.busy:
            return
        sel = self.tree_panel.selection()
        if sel.kind != "device" or sel.device is None:
            return
        d = sel.device
        menu = tk.Menu(self, tearoff=0)
        running = self._running_status_for(d) is not None
        menu.add_command(label="Show CERF" if running else "Start", command=self._launch)
        if saved_state_info(self.manager.devices_dir / d.name) is not None:
            menu.add_command(label="Discard saved state",
                             command=self._discard_state)
        menu.add_command(label="Open device folder",
                         command=lambda: self._open_device_folder(d))
        if d.has_update:
            menu.add_command(label="Update", command=self._update_selected)
        menu.add_separator()
        menu.add_command(label="Remove", command=self._delete_selected)
        try:
            menu.tk_popup(event.x_root, event.y_root)
        finally:
            menu.grab_release()

    def _on_tree_select(self, sel: TreeSelection) -> None:
        if sel.kind == "device" and sel.device is not None:
            self.details.show_device(sel.device)
            self.launch_options.set_device(sel.device)
            self.split.set_device(sel.device)
            self.split.set_running(self._running_status_for(sel.device) is not None)
            self.preview.set_device(sel.device)
            self.launch_options.frame.grid()
            self.launch_bar.grid()
        self._refresh_selection_state()

    def _on_tree_activate(self, sel: TreeSelection) -> None:
        if sel.kind == "device":
            self._launch()

    def _refresh_selection_state(self) -> None:
        sel = self.tree_panel.selection()
        if self.busy:
            return
        d = sel.device
        if sel.kind == "device" and d is not None:
            can_discard = saved_state_info(
                self.manager.devices_dir / d.name) is not None
            self.toolbar.set_selection_enabled(d.has_update, d.is_installed,
                                               can_discard)
        else:
            self.toolbar.set_selection_enabled(False, False, False)
        self.split.set_enabled(d is not None and self.cerf_exe is not None)

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

    def _on_close(self) -> None:
        old = getattr(self, "_old_wndproc", None)
        if old and sys.platform == "win32":
            try:
                set_long = ctypes.windll.user32.SetWindowLongPtrW
                set_long.restype = ctypes.c_void_p
                set_long.argtypes = [wintypes.HWND, ctypes.c_int,
                                     ctypes.c_void_p]
                set_long(self._theme_hwnd, _GWLP_WNDPROC, old)
                self._old_wndproc = None
            except OSError:
                pass
        self.cancel_event.set()
        self.manager.shutdown()
        self.destroy()
