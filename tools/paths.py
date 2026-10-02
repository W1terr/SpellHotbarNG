"""
Folders the tools work with.

The defaults suit a fresh clone. Override them per machine in tools/paths_local.json (not in git), e.g.

    {
        "build": "D:/SkyrimBuild/SpellHotbarNG/build",
        "gameData": "D:/Games/steamapps/common/Skyrim Special Edition/Data"
    }

or with the environment variable named next to each entry. Environment variables win over the file.
"""

import json
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

_local_file = Path(__file__).with_name("paths_local.json")
_local = json.loads(_local_file.read_text(encoding="utf-8")) if _local_file.exists() else {}


def _path(key: str, env: str, default) -> Path:
    return Path(os.environ.get(env) or _local.get(key) or default)


# xmake output folder (build.bat passes the same folder to "xmake f -o"), SHNG_BUILD_DIR
BUILD = _path("build", "SHNG_BUILD_DIR", ROOT / "build")
# the built plugin inside it
PLUGIN_BUILD = BUILD / "windows" / "x64" / "releasedbg"
# where the mod files are assembled before zipping, SHNG_STAGING
STAGING = _path("staging", "SHNG_STAGING", BUILD / "staging")
# a clone of https://github.com/pWn3d1337/Skyrim_SpellHotbar2 (icon sources), SHNG_SPELLHOTBAR2
SPELLHOTBAR2 = _path("spellhotbar2", "SHNG_SPELLHOTBAR2", ROOT / "external" / "SpellHotbar2")
# the game's Data folder (vanilla casting clips come from "Skyrim - Animations.bsa"), SHNG_GAME_DATA
GAME_DATA = _path("gameData", "SHNG_GAME_DATA", "C:/Program Files (x86)/Steam/steamapps/common/Skyrim Special Edition/Data")
# where package.py writes the zips, SHNG_OUTPUT
OUTPUT = _path("output", "SHNG_OUTPUT", ROOT / "dist")
