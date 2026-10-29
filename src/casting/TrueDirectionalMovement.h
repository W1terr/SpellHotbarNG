#pragma once

// True Directional Movement: while it moves the player in its directional mode, the character turns to where it walks,
// which takes over the hotbar's "aim at the crosshair" turn. TDM only faces the crosshair for spells cast from the
// hands, so for a hotbar cast that aims at the crosshair the bar asks TDM (its API) to switch its directional movement
// off, and hands it back when the cast ends or stops.
namespace TrueDirectionalMovement
{
	// After all plugins loaded (kPostLoad or later)
	void Init();

	// Game thread, every frame: true while a hotbar cast should keep the character facing the crosshair
	void FaceCrosshair(bool a_face);
}
