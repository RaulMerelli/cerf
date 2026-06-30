"""Local device state: per-device metadata (cerf.json), package install
status, and the local install manifest with its additional-package nesting."""
from __future__ import annotations

import json
import os
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional

from bundles import (
    BundleError,
    RemoteBundle,
    RemotePackage,
    package_category_label,
)


STATE_INSTALLED = "Installed"
STATE_UPDATE    = "Update available"
STATE_AVAILABLE = "Available"
STATE_USER      = "User device"

# Mirrors CERF's kDefaultStateFile (cerf/state/state_image_format.h): the
# hibernation .img cerf.exe writes/reads in each device directory.
STATE_IMAGE_FILENAME = "state.img"


@dataclass
class DeviceSource:
    """Who preserved/provided this ROM bundle. Optional in the manifest; when
    present, `name` is mandatory. The three links are each optional."""

    name: str = ""
    website: str = ""   # "pay them a visit" target
    donate: str = ""    # "support them" target
    origin: str = ""    # "Source data link" target (where the ROM came from)

    @property
    def has_links(self) -> bool:
        return bool(self.website or self.donate or self.origin)


@dataclass
class DeviceMeta:
    device_name: str = ""
    board_name: str = ""
    board_id: str = ""
    soc_family: str = ""
    os_name: str = ""
    os_ver_major: int = 0
    os_ver_minor: int = 0
    os_ver_build: int = 0
    os_language: str = ""
    device_year: int = 0
    os_year: int = 0
    os_notes: List[str] = field(default_factory=list)
    description: str = ""
    notes: List[str] = field(default_factory=list)
    source: Optional[DeviceSource] = None

    @property
    def os_version(self) -> str:
        if self.os_name and (self.os_ver_major or self.os_ver_minor):
            ver = f"CE {self.os_ver_major}.{self.os_ver_minor}"
            if self.os_ver_build:
                ver += f".{self.os_ver_build}"
            return f"{self.os_name} ({ver})"
        if self.os_name:
            return self.os_name
        return ""


@dataclass
class PackageStatus:
    """One additional package of one device, with its local install state.
    Freshness follows the ROM pattern: the archive sha256 recorded at install
    time is compared against the remote manifest's current sha256."""

    remote: RemotePackage
    installed: bool
    installed_sha256: Optional[str] = None

    @property
    def has_update(self) -> bool:
        return (self.installed
                and self.installed_sha256 is not None
                and self.remote.archive_sha256 is not None
                and self.installed_sha256.lower() != self.remote.archive_sha256.lower())

    @property
    def category_label(self) -> str:
        return package_category_label(self.remote.category)

    @property
    def state_label(self) -> str:
        if self.has_update:
            return STATE_UPDATE
        if self.installed:
            return STATE_INSTALLED
        return STATE_AVAILABLE


@dataclass
class DeviceBundle:
    name: str
    remote: Optional[RemoteBundle]
    local_dir_exists: bool
    installed_at: Optional[str]
    meta: DeviceMeta = field(default_factory=DeviceMeta)
    screen_supported: Optional[bool] = None
    default_screen_width: Optional[int] = None
    default_screen_height: Optional[int] = None
    packages: List[PackageStatus] = field(default_factory=list)

    @property
    def is_installed(self) -> bool:
        return self.local_dir_exists

    @property
    def is_user_device(self) -> bool:
        return self.local_dir_exists and self.remote is None

    @property
    def has_update(self) -> bool:
        if not self.local_dir_exists or self.remote is None or self.installed_at is None:
            return False
        return self.installed_at != self.remote.updated_at

    @property
    def state_label(self) -> str:
        if self.is_user_device:
            return STATE_USER
        if self.has_update:
            return STATE_UPDATE
        if self.is_installed:
            return STATE_INSTALLED
        return STATE_AVAILABLE

    @property
    def has_package_updates(self) -> bool:
        return any(p.has_update for p in self.packages)


def package_artifact_present(device_dir: Path, pkg: RemotePackage) -> bool:
    path = device_dir / pkg.key
    if pkg.is_directory:
        if not path.is_dir():
            return False
        for _ in path.iterdir():
            return True
        return False
    return path.is_file()


@dataclass
class SavedStateInfo:
    saved_at: float  # state.img mtime, epoch seconds
    size: int        # state.img size in bytes


def saved_state_info(device_dir: Path) -> Optional[SavedStateInfo]:
    """Hibernation snapshot for a device, or None when no state.img exists."""
    path = device_dir / STATE_IMAGE_FILENAME
    if not path.is_file():
        return None
    try:
        st = path.stat()
    except OSError:
        return None
    return SavedStateInfo(saved_at=st.st_mtime, size=st.st_size)


def format_size(n: Optional[int]) -> str:
    if not isinstance(n, int) or n <= 0:
        return ""
    if n >= 1024 ** 3:
        return f"{n / 1024 ** 3:.1f} GB"
    if n >= 1024 ** 2:
        return f"{n / 1024 ** 2:.1f} MB"
    if n >= 1024:
        return f"{n / 1024:.0f} KB"
    return f"{n} B"


def parse_cerf_json_object(obj) -> tuple[DeviceMeta, Optional[bool], Optional[int], Optional[int]]:
    meta = DeviceMeta()
    screen_supported: Optional[bool] = None
    width: Optional[int] = None
    height: Optional[int] = None
    if not isinstance(obj, dict):
        return meta, None, None, None

    m = obj.get("meta")
    if isinstance(m, dict):
        meta.device_name = _str_or_empty(m.get("device_name"))
        meta.board_name = _str_or_empty(m.get("board_name"))
        meta.soc_family = _str_or_empty(m.get("soc_family"))
        meta.device_year = _int_or_zero(m.get("device_year"))
        meta.description = _str_or_empty(m.get("description"))
        meta.notes = _str_list(m.get("notes"))
        src_block = m.get("source")
        if isinstance(src_block, dict):
            # source.name is mandatory when the source block exists; without it
            # the block is meaningless, so an unnamed source is ignored.
            src_name = _str_or_empty(src_block.get("name"))
            if src_name:
                meta.source = DeviceSource(
                    name=src_name,
                    website=_str_or_empty(src_block.get("website")),
                    donate=_str_or_empty(src_block.get("donate")),
                    origin=_str_or_empty(src_block.get("origin")),
                )
        os_block = m.get("os")
        if isinstance(os_block, dict):
            meta.os_name = _str_or_empty(os_block.get("name"))
            meta.os_ver_major = _int_or_zero(os_block.get("ver_major"))
            meta.os_ver_minor = _int_or_zero(os_block.get("ver_minor"))
            meta.os_ver_build = _int_or_zero(os_block.get("ver_build"))
            meta.os_language = _str_or_empty(os_block.get("language"))
            meta.os_year = _int_or_zero(os_block.get("year"))
            meta.os_notes = _str_list(os_block.get("notes"))

    board = obj.get("board")
    if isinstance(board, dict):
        meta.board_id = _str_or_empty(board.get("id"))
        s = board.get("configurable_screen_resolution_supported")
        if isinstance(s, bool):
            screen_supported = s
        w = board.get("configurable_screen_width")
        h = board.get("configurable_screen_height")
        if isinstance(w, int) and w > 0:
            width = w
        if isinstance(h, int) and h > 0:
            height = h

    return meta, screen_supported, width, height


def parse_cerf_json(path: Path) -> tuple[DeviceMeta, Optional[bool], Optional[int], Optional[int]]:
    try:
        with path.open("r", encoding="utf-8") as f:
            obj = json.load(f)
    except (OSError, json.JSONDecodeError):
        return DeviceMeta(), None, None, None
    return parse_cerf_json_object(obj)


def _str_or_empty(v) -> str:
    return v if isinstance(v, str) else ""


def _int_or_zero(v) -> int:
    return v if isinstance(v, int) else 0


def _str_list(v) -> List[str]:
    if not isinstance(v, list):
        return []
    return [s for s in v if isinstance(s, str) and s.strip()]


def write_cerf_json(path: Path, obj: dict) -> None:
    tmp = path.with_suffix(".json.tmp")
    with tmp.open("w", encoding="utf-8", newline="\n") as f:
        json.dump(obj, f, indent=2, ensure_ascii=False)
        f.write("\n")
    os.replace(tmp, path)


CERF_USER_JSON_FILENAME = "cerf-user.json"


def _load_json_object(path: Path):
    try:
        with path.open("r", encoding="utf-8") as f:
            obj = json.load(f)
    except (OSError, json.JSONDecodeError):
        return None
    return obj if isinstance(obj, dict) else None


def _extract_persist_fields(obj) -> dict:
    out: dict = {}
    if not isinstance(obj, dict):
        return out
    net = obj.get("network")
    if isinstance(net, dict) and isinstance(net.get("enabled"), bool):
        out["network_enabled"] = net["enabled"]
    if isinstance(obj.get("guest_additions"), bool):
        out["guest_additions"] = obj["guest_additions"]
    if isinstance(obj.get("full_screen"), bool):
        out["full_screen"] = obj["full_screen"]
    board = obj.get("board")
    if isinstance(board, dict):
        w = board.get("configurable_screen_width")
        h = board.get("configurable_screen_height")
        d = board.get("configurable_screen_dpi")
        if isinstance(w, int) and w > 0:
            out["width"] = w
        if isinstance(h, int) and h > 0:
            out["height"] = h
        if isinstance(d, int) and d > 0:
            out["dpi"] = d
    return out


def read_persist_fields(device_dir: Path) -> tuple[dict, dict]:
    base = _extract_persist_fields(_load_json_object(device_dir / "cerf.json"))
    override = _extract_persist_fields(
        _load_json_object(device_dir / CERF_USER_JSON_FILENAME))
    return base, override


def write_persist_overrides(device_dir: Path, fields: dict) -> None:
    path = device_dir / CERF_USER_JSON_FILENAME
    obj: dict = {}
    if "network_enabled" in fields:
        obj["network"] = {"enabled": fields["network_enabled"]}
    if "guest_additions" in fields:
        obj["guest_additions"] = fields["guest_additions"]
    if "full_screen" in fields:
        obj["full_screen"] = fields["full_screen"]
    board: dict = {}
    if "width" in fields:
        board["configurable_screen_width"] = fields["width"]
    if "height" in fields:
        board["configurable_screen_height"] = fields["height"]
    if "dpi" in fields:
        board["configurable_screen_dpi"] = fields["dpi"]
    if board:
        obj["board"] = board
    if not obj:
        try:
            path.unlink(missing_ok=True)
        except OSError:
            pass
        return
    write_cerf_json(path, obj)


def write_cerf_json_if_changed(path: Path, obj: dict) -> bool:
    """Rewrite cerf.json only when its parsed content differs from obj.
    Comparison is semantic (parsed JSON), so reformatting / key reordering
    on disk never triggers a spurious rewrite. Returns True if written."""
    try:
        with path.open("r", encoding="utf-8") as f:
            existing = json.load(f)
    except (OSError, json.JSONDecodeError):
        existing = None
    if existing == obj:
        return False
    write_cerf_json(path, obj)
    return True


@dataclass
class LocalPackageRecord:
    category: str
    key: str
    is_directory: bool
    sha256: str


@dataclass
class LocalBundleRecord:
    # None when the device dir was placed by hand and only a package was ever
    # installed through the launcher; ROM freshness is then unknown.
    updated_at: Optional[str] = None
    packages: List[LocalPackageRecord] = field(default_factory=list)

    def find_package(self, category: str, key: str) -> Optional[LocalPackageRecord]:
        for rec in self.packages:
            if rec.category == category and rec.key == key:
                return rec
        return None

    def drop_package(self, category: str, key: str) -> None:
        self.packages = [r for r in self.packages
                         if not (r.category == category and r.key == key)]


def _parse_local_packages(raw) -> List[LocalPackageRecord]:
    out: List[LocalPackageRecord] = []
    if not isinstance(raw, dict):
        return out
    for category, entries in raw.items():
        if not isinstance(category, str) or not isinstance(entries, list):
            continue
        for item in entries:
            if not isinstance(item, dict):
                continue
            sha = item.get("sha256")
            if not isinstance(sha, str) or not sha:
                continue
            file_key = item.get("file")
            dir_key = item.get("directory")
            if isinstance(file_key, str) and file_key:
                out.append(LocalPackageRecord(category, file_key, False, sha))
            elif isinstance(dir_key, str) and dir_key:
                out.append(LocalPackageRecord(category, dir_key, True, sha))
    return out


def load_local_manifest(local_manifest_path: Path) -> Dict[str, LocalBundleRecord]:
    if not local_manifest_path.exists():
        return {}
    try:
        with local_manifest_path.open("r", encoding="utf-8") as f:
            manifest = json.load(f)
    except Exception as exc:
        raise BundleError(f"failed to read local manifest: {exc}") from exc

    installed: Dict[str, LocalBundleRecord] = {}
    bundles = manifest.get("bundles") if isinstance(manifest, dict) else None
    if isinstance(bundles, list):
        for item in bundles:
            if not isinstance(item, dict):
                continue
            n = item.get("name")
            if not isinstance(n, str):
                continue
            u = item.get("updated_at")
            installed[n] = LocalBundleRecord(
                updated_at=u if isinstance(u, str) else None,
                packages=_parse_local_packages(item.get("additional_packages")),
            )
    elif isinstance(bundles, dict):
        # Legacy pre-list format: {"name": "<updated_at>"} or nested dicts.
        for n, v in bundles.items():
            if isinstance(v, dict) and isinstance(v.get("updated_at"), str):
                installed[str(n)] = LocalBundleRecord(updated_at=v["updated_at"])
            elif isinstance(v, str):
                installed[str(n)] = LocalBundleRecord(updated_at=v)
    return installed


def save_local_manifest(local_manifest_path: Path,
                        installed: Dict[str, LocalBundleRecord]) -> None:
    bundles = []
    for name in sorted(installed, key=str.lower):
        record = installed[name]
        entry: dict = {"name": name}
        if record.updated_at is not None:
            entry["updated_at"] = record.updated_at
        if record.packages:
            # Mirrors the remote manifest's additional_packages nesting.
            categories: Dict[str, list] = {}
            for rec in record.packages:
                identity = "directory" if rec.is_directory else "file"
                categories.setdefault(rec.category, []).append(
                    {identity: rec.key, "sha256": rec.sha256})
            entry["additional_packages"] = categories
        bundles.append(entry)
    payload = {"bundles": bundles}
    tmp = local_manifest_path.with_suffix(".json.tmp")
    with tmp.open("w", encoding="utf-8", newline="\n") as f:
        json.dump(payload, f, indent=2)
        f.write("\n")
    os.replace(tmp, local_manifest_path)
