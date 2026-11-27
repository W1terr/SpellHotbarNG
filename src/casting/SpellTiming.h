#pragma once

// Cast time and cooldown of single spells and scrolls cast from the bar, set by the player on the Spell Timing page
// (like Spell Hotbar 2's spell editing). Saved in Data\SKSE\Plugins\SpellHotbarNG\spell_timing.json as
// "Plugin.esp|0xID" -> { "castTime": s, "cooldown": s }, the same for every character. Running cooldowns tick in
// gameplay seconds (not in menus) and are kept in the co-save.
// Only casts the bar does itself use them: Equip mode and the vampire lord's / lich's hand casting keep the game's
// cast time (their cooldown still counts).
namespace SpellTiming
{
	inline constexpr std::uint32_t kRecord = 'SPCD';
	inline constexpr std::uint32_t kRecordVersion = 1;

	inline constexpr float kMinCastTime = 0.25f;  // shortest cast with the casting animation
	inline constexpr float kMaxCastTime = 10.0f;
	inline constexpr float kMaxCooldown = 600.0f;

	struct Timing
	{
		float castTime{ 0.0f };  // 0 = the spell's own
		float cooldown{ 0.0f };  // 0 = none

		bool Empty() const { return castTime <= 0.0f && cooldown <= 0.0f; }
	};

	// Reads the file (after the game data is loaded)
	void Load();
	// Data\SKSE\Plugins\SpellHotbarNG\compat\*.json "powerCooldowns": [{ "plugin", "power", "cooldownGlobal" }]: a
	// power whose cooldown is its mod's own setting (the MCM slider's global, Ciri Abilities). The bar only shows it
	// (Get / StartCooldown read the global); the mod's menu is the one place to change it.
	void LoadCompat();

	Timing Get(const RE::TESForm* a_form);
	// a_write: save the file too (not while a slider is being dragged)
	void   Set(const RE::TESForm* a_form, const Timing& a_timing, bool a_write = true);
	bool   CanHaveCastTime(const RE::TESForm* a_form);  // fire-and-forget spells and scrolls (concentration: held = casting)
	bool   CanHaveTiming(const RE::TESForm* a_form);    // spells (not powers) and scrolls

	// cast time to use for a cast from the bar, nullopt = the spell's own
	std::optional<float> CastTime(const RE::TESForm* a_form);

	// the spell was cast from the bar (a concentration spell: its channel ended): its cooldown starts
	void StartCooldown(const RE::TESForm* a_form);

	// remaining / total seconds of the spell's cooldown, {0, 0} if none runs
	std::pair<float, float> Cooldown(const RE::TESForm* a_form);

	void Update(float a_delta);  // game thread, gameplay time

	void Save(SKSE::SerializationInterface* a_intfc);
	void LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version);
	void Clear();  // new game / load: no cooldowns running
}
