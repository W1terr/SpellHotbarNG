#pragma once

// SKSE Menu Framework integration: settings pages, HUD element and input hook
namespace UI
{
	// Registers everything with SKSE Menu Framework. Returns false if it is not installed.
	bool Register();

	// A framework window that pauses the game is open (the settings menu)
	bool IsBlockingWindowOpen();

	// The settings preview shows the bars where they sit in the inventory / magic menus (the player last edited that
	// position), not where they sit in game
	bool PreviewMenuPosition();
}
