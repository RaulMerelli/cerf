from __future__ import annotations

import ctypes
import subprocess
import time
from ctypes import wintypes
from pathlib import Path
from typing import List, Optional


CERF_EXE_NAME = "cerf.exe"
UPGRADE_DIR_NAME = "upgrade"
UPGRADE_ZIP_NAME = "upgrade.zip"
GLOBAL_CONFIG_NAME = "cerf.json"
PID_WAIT_TIMEOUT = 30.0

INSTALL_FLAG = "--upgrade"
POST_UPGRADE_FLAG = "--post-upgrade"
WAIT_FOR_PID_PREFIX = "--wait-for-pid="

_TH32CS_SNAPPROCESS = 0x00000002
_SYNCHRONIZE = 0x00100000
_WAIT_TIMEOUT = 0x00000102
_ERROR_ACCESS_DENIED = 5
_INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value


class UpgradeError(RuntimeError):
    pass


class _ProcessEntry32W(ctypes.Structure):
    _fields_ = [
        ("dwSize", wintypes.DWORD),
        ("cntUsage", wintypes.DWORD),
        ("th32ProcessID", wintypes.DWORD),
        ("th32DefaultHeapID", ctypes.POINTER(ctypes.c_ulong)),
        ("th32ModuleID", wintypes.DWORD),
        ("cntThreads", wintypes.DWORD),
        ("th32ParentProcessID", wintypes.DWORD),
        ("pcPriClassBase", ctypes.c_long),
        ("dwFlags", wintypes.DWORD),
        ("szExeFile", wintypes.WCHAR * 260),
    ]


def running_cerf_pids() -> List[int]:
    kernel32 = ctypes.windll.kernel32
    snapshot = kernel32.CreateToolhelp32Snapshot(_TH32CS_SNAPPROCESS, 0)
    if snapshot == _INVALID_HANDLE_VALUE:
        raise UpgradeError("cannot enumerate running processes")
    entry = _ProcessEntry32W()
    entry.dwSize = ctypes.sizeof(_ProcessEntry32W)
    pids: List[int] = []
    try:
        if not kernel32.Process32FirstW(snapshot, ctypes.byref(entry)):
            return pids
        while True:
            if entry.szExeFile.lower() == CERF_EXE_NAME:
                pids.append(int(entry.th32ProcessID))
            if not kernel32.Process32NextW(snapshot, ctypes.byref(entry)):
                return pids
    finally:
        kernel32.CloseHandle(snapshot)


def _pid_alive(pid: int) -> bool:
    kernel32 = ctypes.windll.kernel32
    handle = kernel32.OpenProcess(_SYNCHRONIZE, False, wintypes.DWORD(pid))
    if not handle:
        return kernel32.GetLastError() == _ERROR_ACCESS_DENIED
    try:
        return kernel32.WaitForSingleObject(handle, 0) == _WAIT_TIMEOUT
    finally:
        kernel32.CloseHandle(handle)


def wait_for_pid_exit(pid: int, timeout: float = PID_WAIT_TIMEOUT) -> None:
    deadline = time.monotonic() + timeout
    while _pid_alive(pid):
        if time.monotonic() >= deadline:
            raise UpgradeError(
                f"the previous CERF launcher (pid {pid}) is still running after "
                f"{int(timeout)}s; its files cannot be replaced")
        time.sleep(0.05)


def spawn_stage(exe: Path, args: List[str], cwd: Path) -> None:
    argv = [str(exe)] + args
    try:
        subprocess.Popen(argv, cwd=str(cwd),
                         creationflags=getattr(subprocess, "DETACHED_PROCESS", 0))
    except OSError as exc:
        raise UpgradeError(f"cannot start {exe}: {exc}") from exc


def stage_argument(wait_pid: int, mode_flag: str) -> List[str]:
    return [f"--wait-for-pid={wait_pid}", mode_flag]


def find_pid_argument(argv: List[str], prefix: str) -> Optional[int]:
    for arg in argv:
        if arg.startswith(prefix):
            value = arg[len(prefix):]
            if value.isdigit():
                return int(value)
    return None
