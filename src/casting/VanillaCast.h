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

	// a_form is a TESShout or a power SpellItem. Returns false if another shout is still running.
	bool StartShout(RE::TESForm* a_form, Page a_page, int a_slot, std::uint32_t a_key, bool a_keyHeld);

	void OnKeyUp(std::uint32_t a_key);
	void Update(float a_delta);
	void Reset();

	std::optional<Info> Current();
}
