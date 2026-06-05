from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import tempfile
import threading
import time
import urllib.parse
import urllib.request
import zipfile
from concurrent.futures import ThreadPoolExecutor, Future
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Dict, List, Optional


BASE_URL = "https://cerf.dz3n.net/cerf-bundles"
REMOTE_MANIFEST_URL = BASE_URL + "/manifest.json"
SUPPORTED_REMOTE_MANIFEST_VERSION = 2
USER_AGENT = "CERF launcher"
SAFE_BUNDLE_NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")
DEFAULT_TIMEOUT = 30
DOWNLOAD_TIMEOUT = 120
DOWNLOAD_CHUNK = 1024 * 1024
PARALLEL_WORKERS = 4


class BundleError(RuntimeError):
    pass


class ManifestVersionError(BundleError):
    """Remote manifest schema version this CERF build cannot read."""

    def __init__(self, remote_version: int, supported_version: int):
        self.remote_version = remote_version
        self.supported_version = supported_version
        self.remote_is_newer = remote_version > supported_version
        if self.remote_is_newer:
            msg = (
                f"remote manifest version {remote_version} is newer than this "
                f"CERF build supports (version {supported_version}); download a "
                f"newer CERF build from https://github.com/gweslab/cerf"
            )
        else:
            msg = (
                f"remote manifest version {remote_version} is older than this "
                f"CERF build expects (version {supported_version})"
            )
        super().__init__(msg)


@dataclass(frozen=True)
class RemoteBundle:
    name: str
    updated_at: str
    archive_path: str
    archive_sha256: Optional[str] = None
    archive_size: Optional[int] = None
    pdbs_path: Optional[str] = None
    pdbs_sha256: Optional[str] = None
    pdbs_size: Optional[int] = None
    cerf_json: Optional[dict] = None

    @property
    def archive_url(self) -> str:
        base = urllib.parse.urljoin(BASE_URL + "/", self.archive_path)
        return _append_query(base, "v", self.updated_at)

    @property
    def pdbs_url(self) -> Optional[str]:
        if not self.pdbs_path:
            return None
        base = urllib.parse.urljoin(BASE_URL + "/", self.pdbs_path)
        return _append_query(base, "v", self.updated_at)


@dataclass
class DeviceMeta:
    device_name: str = ""
    board_name: str = ""
    soc_family: str = ""
    os_name: str = ""
    os_ver_major: int = 0
    os_ver_minor: int = 0
    device_year: int = 0
    description: str = ""
    notes: List[str] = field(default_factory=list)

    @property
    def os_version(self) -> str:
        if self.os_name and (self.os_ver_major or self.os_ver_minor):
            return f"{self.os_name} {self.os_ver_major}.{self.os_ver_minor}"
        if self.os_name:
            return self.os_name
        return ""


@dataclass
class DeviceBundle:
    name: str
    remote: Optional[RemoteBundle]
    local_dir_exists: bool
    installed_at: Optional[str]
    meta: DeviceMeta = field(default_factory=DeviceMeta)
    has_pdbs: bool = False
    screen_supported: Optional[bool] = None
    default_screen_width: Optional[int] = None
    default_screen_height: Optional[int] = None

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


def _append_query(url: str, key: str, value: str) -> str:
    sep = "&" if "?" in url else "?"
    return f"{url}{sep}{key}={urllib.parse.quote(value, safe='')}"


def _fetch_bytes(url: str, timeout: int = DEFAULT_TIMEOUT) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return response.read()


def _sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(DOWNLOAD_CHUNK), b""):
            h.update(chunk)
    return h.hexdigest()


def is_safe_bundle_name(name: str) -> bool:
    return bool(SAFE_BUNDLE_NAME.fullmatch(name)) and name not in {".", ".."}


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
        os_block = m.get("os")
        if isinstance(os_block, dict):
            meta.os_name = _str_or_empty(os_block.get("name"))
            meta.os_ver_major = _int_or_zero(os_block.get("ver_major"))
            meta.os_ver_minor = _int_or_zero(os_block.get("ver_minor"))

    board = obj.get("board")
    if isinstance(board, dict):
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


def load_remote_manifest() -> List[RemoteBundle]:
    fresh_url = _append_query(REMOTE_MANIFEST_URL, "cb", str(int(time.time())))
    try:
        raw = _fetch_bytes(fresh_url)
        manifest = json.loads(raw.decode("utf-8"))
    except Exception as exc:
        raise BundleError(f"failed to download remote manifest: {exc}") from exc

    version = manifest.get("version")
    if not isinstance(version, int):
        raise BundleError("remote manifest has no integer version")
    if version != SUPPORTED_REMOTE_MANIFEST_VERSION:
        raise ManifestVersionError(version, SUPPORTED_REMOTE_MANIFEST_VERSION)

    bundles = manifest.get("bundles")
    if not isinstance(bundles, list):
        raise BundleError("remote manifest has no bundles list")

    parsed: List[RemoteBundle] = []
    for item in bundles:
        if not isinstance(item, dict):
            raise BundleError("remote manifest contains a malformed entry")
        name = item.get("name")
        updated_at = item.get("updated_at")
        archive_path = item.get("archive_path")
        if not isinstance(name, str) or not is_safe_bundle_name(name):
            raise BundleError(f"remote manifest contains unsafe name: {name!r}")
        if not isinstance(updated_at, str) or not updated_at:
            raise BundleError(f"bundle {name} has no updated_at")
        if not isinstance(archive_path, str) or not archive_path:
            raise BundleError(f"bundle {name} has no archive_path")
        parsed.append(RemoteBundle(
            name=name,
            updated_at=updated_at,
            archive_path=archive_path,
            archive_sha256=item.get("archive_sha256") if isinstance(item.get("archive_sha256"), str) else None,
            archive_size=item.get("archive_size") if isinstance(item.get("archive_size"), int) else None,
            pdbs_path=item.get("pdbs_path") if isinstance(item.get("pdbs_path"), str) and item.get("pdbs_path") else None,
            pdbs_sha256=item.get("pdbs_sha256") if isinstance(item.get("pdbs_sha256"), str) else None,
            pdbs_size=item.get("pdbs_size") if isinstance(item.get("pdbs_size"), int) else None,
            cerf_json=item.get("cerf_json") if isinstance(item.get("cerf_json"), dict) else None,
        ))
    return sorted(parsed, key=lambda b: b.name.lower())


def load_local_manifest(local_manifest_path: Path) -> Dict[str, str]:
    if not local_manifest_path.exists():
        return {}
    try:
        with local_manifest_path.open("r", encoding="utf-8") as f:
            manifest = json.load(f)
    except Exception as exc:
        raise BundleError(f"failed to read local manifest: {exc}") from exc

    installed: Dict[str, str] = {}
    bundles = manifest.get("bundles") if isinstance(manifest, dict) else None
    if isinstance(bundles, list):
        for item in bundles:
            if isinstance(item, dict):
                n = item.get("name")
                u = item.get("updated_at")
                if isinstance(n, str) and isinstance(u, str):
                    installed[n] = u
    elif isinstance(bundles, dict):
        for n, v in bundles.items():
            if isinstance(v, dict) and isinstance(v.get("updated_at"), str):
                installed[str(n)] = v["updated_at"]
            elif isinstance(v, str):
                installed[str(n)] = v
    return installed


def save_local_manifest(local_manifest_path: Path, installed: Dict[str, str]) -> None:
    payload = {
        "bundles": [
            {"name": name, "updated_at": installed[name]}
            for name in sorted(installed, key=str.lower)
        ]
    }
    tmp = local_manifest_path.with_suffix(".json.tmp")
    with tmp.open("w", encoding="utf-8", newline="\n") as f:
        json.dump(payload, f, indent=2)
        f.write("\n")
    os.replace(tmp, local_manifest_path)
