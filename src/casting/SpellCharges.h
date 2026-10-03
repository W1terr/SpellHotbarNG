#pragma once

// Perks that count spell casts in a Papyrus OnSpellCast event, like Ordinator's Vancian Magic (a number of free casts
// per rest). Those scripts only count spells the player has equipped in a hand, so hotbar casts were never counted.
// A compatibility file (Data\SKSE\Plugins\SpellHotbarNG\compat\*.json, installed by the FOMOD) describes what such a
// script does, and every hotbar spell cast does the same:
//   "spellCharges": [ {
//       "plugin": "Ordinator - Perks of Skyrim.esp",
//       "activeEffect": "0x167A0C",          the rule applies while the player has this magic effect (the script's)
//       "counter": "0x167A0E",               global variable lowered by 1 per cast
//       "messages": [ { "at": 10, "message": "0x167A15" },
//                     { "at": 0, "message": "0x167A16", "interruptCast": true } ],   when the counter reaches "at"
//       "healthDamagePerMissingCharge": "0x167A22"   global: below 0, each cast costs health = missing charges * it
//   } ]
// Hotbar casts in Equip mode are the game's own casts and get counted by the perk itself.
namespace SpellCharges
{
	// Reads the compatibility files. Call once the game data is loaded.
	void Load();

	// A hotbar cast of a hand spell went off (a_casts: 2 for both hands without dual casting). Returns true if a rule
	// interrupts the cast now (a concentration spell stops).
	bool OnHotbarCast(RE::MagicItem* a_spell, int a_casts);
}
