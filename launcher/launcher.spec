# -*- mode: python ; coding: utf-8 -*-
from pathlib import Path
import glob
import os

THIS_DIR = Path(os.path.abspath(SPEC)).parent
REPO_ROOT = THIS_DIR.parent
ICON_PATH    = str(REPO_ROOT / "cerf" / "assets" / "launcher.ico")
VERSION_PATH = str(REPO_ROOT / "cerf" / "version.h")

NAME = os.environ.get("CERF_LAUNCHER_NAME", "launcher")

# Windows carries the Universal CRT in-box only from Windows 10; on Vista it is
# an update (KB2999226). Microsoft supports app-local UCRT deployment, so the
# build ships the redistributable inside the exe and needs no update.
# build.ps1 points CERF_LAUNCHER_UCRT at the SDK's x86 redist directory.
UCRT_DIR = os.environ.get("CERF_LAUNCHER_UCRT", "")
UCRT_BINARIES = [(p, ".") for p in glob.glob(os.path.join(UCRT_DIR, "*.dll"))] \
                if UCRT_DIR else []

block_cipher = None

a = Analysis(
    [str(THIS_DIR / "launcher.py")],
    pathex=[str(THIS_DIR)],
    binaries=UCRT_BINARIES,
    datas=[(ICON_PATH, "."), (VERSION_PATH, "."),
           (str(THIS_DIR / "assets" / "icons"), "assets/icons"),
           (str(REPO_ROOT / "gweslab.png"), "assets")],
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
    name=NAME,
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
