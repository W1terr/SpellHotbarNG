#pragma once

// Reticle Arcs (Chiktabba, Nexus 193635) shows spell charges around the crosshair from the hand casters, which hotbar
// casts don't use (they time the charge themselves and fire with CastSpellImmediate). Through its public interface
// (lib/ReticleArcs):
// - Reticle Arcs 2.1.0 and later: hotbar casts go on its own left / right spell bars (cast provider), in the spell's
//   colours and with the player's charged / released / interrupted effects; dual casts and rituals fill both bars.
// - older versions: two add-on bars, left hand on its left side, right hand on its right side, charge filling up,
//   concentration spells draining with the magicka left, a fizzled / let go cast as a break, a press without enough
//   magicka as its warning flash. Its own menu sets their look or turns them off.
namespace ReticleArcs
{
	// SKSE kPostLoad: Reticle Arcs only takes add-on bars then
	void Init();
}
