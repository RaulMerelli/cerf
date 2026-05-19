"""IDAPython startup script: wait for auto-analysis, save DB, start ida_server.
Used by open_ida.py with IDA's -A (autonomous) flag for unattended opening."""

import ctypes
import os
import ida_auto
import ida_loader
import idaapi

# Wait for auto-analysis to finish
ida_auto.auto_wait()

# Save database immediately (protect against crashes)
idb_path = ida_loader.get_path(ida_loader.PATH_TYPE_IDB)
if idb_path:
    ida_loader.save_database(idb_path, 0)

# Minimize IDA window — we only need the server
# Window may appear with a delay, so retry several times
import time
import ctypes.wintypes as wt
_pid = os.getpid()
_WNDENUMPROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

for _attempt in range(5):
    _found_unminimized = False
    def _minimize_own_windows(hwnd, _):
        global _found_unminimized
        pid = wt.DWORD()
        ctypes.windll.user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if pid.value == _pid and ctypes.windll.user32.IsWindowVisible(hwnd):
            # Skip already-minimized windows (IsIconic returns nonzero if minimized)
            if not ctypes.windll.user32.IsIconic(hwnd):
                ctypes.windll.user32.ShowWindow(hwnd, 6)  # SW_MINIMIZE
                _found_unminimized = True
        return True
    ctypes.windll.user32.EnumWindows(_WNDENUMPROC(_minimize_own_windows), 0)
    if _found_unminimized:
        pass  # found and minimized something, but keep retrying for late windows
    time.sleep(5)

# Now start the HTTP API server
server_script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ida_server.py")
with open(server_script) as f:
    exec(compile(f.read(), server_script, "exec"))
