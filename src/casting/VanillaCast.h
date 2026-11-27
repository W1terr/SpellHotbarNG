#pragma once

#include "core/Bindings.h"

// Shouts and powers through the game's own Shout control: the power / shout goes into the voice slot and
// the Shout button is pressed for the player while the slot key is held (more words the longer it is held),
// then the previous power is equipped again. Hands are never touched.
namespace VanillaCast
{
	struct Info
	{
		Page  page{ Page::kMain };
		int   slot{ -1 };
		float progress{ 0.0f };
		bool  channeling{ false };
	};

	// a_form is a TESShout or a power SpellItem. Returns false if another shout is still running (queue the press).
	bool StartShout(RE::TESForm* a_form, Page a_page, int a_slot, std::uint32_t a_key, bool a_keyHeld);

	// Data\SKSE\Plugins\SpellHotbarNG\compat\*.json "shoutKeyPlugins": powers / shouts of these plugins are used by
	// sending the Shout button through the game's input events, so mods that watch the Shout key see it too (Ciri
	// Abilities starts its abilities from the key, not from the cast)
	void LoadCompat();

	void OnKeyUp(std::uint32_t a_key);
	void Update(float a_delta);
	void Reset();

	std::optional<Info> Current();
}
