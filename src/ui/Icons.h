#pragma once

// Icon lookup. Atlases are DDS files with a CSV next to them, see tools/build_icons.py.
// Textures themselves are created on first use by the HUD (Hud.cpp) through SKSE Menu Framework.
namespace Icons
{
	struct Icon
	{
		int   atlas{ -1 };
		float u0{ 0.0f };
		float v0{ 0.0f };
		float u1{ 1.0f };
		float v1{ 1.0f };
		void* texture{ nullptr };  // own texture instead of an atlas (item model icons, SkyUI icons, see ItemIcons)
		std::uint32_t rgb{ 0xFFFFFF };  // color the picture is tinted with (SkyUI icons are white)

		[[nodiscard]] bool Valid() const { return atlas >= 0 || texture; }
	};

	// Reads every atlas CSV and the player's icon choices. Needs the data handler, so call it on kDataLoaded.
	void Load();

	const std::string& AtlasPath(int a_atlas);
	int                AtlasCount();

	// Icon for a form: the one the player picked, else SkyUI's inventory icon if the icon style wants it, else its own,
	// else a generic school / item icon. Weapons, armor, ammo and torches without an atlas icon get a picture of their
	// model once it's made (ItemIcons).
	Icon ForForm(RE::TESForm* a_form);

	// UI and fallback icons: BAR_EMPTY, BAR_OVERLAY, BAR_HIGHLIGHT, UNKNOWN, ...
	Icon Named(std::string_view a_name, bool a_nordic = false);

	// ---- icons picked by the player (Icons page) ---------------------------------------------
	// Saved in Data\SKSE\Plugins\SpellHotbarNG\custom_icons.json as "Plugin.esp|0xID" -> icon key, the same for every
	// character. Render thread only.

	// A picture the player can choose: every distinct atlas picture, also those of plugins that aren't loaded
	struct Choice
	{
		std::string key;    // icon key: "Plugin.esp|0xID" or "@NAME"
		std::string name;   // shown name
		std::string group;  // atlas it comes from ("vanilla_spells", a spell pack...)
		Icon        icon;
		bool        spellHotbar2{ false };  // from an icon pack made for SpellHotbar2
	};
	const std::vector<Choice>& Choices();

	// The form has an icon of its own (from an icon list or its model), not a generic one
	bool HasOwnIcon(RE::TESForm* a_form);

	// Loaded forms with a picture in an icon pack made for SpellHotbar2 (Data\SKSE\Plugins\SpellHotbar\images). Those
	// pictures are also in Choices() (key "sh2:<list>:<Plugin>|<FormID>"); Settings::spellHotbar2Icons makes them the
	// forms' own icons.
	std::size_t SpellHotbar2IconCount();

	// Icon key the player picked for the form, empty if none
	std::string CustomKey(RE::TESForm* a_form);

	// Picks an icon (a Choice key) for the form; an empty key goes back to the form's own icon. Saves the file.
	void SetCustom(RE::TESForm* a_form, std::string_view a_key);
}
