#pragma once

// "Each shout has its own cooldown" (Settings::individualShoutCooldowns), like Spell Hotbar 2's option.
// The game has one shout recovery timer for the player. With the option on, the timer is swapped whenever the equipped
// shout changes (hotbar, Favorites, Equip mode alike): the shout put away keeps what was left of the timer, the shout
// equipped gets its own remaining time back. Kept in game time, so waiting / sleeping counts, and in the co-save.
namespace ShoutCooldowns
{
	inline constexpr std::uint32_t kRecord = 'SHCD';
	inline constexpr std::uint32_t kRecordVersion = 1;

	// Game thread, every frame (before the hotbar presses the Shout button)
	void Update();

	// Load / new game: what was equipped before isn't a switch
	void Reset();

	// Remaining and total seconds of a shout or voice power's cooldown (the game's timer while it is equipped)
	std::pair<float, float> Get(RE::TESForm* a_form);

	void Save(SKSE::SerializationInterface* a_intfc);
	void Load(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);
	void Clear();
}
