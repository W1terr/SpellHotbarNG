#pragma once

// SkyUI's inventory icons: which frame of which icon movie the inventory / magic menu shows for a form, and its color.
//
// SkyUI's own icons come from its icon movie (Interface\skyui\icons_item_psychosteve.swf, or whatever SkyUI's
// config.txt names), picked by rules ported from SkyUI's InventoryDataSetter / InventoryIconSetter / MagicIconSetter.
// On top of that come the rules of Inventory Interface Information Injector (I4) mods like KIT: every
// Data\SKSE\Plugins\InventoryInjector\<plugin>.json of an active plugin, matched against the same item data SKSE and
// SkyUI give the menus (formType, keywords, school, effectKeywords, ...) and able to name another movie (iconSource),
// frame (iconLabel) and color (iconColor). Ported from I4 (MIT, github.com/Exit-9B/InventoryInjector).
// The frames are drawn into textures by ItemIcons.
namespace InventoryIcons
{
	struct Look
	{
		std::string   movie;               // icon movie under Interface, without ".swf"
		std::string   label;               // frame label, e.g. "weapon_waraxe"
		std::uint32_t rgb{ 0xFFFFFF };     // icon color (0xRRGGBB)
	};

	// nullopt for forms SkyUI shows no icon for, or without SkyUI
	std::optional<Look> For(RE::TESForm* a_form);

	// Weapons, armor, ammo, torches, potions and food (not spells, scrolls, powers, shouts)
	bool IsItem(const RE::TESForm* a_form);

	// SkyUI's icon movie, path under Interface without ".swf" (empty without SkyUI). Read once from its config.txt.
	const std::string& MoviePath();

	// I4 rules read (they're read on first use)
	std::size_t RuleCount();
}
