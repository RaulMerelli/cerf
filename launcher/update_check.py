from __future__ import annotations

import os
import threading
import webbrowser
from pathlib import Path
from typing import Optional

from app_paths import exe_dir, resolve_version
from bundles import RELEASE_LATEST_URL, parse_version_tuple
from github_release import GithubRelease
import upgrade_dialog
from ui_dialogs import show_dialog, show_error, show_update_available
from upgrade_download import download_upgrade
from upgrade_process import (INSTALL_FLAG, UPGRADE_DIR_NAME, UpgradeError,
                             running_cerf_pids, spawn_stage, stage_argument)
from upgrade_window import UpgradeWindow
import ui_theme as theme


class UpdateCheck:
    def __init__(self, app) -> None:
        self.app = app
        self.release: Optional[GithubRelease] = None
        self.fallback_version: Optional[str] = None

    def start(self) -> None:
        self.app.status_bar.set_update_status("Checking updates…", theme.FG_DIM,
                                              link=False)
        future = self.app.manager.submit_release_check()

        def done(exc: Optional[BaseException]) -> None:
            if exc is not None:
                self._start_fallback()
                return
            self._apply_release(future.result())

        self.app._await_future(future, done)

    def _start_fallback(self) -> None:
        future = self.app.manager.submit_version_check()

        def done(exc: Optional[BaseException]) -> None:
            self._apply_fallback(None if exc is not None else future.result())

        self.app._await_future(future, done)

    def _is_newer(self, version: Optional[str]) -> bool:
        current = parse_version_tuple(resolve_version())
        remote = parse_version_tuple(version) if version else None
        return current is not None and remote is not None and remote > current

    def _announce(self, version: str) -> None:
        self.app.status_bar.set_update_status(
            f"CERF {version} is available", theme.UPDATE_LINK, link=True,
            on_click=self.open_offer)

    def _no_update(self, checked: bool) -> None:
        text = "No new releases of CERF available" if checked else ""
        self.app.status_bar.set_update_status(text, theme.FG_DIM, link=False)

    def _apply_release(self, release: GithubRelease) -> None:
        if not self._is_newer(release.tag):
            self._no_update(True)
            return
        self.release = release
        self._announce(release.tag)
        self.open_offer()

    def _apply_fallback(self, version: Optional[str]) -> None:
        if not self._is_newer(version):
            self._no_update(version is not None)
            return
        self.fallback_version = version
        self._announce(version)
        show_update_available(self.app, f"v{version}", RELEASE_LATEST_URL)

    def open_offer(self) -> None:
        if self.release is None:
            webbrowser.open(RELEASE_LATEST_URL)
            return
        choice = upgrade_dialog.show_release_available(self.app, self.release)
        if choice == upgrade_dialog.UPGRADE:
            self._start_upgrade()
        elif choice == upgrade_dialog.BROWSER:
            webbrowser.open(self.release.html_url)

    def _cerf_is_clear(self) -> bool:
        while True:
            try:
                pids = running_cerf_pids()
            except UpgradeError as exc:
                show_error(self.app, "Upgrade", str(exc))
                return False
            if not pids:
                return True
            answer = show_dialog(
                self.app, "cerf.exe is running",
                "CERF is running and its files cannot be replaced while it is.\n\n"
                "Close every CERF window, then retry.",
                ("Retry", "Cancel"), default="Cancel")
            if answer != "Retry":
                return False

    def _start_upgrade(self) -> None:
        if self.release is None or not self._cerf_is_clear():
            return
        install_dir = exe_dir()
        window = UpgradeWindow(
            self.app, f"Upgrading to CERF {self.release.tag}",
            lambda exc: self._download_finished(exc, window, install_dir))
        threading.Thread(target=self._download, args=(window, install_dir),
                         daemon=True).start()

    def _download(self, window: UpgradeWindow, install_dir: Path) -> None:
        try:
            download_upgrade(self.release, install_dir, window.post_log,
                             window.post_progress)
        except BaseException as exc:
            window.post_result(exc)
            return
        window.post_result(None)

    def _download_finished(self, error: Optional[BaseException],
                           window: UpgradeWindow, install_dir: Path) -> None:
        if error is None:
            staged = install_dir / UPGRADE_DIR_NAME
            try:
                spawn_stage(staged / "launcher.exe",
                            stage_argument(os.getpid(), INSTALL_FLAG), staged)
            except UpgradeError as exc:
                error = exc
        if error is not None:
            window.destroy()
            show_error(self.app, "Upgrade failed",
                       f"{error}\n\nYour installation is untouched.")
            return
        self.app._on_close()
