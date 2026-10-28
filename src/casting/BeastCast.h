#pragma once

#include "core/Bindings.h"

// Spells from the bar in beast forms that cast with their own body (Util::CastingForm): Dawnguard's vampire lord and
// Undeath's lich (dragon priest body). Their behavior graphs only play casting animations for the game's own hand
// casting, so the spell is put into the hand, the attack button of that hand is pressed for the player (held while it
// charges / channels), and the hand gets its previous spell back afterwards - casting, magicka, perks and animations
// are all the game's. A vampire lord first rises into the air (the Sneak action, like the game's Sneak key), a lich
// raises its hands like Undeath's own key does.
namespace BeastCast
{
	enum class StartResult
	{
		kStarted,
		kBusy,   // the previous beast cast still runs: queue the press
		kFailed
	};

	StartResult Start(RE::SpellItem* a_spell, const SlotBinding& a_binding, Page a_page, int a_slot, std::uint32_t a_key, bool a_keyHeld);

	void OnKeyUp(std::uint32_t a_key);
	void Update(float a_delta);
	void Reset();

	struct Info
	{
		Page  page{ Page::kMain };
		int   slot{ -1 };
		float progress{ 0.0f };
		bool  channeling{ false };
	};
	std::optional<Info> Current();
}
