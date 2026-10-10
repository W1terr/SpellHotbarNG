"""
Assembles the FOMOD installer for Spell Hotbar NG and zips it.

Expects the icon atlases from build_icons.py in the staging folder and a built DLL.
Usage: python package.py [output zip]   (folders: tools/paths.py)
"""

import shutil
import sys
import zipfile
from pathlib import Path
from xml.sax.saxutils import escape

import build_anims
import build_icons as icons
import paths
import patches

VERSION = "1.9"
ROOT = Path(__file__).parent.parent
BUILD = paths.PLUGIN_BUILD
STAGING = icons.OUT
SRC = icons.SRC
OUTPUT = Path(sys.argv[1]) if len(sys.argv) > 1 else paths.OUTPUT / f"Spell Hotbar NG {VERSION}.zip"

CORE = STAGING / "00 Core"
PACKS = STAGING / "10 Spell Packs"
PATCHES = STAGING / "30 Patches"
COMPAT = STAGING / "40 Compatibility"
IMAGES = STAGING / "fomod" / "images"
PLUGIN_DIR = Path("SKSE/Plugins")
DATA_DIR = PLUGIN_DIR / "SpellHotbarNG"

def copy(src: Path, dst: Path):
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def spell_pack_plugin_xml(pack) -> str:
    pack_id, display, plugin, _, _, credits = pack
    credit = f"\n\nIcons contributed by {credits}." if credits else ""
    desc = f"Icons for {display} ({plugin}).{credit}"
    return f"""
                        <plugin name="{escape(display)}">
                            <description>{escape(desc)}</description>
                            <files>
                                <folder source="10 Spell Packs\\{pack_id}" destination="" priority="0"/>
                            </files>
                            <typeDescriptor>
                                <dependencyType>
                                    <defaultType name="Optional"/>
                                    <patterns>
                                        <pattern>
                                            <dependencies operator="And">
                                                <fileDependency file="{escape(plugin)}" state="Active"/>
                                            </dependencies>
                                            <type name="Recommended"/>
                                        </pattern>
                                    </patterns>
                                </dependencyType>
                            </typeDescriptor>
                        </plugin>"""


def patch_plugin_xml(patch: dict) -> str:
    return f"""
                        <plugin name="{escape(patch['name'])}">
                            <description>{escape(patch['description'])}</description>
                            <files>
                                <folder source="30 Patches\\{escape(patch['folder'])}" destination="" priority="0"/>
                            </files>
                            <typeDescriptor>
                                <type name="Optional"/>
                            </typeDescriptor>
                        </plugin>"""


def compat_plugin_xml(patch: dict) -> str:
    """Recommended (pre-selected) when the mod's plugin is active"""
    return f"""
                        <plugin name="{escape(patch['name'])}">
                            <description>{escape(patch['description'])}</description>
                            <files>
                                <folder source="40 Compatibility\\{escape(patch['folder'])}" destination="" priority="0"/>
                            </files>
                            <typeDescriptor>
                                <dependencyType>
                                    <defaultType name="Optional"/>
                                    <patterns>
                                        <pattern>
                                            <dependencies operator="And">
                                                <fileDependency file="{escape(patch['plugin'])}" state="Active"/>
                                            </dependencies>
                                            <type name="Recommended"/>
                                        </pattern>
                                    </patterns>
                                </dependencyType>
                            </typeDescriptor>
                        </plugin>"""


VANILLA_ANIM_TEXT = (
    "By default Spell Hotbar NG uses the vanilla Skyrim casting animations. They come with the main files "
    "(Open Animation Replacer plays them), so there is nothing to choose here. Only pick a patch below if you use one of those animation mods and "
    "want its animations for hotbar casts too."
)


def module_config() -> str:
    packs = "".join(spell_pack_plugin_xml(p) for p in sorted(icons.SPELL_PACKS, key=lambda p: p[1].lower()))
    perks = "".join(spell_pack_plugin_xml(p) for p in sorted(icons.PERK_PACKS, key=lambda p: p[1].lower()))
    anim_patches = "".join(patch_plugin_xml(p) for p in patches.PATCHES)
    compat_patches = "".join(compat_plugin_xml(p) for p in patches.COMPAT_PATCHES)
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<config xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="http://qconsulting.ca/fo3/ModConfig5.0.xsd">
    <moduleName>Spell Hotbar NG {VERSION}</moduleName>
    <requiredInstallFiles>
        <folder source="00 Core" destination="" priority="0"/>
    </requiredInstallFiles>
    <installSteps order="Explicit">
        <installStep name="Spell Packs">
            <optionalFileGroups order="Explicit">
                <group name="Spell mod icons (auto-selected for installed mods)" type="SelectAny">
                    <plugins order="Explicit">{packs}
                    </plugins>
                </group>
                <group name="Perk overhaul icons (powers and spells added by perks)" type="SelectAny">
                    <plugins order="Explicit">{perks}
                    </plugins>
                </group>
            </optionalFileGroups>
        </installStep>
        <installStep name="Casting Animations">
            <optionalFileGroups order="Explicit">
                <group name="By default Spell Hotbar NG uses the vanilla casting animations" type="SelectAll">
                    <plugins order="Explicit">
                        <plugin name="Vanilla casting animations (always installed)">
                            <description>{escape(VANILLA_ANIM_TEXT)}</description>
                            <conditionFlags>
                                <flag name="vanilla_anims">On</flag>
                            </conditionFlags>
                            <typeDescriptor>
                                <type name="Required"/>
                            </typeDescriptor>
                        </plugin>
                    </plugins>
                </group>
                <group name="Optional: use the animations of a mod you have installed (only pick the ones you have)" type="SelectAny">
                    <plugins order="Explicit">{anim_patches}
                    </plugins>
                </group>
            </optionalFileGroups>
        </installStep>
        <installStep name="Compatibility">
            <optionalFileGroups order="Explicit">
                <group name="Fixes for other mods (auto-selected for installed mods)" type="SelectAny">
                    <plugins order="Explicit">{compat_patches}
                    </plugins>
                </group>
            </optionalFileGroups>
        </installStep>
    </installSteps>
</config>
"""


def info_xml() -> str:
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<fomod>
    <Name>Spell Hotbar NG</Name>
    <Author>Iuko</Author>
    <Version MachineVersion="{VERSION}">{VERSION}</Version>
    <Description>A spell hotbar for Skyrim, pure SKSE (no ESP), configured through SKSE Menu Framework. Casting animations through Open Animation Replacer. Icons from Spell Hotbar 2 by pWn3d1337 (MIT).</Description>
    <Groups>
        <element>Gameplay</element>
        <element>User Interface</element>
    </Groups>
</fomod>
"""


def main():
    dll = BUILD / "SpellHotbarNG.dll"
    if not dll.exists():
        sys.exit(f"missing {dll}, build first")
    if not (CORE / DATA_DIR / "icons" / "ui.dds").exists():
        sys.exit("missing icon atlases, run build_icons.py first")

    copy(dll, CORE / PLUGIN_DIR / dll.name)
    copy(BUILD / "SpellHotbarNG.pdb", CORE / PLUGIN_DIR / "SpellHotbarNG.pdb")
    copy(ROOT / "LICENSE", CORE / DATA_DIR / "LICENSE.txt")
    copy(ROOT / "res" / "CREDITS.txt", CORE / DATA_DIR / "CREDITS.txt")
    shutil.rmtree(CORE / DATA_DIR / "lang", ignore_errors=True)
    for lang in sorted((ROOT / "res" / "lang").glob("*.json")):
        copy(lang, CORE / DATA_DIR / "lang" / lang.name)

    # outputs of older versions that would otherwise end up in the zip (preset profiles, installer images)
    for stale in (CORE / DATA_DIR / "presets", STAGING / "20 Auto Profiles", CORE / "Interface"):
        shutil.rmtree(stale, ignore_errors=True)
    shutil.rmtree(IMAGES, ignore_errors=True)  # installer images (banner, layout previews) are no longer used

    (STAGING / "fomod").mkdir(parents=True, exist_ok=True)
    (STAGING / "fomod" / "info.xml").write_text(info_xml(), encoding="utf-8")
    (STAGING / "fomod" / "ModuleConfig.xml").write_text(module_config(), encoding="utf-8")

    build_anims.build(staging=STAGING)

    shutil.rmtree(PATCHES, ignore_errors=True)
    for patch in patches.PATCHES:
        count = patches.build_patch(patch, PATCHES / patch["folder"])
        print(f"patch {patch['name']}: {count} files")
    shutil.rmtree(COMPAT, ignore_errors=True)
    for patch in patches.COMPAT_PATCHES:
        count = patches.build_compat_patch(patch, COMPAT / patch["folder"])
        print(f"compatibility patch {patch['name']}: {count} files")

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    if OUTPUT.exists():
        OUTPUT.unlink()
    with zipfile.ZipFile(OUTPUT, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for file in sorted(STAGING.rglob("*")):
            if file.is_file():
                zf.write(file, file.relative_to(STAGING).as_posix())
    print(f"wrote {OUTPUT} ({OUTPUT.stat().st_size / 1e6:.1f} MB)")
    write_complete_zip()
    write_patch_zips()


def write_complete_zip():
    """Everything merged (core + every spell pack), for installing without the FOMOD"""
    out = OUTPUT.with_name(f"{OUTPUT.stem} - Complete (no installer).zip")
    files = {f.relative_to(CORE).as_posix(): f for f in CORE.rglob("*") if f.is_file()}
    for pack in PACKS.iterdir():
        files.update({f.relative_to(pack).as_posix(): f for f in pack.rglob("*") if f.is_file()})
    if out.exists():
        out.unlink()
    with zipfile.ZipFile(out, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for arc, file in sorted(files.items()):
            zf.write(file, arc)
    print(f"wrote {out} ({out.stat().st_size / 1e6:.1f} MB)")


def write_patch_zips():
    """Every animation / compatibility patch on its own, for the no-installer version"""
    for patch, root in [(p, PATCHES / p["folder"]) for p in patches.PATCHES] + [(p, COMPAT / p["folder"]) for p in patches.COMPAT_PATCHES]:
        out = OUTPUT.with_name(f"Spell Hotbar NG - {patch['name']} Patch.zip")
        if out.exists():
            out.unlink()
        with zipfile.ZipFile(out, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
            for file in sorted(root.rglob("*")):
                if file.is_file():
                    zf.write(file, file.relative_to(root).as_posix())
        print(f"wrote {out}")


if __name__ == "__main__":
    main()
