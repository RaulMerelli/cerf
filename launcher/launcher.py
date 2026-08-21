#!/usr/bin/env python3
from __future__ import annotations

import sys
import traceback
from pathlib import Path
from typing import List

_THIS_DIR = Path(__file__).resolve().parent
if str(_THIS_DIR) not in sys.path:
    sys.path.insert(0, str(_THIS_DIR))

from app_paths import resolve_cerf_exe, resolve_devices_dir
from cli_console import attach_parent_console
from launcher_cli import run_cli
from operations import BundleManager
from transactional import TRANSACTIONAL_COMMAND, run_transactional
from ui_theme import enable_dpi_awareness
from upgrade_cli import (INSTALL_STAGE, POST_UPGRADE_STAGE, parse_stage,
                         run_install_stage, run_post_upgrade)


def main(argv: List[str]) -> int:
    stage, wait_pid = parse_stage(argv)
    if stage == INSTALL_STAGE:
        enable_dpi_awareness()
        return run_install_stage(wait_pid)

    upgraded = stage == POST_UPGRADE_STAGE
    if upgraded:
        enable_dpi_awareness()
        run_post_upgrade(wait_pid)

    if bool(argv) and argv[0] == TRANSACTIONAL_COMMAND:
        enable_dpi_awareness()
        return run_transactional(argv[1:])

    cli = bool(argv) and argv[0] == "sync"
    if cli:
        attach_parent_console()

    devices_dir = resolve_devices_dir()
    if not devices_dir.exists():
        try:
            devices_dir.mkdir(parents=True, exist_ok=True)
        except OSError as exc:
            print(f"ERROR: cannot create {devices_dir}: {exc}", file=sys.stderr)
            return 1

    if cli:
        return run_cli(devices_dir, argv[1:])

    enable_dpi_awareness()

    # Imported lazily so `launcher sync ...` works on hosts without a display.
    from launcher_app import LauncherApp

    manager = BundleManager(devices_dir)
    cerf_exe = resolve_cerf_exe()
    app = LauncherApp(manager, cerf_exe, upgraded=upgraded)
    try:
        app.mainloop()
    except Exception:
        traceback.print_exc()
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
