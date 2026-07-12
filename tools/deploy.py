#!/usr/bin/env python3
"""Publish a CERF release: run from the repo root as `python tools\\deploy.py`.

  --sha=<commit>  release the artifact built from that commit, not the newest
  --yes           take every confirmation as yes

The release is built from a CI artifact, never from the working tree - the tree
may already be ahead of what is being released. Every step asks first, so an
unexpected value can be answered with n and finished by hand."""
from __future__ import annotations

import json
import os
import re
import sys
import urllib.error
import urllib.request
from dataclasses import dataclass
from html.parser import HTMLParser
from pathlib import Path
from typing import List, Optional

REPO = "gweslab/cerf"
GITHUB_API = "https://api.github.com"
GITHUB_UPLOADS = "https://uploads.github.com"
DISCORD_API = "https://discord.com/api/v10"
DISCORD_CHANNEL_ID = "1517249750796206191"
DISCORD_MESSAGE_LIMIT = 2000

ARTIFACT_NAME = re.compile(r"^CERF-(\d+)\.(\d+)\.(\d+)-([0-9a-f]+)-Release-Win32$")
CHANGELOG_PATH = Path("docs/changelog.html")
ENV_PATH = Path(".env")
USER_AGENT = "CERF deploy"


class DeployError(RuntimeError):
    pass


@dataclass(frozen=True)
class Artifact:
    id: int
    name: str
    size: int
    created_at: str
    branch: str
    sha: str
    version: str
    tag: str


def load_env() -> dict:
    values = {}
    if ENV_PATH.is_file():
        for line in ENV_PATH.read_text(encoding="utf-8-sig").splitlines():
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, _, value = line.partition("=")
            values[key.strip()] = value.strip()
    return values


def load_credentials() -> tuple:
    values = load_env()

    def pick(*names: str) -> str:
        for name in names:
            value = os.environ.get(name) or values.get(name)
            if value:
                return value
        raise DeployError(
            f"no {' / '.join(names)} in the environment or {ENV_PATH}")

    # Actions refuses to store a secret whose name starts with GITHUB_, so the
    # workflow supplies the same token as GH_TOKEN.
    return pick("GITHUB_TOKEN", "GH_TOKEN"), pick("DISCORD_SECRET")


class AuthStrippingRedirect(urllib.request.HTTPRedirectHandler):
    # An artifact download redirects to pre-signed blob storage, which rejects
    # the request outright if GitHub's Authorization header rides along.
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        new = super().redirect_request(req, fp, code, msg, headers, newurl)
        if new is not None:
            for name in [h for h in new.headers if h.lower() == "authorization"]:
                del new.headers[name]
        return new


def request(url: str, headers: dict, method: str = "GET",
            body: Optional[bytes] = None, content_type: Optional[str] = None):
    headers = dict(headers)
    headers["User-Agent"] = USER_AGENT
    if content_type:
        headers["Content-Type"] = content_type
    req = urllib.request.Request(url, data=body, headers=headers, method=method)
    opener = urllib.request.build_opener(AuthStrippingRedirect)
    try:
        with opener.open(req, timeout=120) as response:
            return response.read()
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", "replace")[:400]
        raise DeployError(f"{method} {url} -> HTTP {exc.code}: {detail}") from exc
    except urllib.error.URLError as exc:
        raise DeployError(f"{method} {url} unreachable: {exc}") from exc


def github(token: str, path: str, method: str = "GET",
           payload: Optional[dict] = None):
    body = json.dumps(payload).encode("utf-8") if payload is not None else None
    raw = request(GITHUB_API + path,
                  {"Authorization": f"Bearer {token}",
                   "Accept": "application/vnd.github+json"},
                  method, body, "application/json" if body else None)
    return json.loads(raw.decode("utf-8")) if raw else {}


def confirm(question: str, assume_yes: bool) -> None:
    if assume_yes:
        print(f"{question} y")
        return
    answer = input(f"{question} [y/n] ").strip().lower()
    if not answer.startswith("y"):
        raise SystemExit("Stopped. Finish by hand from here.")


def latest_artifact(token: str, sha: Optional[str] = None) -> Artifact:
    payload = github(token, f"/repos/{REPO}/actions/artifacts?per_page=100")
    for item in payload.get("artifacts", []):
        if item.get("expired"):
            continue
        match = ARTIFACT_NAME.match(item.get("name", ""))
        if not match:
            continue
        run = item.get("workflow_run") or {}
        head_sha = run.get("head_sha") or ""
        if sha and not head_sha.startswith(sha) and not sha.startswith(head_sha):
            continue
        major, minor, patch, name_sha = match.groups()
        return Artifact(
            id=item["id"], name=item["name"], size=item["size_in_bytes"],
            created_at=item["created_at"],
            branch=run.get("head_branch") or "?",
            sha=head_sha or name_sha,
            version=f"{major}.{minor}.{patch}", tag=f"{major}.{minor}")
    where = f" built from {sha[:7]}" if sha else ""
    raise DeployError(f"no unexpired Release-Win32 artifact{where} found")


class ChangelogParser(HTMLParser):
    def __init__(self, tag: str) -> None:
        super().__init__(convert_charrefs=True)
        self._wanted = f"v{tag}"
        self._row: List[str] = []
        self._cell: List[str] = []
        self._items: List[str] = []
        self._in_cell = False
        self._in_item = False
        self.bullets: Optional[List[str]] = None

    def handle_starttag(self, tag, attrs):
        if tag == "tr":
            self._row, self._items = [], []
        elif tag == "td":
            self._in_cell, self._cell = True, []
        elif tag == "li":
            self._in_item, self._cell = True, []

    def handle_endtag(self, tag):
        text = " ".join("".join(self._cell).split())
        if tag == "li" and self._in_item:
            self._in_item = False
            if text:
                self._items.append(text)
        elif tag == "td" and self._in_cell:
            self._in_cell = False
            self._row.append(text)
        elif tag == "tr" and self.bullets is None:
            version = self._row[0] if self._row else ""
            if version == self._wanted or version.startswith(self._wanted + " "):
                self.bullets = list(self._items)

    def handle_data(self, data):
        if self._in_cell or self._in_item:
            self._cell.append(data)


def changelog_bullets(tag: str) -> List[str]:
    if not CHANGELOG_PATH.is_file():
        raise DeployError(f"{CHANGELOG_PATH} not found; run from the repo root")
    parser = ChangelogParser(tag)
    parser.feed(CHANGELOG_PATH.read_text(encoding="utf-8"))
    if not parser.bullets:
        raise DeployError(f"{CHANGELOG_PATH} has no entries for v{tag}")
    return parser.bullets


def existing_release(token: str, tag: str) -> Optional[dict]:
    try:
        return github(token, f"/repos/{REPO}/releases/tags/{tag}")
    except DeployError as exc:
        if "HTTP 404" in str(exc):
            return None
        raise


def download_artifact(token: str, artifact: Artifact, destination: Path) -> None:
    raw = request(
        f"{GITHUB_API}/repos/{REPO}/actions/artifacts/{artifact.id}/zip",
        {"Authorization": f"Bearer {token}",
         "Accept": "application/vnd.github+json"})
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(raw)
    if destination.stat().st_size == 0:
        raise DeployError(f"{destination} came back empty")


def create_release(token: str, artifact: Artifact, body: str) -> dict:
    return github(token, f"/repos/{REPO}/releases", "POST", {
        "tag_name": artifact.tag,
        "target_commitish": artifact.sha,
        "name": artifact.tag,
        "body": body,
        "draft": False,
        "prerelease": False,
    })


def upload_asset(token: str, release_id: int, archive: Path) -> str:
    payload = json.loads(request(
        f"{GITHUB_UPLOADS}/repos/{REPO}/releases/{release_id}/assets"
        f"?name={archive.name}",
        {"Authorization": f"Bearer {token}",
         "Accept": "application/vnd.github+json"},
        "POST", archive.read_bytes(), "application/zip").decode("utf-8"))
    return payload["browser_download_url"]


def post_discord(secret: str, tag: str, changelog: str) -> None:
    content = (f"[**CE Runtime Foundation {tag} Released**]"
               f"(https://github.com/{REPO}/releases/tag/{tag})\n\n{changelog}")
    if len(content) > DISCORD_MESSAGE_LIMIT:
        raise DeployError(
            f"the Discord message is {len(content)} characters, over the "
            f"{DISCORD_MESSAGE_LIMIT} limit; shorten the changelog")
    request(f"{DISCORD_API}/channels/{DISCORD_CHANNEL_ID}/messages",
            {"Authorization": f"Bot {secret}"}, "POST",
            json.dumps({"content": content}).encode("utf-8"),
            "application/json")


def main(argv: List[str]) -> int:
    assume_yes = "--yes" in argv
    sha = next((a.partition("=")[2] for a in argv if a.startswith("--sha=")), None)
    token, secret = load_credentials()

    artifact = latest_artifact(token, sha)
    print(f"\nArtifact        : {artifact.name}")
    print(f"  version / tag : {artifact.version} -> {artifact.tag}")
    print(f"  branch / sha  : {artifact.branch} / {artifact.sha[:7]}")
    print(f"  built         : {artifact.created_at}")
    print(f"  size          : {artifact.size / 1024 / 1024:.1f} MB")
    confirm(f"Release {artifact.tag} from this artifact?", assume_yes)

    if existing_release(token, artifact.tag) is not None:
        raise DeployError(f"release {artifact.tag} already exists")

    bullets = changelog_bullets(artifact.tag)
    body = "\n".join(f"- {item}" for item in bullets)
    print(f"\nChangelog for v{artifact.tag}:\n{body}\n")
    confirm("Use this as the release description?", assume_yes)

    archive = Path("tmp") / f"{artifact.name}.zip"
    print(f"\nDownloading {artifact.name} ...")
    download_artifact(token, artifact, archive)
    print(f"  {archive} ({archive.stat().st_size / 1024 / 1024:.1f} MB)")
    confirm(f"Publish tag {artifact.tag} at {artifact.sha[:7]} with this asset?",
            assume_yes)

    release = create_release(token, artifact, body)
    print(f"  release created: {release['html_url']}")
    print(f"  uploading {archive.name} ...")
    print(f"  asset: {upload_asset(token, release['id'], archive)}")

    confirm(f"\nPost the {artifact.tag} announcement to Discord?", assume_yes)
    post_discord(secret, artifact.tag, body)
    print("  posted.\n")
    print(f"Released {artifact.tag}: {release['html_url']}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except DeployError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
