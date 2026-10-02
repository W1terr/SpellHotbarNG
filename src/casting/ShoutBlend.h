#pragma once

// 3rd person casts while moving. Hotbar casts run in the shout behavior, whose moving state ("MT_ShoutLocomotionBlend")
// takes spine, clavicles, arms and their twist bones half from the shout clip (our casting clip) and half from the walk /
// run, so the casting arm hangs halfway down. The magic behavior takes the whole upper body (spine, arms, head, fingers)
// from the casting clip and only the rest from the walk / run. While a hotbar cast holds the shout state the blend's bone
// weights are switched to that, and put back afterwards (they may be shared with other actors' graphs, so only for as long
// as our cast runs).
namespace ShoutBlend
{
	// Every frame: a_active = our cast holds the graph in the shout state
	void Update(bool a_active);
	// Puts the original weights back
	void Restore();
}
