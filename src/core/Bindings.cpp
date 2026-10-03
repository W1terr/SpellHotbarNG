#include "core/Bindings.h"

#include "casting/ShoutCooldowns.h"
#include "core/Util.h"

namespace Bindings
{
	namespace
	{
		constexpr std::uint32_t kSerializationID = 'SHNG';
		constexpr std::uint32_t kRecordBindings = 'BIND';
		constexpr std::uint32_t kRecordReady = 'REDY';
		constexpr std::uint32_t kRecordVersion = 1;

		std::array<std::array<SlotBinding, kMaxSlots>, kPageCount> slots{};
		SlotBinding                                                readySpell{};
		SlotBinding                                                readyPotion{};

		void WriteReady(SKSE::SerializationInterface* a_intfc)
		{
			if (!a_intfc->OpenRecord(kRecordReady, kRecordVersion)) {
				logs::error("Failed to open ready slots record");
				return;
			}
			a_intfc->WriteRecordData(readySpell.form);
			a_intfc->WriteRecordData(static_cast<std::uint8_t>(readySpell.hand));
			a_intfc->WriteRecordData(readyPotion.form);
		}

		void ReadReady(SKSE::SerializationInterface* a_intfc)
		{
			RE::FormID   spell = 0, potion = 0;
			std::uint8_t hand = 0;
			a_intfc->ReadRecordData(spell);
			a_intfc->ReadRecordData(hand);
			a_intfc->ReadRecordData(potion);

			const auto resolve = [&](RE::FormID a_form) -> RE::FormID {
				RE::FormID resolved = 0;
				return a_form && a_intfc->ResolveFormID(a_form, resolved) && RE::TESForm::LookupByID(resolved) ? resolved : 0;
			};
			readySpell = { resolve(spell), static_cast<Hand>(std::min<std::uint8_t>(hand, 3)) };
			readyPotion = { resolve(potion), Hand::kAuto };
		}

		bool Valid(Page a_page, int a_slot)
		{
			const auto page = static_cast<int>(a_page);
			return page >= 0 && page < kPageCount && a_slot >= 0 && a_slot < kMaxSlots;
		}

		void OnSave(SKSE::SerializationInterface* a_intfc)
		{
			std::scoped_lock lock(Config::Lock());

			std::vector<std::tuple<std::uint8_t, std::uint8_t, RE::FormID, std::uint8_t>> entries;
			for (int page = 0; page < kPageCount; ++page) {
				for (int slot = 0; slot < kMaxSlots; ++slot) {
					const auto& binding = slots[page][slot];
					if (!binding.Empty()) {
						entries.emplace_back(static_cast<std::uint8_t>(page), static_cast<std::uint8_t>(slot), binding.form, static_cast<std::uint8_t>(binding.hand));
					}
				}
			}

			if (!a_intfc->OpenRecord(kRecordBindings, kRecordVersion)) {
				logs::error("Failed to open bindings record");
				return;
			}
			const auto count = static_cast<std::uint32_t>(entries.size());
			a_intfc->WriteRecordData(count);
			for (const auto& [page, slot, form, hand] : entries) {
				a_intfc->WriteRecordData(page);
				a_intfc->WriteRecordData(slot);
				a_intfc->WriteRecordData(form);
				a_intfc->WriteRecordData(hand);
			}
			logs::info("Saved {} slot bindings", count);
			WriteReady(a_intfc);
			ShoutCooldowns::Save(a_intfc);
		}

		void OnLoad(SKSE::SerializationInterface* a_intfc)
		{
			std::scoped_lock lock(Config::Lock());
			ClearAll();

			std::uint32_t type, version, length;
			while (a_intfc->GetNextRecordInfo(type, version, length)) {
				if (type == kRecordReady && version == kRecordVersion) {
					ReadReady(a_intfc);
					continue;
				}
				if (type == ShoutCooldowns::kRecord) {
					ShoutCooldowns::Load(a_intfc, version);
					continue;
				}
				if (type != kRecordBindings || version != kRecordVersion) {
					logs::warn("Unknown co-save record {:X} v{}", type, version);
					continue;
				}
				std::uint32_t count = 0;
				a_intfc->ReadRecordData(count);
				std::uint32_t restored = 0;
				for (std::uint32_t i = 0; i < count; ++i) {
					std::uint8_t page = 0, slot = 0, hand = 0;
					RE::FormID   form = 0;
					a_intfc->ReadRecordData(page);
					a_intfc->ReadRecordData(slot);
					a_intfc->ReadRecordData(form);
					a_intfc->ReadRecordData(hand);

					RE::FormID resolved = 0;
					if (!a_intfc->ResolveFormID(form, resolved) || !Valid(static_cast<Page>(page), slot)) {
						continue;
					}
					if (!RE::TESForm::LookupByID(resolved)) {
						continue;
					}
					slots[page][slot] = { resolved, static_cast<Hand>(std::min<std::uint8_t>(hand, 3)) };
					++restored;
				}
				logs::info("Loaded {} of {} slot bindings", restored, count);
			}
		}

		void OnRevert(SKSE::SerializationInterface*)
		{
			std::scoped_lock lock(Config::Lock());
			ClearAll();
			ShoutCooldowns::Clear();
		}
	}

	SlotBinding& Get(Page a_page, int a_slot)
	{
		static SlotBinding empty{};
		if (a_slot == kReadySpellSlot) {
			return readySpell;
		}
		if (a_slot == kReadyPotionSlot) {
			return readyPotion;
		}
		if (!Valid(a_page, a_slot)) {
			empty = {};
			return empty;
		}
		return slots[static_cast<int>(a_page)][a_slot];
	}

	RE::TESForm* GetForm(Page a_page, int a_slot)
	{
		const auto& binding = Get(a_page, a_slot);
		return binding.Empty() ? nullptr : RE::TESForm::LookupByID(binding.form);
	}

	void Set(Page a_page, int a_slot, RE::FormID a_form, Hand a_hand)
	{
		if (Valid(a_page, a_slot) || IsReadySlot(a_slot)) {
			Get(a_page, a_slot) = { a_form, a_hand };
		}
	}

	void Clear(Page a_page, int a_slot)
	{
		Set(a_page, a_slot, 0, Hand::kAuto);
	}

	void ClearAll()
	{
		for (auto& page : slots) {
			page.fill({});
		}
		readySpell = {};
		readyPotion = {};
	}

	bool Toggle(Page a_page, int a_slot, RE::TESForm* a_form)
	{
		if (!a_form || !(Valid(a_page, a_slot) || IsReadySlot(a_slot))) {
			return false;
		}
		auto& binding = Get(a_page, a_slot);
		if (binding.form == a_form->GetFormID()) {
			binding = {};
			return false;
		}
		binding = { a_form->GetFormID(), Hand::kAuto };
		return true;
	}

	bool FitsReadySlot(int a_slot, const RE::TESForm* a_form)
	{
		if (!a_form) {
			return false;
		}
		if (a_slot == kReadySpellSlot) {
			const auto spell = a_form->As<RE::SpellItem>();
			return (spell && spell->GetSpellType() == RE::MagicSystem::SpellType::kSpell) || a_form->Is(RE::FormType::Scroll);
		}
		return a_slot == kReadyPotionSlot && a_form->Is(RE::FormType::AlchemyItem);
	}

	bool IsBindable(const RE::TESForm* a_form)
	{
		if (!a_form) {
			return false;
		}
		switch (a_form->GetFormType()) {
		case RE::FormType::Spell:
			{
				const auto type = a_form->As<RE::SpellItem>()->GetSpellType();
				return type == RE::MagicSystem::SpellType::kSpell || type == RE::MagicSystem::SpellType::kPower ||
				       type == RE::MagicSystem::SpellType::kLesserPower || type == RE::MagicSystem::SpellType::kVoicePower;
			}
		case RE::FormType::Scroll:
		case RE::FormType::Shout:
		case RE::FormType::AlchemyItem:
		case RE::FormType::Weapon:
		case RE::FormType::Armor:
		case RE::FormType::Ammo:
		case RE::FormType::Light:
			return true;
		default:
			return false;
		}
	}

	json ToJson()
	{
		json out = json::array();
		for (int page = 0; page < kPageCount; ++page) {
			for (int slot = 0; slot < kMaxSlots; ++slot) {
				const auto& binding = slots[page][slot];
				if (binding.Empty()) {
					continue;
				}
				auto key = Util::ToPluginKey(binding.form);
				if (key.empty()) {
					continue;  // player made potions etc. only exist in that save
				}
				out.push_back({ { "page", page }, { "slot", slot }, { "form", key }, { "hand", static_cast<int>(binding.hand) } });
			}
		}
		return out;
	}

	void FromJson(const json& a_json)
	{
		if (!a_json.is_array()) {
			return;
		}
		ClearAll();
		for (const auto& entry : a_json) {
			const auto page = entry.value("page", -1);
			const auto slot = entry.value("slot", -1);
			const auto form = Util::FromPluginKey(entry.value("form", ""s));
			const auto hand = std::clamp(entry.value("hand", 0), 0, 3);
			if (form && RE::TESForm::LookupByID(form) && Valid(static_cast<Page>(page), slot)) {
				slots[page][slot] = { form, static_cast<Hand>(hand) };
			}
		}
	}

	void RegisterSerialization()
	{
		auto serialization = SKSE::GetSerializationInterface();
		serialization->SetUniqueID(kSerializationID);
		serialization->SetSaveCallback(OnSave);
		serialization->SetLoadCallback(OnLoad);
		serialization->SetRevertCallback(OnRevert);
	}
}
