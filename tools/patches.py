"""
Compatibility patches for animation mods.

Mods made for Spell Hotbar 2 are OAR submods whose conditions read Spell Hotbar 2's ESP globals, which never change without
Spell Hotbar 2. A patch ships no animations: it only adds an OAR "user.json" next to each submod's config.json
(OAR uses user.json instead of config.json), pointing the submod at Spell Hotbar NG's "SpellHotbarNG_Casting"
condition with a priority above Spell Hotbar NG's own animations, plus a timing file with the lengths of the
mod's release clips so the bar waits for them (Data\\SKSE\\Plugins\\SpellHotbarNG\\animations\\*.json).

Mods that replace the normal magic casting clips in Dynamic Animation Replacer folders ("dar_folders") or OAR submods
("oar_folders"): their animations can't be shipped, so the patch is only a file listing the mod's folders. At game start the DLL copies those
folders' clips into OAR submods for the shout clips and reads the clip lengths itself (src/casting/Replacers.cpp).
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

# Smooth Magic Casting Animation (Nexus 45799, version 4.2). 996 = base set, the rest are variants by spell school
# (1516/1616/1716 Restoration, 1816/1826 Conjuration, 1830/1831 Destruction or not, for dual casts). Third person only.
# 1391/1392 only have ritualspell_ready, which hotbar casts don't use in third person.
SMOOTH_MAGIC_CASTING = {
    "name": "Smooth Magic Casting Animation",
    "folder": "Smooth Magic Casting Animation",
    "description": "Use the animations of \"Smooth Magic Casting Animation\" for Spell Hotbar NG casts. "
                   "Needs that mod installed; contains no animations itself. At game start Spell Hotbar NG copies its "
                   "animations for the hotbar (in MO2 they end up in the overwrite folder).",
    "dar_folders": [996, 1516, 1616, 1716, 1816, 1826, 1830, 1831],
}

# Goetia Animations - Magic Spell Casting (Nexus 70204, version 1.5b), an OAR mod. "Main" (no conditions) has every
# casting clip plus magcast_*; the other submods only change sprinting / idles with weapon + spell. Third person only.
# Its aimed wind-up is the 4.3 s m?h_precharge (the magic behavior plays it before m?h_chargeloop, which the mod leaves
# vanilla for the right hand), so that is the charge clip for both hands.
GOETIA_MAGIC_CASTING = {
    "name": "Goetia Animations - Magic Spell Casting",
    "folder": "Goetia Animations - Magic Spell Casting",
    "description": "Use the animations of \"Goetia Animations - Magic Spell Casting\" for Spell Hotbar NG casts. "
                   "Needs that mod installed; contains no animations itself. At game start Spell Hotbar NG copies its "
                   "animations for the hotbar (in MO2 they end up in the overwrite folder).",
    "oar_folders": ["meshes/actors/character/animations/OpenAnimationReplacer/Magic Spell Casting/Main"],
    "clip_substitutes": {"mrh_chargeloop.hkx": "mrh_precharge.hkx", "mlh_chargeloop.hkx": "mlh_precharge.hkx"},
}

# SIGMA - Magic animations - 1st person (Nexus 166987, version 1.0.3), an OAR mod, first person only. Submods per school
# (Alt / Con / Des / Ill / Res; IsEquippedType 12-16) for the right hand, left hand and both hands (B, only dual casting
# clips), plus higher priority "Cast" submods with only release clips. Several clips sit in OAR _variants_<clip> folders
# (e.g. two Destruction releases), one of them is picked per cast. Its m?h_precharge is a short lead-in and
# m?h_chargeloop the wind-up loop, like vanilla, so no clip substitutes. The *_dodge submods have no casting clips.
SIGMA_ROOT = "meshes/actors/character/_1stperson/animations/OpenAnimationReplacer/Sigma - Magic"
SIGMA_MAGIC_ANIMATIONS = {
    "name": "SIGMA - Magic animations - 1st person",
    "folder": "SIGMA - Magic animations - 1st person",
    "description": "Use the animations of \"SIGMA - Magic animations - 1st person\" for Spell Hotbar NG casts in first "
                   "person. Needs that mod installed; contains no animations itself. At game start Spell Hotbar NG copies "
                   "its animations for the hotbar (in MO2 they end up in the overwrite folder).",
    "oar_folders": [f"{SIGMA_ROOT}/Mag{school}{part}"
                    for school in ("Alt", "Con", "Des", "Ill", "Res")
                    for part in ("R", "L", "B", "RCast", "LCast")],
}

PATCHES = [SH2_ANIM_REPLACER, SMOOTH_MAGIC_CASTING, GOETIA_MAGIC_CASTING, SIGMA_MAGIC_ANIMATIONS]

# Compatibility patches: a file in Data\SKSE\Plugins\SpellHotbarNG\compat read by the DLL (src/casting/SpellCharges.h).
# Ordinator - Perks of Skyrim (Nexus 1137), Alteration perk "Vancian Magic": ORD_NewVancianMagicCast_Script (on effect
# ORD_Alt_NewVancianMagic_Effect_Ab) lowers ORD_Alt_NewVancianMagic_Global_Count in OnSpellCast for spells equipped in a
# hand, shows _Message_AlmostDepleted at 10 and _Message_Depleted + InterruptCast at 0; the "Dungeon Master" blood
# magic variant (ORD_VancianBloodMagic_Script on _Effect_Ab_Blood) also lowers it and below 0 costs
# -count * _Global_DungeonMaster_BloodMagicMult health. Hotbar casts equip nothing, so the scripts never counted them.
# Both effects sit on ORD_Alt_NewVancianMagic_Spell_Ab with exclusive conditions (normal: count > 0; blood: count <= 0
# and _Global_DungeonMaster_SideEffects == 3), so the DLL only applies the rule whose effect is currently active.
ORDINATOR = "Ordinator - Perks of Skyrim.esp"
ORDINATOR_VANCIAN_MAGIC = {
    "name": "Ordinator - Vancian Magic",
    "folder": "Ordinator - Vancian Magic",
    "plugin": ORDINATOR,
    "description": "For \"Ordinator - Perks of Skyrim\": with the Vancian Magic perk, spells cast from the hotbar use up "
                   "spell charges like normal casts (without this they were free). Needs Ordinator installed.",
    "compat": {
        "name": "Ordinator - Vancian Magic",
        "spellCharges": [
            {
                "plugin": ORDINATOR,
                "activeEffect": "0x167A0C",
                "counter": "0x167A0E",
                "messages": [
                    {"at": 10, "message": "0x167A15"},
                    {"at": 0, "message": "0x167A16", "interruptCast": True},
                ],
            },
            {
                "plugin": ORDINATOR,
                "activeEffect": "0x167A27",
                "counter": "0x167A0E",
                "healthDamagePerMissingCharge": "0x167A22",
            },
        ],
    },
}

COMPAT_PATCHES = [ORDINATOR_VANCIAN_MAGIC]


def build_compat_patch(patch: dict, out_dir: Path) -> int:
    """Writes a compatibility patch's file into out_dir (a mod root). Returns the number of files."""
    dest = out_dir / "SKSE/Plugins/SpellHotbarNG/compat" / f"{patch['folder']}.json"
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(json.dumps(patch["compat"], indent=4), encoding="utf-8")
    return 1


def build_patch(patch: dict, out_dir: Path) -> int:
    """Writes the patch files into out_dir (a mod root: meshes\\..., SKSE\\...). Returns the number of files."""
    timings = out_dir / "SKSE/Plugins/SpellHotbarNG/animations" / f"{patch['folder']}.json"
    timings.parent.mkdir(parents=True, exist_ok=True)
    if "dar_folders" in patch:
        timings.write_text(json.dumps({"name": patch["name"], "darFolders": patch["dar_folders"]}, indent=4), encoding="utf-8")
        return 1
    if "oar_folders" in patch:
        timings.write_text(json.dumps({"name": patch["name"], "oarFolders": patch["oar_folders"],
                                       "clipSubstitutes": patch.get("clip_substitutes", {})}, indent=4), encoding="utf-8")
        return 1

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

    timings.write_text(json.dumps({"name": patch["name"], "releaseTime": patch["release_time"]}, indent=4), encoding="utf-8")
    return count + 1
