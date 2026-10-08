"""
Builds the icon atlases used by Spell Hotbar NG from the SpellHotbar2 icon sources (MIT, pWn3d1337).

Every atlas is a BC3 (DXT5) DDS with a short mip chain plus a tab separated CSV:
    Key  u0  v0  u1  v1  Name
Key is one of
    Plugin.esp|0x00ABCD  - a form, resolved at runtime (light plugins supported)
    @ICON_NAME           - a named UI / fallback icon (BAR_EMPTY, DESTRUCTION_FIRE_NOVICE, ...)
    name:some_item_name  - matched against the normalised display name (used for food)

Usage: python build_icons.py [SpellHotbar2 python_scripts dir] [staging dir]   (defaults: tools/paths.py)
"""

import math
import struct
import sys
from io import BytesIO
from pathlib import Path

import numpy as np
from PIL import Image, ImageColor, ImageDraw, ImageFont

CELL = 128
MIP_LEVELS = 5  # 128 -> 8 px per cell

import paths

SRC = Path(sys.argv[1]) if len(sys.argv) > 1 else paths.SPELLHOTBAR2 / "python_scripts"
OUT = Path(sys.argv[2]) if len(sys.argv) > 2 else paths.STAGING

CORE = OUT / "00 Core"
PACKS = OUT / "10 Spell Packs"
ICON_DIR = Path("SKSE/Plugins/SpellHotbarNG/icons")

alpha_mask = None
dragon_font = None
rank_font = None
overlay_images: dict[str, np.ndarray] = {}


def image_name(name: str) -> str:
    """Same normalisation SpellHotbar2 uses to map a spell name to an icon file name"""
    return name.lower().replace(" ", "_").replace("'", "").replace(":", "").replace("-", "_").replace("’", "")


def find_image(base: str, folders: list[Path]) -> Path | None:
    for folder in folders:
        hits = sorted(folder.glob(f"{base}*.png"))
        if hits:
            return hits[0]
    return None


def load_cell(path: Path) -> Image.Image:
    img = Image.open(path).convert("RGBA")
    if img.size != (CELL, CELL):
        img = img.resize((CELL, CELL), Image.Resampling.LANCZOS)
    return img


def apply_mask(img: Image.Image) -> Image.Image:
    arr = np.array(img).astype(np.float32)
    arr[:, :, 3] *= alpha_mask
    return Image.fromarray(arr.clip(0, 255).astype(np.uint8), "RGBA")


def apply_overlay(img: Image.Image, overlay: str) -> Image.Image:
    over = overlay_images.get(overlay.lower())
    if over is None:
        return img
    out = img.copy()
    out.alpha_composite(Image.fromarray(over, "RGBA"))
    return out


def draw_text(img: Image.Image, text: str, font, anchor_xy, anchor: str, stroke: int, fill=(255, 255, 255, 255)):
    draw = ImageDraw.Draw(img)
    draw.text(anchor_xy, text, font=font, anchor=anchor, align="center", fill=fill,
              stroke_fill=ImageColor.getrgb("black"), stroke_width=stroke, spacing=0)
    return img


def make_icon(path: Path, overlay: str | None = None, shout_text: str | None = None, rank_text: str | None = None,
              mask: bool = True) -> Image.Image:
    img = load_cell(path)
    if overlay:
        img = apply_overlay(img, overlay)
    if shout_text:
        img = draw_text(img, shout_text.upper().replace(" ", "\n").replace("_", "\n"), dragon_font,
                        (CELL * 0.5, CELL * 0.5), "mm", 3)
    if rank_text:
        img = draw_text(img, rank_text, rank_font, (CELL * 0.95, CELL * 0.75), "rm", 1)
    if mask:
        img = apply_mask(img)
    return img


def encode_bc3(img: Image.Image) -> bytes:
    buf = BytesIO()
    img.save(buf, format="DDS", pixel_format="DXT5")
    data = buf.getvalue()
    assert data[:4] == b"DDS "
    blocks = max(1, (img.width + 3) // 4) * max(1, (img.height + 3) // 4)
    payload = data[128:]
    assert len(payload) == blocks * 16, f"unexpected BC3 payload size {len(payload)} for {img.size}"
    return payload


def write_dds(img: Image.Image, path: Path):
    """BC3 DDS with a mip chain, encoded level by level with Pillow"""
    levels = []
    cur = img
    for level in range(MIP_LEVELS):
        if level > 0:
            cur = cur.resize((max(1, cur.width // 2), max(1, cur.height // 2)), Image.Resampling.BOX)
        levels.append(encode_bc3(cur))
    w, h = img.size
    DDSD_CAPS, DDSD_HEIGHT, DDSD_WIDTH, DDSD_PIXELFORMAT, DDSD_MIPMAPCOUNT, DDSD_LINEARSIZE = (
        0x1, 0x2, 0x4, 0x1000, 0x20000, 0x80000)
    flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_MIPMAPCOUNT | DDSD_LINEARSIZE
    caps = 0x1000 | 0x8 | 0x400000  # TEXTURE | COMPLEX | MIPMAP
    header = struct.pack("<4sIIIIIII44x", b"DDS ", 124, flags, h, w, len(levels[0]), 0, MIP_LEVELS)
    pixel_format = struct.pack("<II4sIIIII", 32, 0x4, b"DXT5", 0, 0, 0, 0, 0)
    header += pixel_format + struct.pack("<IIIII", caps, 0, 0, 0, 0)
    assert len(header) == 128
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "wb") as f:
        f.write(header)
        for lvl in levels:
            f.write(lvl)


def build_atlas(entries: list[tuple[str, str, Image.Image | None, str | None]], out_base: Path):
    """
    entries: (key, display name, image, dedupe key). Entries with image None reuse the image of their dedupe key.
    """
    unique: dict[str, Image.Image] = {}
    for key, name, img, dedupe in entries:
        if img is not None and dedupe not in unique:
            unique[dedupe] = img
    if not unique:
        print(f"  nothing to write for {out_base.name}")
        return
    count = len(unique)
    cols = math.ceil(math.sqrt(count))
    rows = math.ceil(count / cols)
    atlas = Image.new("RGBA", (cols * CELL, rows * CELL), (0, 0, 0, 0))
    uv: dict[str, tuple[float, float, float, float]] = {}
    for i, (dedupe, img) in enumerate(unique.items()):
        x, y = (i % cols) * CELL, (i // cols) * CELL
        atlas.paste(img, (x, y))
        uv[dedupe] = (x / atlas.width, y / atlas.height, (x + CELL) / atlas.width, (y + CELL) / atlas.height)

    write_dds(atlas, out_base.with_suffix(".dds"))
    with open(out_base.with_suffix(".csv"), "w", encoding="utf-8", newline="\n") as f:
        f.write("Key\tu0\tv0\tu1\tv1\tName\n")
        seen = set()
        for key, name, img, dedupe in entries:
            if dedupe not in uv or key in seen:
                continue
            seen.add(key)
            u0, v0, u1, v1 = uv[dedupe]
            f.write(f"{key}\t{u0:.6f}\t{v0:.6f}\t{u1:.6f}\t{v1:.6f}\t{name}\n")
    print(f"  {out_base.name}: {count} icons, {len(seen)} keys, atlas {atlas.width}x{atlas.height}")


def read_list(csv_path: Path) -> list[dict[str, str]]:
    lines = csv_path.read_text(encoding="utf-8-sig").splitlines()
    header = [h.strip() for h in lines[0].split("\t")]
    rows = []
    for line in lines[1:]:
        if not line.strip():
            continue
        cols = [c.strip() for c in line.split("\t")]
        cols += [""] * (len(header) - len(cols))
        rows.append(dict(zip(header, cols)))
    return rows


def norm_id(raw: str) -> str | None:
    raw = raw.strip()
    if not raw:
        return None
    value = int(raw, 16)
    return f"0x{value:06X}"


def spell_list_entries(csv_paths: list[Path], folders: list[Path]) -> list[tuple[str, str, Image.Image | None, str | None]]:
    entries = []
    cache: dict[str, Image.Image] = {}
    missing = []
    for csv_path in csv_paths:
        for row in read_list(csv_path):
            name = row.get("Name", "")
            path = find_image(image_name(name), folders)
            if path is None:
                missing.append(name)
                continue
            shout = row.get("Shouttext") or None
            rank = row.get("Ranktext") or None
            overlay = row.get("overlay") or None
            dedupe = f"{path}|{shout}|{rank}|{overlay}"
            img = None
            if dedupe not in cache:
                cache[dedupe] = make_icon(path, overlay, shout, rank)
                img = cache[dedupe]
            plugin = row.get("Plugin", "")
            for col in ("FormID", "ScrollID"):
                form = norm_id(row.get(col, ""))
                if form and plugin:
                    entries.append((f"{plugin}|{form}", name, img, dedupe))
                    img = None
    if missing:
        print(f"  no icon for: {', '.join(missing)}")
    return entries


def named_entries(names: list[str], folders: list[Path], no_mask: set[str] = frozenset(), key_prefix: str = "@",
                  white: set[str] = frozenset()):
    """white: icons turned into a white shape (alpha kept) that the HUD fills with a color"""
    entries = []
    for icon in names:
        path = find_image(image_name(icon), folders)
        if path is None:
            print(f"  missing named icon {icon}")
            continue
        overlay = None
        parts = icon.split("_")
        if len(parts) > 1 and parts[0].lower() in overlay_images:
            overlay = parts[0].lower()
        shout = "DS" if "shout" in image_name(icon) else None
        img = make_icon(path, overlay, shout, None, mask=icon not in no_mask)
        if icon in white:
            full = Image.new("L", img.size, 255)
            img = Image.merge("RGBA", (full, full, full, img.convert("RGBA").getchannel("A")))
        key = f"{key_prefix}{icon}"
        entries.append((key, icon, img, key))
    return entries


UI_ICONS = [
    "UNKNOWN", "BAR_EMPTY", "BAR_OVERLAY", "BAR_HIGHLIGHT", "LESSER_POWER", "GREATER_POWER", "SHOUT_GENERIC",
    "SCROLL_OVERLAY", "SINGLE_CAST", "DUAL_CAST",
    "GENERIC_POTION", "GENERIC_POTION_SMALL", "GENERIC_POTION_LARGE",
    "GENERIC_POISON", "GENERIC_POISON_SMALL", "GENERIC_POISON_LARGE",
    "GENERIC_FOOD", "GENERIC_FOOD_SOUP", "GENERIC_FOOD_DRINK",
]
for lvl in ["NOVICE", "APPRENTICE", "ADEPT", "EXPERT", "MASTER"]:
    UI_ICONS += [f"DESTRUCTION_{e}_{lvl}" for e in ["FIRE", "FROST", "SHOCK", "GENERIC"]]
    UI_ICONS += [f"ALTERATION_{lvl}"]
    UI_ICONS += [f"RESTORATION_{e}_{lvl}" for e in ["FRIENDLY", "HOSTILE"]]
    UI_ICONS += [f"ILLUSION_{e}_{lvl}" for e in ["FRIENDLY", "HOSTILE"]]
    UI_ICONS += [f"CONJURATION_{e}_{lvl}" for e in ["BOUND_WEAPON", "SUMMON"]]

# (pack id, display name, plugin used for the FOMOD auto detection, spell list csvs, icon folders, credits)
SPELL_PACKS = [
    ("abyss", "Abyss", "Abyss.esp", ["mods/abyss"], ["abyss"], None),
    ("abyssal_tides_magic", "Abyssal Tides Magic", "Aqua.esl", ["mods/abyssal_tides_magic"], ["abyssal_tides_magic"], None),
    ("abyssal_wind_magic", "Abyssal Wind Magic", "Aero.esl", ["mods/abyssal_wind_magic"], ["abyssal_wind_magic"], None),
    ("ancient_blood_magic_2", "Ancient Blood Magic II", "AncientBloodII.esl", ["mods/ancient_blood_magic_2"], ["ancient_blood_magic_2"], None),
    ("andromeda", "Andromeda - Unique Standing Stones", "Andromeda - Unique Standing Stones of Skyrim.esp", ["mods/andromeda"], ["andromeda"], None),
    ("apocalypse", "Apocalypse - Magic of Skyrim", "Apocalypse - Magic of Skyrim.esp",
     [f"mods/apocalypse_{s}" for s in ["alteration", "conjuration", "destruction", "illusion", "restoration", "scrolls"]],
     [f"apocalypse_{s}" for s in ["alteration", "conjuration", "destruction", "illusion", "restoration"]], None),
    ("arclight", "Arclight", "Arclight.esp", ["mods/arclight"], ["arclight"], None),
    ("astral_magic_2", "Astral Magic 2", "Astral.esl", ["mods/astral_magic_2"], ["astral_magic_2"], None),
    ("constellation_magic", "Constellation Magic", "Supernova.esl", ["mods/constellation_magic"], ["constellation_magic"], None),
    ("dark_hierophant_magic", "Dark Hierophant Magic", "Ghostlight.esl", ["mods/dark_hierophant_magic"], ["dark_hierophant_magic"], None),
    ("desecration", "Desecration", "Desecration.esp", ["mods/desecration"], ["desecration"], None),
    ("elemental_destruction_magic_redux", "Elemental Destruction Magic Redux", "Elemental Destruction Magic Redux.esp",
     ["mods/elemental_destruction_magic_redux"], ["elemental_destruction_magic_redux"], None),
    ("elemental_mastery_magic", "Elemental Mastery Magic", "KittySpellPack02.esl", ["mods/elemental_mastery_magic"], ["elemental_mastery_magic"], None),
    ("holy_templar_magic", "Holy Templar Magic", "Lightpower.esl", ["mods/holy_templar_magic"], ["holy_templar_magic"], None),
    ("miracles_of_skyrim", "Miracles of Skyrim", "DS2Miracles.esp", ["mods/miracles_of_skyrim"], ["miracles_of_skyrim"], None),
    ("mysticism", "Mysticism - A Magic Overhaul", "MysticismMagic.esp",
     [f"mods/mysticism_{s}" for s in ["alteration", "conjuration", "destruction", "illusion", "restoration"]],
     [f"mysticism_{s}" for s in ["alteration", "conjuration", "destruction", "illusion", "restoration"]], None),
    ("obscure_magic", "Obscure Magic", "KittySpellPack01.esl", ["mods/obscure_magic"], ["obscure_magic"], None),
    ("odin", "Odin - Skyrim Magic Overhaul", "Odin - Skyrim Magic Overhaul.esp",
     [f"mods/odin_{s}" for s in ["alteration", "conjuration", "destruction", "illusion", "restoration"]],
     [f"odin_{s}" for s in ["alteration", "conjuration", "destruction", "illusion", "restoration"]], None),
    ("sacrosanct", "Sacrosanct - Vampires of Skyrim", "Sacrosanct - Vampires of Skyrim.esp", ["mods/sacrosanct"], ["sacrosanct"], None),
    ("sonic_magic", "Sonic Magic", "Shockwave.esl", ["mods/sonic_magic"], ["sonic_magic"], None),
    ("stellaris", "Stellaris", "Stellaris.esp", ["mods/stellaris"], ["stellaris"], None),
    ("storm_calling_magic2", "Storm Calling Magic 2", "StormCalling.esl", ["mods/storm_calling_magic2"], ["storm_calling_magic2"], None),
    ("thunderchild", "Thunderchild - Epic Shouts", "Thunderchild - Epic Shout Package.esp", ["mods/thunderchild"], ["thunderchild"], None),
    ("triumvirate", "Triumvirate - Mage Archetypes", "Triumvirate - Mage Archetypes.esp",
     [f"mods/triumvirate_{a}" for a in ["cleric", "druid", "shadow_mage", "shaman", "warlock"]],
     [f"triumvirate_{a}" for a in ["cleric", "druid", "shadow_mage", "shaman", "warlock"]], None),
    ("vulcano", "Vulcano", "Vulcano.esp", ["mods/vulcano"], ["vulcano"], None),
    ("winter_wonderland_magic", "Winter Wonderland Magic", "Icebloom.esl", ["mods/winter_wonderland_magic"], ["winter_wonderland_magic"], None),
    ("witcher_signs", "The Witcher Signs", "W3S.esl", ["mods/witcher_signs"], ["witcher_signs"], None),
    ("shadow_spell_package", "Shadow Spell Package", "ShadowSpellPackage.esp", ["mods_contributed/shadow_spell_package"],
     ["contributed:shadow_spell_package"], "ArchAngelAries"),
    ("star_wars_spell_pack", "Star Wars Spell Pack", "starwarsspellpack.esp", ["mods_contributed/star_wars_spell_pack"],
     ["contributed:star_wars_spell_pack"], "ArchAngelAries"),
    ("star_wars_spell_pack_esl", "Star Wars Spell Pack (ESL version)", "starwarsspellpack_ESLversion.esl",
     ["mods_contributed/star_wars_spell_pack_esl"], ["contributed:star_wars_spell_pack"], "ArchAngelAries"),
    ("undead_horse", "Skull Commander Armor - Undead Horse", "Undead Horse.esl", ["mods_contributed/undead_horse"],
     ["contributed:undead_horse"], "ArchAngelAries"),
]

PERK_PACKS = [
    ("ordinator", "Ordinator - Perks of Skyrim", "Ordinator - Perks of Skyrim.esp", ["mods/ordinator"], ["ordinator"], None),
    ("path_of_sorcery", "Path of Sorcery", "PathOfSorcery.esp", ["mods/path_of_sorcery"], ["path_of_sorcery"], None),
    ("sperg", "SPERG", "SPERG-SSE.esp", ["mods/sperg"], ["sperg"], None),
]


def icon_folder(name: str) -> Path:
    if name.startswith("contributed:"):
        return SRC / "modded_spell_icons_contributed" / name.split(":", 1)[1]
    return SRC / "modded_spell_icons" / name


def build_pack(pack) -> None:
    pack_id, display, plugin, lists, folders, credits = pack
    print(f"pack {pack_id}")
    entries = spell_list_entries([SRC / "spell_lists2" / f"{l}.csv" for l in lists], [icon_folder(f) for f in folders])
    build_atlas(entries, PACKS / pack_id / ICON_DIR / pack_id)


def main():
    global alpha_mask, dragon_font, rank_font
    mask_img = Image.open(SRC / "icons/alpha_mask.png").convert("L").resize((CELL, CELL), Image.Resampling.LANCZOS)
    alpha_mask = np.array(mask_img).astype(np.float32) / 255.0
    dragon_font = ImageFont.truetype(str(SRC / "icons/6_$DragonFont_Dragon_script.ttf"), round(CELL * 0.5))
    rank_font = ImageFont.truetype(str(SRC / "icons/sovngarde_font.ttf"), round(CELL * 0.6))
    for school in ["alteration", "conjuration", "destruction", "illusion", "restoration"]:
        overlay_images[school] = np.array(load_cell(SRC / f"icons/school_{school}.png"))

    only = set(sys.argv[3:])

    vi = SRC / "vanilla_spell_icons"
    lists = SRC / "spell_lists2"
    core_icons = CORE / ICON_DIR

    if not only or "core" in only:
        print("ui")
        ui_folders = [SRC / "icons"] + [vi / f"{s}_generic" for s in
                                        ["alteration", "restoration", "destruction", "illusion", "conjuration",
                                         "shouts", "potions", "foods"]]
        build_atlas(named_entries(UI_ICONS, ui_folders, no_mask={"BAR_EMPTY", "BAR_OVERLAY", "BAR_HIGHLIGHT"}),
                    core_icons / "ui")
        print("ui nordic")
        build_atlas(named_entries(["BAR_EMPTY", "BAR_OVERLAY"], [SRC / "icons/nordic_ui"],
                                  no_mask={"BAR_EMPTY", "BAR_OVERLAY"}, white={"BAR_EMPTY"}), core_icons / "ui_nordic")

        print("vanilla spells")
        schools = ["alteration", "destruction", "restoration", "illusion", "conjuration"]
        build_atlas(spell_list_entries([lists / f"{s}.csv" for s in schools], [vi / s for s in schools]),
                    core_icons / "vanilla_spells")
        print("vanilla powers + shouts")
        build_atlas(spell_list_entries([lists / "powers.csv", lists / "shouts.csv"], [vi / "powers", vi / "shouts"]),
                    core_icons / "vanilla_powers")
        print("vanilla potions")
        build_atlas(spell_list_entries([lists / "potions.csv"], [vi / "potions"]), core_icons / "vanilla_potions")
        print("vanilla poisons")
        build_atlas(spell_list_entries([lists / "poisons.csv"], [vi / "poisons"]), core_icons / "vanilla_poisons")
        print("vanilla food")
        foods = [p.stem for p in sorted((vi / "foods_vanilla").glob("*.png"))]
        build_atlas(named_entries(foods, [vi / "foods_vanilla"], key_prefix="name:"), core_icons / "vanilla_food")

    for pack in SPELL_PACKS + PERK_PACKS:
        if not only or pack[0] in only or "packs" in only:
            build_pack(pack)


if __name__ == "__main__":
    main()
