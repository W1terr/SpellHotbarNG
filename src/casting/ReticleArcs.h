#pragma once

// Reticle Arcs (Chiktabba, Nexus 193635) shows spell charges around the crosshair from the hand casters, which hotbar
// casts don't use (they time the charge themselves and fire with CastSpellImmediate). When it is installed, two add-on
// bars through its public interface (lib/ReticleArcs) show hotbar casts instead: left hand on its left side, right hand
// on its right side, charge filling up, concentration spells draining with the magicka left, a fizzled / let go cast as
// a break, a press without enough magicka as its warning flash. Its own menu sets their look or turns them off.
namespace ReticleArcs
{
	// SKSE kPostLoad: Reticle Arcs only takes add-on bars then
	void Init();
}
