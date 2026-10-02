#pragma once

// What a hotbar cast does to the player besides the spell: no sprinting while casting, and aiming at the crosshair
// (third person: the character turns to the camera; at the cast the aim points from the hand to the crosshair).
namespace PlayerControl
{
	// Ends a sprint like vanilla casting does (the game's own "sprint stop" action)
	void StopSprinting();

	// Projectile spells, runes and summons go to the crosshair (setting on) instead of the enemy being fought
	bool AimsAtCrosshair(const RE::MagicItem* a_item);

	// Third person: turns the character to where the camera looks without moving the camera.
	// a_maxStep limits the turn (radians), < 0 turns all the way.
	void FaceCamera(float a_maxStep);

	// While alive, the player's aim points from the caster's hand straight at the crosshair (removes the
	// over-the-shoulder offset between the camera's crosshair and the hand the projectile starts from).
	class ScopedCrosshairAim
	{
	public:
		explicit ScopedCrosshairAim(RE::MagicCaster* a_caster);
		~ScopedCrosshairAim();

		ScopedCrosshairAim(const ScopedCrosshairAim&) = delete;
		ScopedCrosshairAim& operator=(const ScopedCrosshairAim&) = delete;

	private:
		std::optional<RE::NiPoint3> _saved;
	};
}
