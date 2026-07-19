from __future__ import annotations

from concurrent.futures import Future
from typing import Callable, List, Optional, Tuple

from device_state import DeviceBundle, PackageStatus
from bundle_download import CancelledError
from ui_dialogs import (ask_yesno, confirm_rom_license, show_dialog,
                        show_error, show_info)
from ui_large_download import (filter_update_all_targets, gate_bundle_download,
                               gate_package_download)


class OperationsMixin:
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
        f = self.manager.submit_install(d, progress=self._progress_cb,
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

    def _update(self) -> None:
        if self.busy:
            return
        sel = self.tree_panel.selection()
        d = sel.device
        rom_update_devices = [x for x in self.tree_panel.devices if x.has_update]
        if not (sel.kind == "device" and d is not None and d.has_update):
            self._update_all()
            return
        others = [x for x in rom_update_devices if x is not d]
        if not others:
            self._download_selected()
            return
        n = len(rom_update_devices)
        more = len(others)
        all_label = f"Update {n} devices"
        one_label = "Update only this device"
        choice = show_dialog(
            self, "Update bundles",
            f"Except this device there are {more} more "
            f"{'devices' if more != 1 else 'device'} ready to update. "
            f"Would you like to update all or only this one?",
            (all_label, one_label, "Cancel"), default="Cancel")
        if choice == one_label:
            self._download_selected()
        elif choice == all_label:
            self._update_all()

    def _delete_package(self, d: DeviceBundle, ps: PackageStatus) -> None:
        if self.busy:
            return
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
            work.append((d.name, lambda dev=d: self.manager.submit_install(
                dev, progress=self._progress_cb, cancel_event=self.cancel_event)))
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
                else:
                    self._reload_device_list()
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
