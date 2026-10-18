#pragma once

// The bar's pictures in SkyUI's Magic menu, like SpellHotbar2 did with Inventory Interface Information Injector rules
// (Settings::magicMenuIcons). SkyUI list entries (InventoryListEntry) show their icon by loading an icon movie into
// their itemIcon clip; formatItemIcon of the Magic menu movie is wrapped so a spell, power or shout with a picture of
// its own (Icons::MenuPicture) loads a one-picture movie made from that picture instead. Everything else goes to the
// previous formatItemIcon (SkyUI's, or I4's when it's installed).
namespace MagicMenuIcons
{
	// Every frame (HUD render): hooks the Magic menu movie once it's open and SkyUI's list classes are there
	void Update();
}
