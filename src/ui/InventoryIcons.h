#pragma once

// SkyUI's inventory icons: which frame of its icon movie (Interface\skyui\icons_item_psychosteve.swf, or whatever
// SkyUI's config.txt names) the inventory / magic menu shows for a form, and its color. Rules ported from SkyUI's
// InventoryIconSetter / MagicIconSetter. The frames are drawn into textures by ItemIcons.
namespace InventoryIcons
{
	struct Look
	{
		std::string   label;               // frame label, e.g. "weapon_waraxe"
		std::uint32_t rgb{ 0xFFFFFF };     // icon color (0xRRGGBB)
	};

	// nullopt for forms SkyUI shows no icon for
	std::optional<Look> For(RE::TESForm* a_form);

	// Weapons, armor, ammo, torches, potions and food (not spells, scrolls, powers, shouts)
	bool IsItem(const RE::TESForm* a_form);

	// SkyUI's icon movie, path under Interface without ".swf" (empty without SkyUI). Read once from its config.txt.
	const std::string& MoviePath();
}
