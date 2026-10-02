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
		void* texture{ nullptr };  // own texture instead of an atlas (item model icons, see ItemIcons)

		[[nodiscard]] bool Valid() const { return atlas >= 0 || texture; }
	};

	// Reads every atlas CSV. Needs the data handler, so call it on kDataLoaded.
	void Load();

	const std::string& AtlasPath(int a_atlas);
	int                AtlasCount();

	// Icon for a form, falling back to a generic school / item icon. Weapons, armor, ammo and torches without an atlas
	// icon get a picture of their model once it's made (ItemIcons).
	Icon ForForm(RE::TESForm* a_form);

	// UI and fallback icons: BAR_EMPTY, BAR_OVERLAY, BAR_HIGHLIGHT, UNKNOWN, ...
	Icon Named(std::string_view a_name, bool a_nordic = false);
}
