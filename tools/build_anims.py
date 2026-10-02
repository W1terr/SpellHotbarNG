"""
Builds the Open Animation Replacer package for the casting animations.

Like Spell Hotbar 2, the hotbar starts the shout animation ("ShoutStart" / "MT_BreathExhaleShort") and OAR swaps
the shout clips for vanilla magic casting clips. Instead of ESP globals the submods use the custom condition
"SpellHotbarNG_Casting" registered by the DLL: "Animation" is the cast type and "Hand" the casting hand
(see src/CastAnim.h). Hand specific submods have a higher priority than the "any hand" ones.

The clips are copied out of the game's Skyrim - Animations.bsa.
Usage: python build_anims.py [path to Skyrim - Animations.bsa] [staging dir]   (defaults: tools/paths.py)
"""

import json
import sys
from pathlib import Path

import paths
from bsa import BSA

GAME_BSA = paths.GAME_DATA / "Skyrim - Animations.bsa"
STAGING = paths.STAGING
PLUGIN_VERSION = "1.1.0"  # first DLL with the "Hand" component and the ritual type
MOVE_PLUGIN_VERSION = "1.2.0"  # first DLL with the "ShoutState" component

OAR_ROOT = Path("meshes/actors/character/OpenAnimationReplacer/SpellHotbarNG")

# shout clips that get replaced: charge ("inhale") and release ("exhale"), standing / armed / sneaking
INHALE_TARGETS = ["mt_shout_inhale.hkx", "1hm_shout_inhale.hkx", "sneak1hm_shout_inhale.hkx"]
EXHALE_TARGETS = ["mt_shout_exhale.hkx", "1hm_shout_exhale.hkx", "sneak1hm_shout_exhale.hkx"]

# 1st person only: while moving, the shout behavior blends the shout clip with its own walk / run clips (MT_ShoutLocomotionBlend:
# arms only 50% from the shout clip, the rest from the unarmed run), which pulls the hands out of view. While a hotbar cast
# holds the graph in the shout state these walk / run clips play the cast's clips too, so the arms follow the casting
# animation. The camera comes from the shout clip anyway; legs aren't drawn in 1st person. The same files are the normal
# unarmed walk / run, so these "_move" submods use the ShoutState condition (only while the graph is in the shout state for
# our cast). The walk / run clips start while charging, so they're interruptible and the Phase condition swaps them to the
# release clip ("_move_release") when the spell is released. Those slots loop, so right before the release clip would start
# over they switch to the ready pose the release ends in ("_move_ready", Phase 3).
MOVE_BLEND_TIME = 0.1  # seconds, blend when the walk / run clip switches from the charge to the release clip
MOVE_TARGETS = [f"mt_{gait}{direction}.hkx" for gait in ("walk", "run") for direction in (
    "forward", "forwardleft", "forwardright", "left", "right", "backward", "backwardleft", "backwardright")]

# 3rd person: the moving shout state blends the shout clip (arms half) with the unarmed walk / run (animations\male and
# \female), so the arms swing half way between running and casting. While a hotbar cast holds the shout state those clips
# become the magic casting locomotion (magcast_*: arms held for casting, legs run), like the magic behavior does.
# Any cast type, so one submod ("0_locomotion"). The same list is in src/casting/Replacers.cpp.
LOCOMOTION_CLIPS = [
    ("mt_walkforward.hkx", "magcast_walkforward.hkx"),
    ("mt_walkforwardright.hkx", "magcast_walkfrwrdright.hkx"),
    ("mt_walkright.hkx", "magcast_walkright.hkx"),
    ("mt_walkbackwardright.hkx", "magcast_walkbckwrdrht.hkx"),
    ("mt_walkbackward.hkx", "magcast_walkbackward.hkx"),
    ("mt_walkbackwardleft.hkx", "magcast_walkbckwrdleft.hkx"),
    ("mt_walkleft.hkx", "magcast_walkleft.hkx"),
    ("mt_walkforwardleft.hkx", "magcast_walkforwrdleft.hkx"),
    ("mt_runforward.hkx", "magcast_runforward.hkx"),
    ("mt_runforwardright.hkx", "magcast_runfrwrdright.hkx"),
    ("mt_runright.hkx", "magcast_runright.hkx"),
    ("mt_runbackwardright.hkx", "magcast_runbckwrdright.hkx"),
    ("mt_runbackward.hkx", "magcast_runbackward.hkx"),
    ("mt_runbackwardleft.hkx", "magcast_runbackwrdleft.hkx"),
    ("mt_runleft.hkx", "magcast_runleft.hkx"),
    ("mt_runforwardleft.hkx", "magcast_runforwardleft.hkx"),
]
LOCOMOTION_DIRS = ["animations/male", "animations/female"]

HAND_ANY, HAND_RIGHT, HAND_LEFT = 0, 1, 2

# (folder, description, cast type (src/CastAnim.h), hand, charge clip, release clip)
SUBMODS = [
    ("1_aimed", "One hand, aimed", 1, HAND_ANY, "mrh_chargeloop.hkx", "mrh_release.hkx"),
    ("1_aimed_left", "Left hand, aimed", 1, HAND_LEFT, "mlh_chargeloop.hkx", "mlh_release.hkx"),
    ("2_self", "One hand, on self", 2, HAND_ANY, "mrh_selfchargeloop.hkx", "mrh_selfrelease.hkx"),
    ("2_self_left", "Left hand, on self", 2, HAND_LEFT, "mlh_selfchargeloop.hkx", "mlh_selfrelease.hkx"),
    ("3_aimed_concentration", "One hand concentration, aimed", 3, HAND_ANY, "mrh_aimedconcentration.hkx", "mrh_release.hkx"),
    ("3_aimed_concentration_left", "Left hand concentration, aimed", 3, HAND_LEFT, "mlh_aimedconcentration.hkx", "mlh_release.hkx"),
    ("4_self_concentration", "One hand concentration, on self", 4, HAND_ANY, "mrh_selfconcentration.hkx", "mrh_selfrelease.hkx"),
    ("4_self_concentration_left", "Left hand concentration, on self", 4, HAND_LEFT, "mlh_selfconcentration.hkx", "mlh_selfrelease.hkx"),
    ("5_dual_aimed", "Dual cast, aimed", 5, HAND_ANY, "ritualspell_charge.hkx", "ritualspell_aimrelease.hkx"),
    ("6_dual_self", "Dual cast, on self", 6, HAND_ANY, "ritualspell_charge.hkx", "ritualspell_release.hkx"),
    ("7_dual_concentration", "Dual concentration", 7, HAND_ANY, "mlhmrh_aimedconcentrationloop.hkx", "dmagaimrelease.hkx"),
    ("8_ritual", "Ritual (two-handed) spell", 8, HAND_ANY, "ritualspell_charge.hkx", "ritualspell_release.hkx"),
    ("9_dual_self_concentration", "Dual concentration, on self", 9, HAND_ANY, "dmagselfconloop.hkx", "dmagselfrelease.hkx"),
]

# 1st person ready loop (the pose a release ends in) per cast type and hand, for the "_move_ready" submods
def ready_clip(type_id: int, hand: int) -> str:
    if type_id in (5, 6, 7, 8, 9):
        return "ritualspell_ready.hkx"
    prefix = "mlh" if hand == HAND_LEFT else "mrh"
    return f"{prefix}_selfreadyloop.hkx" if type_id in (2, 4) else f"{prefix}_readyloop.hkx"


VIEWS = {
    "animations": "meshes\\actors\\character\\animations\\",
    "_1stperson/animations": "meshes\\actors\\character\\_1stperson\\animations\\",
}


def casting_condition(type_id: int, hand: int, required_version: str = PLUGIN_VERSION, shout_state: bool = False, phase: int = 0) -> dict:
    """The OAR condition entry for a cast type (and hand, 0 = any)"""
    condition = {
        "condition": "SpellHotbarNG_Casting",
        "requiredPlugin": "SpellHotbarNG",
        "requiredVersion": MOVE_PLUGIN_VERSION if shout_state else required_version,
        "Animation": {"value": float(type_id)},
    }
    if hand != HAND_ANY:
        condition["Hand"] = {"value": float(hand)}
    if shout_state:
        condition["ShoutState"] = {"value": 1.0}
    if phase:
        condition["Phase"] = {"value": float(phase)}
    return condition


def submod_config(folder: str, description: str, type_id: int, hand: int, move_phase: int = 0) -> dict:
    """move_phase: 0 = shout clips, 1 / 2 / 3 = 1st person walk / run clips while charging / releasing / after the release"""
    config = {
        "name": folder,
        "description": f"Spell Hotbar NG casting animation: {description}" + (
            {0: "", 1: " (1st person, moving, charge)", 2: " (1st person, moving, release)", 3: " (1st person, moving, after the release)"}[move_phase]),
        "priority": 2140000000 + type_id * 10 + (hand != HAND_ANY),
        "interruptible": bool(move_phase),
        "conditions": [casting_condition(type_id, hand, shout_state=bool(move_phase), phase=move_phase)],
    }
    if move_phase:
        config["hasCustomBlendTimeOnInterrupt"] = True
        config["blendTimeOnInterrupt"] = MOVE_BLEND_TIME
    return config


def build(bsa_path: Path = GAME_BSA, staging: Path = STAGING):
    out_root = staging / "00 Core" / OAR_ROOT
    bsa = BSA(bsa_path)

    out_root.mkdir(parents=True, exist_ok=True)
    (out_root / "config.json").write_text(json.dumps({
        "name": "Spell Hotbar NG",
        "author": "Iuko",
        "description": "Vanilla casting animations for spells cast from Spell Hotbar NG. Only active while the hotbar casts.",
    }, indent=4), encoding="utf-8")

    count = 0
    for folder, description, type_id, hand, charge, release in SUBMODS:
        sub = out_root / folder
        sub.mkdir(parents=True, exist_ok=True)
        (sub / "config.json").write_text(json.dumps(submod_config(folder, description, type_id, hand), indent=4), encoding="utf-8")
        for view, bsa_dir in VIEWS.items():
            for clip, targets in ((charge, INHALE_TARGETS), (release, EXHALE_TARGETS)):
                data = bsa.read(bsa_dir + clip)
                for target in targets:
                    dest = sub / view / target
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    dest.write_bytes(data)
                    count += 1

        for move_phase, suffix, clip in ((1, "_move", charge), (2, "_move_release", release), (3, "_move_ready", ready_clip(type_id, hand))):
            move = out_root / f"{folder}{suffix}"
            move.mkdir(parents=True, exist_ok=True)
            (move / "config.json").write_text(
                json.dumps(submod_config(f"{folder}{suffix}", description, type_id, hand, move_phase), indent=4), encoding="utf-8")
            data = bsa.read(VIEWS["_1stperson/animations"] + clip)
            for target in MOVE_TARGETS:
                dest = move / "_1stperson" / "animations" / target
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes(data)
                count += 1

    locomotion = out_root / "0_locomotion"
    locomotion.mkdir(parents=True, exist_ok=True)
    (locomotion / "config.json").write_text(json.dumps({
        "name": "0_locomotion",
        "description": "Spell Hotbar NG casting animation: walk / run with the arms held for casting (3rd person, any cast)",
        "priority": 2140000001,
        "interruptible": False,
        "conditions": [casting_condition(0, HAND_ANY, shout_state=True)],
    }, indent=4), encoding="utf-8")
    for target, clip in LOCOMOTION_CLIPS:
        data = bsa.read(VIEWS["animations"] + clip)
        for folder in LOCOMOTION_DIRS:
            dest = locomotion / folder / target
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(data)
            count += 1
    print(f"wrote {count} animation files to {out_root}")


if __name__ == "__main__":
    build(Path(sys.argv[1]) if len(sys.argv) > 1 else GAME_BSA, Path(sys.argv[2]) if len(sys.argv) > 2 else STAGING)
