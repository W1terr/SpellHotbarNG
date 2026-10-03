#include "casting/SpellCharges.h"

#include "core/Config.h"

namespace SpellCharges
{
	namespace
	{
		struct Message
		{
			int              at{ 0 };
			RE::BGSMessage*  message{ nullptr };
			bool             interruptCast{ false };
		};

		struct Rule
		{
			std::string          name;
			RE::EffectSetting*   activeEffect{ nullptr };
			RE::TESGlobal*       counter{ nullptr };
			std::vector<Message> messages;
			RE::TESGlobal*       healthDamagePerMissingCharge{ nullptr };
		};

		std::vector<Rule> rules;

		template <class T>
		T* LookupForm(const std::string& a_plugin, const json& a_id)
		{
			if (!a_id.is_string()) {
				return nullptr;
			}
			const auto        text = a_id.get<std::string>();
			RE::FormID        local = 0;
			const std::string_view digits = text.starts_with("0x") || text.starts_with("0X") ? std::string_view(text).substr(2) : std::string_view(text);
			if (std::from_chars(digits.data(), digits.data() + digits.size(), local, 16).ec != std::errc{}) {
				return nullptr;
			}
			const auto dataHandler = RE::TESDataHandler::GetSingleton();
			return dataHandler ? dataHandler->LookupForm<T>(local, a_plugin) : nullptr;
		}

		void ShowMessage(RE::BGSMessage* a_message)
		{
			RE::BSString text;
			a_message->GetDescription(text, a_message);
			if (text.empty()) {
				return;
			}
			if (a_message->flags.all(RE::BGSMessage::MessageFlag::kMessageBox)) {
				RE::DebugMessageBox(text.c_str());
			} else {
				RE::SendHUDMessage::ShowHUDMessage(text.c_str());
			}
		}

		// one counted cast, like the perk's OnSpellCast: returns true if it interrupts
		bool CountCast(const Rule& a_rule, RE::PlayerCharacter* a_player)
		{
			a_rule.counter->value -= 1.0f;
			const int left = static_cast<int>(a_rule.counter->value);
			bool      interrupt = false;
			for (const auto& message : a_rule.messages) {
				if (left == message.at) {
					if (message.message) {
						ShowMessage(message.message);
					}
					interrupt |= message.interruptCast;
				}
			}
			if (a_rule.healthDamagePerMissingCharge) {
				const float damage = static_cast<float>(-left) * a_rule.healthDamagePerMissingCharge->value;
				if (damage > 0.0f) {
					a_player->AsActorValueOwner()->DamageActorValue(RE::ActorValue::kHealth, damage);
				}
			}
			return interrupt;
		}
	}

	void Load()
	{
		rules.clear();
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
				const auto    name = j.value("name", entry.path().stem().string());
				for (const auto& item : j.value("spellCharges", json::array())) {
					const auto plugin = item.value("plugin", "");
					Rule       rule{ .name = name };
					rule.activeEffect = LookupForm<RE::EffectSetting>(plugin, item.value("activeEffect", json()));
					rule.counter = LookupForm<RE::TESGlobal>(plugin, item.value("counter", json()));
					if (!rule.activeEffect || !rule.counter) {
						logs::info("Compatibility {}: {} isn't loaded, skipped", name, plugin);
						continue;
					}
					for (const auto& message : item.value("messages", json::array())) {
						rule.messages.push_back({ message.value("at", 0), LookupForm<RE::BGSMessage>(plugin, message.value("message", json())),
							message.value("interruptCast", false) });
					}
					rule.healthDamagePerMissingCharge = LookupForm<RE::TESGlobal>(plugin, item.value("healthDamagePerMissingCharge", json()));
					rules.push_back(std::move(rule));
				}
			} catch (const std::exception& e) {
				logs::error("Failed to read compatibility file {}: {}", entry.path().string(), e.what());
			}
		}
		for (const auto& rule : rules) {
			logs::info("Compatibility {}: hotbar casts count for {}", rule.name, rule.counter->GetFormEditorID());
		}
	}

	bool OnHotbarCast(RE::MagicItem* a_spell, int a_casts)
	{
		const auto player = RE::PlayerCharacter::GetSingleton();
		const auto spell = a_spell ? a_spell->As<RE::SpellItem>() : nullptr;
		if (rules.empty() || !player || !spell || spell->GetSpellType() != RE::MagicSystem::SpellType::kSpell) {
			return false;
		}
		bool interrupt = false;
		for (const auto& rule : rules) {
			if (!player->AsMagicTarget()->HasMagicEffect(rule.activeEffect)) {
				continue;
			}
			for (int i = 0; i < a_casts; ++i) {
				interrupt |= CountCast(rule, player);
			}
			logs::debug("Compatibility {}: {} left", rule.name, rule.counter->value);
		}
		return interrupt;
	}
}
