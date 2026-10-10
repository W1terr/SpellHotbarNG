#pragma once

#include "casting/CastAnim.h"

// Strange Runes (powerofthree, Nexus 19456): glowing rune circles on the casting hands. Its Papyrus script
// (PO3_RUNEDetectPlayerCasting, on an ability of the player) reacts to the vanilla "BeginCastRight / Left" animation
// events and reads the spell equipped in that hand, so hotbar casts (nothing equipped, shout animation) never got
// runes. Here every hotbar cast asks that same script which rune the spell gets (its GetRuneType function, so its
// own rules and its MCM toggles apply), adds the rune spell for the casting hand(s) from the script's properties, and
// takes it off again when the cast ends, like the script does. Nothing happens without Strange Runes.
namespace StrangeRunes
{
	// A hotbar cast starts charging (or channeling). a_scroll: the item is a scroll.
	void Start(RE::MagicItem* a_item, CastAnim::Side a_side, bool a_scroll);

	// The cast is over (fired, channel ended, fizzled): its runes go away a moment later
	void Stop();

	void Update(float a_delta);

	// Loading a save: runes off right away
	void Reset();
}
