# Spell Hotbar NG

A spell and item hotbar for Skyrim SE / AE, written purely as an SKSE plugin: no ESP, no Papyrus. Everything is set
up in the [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352) menu. Inspired by and
using the icons of [Spell Hotbar 2](https://github.com/pWn3d1337/Skyrim_SpellHotbar2).

Main features
- Hotbar casting: cast spells, powers, shouts and scrolls straight from the bar.
- Potions: potions, food can go on the bar too.
- Easy binding: select a spell or item in the Magic or Inventory menu and press a slot key.
- Extra bars and slots: hold a key like Shift to switch bars.
- Change the position: move, resize and arrange the bar anywhere on screen.
- Change the colors: frame, background, text, cooldowns and more.
- Save profiles: save your setup.

## Requirements

- Skyrim SE / AE with [SKSE](https://skse.silverlock.org/) (built against CommonLibSSE-NG, tested on 1.7.104)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
- [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352) 3 or newer
- [Open Animation Replacer](https://www.nexusmods.com/skyrimspecialedition/mods/92109) for the casting animations
  (without it spells are cast without animation)


## Credits

- [Spell Hotbar 2](https://github.com/pWn3d1337/Skyrim_SpellHotbar2) by pWn3d1337 (MIT): icons, spell lists, bar
  textures and the idea of casting through the shout animation. Community icon packs by ArchAngelAries.
- [CommonLibSSE-NG](https://github.com/alandtse/CommonLibVR/tree/ng), [SKSE Menu Framework](https://github.com/QTR-Modding/SKSE-Menu-Framework-3-API)
  (LGPL-2.1), [Open Animation Replacer](https://github.com/ersh1/OpenAnimationReplacer) condition API by ersh1.
- The item picture technique (the game's Inventory3DManager drawn over two backdrops) was learned from
  [Grid Inventory](https://github.com/skypia0147-dev/grid-inventory) and Modex; the code here is our own.
- Reading the selected menu entry follows Spell Hotbar 2 and Wheeler.

## License

Copyright © 2007 Free Software Foundation, Inc. <https://fsf.org/>

Everyone is permitted to copy and distribute verbatim copies of this license document, but changing it is not allowed.
