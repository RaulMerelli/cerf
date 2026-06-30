"""LauncherApp: window composition and operation orchestration - manifest
refresh, release check, device & package download/update/delete, launch."""
from __future__ import annotations

import queue
import subprocess
import threading
import tkinter as tk
from concurrent.futures import Future
from pathlib import Path
from tkinter import ttk
from typing import Callable, List, Optional, Tuple

from app_paths import resolve_icon, resolve_icons_dir, resolve_version
from bundles import ManifestVersionError, RELEASE_LATEST_URL, parse_version_tuple
from device_state import DeviceBundle, PackageStatus
from device_tree import DeviceTreePanel, TreeSelection
from details_panel import DetailsPanel
from launch_button import LaunchSplitButton
from launch_options import LaunchOptionsPanel
from operations import BundleManager, CancelledError
from status_bar import StatusBar
from ui_dialogs import ask_yesno, confirm_rom_license, show_error, show_info
from ui_large_download import (filter_update_all_targets, gate_bundle_download,
                               gate_package_download)
from ui_scroll import ScrollColumn
from ui_theme import FG_DIM, UPDATE_LINK, apply_dark_theme, enable_dark_titlebar


class LauncherApp(tk.Tk):
    def __init__(self, manager: BundleManager, cerf_exe: Optional[Path]):
        super().__init__()
        self.manager = manager
        self.cerf_exe = cerf_exe
        version = resolve_version()
        self.title(f"CERF {version} Launcher" if version else "CERF Launcher")

        try:
            dpi = float(self.winfo_fpixels("1i"))
            self.tk.call("tk", "scaling", dpi / 72.0)
        except tk.TclError:
            dpi = 96.0

        scale = max(1.0, dpi / 96.0)
        self.geometry(f"{int(1180 * scale)}x{int(620 * scale)}")
        self.minsize(int(1000 * scale), int(520 * scale))

        icon = resolve_icon()
        if icon is not None:
            try:
                self.iconbitmap(str(icon))
            except tk.TclError:
                pass

        apply_dark_theme(self)

        self.progress_queue: "queue.Queue[tuple[str, int, Optional[int]]]" = queue.Queue()
        self.cancel_event = threading.Event()
        self.busy = False

        self._build_ui()
        enable_dark_titlebar(self)
        self._pump_progress()
        self.after(50, self._refresh_manifest)
        self.after(50, self._start_update_check)

        self.protocol("WM_DELETE_WINDOW", self._on_close)

    def _build_ui(self) -> None:
        # Status bar is packed first (side=bottom) so it is always reserved at
        # the window's bottom edge; the expanding content fills the rest above.
        self.status_bar = StatusBar(self)

        outer = ttk.Frame(self, padding=8)
        outer.pack(fill="both", expand=True)
        outer.columnconfigure(0, weight=1, minsize=650)
        outer.columnconfigure(1, weight=0, minsize=300)
        outer.rowconfigure(0, weight=1)

        self.tree_panel = DeviceTreePanel(
            outer,
            on_select=self._on_tree_select,
            on_activate=self._on_tree_activate,
            on_refresh=self._refresh_manifest,
            on_update_all=self._update_all,
            icons_dir=resolve_icons_dir())

        right = ttk.Frame(outer)
        right.grid(row=0, column=1, sticky="nsew")
        right.columnconfigure(0, weight=1)
        right.rowconfigure(0, weight=1)  # scroll area expands; launch bar pinned

        def on_width(width: int) -> None:
            self.details.set_wraplength(max(120, width - 24))
        self.scroll = ScrollColumn(right, width=300, on_width_changed=on_width)
        self.scroll.grid(row=0, column=0, sticky="nsew")

        inner = self.scroll.inner
        self.details = DetailsPanel(inner, resolve_icons_dir(), self.manager.devices_dir,
                                    bind_wheel=self.scroll.bind_wheel, on_state_changed=lambda: self.split.refresh())

        actions = ttk.LabelFrame(inner, text="Bundle actions", padding=8)
        actions.grid(row=3, column=0, sticky="ew", pady=(0, 8))
        actions.columnconfigure(0, weight=1)
        actions.columnconfigure(1, weight=1)
        actions.columnconfigure(2, weight=1)
        self.actions_frame = actions
        self.btn_download = ttk.Button(actions, text="Download", command=self._download_selected,
                                       style="Accent.TButton")
        self.btn_update   = ttk.Button(actions, text="Update",   command=self._update_selected,
                                       style="Accent.TButton")
        self.btn_delete   = ttk.Button(actions, text="Delete",   command=self._delete_selected,
                                       style="Danger.TButton")
        self.btn_download.grid(row=0, column=0, sticky="ew", padx=(0, 4))
        self.btn_update.grid  (row=0, column=1, sticky="ew", padx=4)
        self.btn_delete.grid  (row=0, column=2, sticky="ew", padx=(4, 0))

        self.launch_options = LaunchOptionsPanel(inner, self, self.manager.devices_dir, row=6)

        # Launch bar is pinned below the scroll area (right row 1), so it stays
        # visible no matter how far the options scroll or how short the window.
        launch_bar = ttk.Frame(right)
        launch_bar.grid(row=1, column=0, columnspan=2, sticky="ew", pady=(8, 0))
        self.launch_bar = launch_bar
        self.split = LaunchSplitButton(launch_bar, self.manager.devices_dir,
                                       self._launch)
        self.split.frame.pack(side="right")

        self.scroll.bind_wheel(inner)

    # ---------------------------------------------------------------- busy

    def _set_busy(self, busy: bool, label: str = "") -> None:
        self.busy = busy
        state = "disabled" if busy else "normal"
        self.tree_panel.set_busy(busy)
        for b in (self.btn_download, self.btn_update, self.btn_delete):
            b.config(state=state)
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
                self.status_bar.set_status(label)
                self.status_bar.show_progress(done, total)
        except queue.Empty:
            pass
        self.after(50, self._pump_progress)

    # ------------------------------------------------------------ manifest

    def _refresh_manifest(self) -> None:
        if self.busy:
            return
        self._set_busy(True, "Fetching manifest…")
        future = self.manager.submit_refresh()
        def done(exc: Optional[BaseException]) -> None:
            self._set_busy(False)
            if isinstance(exc, ManifestVersionError):
                self._show_manifest_version_error(exc)
            elif exc is not None:
                show_error(
                    self,
                    "Remote manifest unavailable",
                    f"{exc}\n\n"
                    f"Local devices remain available to launch. Download / "
                    f"update / package fetch require a reachable remote "
                    f"manifest - try again later or check your network."
                )
            self._reload_device_list()
        self._await_future(future, done)

    def _show_manifest_version_error(self, exc: ManifestVersionError) -> None:
        if exc.remote_is_newer:
            show_info(
                self,
                "A newer CERF build is required",
                f"The bundle catalog on the server uses a newer format "
                f"(manifest version {exc.remote_version}) than this CERF build "
                f"understands (version {exc.supported_version}).\n\n"
                f"Download a newer CERF build to fetch or update ROM bundles:\n"
                f"https://github.com/gweslab/cerf\n"
                f"  • Releases - latest stable build\n"
                f"  • Actions artifacts - newest CI build\n\n"
                f"Your already-installed devices remain available to launch."
            )
        else:
            show_error(
                self,
                "Remote manifest outdated",
                f"The server's bundle catalog (manifest version "
                f"{exc.remote_version}) is older than this CERF build expects "
                f"(version {exc.supported_version}). This is usually "
                f"temporary - try again later.\n\n"
                f"Your already-installed devices remain available to launch."
            )

    def _start_update_check(self) -> None:
        self.status_bar.set_update_status("Checking updates…", FG_DIM, link=False)
        future = self.manager.submit_version_check()
        def done(exc: Optional[BaseException]) -> None:
            remote: Optional[str] = None
            if exc is None:
                try:
                    remote = future.result()
                except Exception:
                    remote = None
            self._apply_update_check(remote)
        self._await_future(future, done)

    def _apply_update_check(self, remote: Optional[str]) -> None:
        current = parse_version_tuple(resolve_version())
        remote_tuple = parse_version_tuple(remote) if remote else None
        # Stay silent when either side is unknown (offline, missing file,
        # unparseable) rather than asserting a state we cannot verify.
        if remote_tuple is None or current is None:
            self.status_bar.set_update_status("", FG_DIM, link=False)
            return
        if remote_tuple > current:
            self.status_bar.set_update_status(
                f"Download CERF v{remote}", UPDATE_LINK, link=True,
                url=RELEASE_LATEST_URL)
        else:
            self.status_bar.set_update_status(
                "No new releases of CERF available", FG_DIM, link=False)

    def _reload_device_list(self) -> None:
        self.tree_panel.reload(self.manager.list_devices())
        self._on_tree_select(self.tree_panel.selection())

    # ----------------------------------------------------------- selection

    def _on_tree_select(self, sel: TreeSelection) -> None:
        if sel.kind == "device" and sel.device is not None:
            self.details.show_device(sel.device)
            self.launch_options.set_device(sel.device)
            self.split.set_device(sel.device)
            self._show_launch_surface(True)
        elif sel.kind == "package" and sel.device is not None and sel.package is not None:
            # A package is not launchable: the panel shows only the package
            # info and its Download/Update/Delete actions.
            self.details.show_package(sel.device, sel.package)
            self._show_launch_surface(False)
        self._refresh_selection_state()

    def _show_launch_surface(self, visible: bool) -> None:
        if visible:
            self.actions_frame.config(text="Bundle actions")
            self.launch_options.frame.grid()
            self.launch_bar.grid()
        else:
            self.actions_frame.config(text="Package actions")
            self.launch_options.frame.grid_remove()
            self.launch_bar.grid_remove()

    def _on_tree_activate(self, sel: TreeSelection) -> None:
        if sel.kind == "device":
            self._launch()
        elif sel.kind == "package":
            # Double-click on a package fetches it (download or update).
            if sel.package is not None and (not sel.package.installed
                                            or sel.package.has_update):
                self._download_selected()

    def _refresh_selection_state(self) -> None:
        sel = self.tree_panel.selection()
        if self.busy:
            return
        d = sel.device
        if sel.kind == "device" and d is not None:
            self.btn_download.config(state=("normal" if (not d.is_installed and d.remote) else "disabled"))
            self.btn_update  .config(state=("normal" if d.has_update else "disabled"))
            self.btn_delete  .config(state=("normal" if d.is_installed else "disabled"))
        elif sel.kind == "package" and d is not None and sel.package is not None:
            ps = sel.package
            self.btn_download.config(state=("normal" if (d.is_installed and not ps.installed) else "disabled"))
            self.btn_update  .config(state=("normal" if ps.has_update else "disabled"))
            self.btn_delete  .config(state=("normal" if ps.installed else "disabled"))
        else:
            for b in (self.btn_download, self.btn_update, self.btn_delete):
                b.config(state="disabled")
        launchable = d is not None and self.cerf_exe is not None
        self.split.set_enabled(launchable)

    # ---------------------------------------------------------- operations

    def _download_selected(self) -> None:
        sel = self.tree_panel.selection()
        if self.busy or sel.device is None:
            return
        if sel.kind == "package" and sel.package is not None:
            self._download_package(sel.device, sel.package)
            return
        d = sel.device
        if d.remote is None:
            return
        if not gate_bundle_download(self, d):
            return
        self._set_busy(True, f"Downloading {d.name}…")
        f = self.manager.submit_install(d.name, progress=self._progress_cb,
                                        cancel_event=self.cancel_event)
        self._await_future(f, lambda exc: self._after_op(exc, f"Downloaded {d.name}"))

    def _download_package(self, d: DeviceBundle, ps: PackageStatus) -> None:
        if not d.is_installed:
            show_error(self, "Cannot download package",
                       f"{d.name} is not installed; install the device first.")
            return
        if not gate_package_download(self, d, ps):
            return
        self._set_busy(True, f"Downloading {ps.remote.name}…")
        f = self.manager.submit_install_package(
            d.name, ps.remote.category, ps.remote.key,
            progress=self._progress_cb, cancel_event=self.cancel_event)
        self._await_future(f, lambda exc: self._after_op(
            exc, f"Installed {ps.remote.name} for {d.name}"))

    def _update_selected(self) -> None:
        self._download_selected()

    def _delete_selected(self) -> None:
        sel = self.tree_panel.selection()
        if self.busy or sel.device is None:
            return
        d = sel.device
        if sel.kind == "package" and sel.package is not None:
            ps = sel.package
            if not ask_yesno(self, "Delete package",
                             f"Remove {ps.remote.name} "
                             f"(devices/{d.name}/{ps.remote.key})?\n"
                             f"This cannot be undone."):
                return
            self._set_busy(True, f"Deleting {ps.remote.name}…")
            f = self.manager.submit_delete_package(d.name, ps.remote.category,
                                                   ps.remote.key)
            self._await_future(f, lambda exc: self._after_op(
                exc, f"Deleted {ps.remote.name}"))
            return
        if not ask_yesno(self, "Delete device",
                         f"Remove devices/{d.name}/ and its files?\nThis cannot be undone."):
            return
        self._set_busy(True, f"Deleting {d.name}…")
        f = self.manager.submit_delete(d.name)
        self._await_future(f, lambda exc: self._after_op(exc, f"Deleted {d.name}"))

    def _update_all(self) -> None:
        if self.busy:
            return
        devices = self.tree_panel.devices
        rom_targets = [d for d in devices if d.has_update]
        pkg_targets: List[Tuple[DeviceBundle, PackageStatus]] = [
            (d, ps) for d in devices for ps in d.packages if ps.has_update]
        if not rom_targets and not pkg_targets:
            show_info(self, "Update all",
                      "All installed bundles and packages are up to date.")
            return
        filtered = filter_update_all_targets(self, rom_targets, pkg_targets)
        if filtered is None:
            return
        rom_targets, pkg_targets = filtered
        total = len(rom_targets) + len(pkg_targets)
        if not total:
            show_info(self, "Update all",
                      "Only large bundles need updating - update each from the "
                      "table individually.")
            return
        if not confirm_rom_license(self, f"{total} item(s)"):
            return
        self._set_busy(True, f"Updating {total} item(s)…")
        work: List[Tuple[str, Callable[[], Future]]] = []
        for d in rom_targets:
            work.append((d.name, lambda name=d.name: self.manager.submit_install(
                name, progress=self._progress_cb, cancel_event=self.cancel_event)))
        for d, ps in pkg_targets:
            work.append((f"{d.name}: {ps.remote.name}",
                         lambda name=d.name, c=ps.remote.category, k=ps.remote.key:
                         self.manager.submit_install_package(
                             name, c, k, progress=self._progress_cb,
                             cancel_event=self.cancel_event)))
        self._run_sequence(work)

    def _run_sequence(self, work: List[Tuple[str, Callable[[], Future]]]) -> None:
        errors: List[tuple[str, BaseException]] = []
        def step(idx: int) -> None:
            if idx >= len(work):
                self._set_busy(False)
                if errors:
                    summary = "\n".join(f"{n}: {e}" for n, e in errors)
                    show_error(self, "Sequence completed with errors", summary)
                self._reload_device_list()
                return
            label, submit = work[idx]
            self.status_bar.set_status(f"[{idx+1}/{len(work)}] {label}…")
            def finished(exc: Optional[BaseException]) -> None:
                if exc is not None and not isinstance(exc, CancelledError):
                    errors.append((label, exc))
                step(idx + 1)
            self._await_future(submit(), finished)
        step(0)

    def _after_op(self, exc: Optional[BaseException], success_msg: str) -> None:
        self._set_busy(False)
        if exc is not None:
            if isinstance(exc, CancelledError):
                self.status_bar.set_status("Cancelled.")
            else:
                show_error(self, "Operation failed", str(exc))
                self.status_bar.set_status("Error.")
        else:
            self.status_bar.set_status(success_msg)
        self._reload_device_list()

    # -------------------------------------------------------------- launch

    def _launch(self, boot: Optional[str] = None) -> None:
        sel = self.tree_panel.selection()
        d = sel.device
        if d is None or self.busy:
            return
        if self.cerf_exe is None:
            show_error(self, "Cannot launch",
                       "cerf.exe not found next to launcher.exe.")
            return
        if not d.is_installed:
            if d.remote is None:
                show_error(self, "Cannot launch",
                           f"{d.name} is not installed and no remote bundle "
                           f"is available to download.")
                return
            if not gate_bundle_download(self, d):
                return
            name = d.name
            self._set_busy(True, f"Downloading {name}…")
            f = self.manager.submit_install(name, progress=self._progress_cb,
                                            cancel_event=self.cancel_event)
            self._await_future(f, lambda exc: self._after_download_for_launch(name, exc))
            return
        self._spawn_cerf(d, boot)

    def _after_download_for_launch(self, name: str,
                                   exc: Optional[BaseException]) -> None:
        self._set_busy(False)
        if exc is not None:
            if isinstance(exc, CancelledError):
                self.status_bar.set_status("Cancelled.")
            else:
                show_error(self, "Download failed", str(exc))
                self.status_bar.set_status("Error.")
            self._reload_device_list()
            return
        self._reload_device_list()
        fresh = next((x for x in self.tree_panel.devices if x.name == name), None)
        if fresh is None or not fresh.is_installed:
            show_error(self, "Launch failed",
                       f"download of {name} reported success but the "
                       f"device is not marked installed.")
            return
        self.status_bar.set_status(f"Downloaded {name}; launching…")
        self._spawn_cerf(fresh)

    def _spawn_cerf(self, d: DeviceBundle, boot: Optional[str] = None) -> None:
        tail = self.launch_options.collect_args(d)
        if tail is None:
            return
        # Always explicit so a dev build (which defaults to cold) still resumes
        # on a normal launch; the dropdown overrides with warm/cold.
        tail.append(f"--boot={boot or 'resume'}")
        argv: List[str] = [str(self.cerf_exe)] + tail
        try:
            subprocess.Popen(argv, cwd=str(self.cerf_exe.parent),
                             creationflags=getattr(subprocess, "DETACHED_PROCESS", 0))
            self.status_bar.set_status(f"Launched cerf.exe for {d.name}.")
        except OSError as exc:
            show_error(self, "Launch failed", str(exc))

    def _on_close(self) -> None:
        self.cancel_event.set()
        self.manager.shutdown()
        self.destroy()
