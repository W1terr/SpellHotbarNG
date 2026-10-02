# Spell Hotbar NG

A spell and item hotbar for Skyrim SE / AE, written purely as an SKSE plugin: no ESP, no Papyrus. Everything is set
up in the [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352) menu. Inspired by and
using the icons of [Spell Hotbar 2](https://github.com/pWn3d1337/Skyrim_SpellHotbar2).

## Features

- **Up to 20 slots** for spells, scrolls, powers, shouts, potions, food, weapons, armor, ammo and torches, plus up to
  three extra bars reached by holding a key (e.g. Shift + 1).
- **Binding**: select a spell or item in the Magic / Inventory / Favorites menu and press a slot key.
- **Casting straight from the bar** with the vanilla casting animations, without equipping anything: real charge
  time, magicka cost, concentration while the key is held, dual casting with the vanilla perks, hand glow, sounds.
  The next cast waits for the previous animation. No casting while jumping, swimming or attacking; casting ends a
  sprint like vanilla. In first person the casting pose holds while running.
- **Aiming at the crosshair**: aimed spells, runes and summons go where the crosshair points (a camera ray, launched
  from the casting hand). In third person the character turns toward the camera while casting.
- **Powers and shouts** go through the game's own Shout control (hold for more words); the previous power comes back.
- **What slot keys do**: *Cast right away*, *Equip* (spells / scrolls into the slot's hand, powers and shouts into the
  voice slot) or *Oblivion style* (a slot key picks the spell or potion, a Cast key and a Potion key use them while
  the hands keep their weapons; a small extra bar shows the picked spell, potion and power).
- **Item pictures**: weapons, armor, ammo and torches without an icon get a picture of their own inventory model,
  photographed once by the game's 3D item view and cached.
- **Layout and look**: size, rows, anchor, offsets, colors, visibility (always / combat / weapon drawn / sneaking)
  with a fade + slide animation. Optionally the slot keys only work while sneaking.
- **Profiles** for settings and bar contents; the bar contents are saved with each character's save game.

## Requirements

- Skyrim SE / AE with [SKSE](https://skse.silverlock.org/) (built against CommonLibSSE-NG, tested on 1.7.104)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
- [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352) 3 or newer
- [Open Animation Replacer](https://www.nexusmods.com/skyrimspecialedition/mods/92109) for the casting animations
  (without it spells are cast without animation)

## Files the mod writes

| What | Where |
| --- | --- |
| Settings | `Documents\My Games\Skyrim Special Edition\SKSE\SpellHotbarNG\settings.json` |
| Profiles | `Data\SKSE\Plugins\SpellHotbarNG\profiles` (Mod Organizer 2: the overwrite folder) |
| Item pictures | `Data\SKSE\Plugins\SpellHotbarNG\item_icons` (Mod Organizer 2: the overwrite folder) |
| Bar contents | the SKSE co-save of each save game |
| Log | `Documents\My Games\Skyrim Special Edition\SKSE\SpellHotbarNG.log` |

## For animation modders

Casts play the shout animation (`ShoutStart`, `MT_BreathExhaleShort`, `ShoutStop`) and Open Animation Replacer swaps
the shout clips for casting clips while the custom condition **`SpellHotbarNG_Casting`** matches. Its components:

| Component | Meaning |
| --- | --- |
| `Animation` | 0 any, 1 aimed, 2 self, 3 aimed concentration, 4 self concentration, 5 dual aimed, 6 dual self, 7 dual concentration, 8 ritual (two-handed) |
| `Hand` | 0 any, 1 right, 2 left (dual casts and two-handed spells only match 0) |
| `ShoutState` | 1: only while the hotbar holds the graph in the shout state (for clips also used outside of it) |
| `Phase` | 0 any, 1 charging / channeling, 2 release playing, 3 release done (use with *Interruptible*) |

Give your submods a higher priority than the built-in ones (2140000000 + ...). If your release clips have other
lengths, add a timing file `Data\SKSE\Plugins\SpellHotbarNG\animations\<name>.json` so the bar waits for them:

```json
{ "releaseTime": { "thirdPerson": { "1": 1.03, "1-left": 1.0 }, "firstPerson": { "1": 0.9 } } }
```

## Building

Needs Visual Studio 2022+ build tools (MSVC, C++23), [xmake](https://xmake.io) 3 and Python 3 for the packaging tools.

```bat
git clone --recursive https://github.com/Iur1M/SpellHotbarNG.git
cd SpellHotbarNG
build.bat
```

`build.bat` builds `SpellHotbarNG.dll` into `build\windows\x64\releasedbg`. To build somewhere else, create
`build.local.bat` (not in git) with `set "SHNG_BUILD_DIR=D:/somewhere"`.

Packaging the mod (FOMOD installer zip):

1. `pip install -r tools\requirements.txt`
2. Clone [Spell Hotbar 2](https://github.com/pWn3d1337/Skyrim_SpellHotbar2) to `external\SpellHotbar2` and run
   `python tools\build_icons.py`: builds the icon atlases (BC3 DDS + CSV).
3. `python tools\package.py`: builds the casting animation folders from the game's `Skyrim - Animations.bsa`
   (`tools\build_anims.py`) and writes the zips to `dist\`.

Folders (build output, game Data folder, Spell Hotbar 2 clone, output) are set in `tools\paths.py` and can be
changed per machine with `tools\paths_local.json` or environment variables.

## Source layout

| Path | What |
| --- | --- |
| `src/main.cpp` | plugin entry, `PlayerCharacter::Update` hook, SKSE messages |
| `src/core/` | settings and profiles (`Config`), slot bindings + co-save (`Bindings`), key handling (`Input`, `Keys`), plugin-relative form ids (`Util`) |
| `src/casting/` | using a slot (`Actions`), casting animations + OAR condition (`CastAnim`), shouts / powers through the Shout control (`VanillaCast`), sprint stop and crosshair aim (`PlayerControl`) |
| `src/ui/` | Menu Framework registration (`UI`), the bar (`Hud`), settings pages (`Menu`), icon atlases (`Icons`), item model pictures (`ItemIcons`) |
| `lib/` | CommonLibSSE-NG (submodule), SKSE Menu Framework API, Open Animation Replacer condition API |
| `tools/` | icon atlas builder, animation folder builder, BSA / HKX readers, FOMOD packager |
| `res/` | files shipped with the mod (credits) |

## Credits

- [Spell Hotbar 2](https://github.com/pWn3d1337/Skyrim_SpellHotbar2) by pWn3d1337 (MIT): icons, spell lists, bar
  textures and the idea of casting through the shout animation. Community icon packs by ArchAngelAries.
- [CommonLibSSE-NG](https://github.com/alandtse/CommonLibVR/tree/ng), [SKSE Menu Framework](https://github.com/QTR-Modding/SKSE-Menu-Framework-3-API)
  (LGPL-2.1), [Open Animation Replacer](https://github.com/ersh1/OpenAnimationReplacer) condition API by ersh1.
- The item picture technique (the game's Inventory3DManager drawn over two backdrops) was learned from
  [Grid Inventory](https://github.com/skypia0147-dev/grid-inventory) and Modex; the code here is our own.
- Reading the selected menu entry follows Spell Hotbar 2 and Wheeler.

## License

[MIT](LICENSE). Third-party code in `lib/` keeps its own license.
