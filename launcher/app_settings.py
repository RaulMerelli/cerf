from __future__ import annotations

import json

from bundle_repositories import config_path, load_config

DISCORD_RICH_PRESENCE_KEY = "discord_rich_presence"


def read_discord_rich_presence() -> bool:
    return load_config(config_path()).get(DISCORD_RICH_PRESENCE_KEY) is True


def write_discord_rich_presence(enabled: bool) -> None:
    path = config_path()
    obj = load_config(path)
    obj[DISCORD_RICH_PRESENCE_KEY] = enabled
    path.write_text(json.dumps(obj, indent=2) + "\n", encoding="utf-8")
