"""
Compatibility patches for animation mods made for Spell Hotbar 2.

Those mods are OAR submods whose conditions read Spell Hotbar 2's ESP globals, which never change without
Spell Hotbar 2. A patch ships no animations: it only adds an OAR "user.json" next to each submod's config.json
(OAR uses user.json instead of config.json), pointing the submod at Spell Hotbar NG's "SpellHotbarNG_Casting"
condition with a priority above Spell Hotbar NG's own animations, plus a timing file with the lengths of the
mod's release clips so the bar waits for them (Data\\SKSE\\Plugins\\SpellHotbarNG\\animations\\*.json).
"""

import json
from pathlib import Path

from build_anims import HAND_ANY, HAND_LEFT, HAND_RIGHT, casting_condition

PATCH_PRIORITY = 2141000000  # above Spell Hotbar NG's own submods (2140000000 + ...)

# Spell Hotbar 2 Anim Replacer (Nexus 142616, Goetia Shouts animations).
# submod folder -> (cast type, hand). Only the submods that ship animations; the rest only re-configure
# Spell Hotbar 2's own (not installed) ones. Third person only, first person keeps the vanilla clips.
SH2_ANIM_REPLACER = {
    "name": "Spell Hotbar 2 Anim Replacer",
    "folder": "SH2 Anim Replacer",
    "description": "Use the animations of \"Spell Hotbar 2 Anim Replacer\" (Goetia Shouts) for Spell Hotbar NG casts. "
                   "Needs that mod installed; contains no animations itself.",
    "oar_root": "meshes/actors/character/OpenAnimationReplacer/SpellHotbar2",
    "submods": {
        "cast_1h_right": (1, HAND_RIGHT),
        "cast_1h_right_self": (2, HAND_RIGHT),
        "cast_1h_left": (1, HAND_LEFT),
        "cast_1h_left_self": (2, HAND_LEFT),
        "cast_1h_left_conc": (3, HAND_LEFT),
        "cast_1h_left_conc_self": (4, HAND_LEFT),
        "cast_dual": (5, HAND_ANY),
        "cast_dual_self": (6, HAND_ANY),
        "cast_ritual": (8, HAND_ANY),
    },
    # release (mt_shout_exhale) clip lengths in seconds, measured with hkx_duration.py (mod version 1.0.0).
    # Concentration casts end with "ShoutStop", their release clips aren't used.
    "release_time": {
        "thirdPerson": {"1-right": 1.033, "1-left": 1.0, "2": 1.033, "5": 1.033, "6": 1.033, "8": 1.0},
    },
}

PATCHES = [SH2_ANIM_REPLACER]


def build_patch(patch: dict, out_dir: Path) -> int:
    """Writes the patch files into out_dir (a mod root: meshes\\..., SKSE\\...). Returns the number of files."""
    count = 0
    for submod, (type_id, hand) in patch["submods"].items():
        config = {
            "name": f"{submod} (Spell Hotbar NG)",
            "description": f"Patched by Spell Hotbar NG: plays for Spell Hotbar NG casts of type {type_id}"
                           + ("" if hand == HAND_ANY else f", {'right' if hand == HAND_RIGHT else 'left'} hand"),
            "priority": PATCH_PRIORITY + type_id * 10 + hand,
            "interruptible": False,
            "conditions": [casting_condition(type_id, hand)],
        }
        dest = out_dir / patch["oar_root"] / submod / "user.json"
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(json.dumps(config, indent=4), encoding="utf-8")
        count += 1

    timings = out_dir / "SKSE/Plugins/SpellHotbarNG/animations" / f"{patch['folder']}.json"
    timings.parent.mkdir(parents=True, exist_ok=True)
    timings.write_text(json.dumps({"name": patch["name"], "releaseTime": patch["release_time"]}, indent=4), encoding="utf-8")
    return count + 1
