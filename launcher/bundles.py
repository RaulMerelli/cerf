from __future__ import annotations

import hashlib
import json
import re
import time
import urllib.parse
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional


BASE_URL = "https://cerf-bundles.dz3n.net/cerf-bundles"
REMOTE_MANIFEST_URL = BASE_URL + "/manifest.json"
SUPPORTED_REMOTE_MANIFEST_VERSION = 2

# Latest released CERF version is published as a plain-text file at the repo
# root on the default branch; the launcher fetches it to offer an update.
LAST_RELEASE_URL = "https://raw.githubusercontent.com/gweslab/cerf/main/.last-release-version"
RELEASE_LATEST_URL = "https://github.com/gweslab/cerf/releases/latest"
USER_AGENT = "CERF launcher"
SAFE_BUNDLE_NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")
DEFAULT_TIMEOUT = 30
DOWNLOAD_TIMEOUT = 120
DOWNLOAD_CHUNK = 1024 * 1024
PARALLEL_WORKERS = 4

# Compressed-download size above which the GUI offers a manual browser download
# instead of streaming through the launcher (a >=2 GB ROM over a flaky link is
# unreliable to pull in-process). GUI-only; the CLI always streams.
LARGE_DOWNLOAD_THRESHOLD = 300 * 1024 * 1024

# Human-readable labels for additional-package categories. Categories without
# a mapping are displayed under their raw manifest key.
PACKAGE_CATEGORY_LABELS = {
    "pdbs": "PDBs",
    "compact_flash_cards": "Compact Flash Cards",
}


def package_category_label(category: str) -> str:
    return PACKAGE_CATEGORY_LABELS.get(category, category)


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
class RemotePackage:
    """One downloadable additional package of a bundle (PDBs, CF card
    images, ...). `key` is the artifact name the package produces in the
    device root: a single file when `is_directory` is False, a directory
    whose contents come from the archive root when True."""

    category: str
    key: str
    is_directory: bool
    name: str
    archive_path: str
    archive_sha256: Optional[str] = None
    archive_size: Optional[int] = None
    unpacked_size: Optional[int] = None

    @property
    def archive_url(self) -> str:
        base = urllib.parse.urljoin(BASE_URL + "/", self.archive_path)
        # Packages carry no updated_at; the content hash is the cache-buster.
        if self.archive_sha256:
            return _append_query(base, "v", self.archive_sha256)
        return base


@dataclass(frozen=True)
class RemoteBundle:
    name: str
    updated_at: str
    archive_path: str
    archive_sha256: Optional[str] = None
    archive_size: Optional[int] = None
    unpacked_size: Optional[int] = None
    cerf_json: Optional[dict] = None
    packages: tuple = ()

    @property
    def archive_url(self) -> str:
        base = urllib.parse.urljoin(BASE_URL + "/", self.archive_path)
        return _append_query(base, "v", self.updated_at)

    def find_package(self, category: str, key: str) -> Optional[RemotePackage]:
        for pkg in self.packages:
            if pkg.category == category and pkg.key == key:
                return pkg
        return None


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


def is_large_download(archive_size: Optional[int]) -> bool:
    """True when a bundle/package's compressed download crosses the GUI
    large-download threshold. Unknown size (None) is treated as not large."""
    return isinstance(archive_size, int) and archive_size >= LARGE_DOWNLOAD_THRESHOLD


def parse_version_tuple(text) -> Optional[tuple]:
    """Parse a dotted version ('3.20', '3.20.1') into a tuple of ints, taking
    the leading numeric components and stopping at the first non-numeric one.
    Returns None when no leading numeric component exists."""
    if not isinstance(text, str):
        return None
    parts: List[int] = []
    for token in text.strip().split("."):
        token = token.strip()
        if not token.isdigit():
            break
        parts.append(int(token))
    return tuple(parts) if parts else None


def fetch_last_release_version(timeout: int = DEFAULT_TIMEOUT) -> str:
    url = _append_query(LAST_RELEASE_URL, "cb", str(int(time.time())))
    raw = _fetch_bytes(url, timeout)
    return raw.decode("utf-8").strip()


def _parse_packages(bundle_name: str, raw) -> tuple:
    if raw is None:
        return ()
    if not isinstance(raw, dict):
        raise BundleError(f"bundle {bundle_name}: malformed additional_packages")
    parsed: List[RemotePackage] = []
    for category, entries in raw.items():
        if not isinstance(category, str) or not category.strip():
            raise BundleError(f"bundle {bundle_name}: malformed package category")
        if not isinstance(entries, list):
            raise BundleError(
                f"bundle {bundle_name}: package category {category} is not a list")
        for item in entries:
            if not isinstance(item, dict):
                raise BundleError(
                    f"bundle {bundle_name}: malformed package entry in {category}")
            file_key = item.get("file")
            dir_key = item.get("directory")
            if isinstance(file_key, str) and not isinstance(dir_key, str):
                key, is_directory = file_key, False
            elif isinstance(dir_key, str) and not isinstance(file_key, str):
                key, is_directory = dir_key, True
            else:
                raise BundleError(
                    f"bundle {bundle_name}: package in {category} needs exactly "
                    f"one of file/directory")
            # The key lands on the local filesystem inside the device dir;
            # reject anything that could escape it.
            if not is_safe_bundle_name(key):
                raise BundleError(
                    f"bundle {bundle_name}: unsafe package artifact name: {key!r}")
            archive_path = item.get("archive_path")
            if not isinstance(archive_path, str) or not archive_path:
                raise BundleError(
                    f"bundle {bundle_name}: package {category}/{key} has no "
                    f"archive_path")
            display = item.get("name")
            parsed.append(RemotePackage(
                category=category,
                key=key,
                is_directory=is_directory,
                name=display if isinstance(display, str) and display else key,
                archive_path=archive_path,
                archive_sha256=item.get("archive_sha256")
                if isinstance(item.get("archive_sha256"), str) else None,
                archive_size=item.get("archive_size")
                if isinstance(item.get("archive_size"), int) else None,
                unpacked_size=item.get("unpacked_size")
                if isinstance(item.get("unpacked_size"), int) else None,
            ))
    return tuple(parsed)


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
            unpacked_size=item.get("unpacked_size") if isinstance(item.get("unpacked_size"), int) else None,
            cerf_json=item.get("cerf_json") if isinstance(item.get("cerf_json"), dict) else None,
            packages=_parse_packages(name, item.get("additional_packages")),
        ))
    return sorted(parsed, key=lambda b: b.name.lower())
