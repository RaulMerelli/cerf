from __future__ import annotations

import json
import os
import re
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import changelog

REPO = "gweslab/cerf"
GITHUB_API = "https://api.github.com"
GITHUB_UPLOADS = "https://uploads.github.com"
DISCORD_API = "https://discord.com/api/v10"
DISCORD_MESSAGE_LIMIT = 2000
DISCORD_SUPPRESS_EMBEDS = 1 << 2

ARTIFACT_NAME = re.compile(r"^CERF-(\d+)\.(\d+)\.(\d+)-([0-9a-f]+)-Release-Win32$")
CHANGELOG_PATH = Path("docs/changelog.yml")
CHANGELOG_URL = f"https://github.com/{REPO}#changelog"
ENV_PATH = Path(".env")
USER_AGENT = "CERF deploy"


class CiError(RuntimeError):
    pass


@dataclass(frozen=True)
class Artifact:
    id: int
    name: str
    size: int
    created_at: str
    branch: str
    sha: str
    run_id: int
    version: str
    tag: str

    @property
    def run_url(self) -> str:
        return f"https://github.com/{REPO}/actions/runs/{self.run_id}"

    @property
    def commit_url(self) -> str:
        return f"https://github.com/{REPO}/commit/{self.sha}"

    @property
    def download_url(self) -> str:
        return f"{self.run_url}/artifacts/{self.id}"


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
        raise CiError(
            f"no {' / '.join(names)} in the environment or {ENV_PATH}")

    return pick("GITHUB_TOKEN", "GH_TOKEN"), pick("DISCORD_SECRET")


class AuthStrippingRedirect(urllib.request.HTTPRedirectHandler):
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
        raise CiError(f"{method} {url} -> HTTP {exc.code}: {detail}") from exc
    except urllib.error.URLError as exc:
        raise CiError(f"{method} {url} unreachable: {exc}") from exc


def github(token: str, path: str, method: str = "GET",
           payload: Optional[dict] = None):
    body = json.dumps(payload).encode("utf-8") if payload is not None else None
    raw = request(GITHUB_API + path,
                  {"Authorization": f"Bearer {token}",
                   "Accept": "application/vnd.github+json"},
                  method, body, "application/json" if body else None)
    return json.loads(raw.decode("utf-8")) if raw else {}


def _artifact(item: dict) -> Optional[Artifact]:
    if item.get("expired"):
        return None
    match = ARTIFACT_NAME.match(item.get("name", ""))
    if not match:
        return None
    run = item.get("workflow_run") or {}
    major, minor, patch, name_sha = match.groups()
    return Artifact(
        id=item["id"], name=item["name"], size=item["size_in_bytes"],
        created_at=item["created_at"],
        branch=run.get("head_branch") or "?",
        sha=run.get("head_sha") or name_sha,
        run_id=run.get("id") or 0,
        version=f"{major}.{minor}.{patch}", tag=f"{major}.{minor}")


def latest_artifact(token: str, sha: Optional[str] = None) -> Artifact:
    payload = github(token, f"/repos/{REPO}/actions/artifacts?per_page=100")
    for item in payload.get("artifacts", []):
        artifact = _artifact(item)
        if artifact is None:
            continue
        if sha and not artifact.sha.startswith(sha) and not sha.startswith(artifact.sha):
            continue
        return artifact
    where = f" built from {sha[:7]}" if sha else ""
    raise CiError(f"no unexpired Release-Win32 artifact{where} found")


def run_artifact(token: str, run_id: int, attempts: int = 12,
                 delay: int = 10) -> Artifact:
    for attempt in range(attempts):
        payload = github(
            token, f"/repos/{REPO}/actions/runs/{run_id}/artifacts?per_page=100")
        for item in payload.get("artifacts", []):
            artifact = _artifact(item)
            if artifact is not None:
                return artifact
        if attempt + 1 < attempts:
            print(f"  artifact of run {run_id} not listed yet, retrying in {delay}s")
            time.sleep(delay)
    raise CiError(f"run {run_id} published no Release-Win32 artifact")


def changelog_markdown(tag: str, fallback: Optional[str] = None) -> str:
    if not CHANGELOG_PATH.is_file():
        raise CiError(f"{CHANGELOG_PATH} not found; run from the repo root")
    entry = changelog.entry_for(tag)
    if entry is None or not entry["groups"]:
        if fallback is not None:
            return fallback
        raise CiError(f"{CHANGELOG_PATH} has no entries for v{tag}")
    return changelog.render_markdown(entry["groups"])


def compose(top: str, body: str, bottom: str) -> str:
    lines = body.splitlines()
    dropped = 0
    while True:
        more = (f"\n... +{dropped} more in the [full changelog]({CHANGELOG_URL})"
                if dropped else "")
        content = "\n\n".join([top, "\n".join(lines) + more, bottom])
        if len(content) <= DISCORD_MESSAGE_LIMIT or not lines:
            return content
        lines.pop()
        dropped += 1


def post_discord(secret: str, channel_id: str, content: str,
                 ping_here: bool = False) -> None:
    if len(content) > DISCORD_MESSAGE_LIMIT:
        raise CiError(
            f"the Discord message is {len(content)} characters, over the "
            f"{DISCORD_MESSAGE_LIMIT} limit; shorten the changelog")
    payload = {"content": content,
               "flags": DISCORD_SUPPRESS_EMBEDS,
               "allowed_mentions": {"parse": ["everyone"] if ping_here else []}}
    request(f"{DISCORD_API}/channels/{channel_id}/messages",
            {"Authorization": f"Bot {secret}"}, "POST",
            json.dumps(payload).encode("utf-8"), "application/json")
