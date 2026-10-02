#pragma once

// Watches the player's behavior graphs while a hotbar cast animation runs.
// - Logs (debug level) the events the graphs send ("[anim]" lines, footsteps and sounds left out) and the events
//   the hotbar sends them, with the time since the cast started. A diagnostic for animations that change mid-cast.
// - Notices when the unarmed behavior (MT_Behavior, magic sheathed) goes back to its default state, out of the shout
//   state our cast animation plays in: entering MT_Default_State sends "HeadTrackingOn".
namespace GraphWatch
{
	// A cast animation started: (re)registers on the player's graphs (they're rebuilt on load) and starts watching
	void Begin(std::string_view a_what);
	// Logs an event the hotbar sent to the graph
	void Note(std::string_view a_what);
	// Every frame; a_casting = a hotbar cast animation runs. Logging stops shortly after it ended.
	void Update(float a_delta, bool a_casting);
	// True once after the unarmed behavior left the shout state on its own
	bool TakeLeftShout();
}
