from __future__ import annotations

import shutil
import tempfile
import threading
import urllib.request
import zipfile
from concurrent.futures import ThreadPoolExecutor, Future
from pathlib import Path
from typing import Callable, Dict, List, Optional, Tuple

from bundles import (
    BundleError,
    DOWNLOAD_CHUNK,
    DOWNLOAD_TIMEOUT,
    PARALLEL_WORKERS,
    USER_AGENT,
    RemoteBundle,
    is_safe_bundle_name,
    load_analytics,
    load_merged_manifest,
    _sha256_file,
)
from bundle_repositories import read_repositories
from github_release import fetch_latest_release
from device_state import (
    DeviceBundle,
    DeviceMeta,
    LocalBundleRecord,
    LocalPackageRecord,
    PackageStatus,
    load_local_manifest,
    package_artifact_present,
    parse_cerf_json,
    parse_cerf_json_object,
    save_local_manifest,
    write_cerf_json,
    write_cerf_json_if_changed,
)


ProgressFn = Callable[[str, int, Optional[int]], None]


class CancelledError(BundleError):
    pass


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


def _remove_artifact(path: Path) -> None:
    if path.is_dir():
        shutil.rmtree(path)
    elif path.exists():
        path.unlink()


def _download_extract(url: str, label: str, tmp: Path,
                      expected_size: Optional[int],
                      expected_sha256: Optional[str],
                      progress: ProgressFn,
                      cancel_event: Optional[threading.Event]) -> Path:
    archive = tmp / "archive.zip"
    extract = tmp / "extract"
    extract.mkdir()
    _stream_download(url, archive, f"Downloading {label}",
                     expected_size, progress, cancel_event)
    _verify_download(archive, label, expected_size, expected_sha256)
    progress(f"Extracting {label}", 0, None)
    _safe_extract(archive, extract)
    return _prepared_bundle_root(extract)


class BundleManager:
    def __init__(self, devices_dir: Path):
        self.devices_dir: Path = devices_dir
        self.local_manifest_path: Path = devices_dir / "manifest.json"
        self._pool = ThreadPoolExecutor(max_workers=PARALLEL_WORKERS)
        self._manifest_lock = threading.Lock()
        self.remote_bundles: List[RemoteBundle] = []
        self.repo_errors: List[Tuple[str, str]] = []
        self.download_places: Optional[Dict[str, int]] = None
        self.installed: Dict[str, LocalBundleRecord] = {}

    def shutdown(self) -> None:
        self._pool.shutdown(wait=False, cancel_futures=True)

    def submit_refresh(self) -> Future:
        return self._pool.submit(self._do_refresh)

    def submit_release_check(self) -> Future:
        return self._pool.submit(fetch_latest_release)

    def _do_refresh(self) -> None:
        try:
            self.installed = load_local_manifest(self.local_manifest_path)
        except BundleError:
            self.installed = {}
        repositories = read_repositories()
        self.remote_bundles, self.repo_errors = load_merged_manifest(repositories)
        self.download_places = load_analytics(repositories)
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

    def _package_statuses(self, rb: RemoteBundle,
                          device_dir: Optional[Path]) -> List[PackageStatus]:
        record = self.installed.get(rb.name)
        statuses: List[PackageStatus] = []
        for pkg in rb.packages:
            present = device_dir is not None and package_artifact_present(device_dir, pkg)
            local = record.find_package(pkg.category, pkg.key) if record else None
            statuses.append(PackageStatus(
                remote=pkg,
                installed=present,
                installed_sha256=local.sha256 if (local and present) else None,
            ))
        return statuses

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
                record = self.installed.get(entry.name)
                result[entry.name] = DeviceBundle(
                    name=entry.name,
                    remote=None,
                    local_dir_exists=True,
                    installed_at=record.updated_at if record else None,
                    meta=meta,
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
                existing.packages = self._package_statuses(
                    rb, self.devices_dir / rb.name)
                if not existing.meta.device_name:
                    existing.meta.device_name = remote_meta.device_name
                if not existing.meta.board_name:
                    existing.meta.board_name = remote_meta.board_name
                if not existing.meta.board_id:
                    existing.meta.board_id = remote_meta.board_id
                if not existing.meta.soc_family:
                    existing.meta.soc_family = remote_meta.soc_family
                if not existing.meta.os_name:
                    existing.meta.os_name = remote_meta.os_name
                if not existing.meta.os_ver_major:
                    existing.meta.os_ver_major = remote_meta.os_ver_major
                if not existing.meta.os_ver_minor:
                    existing.meta.os_ver_minor = remote_meta.os_ver_minor
                if not existing.meta.os_ver_build:
                    existing.meta.os_ver_build = remote_meta.os_ver_build
                if not existing.meta.os_language:
                    existing.meta.os_language = remote_meta.os_language
                if not existing.meta.device_year:
                    existing.meta.device_year = remote_meta.device_year
                if not existing.meta.os_year:
                    existing.meta.os_year = remote_meta.os_year
                if not existing.meta.os_notes:
                    existing.meta.os_notes = remote_meta.os_notes
                if existing.meta.source is None:
                    existing.meta.source = remote_meta.source
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
                    screen_supported=remote_screen_supported,
                    default_screen_width=remote_w,
                    default_screen_height=remote_h,
                    packages=self._package_statuses(rb, None),
                )

        return sorted(result.values(), key=lambda b: b.name.lower())

    def submit_install(self, name: str, progress: ProgressFn,
                       cancel_event: Optional[threading.Event] = None) -> Future:
        return self._pool.submit(self._do_install, name, progress, cancel_event)

    def _do_install(self, name: str, progress: ProgressFn,
                    cancel_event: Optional[threading.Event]) -> None:
        bundle = self._find_remote(name)
        target = self._bundle_dir(name)
        with tempfile.TemporaryDirectory(prefix=".sync_", dir=str(self.devices_dir)) as tmp_name:
            tmp = Path(tmp_name)
            prepared = _download_extract(
                bundle.archive_url, name, tmp,
                bundle.archive_size, bundle.archive_sha256,
                progress, cancel_event)
            # Installed additional packages (a CF image may be the user's
            # mutated persistent disk) survive a ROM update: stash their
            # artifacts aside, wipe, restore after the new ROM is in place.
            preserved = self._stash_installed_packages(name, target, tmp / "preserved")
            if target.exists():
                shutil.rmtree(target)
            shutil.move(str(prepared), str(target))
            for record, stash_path in preserved:
                dest = target / record.key
                _remove_artifact(dest)
                shutil.move(str(stash_path), str(dest))

        # Manifest v2: cerf.json is not packed in the ROM zip; the launcher
        # writes it from the manifest's cerf_json after unpacking.
        if bundle.cerf_json is not None:
            write_cerf_json(target / "cerf.json", bundle.cerf_json)

        with self._manifest_lock:
            self.installed[name] = LocalBundleRecord(
                updated_at=bundle.updated_at,
                packages=[record for record, _ in preserved],
            )
            save_local_manifest(self.local_manifest_path, self.installed)

    def _stash_installed_packages(
            self, name: str, target: Path,
            stash_dir: Path) -> List[Tuple[LocalPackageRecord, Path]]:
        record = self.installed.get(name)
        if record is None or not record.packages or not target.is_dir():
            return []
        preserved: List[Tuple[LocalPackageRecord, Path]] = []
        for pkg_record in record.packages:
            src = target / pkg_record.key
            if not src.exists():
                continue
            stash_dir.mkdir(parents=True, exist_ok=True)
            stash_path = stash_dir / f"{pkg_record.category}__{pkg_record.key}"
            shutil.move(str(src), str(stash_path))
            preserved.append((pkg_record, stash_path))
        return preserved

    def prepare_manual_install(self, name: str) -> Path:
        """GUI manual-download path: pre-create the device directory, write its
        cerf.json from the remote manifest, and record the bundle in the local
        manifest at the remote version. The user then unpacks the ZIP they
        downloaded in a browser into this directory; update tracking already
        matches the recorded version, so the bundle reads as up to date once
        the ROM files are in place."""
        bundle = self._find_remote(name)
        target = self._bundle_dir(name)
        target.mkdir(parents=True, exist_ok=True)
        if bundle.cerf_json is not None:
            write_cerf_json(target / "cerf.json", bundle.cerf_json)
        with self._manifest_lock:
            record = self.installed.get(name) or LocalBundleRecord()
            record.updated_at = bundle.updated_at
            self.installed[name] = record
            save_local_manifest(self.local_manifest_path, self.installed)
        return target

    def prepare_manual_install_package(self, name: str, category: str,
                                       key: str) -> None:
        """GUI manual-download path for an additional package: record it in the
        local manifest at the remote sha256 so update tracking matches once the
        user unpacks the browser-downloaded artifact into the device directory.
        Presence is re-derived from disk at list time, so the package reads as
        installed only after its artifact actually lands."""
        bundle = self._find_remote(name)
        pkg = bundle.find_package(category, key)
        if pkg is None:
            raise BundleError(f"{name}: unknown package {category}/{key}")
        target = self._bundle_dir(name)
        if not target.is_dir():
            raise BundleError(f"{name}: device not installed; install bundle first")
        with self._manifest_lock:
            record = self.installed.setdefault(name, LocalBundleRecord())
            record.drop_package(category, key)
            record.packages.append(LocalPackageRecord(
                category=category, key=pkg.key,
                is_directory=pkg.is_directory,
                sha256=pkg.archive_sha256 or ""))
            save_local_manifest(self.local_manifest_path, self.installed)

    def submit_install_package(self, name: str, category: str, key: str,
                               progress: ProgressFn,
                               cancel_event: Optional[threading.Event] = None) -> Future:
        return self._pool.submit(self._do_install_package, name, category, key,
                                 progress, cancel_event)

    def _do_install_package(self, name: str, category: str, key: str,
                            progress: ProgressFn,
                            cancel_event: Optional[threading.Event]) -> None:
        bundle = self._find_remote(name)
        pkg = bundle.find_package(category, key)
        if pkg is None:
            raise BundleError(f"{name}: unknown package {category}/{key}")
        target = self._bundle_dir(name)
        if not target.is_dir():
            raise BundleError(f"{name}: device not installed; install bundle first")
        label = f"{name} {pkg.name}"
        with tempfile.TemporaryDirectory(prefix=".pkg_", dir=str(self.devices_dir)) as tmp_name:
            tmp = Path(tmp_name)
            prepared = _download_extract(
                pkg.archive_url, label, tmp,
                pkg.archive_size, pkg.archive_sha256,
                progress, cancel_event)
            dest = target / pkg.key
            if pkg.is_directory:
                # Directory packages (e.g. PDBs) ship a flat archive whose
                # root contents become devices/<name>/<directory>/.
                _remove_artifact(dest)
                shutil.move(str(prepared), str(dest))
            else:
                src = prepared / pkg.key
                if not src.is_file():
                    raise BundleError(
                        f"{label}: archive does not contain {pkg.key}")
                _remove_artifact(dest)
                shutil.move(str(src), str(dest))

        with self._manifest_lock:
            record = self.installed.setdefault(name, LocalBundleRecord())
            record.drop_package(category, key)
            record.packages.append(LocalPackageRecord(
                category=category, key=pkg.key,
                is_directory=pkg.is_directory,
                sha256=pkg.archive_sha256 or ""))
            save_local_manifest(self.local_manifest_path, self.installed)

    def submit_delete_package(self, name: str, category: str, key: str) -> Future:
        return self._pool.submit(self._do_delete_package, name, category, key)

    def _do_delete_package(self, name: str, category: str, key: str) -> None:
        if not is_safe_bundle_name(key):
            raise BundleError(f"unsafe package artifact name: {key!r}")
        target = self._bundle_dir(name)
        _remove_artifact(target / key)
        with self._manifest_lock:
            record = self.installed.get(name)
            if record is not None:
                record.drop_package(category, key)
                save_local_manifest(self.local_manifest_path, self.installed)

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
