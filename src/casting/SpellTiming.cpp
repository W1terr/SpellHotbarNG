#include "casting/SpellTiming.h"

#include "core/Config.h"
#include "core/Util.h"

namespace SpellTiming
{
	namespace
	{
		struct Running
		{
			float remaining{ 0.0f };
			float total{ 0.0f };
		};

		std::mutex                                 lock;      // menu (render thread) and game thread
		std::map<std::string, Timing>              byKey;     // the file, also for plugins that aren't loaded
		std::unordered_map<RE::FormID, Timing>     byForm;    // the same for loaded forms
		std::unordered_map<RE::FormID, Running>    running;   // cooldowns ticking now

		std::unordered_map<RE::FormID, RE::TESGlobal*> linked;  // powers whose cooldown is their mod's own setting

		template <class T>
		T* LookupCompatForm(const std::string& a_plugin, const json& a_id)
		{
			const auto handler = RE::TESDataHandler::GetSingleton();
			if (!handler || !a_id.is_string()) {
				return nullptr;
			}
			try {
				return handler->LookupForm<T>(static_cast<RE::FormID>(std::stoul(a_id.get<std::string>(), nullptr, 16)), a_plugin);
			} catch (const std::exception&) {
				return nullptr;
			}
		}

		std::filesystem::path FilePath()
		{
			return Config::DataDir() / "spell_timing.json";
		}

		Timing Clamped(Timing a_timing)
		{
			a_timing.castTime = a_timing.castTime > 0.0f ? std::clamp(a_timing.castTime, kMinCastTime, kMaxCastTime) : 0.0f;
			a_timing.cooldown = std::clamp(a_timing.cooldown, 0.0f, kMaxCooldown);
			return a_timing;
		}

		void WriteFile()
		{
			json spells = json::object();
			for (const auto& [key, timing] : byKey) {
				json entry = json::object();
				if (timing.castTime > 0.0f) {
					entry["castTime"] = timing.castTime;
				}
				if (timing.cooldown > 0.0f) {
					entry["cooldown"] = timing.cooldown;
				}
				spells[key] = entry;
			}
			Config::WriteJsonFile(FilePath(), json{ { "version", 1 }, { "spells", spells } });
		}
	}

	void Load()
	{
		std::scoped_lock guard(lock);
		byKey.clear();
		byForm.clear();
		const auto j = Config::ReadJsonFile(FilePath());
		if (!j || !j->contains("spells") || !(*j)["spells"].is_object()) {
			return;
		}
		for (const auto& [key, entry] : (*j)["spells"].items()) {
			if (!entry.is_object()) {
				continue;
			}
			Timing timing;
			timing.castTime = entry.value("castTime", 0.0f);
			timing.cooldown = entry.value("cooldown", 0.0f);
			timing = Clamped(timing);
			if (timing.Empty()) {
				continue;
			}
			byKey[key] = timing;
			if (const auto form = Util::FromPluginKey(key)) {
				byForm[form] = timing;
			}
		}
		logs::info("Spell timing: {} spell(s) changed ({} loaded)", byKey.size(), byForm.size());
	}

	bool CanHaveTiming(const RE::TESForm* a_form)
	{
		if (!a_form) {
			return false;
		}
		if (a_form->Is(RE::FormType::Scroll)) {
			return true;
		}
		const auto spell = a_form->As<RE::SpellItem>();
		return spell && spell->GetSpellType() == RE::MagicSystem::SpellType::kSpell;
	}

	bool CanHaveCastTime(const RE::TESForm* a_form)
	{
		const auto item = CanHaveTiming(a_form) ? a_form->As<RE::MagicItem>() : nullptr;
		return item && item->GetCastingType() != RE::MagicSystem::CastingType::kConcentration;
	}

	void LoadCompat()
	{
		std::scoped_lock guard(lock);
		linked.clear();
		std::error_code ec;
		const auto      dir = Config::DataDir() / "compat";
		if (!std::filesystem::is_directory(dir, ec)) {
			return;
		}
		for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
			if (!entry.is_regular_file(ec) || entry.path().extension() != ".json") {
				continue;
			}
			try {
				std::ifstream file(entry.path());
				const auto    j = json::parse(file, nullptr, true, true);
				for (const auto& item : j.value("powerCooldowns", json::array())) {
					const auto plugin = item.value("plugin", "");
					const auto power = LookupCompatForm<RE::SpellItem>(plugin, item.value("power", json()));
					const auto global = LookupCompatForm<RE::TESGlobal>(plugin, item.value("cooldownGlobal", json()));
					if (!power || !global) {
						continue;
					}
					linked[power->GetFormID()] = global;
					logs::info("Compatibility: {} cooldown = {} ({:.1f} s)", power->GetName(), global->GetFormEditorID(), global->value);
				}
			} catch (const std::exception& e) {
				logs::error("Failed to read compatibility file {}: {}", Util::ToUtf8(entry.path()), e.what());
			}
		}
	}

	Timing Get(const RE::TESForm* a_form)
	{
		std::scoped_lock guard(lock);
		if (const auto it = a_form ? linked.find(a_form->GetFormID()) : linked.end(); it != linked.end()) {
			return { 0.0f, std::clamp(it->second->value, 0.0f, kMaxCooldown) };
		}
		const auto       it = a_form ? byForm.find(a_form->GetFormID()) : byForm.end();
		return it != byForm.end() ? it->second : Timing{};
	}

	void Set(const RE::TESForm* a_form, const Timing& a_timing, bool a_write)
	{
		const auto key = a_form ? Util::ToPluginKey(a_form->GetFormID()) : std::string{};
		if (key.empty() || !CanHaveTiming(a_form)) {
			return;  // made in game (0xFF...): no stable id to save it under
		}
		std::scoped_lock guard(lock);
		const auto       timing = Clamped(a_timing);
		const auto       id = a_form->GetFormID();
		if (timing.Empty()) {
			byKey.erase(key);
			byForm.erase(id);
		} else {
			byKey[key] = timing;
			byForm[id] = timing;
		}
		if (timing.cooldown <= 0.0f) {
			running.erase(id);
		} else if (const auto it = running.find(id); it != running.end()) {
			it->second.remaining = std::min(it->second.remaining, timing.cooldown);  // made shorter: no longer wait
			it->second.total = timing.cooldown;
		}
		if (a_write) {
			WriteFile();
		}
	}

	std::optional<float> CastTime(const RE::TESForm* a_form)
	{
		if (!CanHaveCastTime(a_form)) {
			return std::nullopt;
		}
		const auto timing = Get(a_form);
		return timing.castTime > 0.0f ? std::optional{ timing.castTime } : std::nullopt;
	}

	void StartCooldown(const RE::TESForm* a_form)
	{
		const auto cooldown = Get(a_form).cooldown;
		if (cooldown <= 0.0f) {
			return;
		}
		std::scoped_lock guard(lock);
		running[a_form->GetFormID()] = { cooldown, cooldown };
		logs::debug("{}: cooldown {:.1f} s", a_form->GetName(), cooldown);
	}

	std::pair<float, float> Cooldown(const RE::TESForm* a_form)
	{
		std::scoped_lock guard(lock);
		const auto       it = a_form ? running.find(a_form->GetFormID()) : running.end();
		return it != running.end() ? std::pair{ it->second.remaining, it->second.total } : std::pair{ 0.0f, 0.0f };
	}

	void Update(float a_delta)
	{
		std::scoped_lock guard(lock);
		for (auto it = running.begin(); it != running.end();) {
			it->second.remaining -= a_delta;
			it = it->second.remaining <= 0.0f ? running.erase(it) : std::next(it);
		}
	}

	void Save(SKSE::SerializationInterface* a_intfc)
	{
		std::scoped_lock guard(lock);
		if (running.empty()) {
			return;
		}
		if (!a_intfc->OpenRecord(kRecord, kRecordVersion)) {
			logs::error("Failed to open spell cooldowns record");
			return;
		}
		a_intfc->WriteRecordData(static_cast<std::uint32_t>(running.size()));
		for (const auto& [form, cooldown] : running) {
			a_intfc->WriteRecordData(form);
			a_intfc->WriteRecordData(cooldown.remaining);
			a_intfc->WriteRecordData(cooldown.total);
		}
	}

	void LoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version)
	{
		std::scoped_lock guard(lock);
		running.clear();
		if (a_version != kRecordVersion) {
			return;
		}
		std::uint32_t count = 0;
		a_intfc->ReadRecordData(count);
		for (std::uint32_t i = 0; i < count; ++i) {
			RE::FormID form = 0;
			Running    cooldown;
			a_intfc->ReadRecordData(form);
			a_intfc->ReadRecordData(cooldown.remaining);
			a_intfc->ReadRecordData(cooldown.total);
			if (RE::FormID resolved = 0; a_intfc->ResolveFormID(form, resolved) && cooldown.remaining > 0.0f) {
				running[resolved] = cooldown;
			}
		}
		logs::info("Loaded {} spell cooldown(s)", running.size());
	}

	void Clear()
	{
		std::scoped_lock guard(lock);
		running.clear();
	}
}
