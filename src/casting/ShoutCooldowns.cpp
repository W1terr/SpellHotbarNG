#include "casting/ShoutCooldowns.h"

#include "core/Config.h"

namespace ShoutCooldowns
{
	namespace
	{
		struct Cooldown
		{
			float readyAt{ 0.0f };  // game time (days) when the shout can be used again
			float total{ 0.0f };    // seconds, for the bar's sweep
		};

		std::unordered_map<RE::FormID, Cooldown> cooldowns;  // shouts that are not equipped
		RE::FormID                               equipped{ 0 };
		bool                                     tracking{ false };  // equipped is known (not right after a load)
		bool                                     wasOn{ false };
		float                                    lastTimer{ 0.0f };
		float                                    equippedTotal{ 0.0f };  // length of the equipped shout's running cooldown

		RE::PlayerCharacter* Player() { return RE::PlayerCharacter::GetSingleton(); }

		// shouts and voice powers use the game's shout timer
		bool UsesShoutTimer(const RE::TESForm* a_form)
		{
			if (!a_form) {
				return false;
			}
			if (a_form->Is(RE::FormType::Shout)) {
				return true;
			}
			const auto spell = a_form->As<RE::SpellItem>();
			return spell && spell->GetSpellType() == RE::MagicSystem::SpellType::kVoicePower;
		}

		float Now()
		{
			const auto calendar = RE::Calendar::GetSingleton();
			return calendar ? calendar->GetCurrentGameTime() : 0.0f;
		}

		// real seconds per game day
		float DaySeconds()
		{
			const auto calendar = RE::Calendar::GetSingleton();
			const float timescale = calendar ? calendar->GetTimescale() : 20.0f;
			return 86400.0f / std::max(timescale, 0.001f);
		}

		float Remaining(const Cooldown& a_cooldown)
		{
			return std::max(0.0f, (a_cooldown.readyAt - Now()) * DaySeconds());
		}

		float Timer()
		{
			const auto player = Player();
			return player ? player->GetVoiceRecoveryTime() : 0.0f;
		}

		void SetTimer(float a_seconds)
		{
			const auto player = Player();
			const auto process = player ? player->GetActorRuntimeData().currentProcess : nullptr;
			if (process && process->high) {
				process->high->voiceRecoveryTime = a_seconds;
			}
		}

		RE::FormID SelectedShout()
		{
			const auto player = Player();
			const auto power = player ? player->GetActorRuntimeData().selectedPower : nullptr;
			return UsesShoutTimer(power) ? power->GetFormID() : 0;
		}

		void Store(RE::FormID a_shout, float a_seconds, float a_total)
		{
			if (a_shout && a_seconds > 0.0f) {
				cooldowns[a_shout] = { Now() + a_seconds / DaySeconds(), std::max(a_total, a_seconds) };
			} else {
				cooldowns.erase(a_shout);
			}
		}

		// what is left of a stored cooldown goes into the game's timer
		void Restore(RE::FormID a_shout)
		{
			float remaining = 0.0f;
			equippedTotal = 0.0f;
			if (const auto it = cooldowns.find(a_shout); it != cooldowns.end()) {
				remaining = Remaining(it->second);
				equippedTotal = it->second.total;
				cooldowns.erase(it);
			}
			SetTimer(remaining);
			lastTimer = remaining;
		}
	}

	void Update()
	{
		const bool on = Config::Get().individualShoutCooldowns;
		const auto timer = Timer();
		const auto selected = SelectedShout();

		if (on != wasOn) {
			wasOn = on;
			if (!on) {
				cooldowns.clear();  // back to one timer: it keeps the equipped shout's time
			}
			tracking = false;
		}

		// a shout was used: the timer jumped up, that's its whole cooldown
		if (timer > lastTimer + 0.05f) {
			equippedTotal = timer;
		}

		if (on && tracking && selected != equipped) {
			// the shout put away keeps what's left of the timer, the new one gets its own
			Store(equipped, timer, equippedTotal);
			Restore(selected);
			logs::debug("Shout cooldowns: {:08X} -> {:08X}, {:.1f} s left", equipped, selected, Timer());
		} else {
			lastTimer = timer;
		}
		equipped = selected;
		tracking = true;

		// drop cooldowns that ran out
		std::erase_if(cooldowns, [](const auto& a_entry) { return Remaining(a_entry.second) <= 0.0f; });
	}

	void Reset()
	{
		tracking = false;
		lastTimer = Timer();
		equippedTotal = 0.0f;
	}

	std::pair<float, float> Get(RE::TESForm* a_form)
	{
		if (!UsesShoutTimer(a_form)) {
			return { 0.0f, 0.0f };
		}
		// the equipped shout (or every shout, with one shared timer): the game's timer
		if (!Config::Get().individualShoutCooldowns || a_form->GetFormID() == SelectedShout()) {
			const float timer = Timer();
			return { timer, std::max({ timer, equippedTotal, 1.0f }) };
		}
		if (const auto it = cooldowns.find(a_form->GetFormID()); it != cooldowns.end()) {
			const float remaining = Remaining(it->second);
			return { remaining, std::max(it->second.total, remaining) };
		}
		return { 0.0f, 0.0f };
	}

	void Save(SKSE::SerializationInterface* a_intfc)
	{
		if (cooldowns.empty()) {
			return;
		}
		if (!a_intfc->OpenRecord(kRecord, kRecordVersion)) {
			logs::error("Failed to open shout cooldowns record");
			return;
		}
		a_intfc->WriteRecordData(static_cast<std::uint32_t>(cooldowns.size()));
		for (const auto& [shout, cooldown] : cooldowns) {
			a_intfc->WriteRecordData(shout);
			a_intfc->WriteRecordData(cooldown.readyAt);
			a_intfc->WriteRecordData(cooldown.total);
		}
	}

	void Load(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version)
	{
		cooldowns.clear();
		if (a_version != kRecordVersion) {
			return;
		}
		std::uint32_t count = 0;
		a_intfc->ReadRecordData(count);
		for (std::uint32_t i = 0; i < count; ++i) {
			RE::FormID shout = 0;
			Cooldown   cooldown;
			a_intfc->ReadRecordData(shout);
			a_intfc->ReadRecordData(cooldown.readyAt);
			a_intfc->ReadRecordData(cooldown.total);
			if (RE::FormID resolved = 0; a_intfc->ResolveFormID(shout, resolved)) {
				cooldowns[resolved] = cooldown;
			}
		}
		logs::info("Loaded {} shout cooldown(s)", cooldowns.size());
	}

	void Clear()
	{
		cooldowns.clear();
	}
}
