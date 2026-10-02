#pragma once

// SKSE Menu Framework integration: settings pages, HUD element and input hook
namespace UI
{
	// Registers everything with SKSE Menu Framework. Returns false if it is not installed.
	bool Register();

	// A framework window that pauses the game is open (the settings menu)
	bool IsBlockingWindowOpen();
}
