#pragma once

// Icons for weapons, armor, ammo and torches that have no icon in the atlases: a picture of the item's own inventory
// model. The game's Inventory3DManager (the 3D item view of the inventory) draws the model, it is read back from the
// render target (once over black, once over white: the difference is the transparency) and saved as a DDS file in
// Data\SKSE\Plugins\SpellHotbarNG\item_icons, one per model, so every model is only photographed once.
//
// The capture runs in the UI render pass of a small invisible menu (kCustomRendering), the place the game itself draws
// the inventory's 3D item. While the inventory (or another menu with a 3D item view) is open, only the item it shows is
// photographed; otherwise the model is loaded into the 3D view, captured and unloaded again.
//
// The same menu also draws SkyUI's inventory icons (MenuIcon).
namespace ItemIcons
{
	// Registers the capture menu. Call on kDataLoaded.
	void Register();

	// Forms that get a model icon
	bool Supports(const RE::TESForm* a_form);

	// Texture (ImTextureID) of the form's model icon. nullptr while it's being made (asking for it queues the capture)
	// or if the model can't be photographed.
	void* Get(RE::TESForm* a_form);

	// Texture of a frame of an inventory icon movie (SkyUI's or an I4 mod's, path under Interface without ".swf", see
	// InventoryIcons): white with transparency, tint it with the icon color. The capture menu draws the frame with
	// Scaleform once per game session (icon packs may change). nullptr while it's being drawn (a_pending set) or if it
	// can't be.
	void* MenuIcon(const std::string& a_movie, const std::string& a_label, bool& a_pending);

	// Opens / closes the capture menu while captures are waiting. Call every frame.
	void Update();
}
