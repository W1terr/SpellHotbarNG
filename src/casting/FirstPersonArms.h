#pragma once

// Improved Camera (1.x from Nexus and 2.x) shows the 3rd person body in 1st person and hides the 1st person arms
// whenever it thinks they aren't needed: weapons / magic sheathed, or (2.x) no spell in a hand being cast. Both scale
// the 1st person "NPC L/R UpperArm" bones down to 0.001. A hotbar cast plays its casting animation on those arms
// without a spell in hand, often with magic sheathed, so the cast was invisible. While a hotbar casting animation
// runs in 1st person, the arms are scaled back up right after the 1st person skeleton update (the call both camera
// versions work around), like during a normal cast. Without such a mod nothing is hidden and nothing changes.
namespace FirstPersonArms
{
	// Hooks the player's 1st person skeleton update. Call on kPostPostLoad, after the camera mods hooked it (needs the
	// trampoline).
	void Install();
}
