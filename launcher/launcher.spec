# -*- mode: python ; coding: utf-8 -*-
from pathlib import Path
import glob
import os
import sys
import zipfile

THIS_DIR = Path(os.path.abspath(SPEC)).parent
REPO_ROOT = THIS_DIR.parent
ICON_PATH    = str(REPO_ROOT / "cerf" / "assets" / "launcher.ico")
VERSION_PATH = str(REPO_ROOT / "cerf" / "version.h")

# Tcl/Tk 9 embeds its script libraries as a zip appended to the Tcl/Tk DLLs
# (zipfs); python-build-standalone ships no on-disk copy. PyInstaller's tkinter
# hook reads `info library`, gets a //zipfs:/ path, finds no on-disk dir, and
# collects nothing -- its runtime hook then aborts looking for _tcl_data.
# Extract the libraries straight out of the DLLs (each is a valid zip) so the
# stock runtime hook finds _tcl_data/_tk_data and sets TCL_LIBRARY/TK_LIBRARY.
TCLTK_DATAS = []
_stage = str(THIS_DIR / "build" / "_tcltk_stage")
for _dll in glob.glob(os.path.join(sys.base_prefix, "DLLs", "*.dll")):
    if not zipfile.is_zipfile(_dll):
        continue
    _z = zipfile.ZipFile(_dll)
    _tops = {n.split("/")[0] for n in _z.namelist() if "/" in n}
    if "tcl_library" in _tops:
        _z.extractall(os.path.join(_stage, "tcl"))
        TCLTK_DATAS.append((os.path.join(_stage, "tcl", "tcl_library"), "_tcl_data"))
    elif "tk_library" in _tops:
        _z.extractall(os.path.join(_stage, "tk"))
        TCLTK_DATAS.append((os.path.join(_stage, "tk", "tk_library"), "_tk_data"))

block_cipher = None

a = Analysis(
    [str(THIS_DIR / "launcher.py")],
    pathex=[str(THIS_DIR)],
    binaries=[],
    datas=[(ICON_PATH, "."), (VERSION_PATH, "."),
           (str(THIS_DIR / "assets" / "icons"), "assets/icons"),
           (str(THIS_DIR / "assets" / "GaBanner.png"), "assets")] + TCLTK_DATAS,
    hiddenimports=[],
    hookspath=[],
    runtime_hooks=[],
    excludes=["numpy", "scipy", "pandas", "matplotlib"],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=block_cipher,
    noarchive=False,
)
pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.zipfiles,
    a.datas,
    [],
    name="launcher",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon=ICON_PATH,
)
