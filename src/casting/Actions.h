#pragma once

#include "core/Bindings.h"

namespace Actions
{
	// Uses the form bound to a slot. a_keyHeld tells whether the slot key is still down
	// (concentration spells channel and shouts gain words while it is).
	void Use(Page a_page, int a_slot, bool a_keyHeld, std::uint32_t a_key);

	// The slot key was released (stops concentration spells started from it)
	void OnKeyUp(std::uint32_t a_key);

	// Game time tick, runs on the main thread from PlayerCharacter::Update
	void Update(float a_delta);

	// Drops casts / cooldowns, e.g. on load
	void Reset();

	// HUD queries
	struct CastInfo
	{
		Page  page{ Page::kMain };
		int   slot{ -1 };
		float progress{ 0.0f };  // 0..1 while charging, 1 while channeling
		bool  channeling{ false };
	};
	std::optional<CastInfo> CurrentCast();

	// Remaining / total seconds of whatever blocks this form right now (gcd, potion cooldown, shout recovery)
	std::pair<float, float> Cooldown(RE::TESForm* a_form);

	bool CanAfford(RE::TESForm* a_form, Hand a_hand);
	bool IsAvailable(RE::TESForm* a_form);     // known spell / owned item
	int  ItemCount(RE::TESForm* a_form);       // -1 for things without a count
	bool IsEquipped(RE::TESForm* a_form);
}
