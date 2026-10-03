#pragma once

#include "core/Config.h"

// Data\SKSE\Plugins\SpellHotbarNG\Hotkeys.ini: the keys in use as an ini, so hotkey tools such as
// Hotkey Atlas list them. Hotkey Atlas remaps a key without touching the file: it turns presses of
// the new input into the key written here, so the mod itself needs no change for that. It only
// shows the user's input in its labels, read from Hotkey Atlas's own ini.
namespace Hotkeys
{
	// Keys found in the file replace the settings' ones (edits made by hand while the game was closed)
	void Read(Settings& a_settings);

	// Rewrites the file if its text changed
	void Write(const Settings& a_settings);

	// What to press for a key of `a_settings` (a_key is one of its members): the key's name, or the
	// input Hotkey Atlas moved it to. Empty for no key. Render thread only (translated text).
	std::string Label(const Settings& a_settings, const std::uint32_t& a_key);

	// Hotkey Atlas moved this key of `a_settings` somewhere else
	bool MovedInHotkeyAtlas(const Settings& a_settings, const std::uint32_t& a_key);
}
