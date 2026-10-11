#pragma once

// Grid Inventory (github skypia0147-dev/grid-inventory) replaces the inventory, container and barter menus with its
// own window ("GridInventoryMenu"), so the vanilla InventoryMenu the hotbar binds from is never open. Its extension
// API (lib/GridInventory/GridInventoryAPI.h) asks its extension for the overlay of the tile under the mouse, every
// frame: the hotbar offers itself as that extension (drawing nothing) to learn which item is hovered, so a slot key
// binds it like in the vanilla inventory. Grid Inventory takes only one extension, the first that asks: the hotbar
// asks after every other mod had its turn, so a real extension keeps its place.
//
// Grid Inventory also runs the game's 3D item view on its own schedule (its scene, deferred teardowns, background
// icon captures), which wiped the hotbar's own item photos. With it installed the hotbar takes the item pictures
// from Grid Inventory's icon cache instead (GridInventory_icons.pak): the same picture the grid shows.
namespace GridInventory
{
	// GridInventory.dll is loaded
	bool Installed();

	// Offers the hotbar as Grid Inventory's extension. Call on kPostPostLoad (after the kPostLoad registrations).
	void OfferExtension();

	// Grid Inventory's window is open
	bool MenuOpen();

	// The player's item under the mouse in Grid Inventory's window, nullptr if none (or the extension slot is taken)
	RE::TESForm* Hovered();

	// Grid Inventory's cache slot of an item's picture (items sharing a model share it)
	std::uint32_t PictureSlot(RE::TESForm* a_form);

	// Grid Inventory's cached picture of an item, fitted into a square of a_size pixels (B8G8R8A8, straight alpha).
	// false while Grid Inventory hasn't photographed it (yet). The cache file is read again when it grew.
	bool Picture(RE::TESForm* a_form, int a_size, std::vector<std::uint8_t>& a_bgra);
}
