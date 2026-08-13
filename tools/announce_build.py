#!/usr/bin/env python3
"""Announce a CI build in the Discord QA channel: run from the repo root as
`python tools\\announce_build.py --run-id=<id> [--build=<n>] [--rc]`.

  --run-id=<id>  the workflow run whose artifact is announced (required)
  --build=<n>    the build number shown in the message
  --rc           announce a release candidate: pings @here and asks for a sweep

The release announcement is tools/deploy.py. This one announces an artifact
that is not a release, so the changelog it carries is whatever has landed for
the version so far."""
from __future__ import annotations

import os
import sys
from typing import List

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ci_release import (Artifact, CiError, changelog_markdown, compose,
                        load_credentials, post_discord, run_artifact)

QA_CHANNEL_ID = "1537263681228771349"
QA_ROLE_ID = "1537262210118852708"


def header(artifact: Artifact, build: str, release_candidate: bool) -> str:
    title = f"CE Runtime Foundation {artifact.tag}"
    if release_candidate:
        return (f"<@&{QA_ROLE_ID}>\n"
                f"[**{title} release candidate - build {build}**]"
                f"({artifact.download_url})\n"
                "!!! RELEASE CANDIDATE !!!!")
    return (f"[**{title} - CI build {build}**]({artifact.download_url})")


def footer(artifact: Artifact) -> str:
    return (f"[Download]({artifact.download_url}) (needs a GitHub account) - "
            f"[CI build]({artifact.run_url}) - "
            f"[`{artifact.sha[:7]}`]({artifact.commit_url})")


def main(argv: List[str]) -> int:
    def value(flag: str) -> str:
        return next((a.partition("=")[2] for a in argv if a.startswith(flag)), "")

    run_id = value("--run-id=")
    if not run_id.isdigit():
        raise CiError("--run-id=<id> is required")
    build = value("--build=") or "?"
    release_candidate = "--rc" in argv

    token, secret = load_credentials()
    artifact = run_artifact(token, int(run_id))
    print(f"\nArtifact        : {artifact.name}")
    print(f"  version / tag : {artifact.version} -> {artifact.tag}")
    print(f"  branch / sha  : {artifact.branch} / {artifact.sha[:7]}")
    print(f"  download      : {artifact.download_url}")

    empty = f"Nothing recorded in the {artifact.tag} changelog yet."
    content = compose(header(artifact, build, release_candidate),
                      changelog_markdown(artifact.tag, empty),
                      footer(artifact))
    print(f"\n{content}\n")
    post_discord(secret, QA_CHANNEL_ID, content,
                 ping_role=QA_ROLE_ID if release_candidate else None)
    print(f"Announced {artifact.name} in channel {QA_CHANNEL_ID}.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except CiError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
