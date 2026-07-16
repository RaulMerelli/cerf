"""Each stage waits for its predecessor to exit: Windows holds a running image
locked, so the files it occupies cannot be replaced or removed until it dies."""
from __future__ import annotations

import os
import shutil
import threading
import tkinter as tk
from pathlib import Path
from typing import List, Optional, Tuple

from app_paths import exe_dir
from ui_dialogs import show_dialog, show_error
from upgrade_install import install_upgrade
from upgrade_process import (INSTALL_FLAG, POST_UPGRADE_FLAG, UPGRADE_DIR_NAME,
                             WAIT_FOR_PID_PREFIX, UpgradeError,
                             find_pid_argument, launcher_exe_in, spawn_stage,
                             stage_argument, wait_for_pid_exit)
from upgrade_window import UpgradeWindow
import ui_theme as theme


NO_STAGE = "none"
INSTALL_STAGE = "install"
POST_UPGRADE_STAGE = "post-upgrade"


def parse_stage(argv: List[str]) -> Tuple[str, Optional[int]]:
    pid = find_pid_argument(argv, WAIT_FOR_PID_PREFIX)
    if INSTALL_FLAG in argv:
        return INSTALL_STAGE, pid
    if POST_UPGRADE_FLAG in argv:
        return POST_UPGRADE_STAGE, pid
    return NO_STAGE, None


def _hidden_root() -> tk.Tk:
    root = tk.Tk()
    root.withdraw()
    theme.apply_theme(root)
    return root


def _install(window: UpgradeWindow, wait_pid: Optional[int],
             upgrade_dir: Path, install_dir: Path) -> None:
    try:
        if wait_pid is not None:
            window.post_log(f"Waiting for the previous launcher (pid {wait_pid})")
            wait_for_pid_exit(wait_pid)
        install_upgrade(upgrade_dir, install_dir, window.post_log,
                        window.ask_retry)
        window.post_log("Restarting the upgraded launcher")
        spawn_stage(launcher_exe_in(install_dir),
                    stage_argument(os.getpid(), POST_UPGRADE_FLAG), install_dir)
    except BaseException as exc:
        window.post_result(exc)
        return
    window.post_result(None)


def run_install_stage(wait_pid: Optional[int]) -> int:
    root = _hidden_root()
    upgrade_dir = exe_dir()
    install_dir = upgrade_dir.parent
    status = {"code": 0}

    def finish(error: Optional[BaseException]) -> None:
        if error is not None:
            status["code"] = 1
            show_dialog(window, "Upgrade failed",
                        f"{error}\n\nYour installation of CERF may now be "
                        f"corrupted: some files were replaced and some were "
                        f"not. Re-download CERF from "
                        f"https://github.com/gweslab/cerf/releases/latest")
        window.destroy()
        root.destroy()

    window = UpgradeWindow(root, "Installing CERF", finish)
    threading.Thread(target=_install,
                     args=(window, wait_pid, upgrade_dir, install_dir),
                     daemon=True).start()
    root.mainloop()
    return status["code"]


def run_post_upgrade(wait_pid: Optional[int]) -> None:
    root = _hidden_root()
    try:
        if wait_pid is not None:
            wait_for_pid_exit(wait_pid)
    except UpgradeError as exc:
        show_error(root, "Upgrade", str(exc))
        root.destroy()
        raise SystemExit(1)
    staged = exe_dir() / UPGRADE_DIR_NAME
    try:
        if staged.is_dir():
            shutil.rmtree(staged)
    except OSError as exc:
        show_error(root, "Upgrade",
                   f"CERF was upgraded, but {staged} could not be removed:\n"
                   f"{exc}\n\nDelete it by hand.")
    root.destroy()
