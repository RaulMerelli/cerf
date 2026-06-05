from __future__ import annotations

import shutil
import sys
import tempfile
import threading
import traceback
import urllib.request
import zipfile
from concurrent.futures import ThreadPoolExecutor, Future
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Dict, List, Optional

from bundles import (
    BundleError,
    DOWNLOAD_CHUNK,
    DOWNLOAD_TIMEOUT,
    PARALLEL_WORKERS,
    USER_AGENT,
    DeviceBundle,
    DeviceMeta,
    RemoteBundle,
    is_safe_bundle_name,
    load_local_manifest,
    load_remote_manifest,
    parse_cerf_json,
    parse_cerf_json_object,
    save_local_manifest,
    write_cerf_json,
    write_cerf_json_if_changed,
    _sha256_file,
)


ProgressFn = Callable[[str, int, Optional[int]], None]
DoneFn = Callable[[Optional[BaseException]], None]


def _stream_download(url: str, destination: Path, label: str,
                     expected_size: Optional[int], progress: ProgressFn,
                     cancel_event: Optional[threading.Event]) -> None:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=DOWNLOAD_TIMEOUT) as response:
        content_length = response.headers.get("Content-Length")
        total = int(content_length) if content_length and content_length.isdigit() else expected_size
        done = 0
        progress(label, 0, total)
        try:
            with destination.open("wb") as f:
                while True:
                    if cancel_event is not None and cancel_event.is_set():
                        raise CancelledError(f"{label}: cancelled")
                    chunk = response.read(DOWNLOAD_CHUNK)
                    if not chunk:
                        break
                    f.write(chunk)
                    done += len(chunk)
                    progress(label, done, total)
        except BaseException:
            try:
                destination.unlink(missing_ok=True)
            except OSError:
                pass
            raise


class CancelledError(BundleError):
    pass


def _verify_download(path: Path, label: str,
                     expected_size: Optional[int],
                     expected_sha256: Optional[str]) -> None:
    if expected_size is not None and path.stat().st_size != expected_size:
        raise BundleError(
            f"{label}: size mismatch (got {path.stat().st_size}, expected {expected_size})"
        )
    if expected_sha256:
        digest = _sha256_file(path)
        if digest.lower() != expected_sha256.lower():
            raise BundleError(
                f"{label}: sha256 mismatch (got {digest}, expected {expected_sha256})"
            )


def _safe_extract(zip_path: Path, destination: Path) -> None:
    destination_resolved = destination.resolve()
    with zipfile.ZipFile(zip_path) as archive:
        for member in archive.infolist():
            member_path = destination / member.filename
            member_resolved = member_path.resolve()
            try:
                member_resolved.relative_to(destination_resolved)
            except ValueError as exc:
                raise BundleError(
                    f"unsafe path in archive: {member.filename}"
                ) from exc
        archive.extractall(destination)


def _prepared_bundle_root(extract_dir: Path) -> Path:
    entries = [e for e in extract_dir.iterdir() if e.name not in {".", ".."}]
    if len(entries) == 1 and entries[0].is_dir():
        return entries[0]
    return extract_dir


class BundleManager:
    def __init__(self, devices_dir: Path):
        self.devices_dir: Path = devices_dir
        self.local_manifest_path: Path = devices_dir / "manifest.json"
        self._pool = ThreadPoolExecutor(max_workers=PARALLEL_WORKERS)
        self._manifest_lock = threading.Lock()
        self.remote_bundles: List[RemoteBundle] = []
        self.installed: Dict[str, str] = {}

    def shutdown(self) -> None:
        self._pool.shutdown(wait=False, cancel_futures=True)

    def submit_refresh(self) -> Future:
        return self._pool.submit(self._do_refresh)

    def _do_refresh(self) -> None:
        try:
            self.installed = load_local_manifest(self.local_manifest_path)
        except BundleError:
            self.installed = {}
        self.remote_bundles = load_remote_manifest()
        self._reconcile_cerf_json()

    def _reconcile_cerf_json(self) -> None:
        """Manifest v2 no longer ships cerf.json inside the ROM zip; the
        launcher owns it. Whenever the remote cerf_json for an installed
        device differs from the on-disk copy, silently rewrite it."""
        for rb in self.remote_bundles:
            if rb.cerf_json is None:
                continue
            if not is_safe_bundle_name(rb.name):
                continue
            target = self.devices_dir / rb.name
            if not target.is_dir():
                continue
            try:
                write_cerf_json_if_changed(target / "cerf.json", rb.cerf_json)
            except OSError:
                pass

    def list_devices(self) -> List[DeviceBundle]:
        result: Dict[str, DeviceBundle] = {}

        if self.devices_dir.is_dir():
            for entry in self.devices_dir.iterdir():
                if not entry.is_dir():
                    continue
                if entry.name.startswith("."):
                    continue
                if not is_safe_bundle_name(entry.name):
                    continue
                meta, screen_supported, w, h = parse_cerf_json(entry / "cerf.json")
                result[entry.name] = DeviceBundle(
                    name=entry.name,
                    remote=None,
                    local_dir_exists=True,
                    installed_at=self.installed.get(entry.name),
                    meta=meta,
                    has_pdbs=_has_pdbs(entry),
                    screen_supported=screen_supported,
                    default_screen_width=w,
                    default_screen_height=h,
                )

        for rb in self.remote_bundles:
            remote_meta = DeviceMeta()
            remote_screen_supported: Optional[bool] = None
            remote_w: Optional[int] = None
            remote_h: Optional[int] = None
            if rb.cerf_json is not None:
                remote_meta, remote_screen_supported, remote_w, remote_h = \
                    parse_cerf_json_object(rb.cerf_json)

            existing = result.get(rb.name)
            if existing is not None:
                existing.remote = rb
                if not existing.meta.device_name:
                    existing.meta.device_name = remote_meta.device_name
                if not existing.meta.board_name:
                    existing.meta.board_name = remote_meta.board_name
                if not existing.meta.soc_family:
                    existing.meta.soc_family = remote_meta.soc_family
                if not existing.meta.os_name:
                    existing.meta.os_name = remote_meta.os_name
                if not existing.meta.os_ver_major:
                    existing.meta.os_ver_major = remote_meta.os_ver_major
                if not existing.meta.os_ver_minor:
                    existing.meta.os_ver_minor = remote_meta.os_ver_minor
                if not existing.meta.device_year:
                    existing.meta.device_year = remote_meta.device_year
                if existing.screen_supported is None and remote_screen_supported is not None:
                    existing.screen_supported = remote_screen_supported
                if existing.default_screen_width is None and remote_w is not None:
                    existing.default_screen_width = remote_w
                if existing.default_screen_height is None and remote_h is not None:
                    existing.default_screen_height = remote_h
            else:
                result[rb.name] = DeviceBundle(
                    name=rb.name,
                    remote=rb,
                    local_dir_exists=False,
                    installed_at=None,
                    meta=remote_meta,
                    has_pdbs=False,
                    screen_supported=remote_screen_supported,
                    default_screen_width=remote_w,
                    default_screen_height=remote_h,
                )

        return sorted(result.values(), key=lambda b: b.name.lower())

    def submit_install(self, name: str, with_pdbs: bool,
                       progress: ProgressFn,
                       cancel_event: Optional[threading.Event] = None) -> Future:
        return self._pool.submit(self._do_install, name, with_pdbs, progress, cancel_event)

    def _do_install(self, name: str, with_pdbs: bool,
                    progress: ProgressFn,
                    cancel_event: Optional[threading.Event]) -> None:
        bundle = self._find_remote(name)
        target = self._bundle_dir(name)
        with tempfile.TemporaryDirectory(prefix=".sync_", dir=str(self.devices_dir)) as tmp_name:
            tmp = Path(tmp_name)
            archive = tmp / f"{name}.zip"
            extract = tmp / "extract"
            extract.mkdir()
            label = f"Downloading {name}"
            _stream_download(bundle.archive_url, archive, label,
                             bundle.archive_size, progress, cancel_event)
            _verify_download(archive, name, bundle.archive_size, bundle.archive_sha256)
            progress(f"Extracting {name}", 0, None)
            _safe_extract(archive, extract)
            prepared = _prepared_bundle_root(extract)
            if target.exists():
                shutil.rmtree(target)
            shutil.move(str(prepared), str(target))

        # Manifest v2: cerf.json is not packed in the ROM zip; the launcher
        # writes it from the manifest's cerf_json after unpacking.
        if bundle.cerf_json is not None:
            write_cerf_json(target / "cerf.json", bundle.cerf_json)

        with self._manifest_lock:
            self.installed[name] = bundle.updated_at
            save_local_manifest(self.local_manifest_path, self.installed)

        if with_pdbs and bundle.pdbs_url:
            self._do_install_pdbs(name, progress, cancel_event)

    def submit_install_pdbs(self, name: str, progress: ProgressFn,
                            cancel_event: Optional[threading.Event] = None) -> Future:
        return self._pool.submit(self._do_install_pdbs, name, progress, cancel_event)

    def _do_install_pdbs(self, name: str, progress: ProgressFn,
                         cancel_event: Optional[threading.Event]) -> None:
        bundle = self._find_remote(name)
        if not bundle.pdbs_url:
            raise BundleError(f"{name}: remote has no PDBs")
        target = self._bundle_dir(name)
        if not target.is_dir():
            raise BundleError(f"{name}: device not installed; install bundle first")
        pdbs = target / "pdbs"
        with tempfile.TemporaryDirectory(prefix=".pdbs_", dir=str(self.devices_dir)) as tmp_name:
            tmp = Path(tmp_name)
            archive = tmp / f"{name}.pdbs.zip"
            extract = tmp / "extract"
            extract.mkdir()
            label = f"Downloading {name} PDBs"
            _stream_download(bundle.pdbs_url, archive, label,
                             bundle.pdbs_size, progress, cancel_event)
            _verify_download(archive, f"{name} PDBs", bundle.pdbs_size, bundle.pdbs_sha256)
            progress(f"Extracting {name} PDBs", 0, None)
            _safe_extract(archive, extract)
            prepared = _prepared_bundle_root(extract)
            if pdbs.exists():
                shutil.rmtree(pdbs)
            shutil.move(str(prepared), str(pdbs))

    def submit_delete(self, name: str) -> Future:
        return self._pool.submit(self._do_delete, name)

    def _do_delete(self, name: str) -> None:
        target = self._bundle_dir(name)
        if target.exists():
            shutil.rmtree(target)
        with self._manifest_lock:
            self.installed.pop(name, None)
            save_local_manifest(self.local_manifest_path, self.installed)

    def _bundle_dir(self, name: str) -> Path:
        if not is_safe_bundle_name(name):
            raise BundleError(f"unsafe bundle name: {name!r}")
        path = (self.devices_dir / name).resolve()
        devices = self.devices_dir.resolve()
        try:
            path.relative_to(devices)
        except ValueError as exc:
            raise BundleError(f"refusing to operate outside {devices}") from exc
        return path

    def _find_remote(self, name: str) -> RemoteBundle:
        for b in self.remote_bundles:
            if b.name == name:
                return b
        raise BundleError(f"unknown remote bundle: {name}")


def _has_pdbs(device_dir: Path) -> bool:
    pdbs = device_dir / "pdbs"
    if not pdbs.is_dir():
        return False
    for _ in pdbs.iterdir():
        return True
    return False
