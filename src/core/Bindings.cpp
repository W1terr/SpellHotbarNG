#include "core/Bindings.h"

#include "casting/ShoutCooldowns.h"
#include "casting/SpellTiming.h"
#include "core/Util.h"

namespace Bindings
{
	namespace
	{
		constexpr std::uint32_t kSerializationID = 'SHNG';
		constexpr std::uint32_t kRecordBindings = 'BIND';
		constexpr std::uint32_t kRecordReady = 'REDY';
		constexpr std::uint32_t kRecordVersion = 1;
		constexpr std::uint32_t kBindingsVersion = 4;  // v2: + slot mode, v4: + gear set (v3 was a test build's cast time)

		std::array<std::array<SlotBinding, kMaxSlots>, kPageCount> slots{};
		SlotBinding                                                readySpell{};
		SlotBinding                                                readyPotion{};

		// Bound forms that were favorites at the last check (option "Unfavoriting removes it from the bar")
		std::unordered_map<RE::FormID, bool> favoriteSeen;
		float                                favoriteTimer{ 0.0f };
		constexpr float                      kFavoriteCheckInterval = 0.25f;

		// Whether the player has the form in the favorites; nullopt if that can't be told (item not in the inventory,
		// e.g. the last potion was drunk: that keeps its slot)
		std::optional<bool> IsFavorite(const RE::TESForm* a_form)
		{
			if (a_form->Is(RE::FormType::Spell, RE::FormType::Shout)) {
				const auto favorites = RE::MagicFavorites::GetSingleton();
				return favorites && std::ranges::find(favorites->spells, a_form) != favorites->spells.end();
			}
			const auto player = RE::PlayerCharacter::GetSingleton();
			const auto changes = player ? player->GetInventoryChanges() : nullptr;
			if (!changes || !changes->entryList) {
				return std::nullopt;
			}
			for (const auto entry : *changes->entryList) {
				if (entry && entry->object == a_form) {
					return entry->countDelta > 0 ? std::optional(entry->IsFavorited()) : std::nullopt;
				}
			}
			return std::nullopt;
		}

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

		// v2 (a 1.9.5 test build) also had a cast time, which is skipped
		void ReadReady(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version)
		{
			RE::FormID   spell = 0, potion = 0;
			std::uint8_t hand = 0;
			a_intfc->ReadRecordData(spell);
			a_intfc->ReadRecordData(hand);
			a_intfc->ReadRecordData(potion);
			if (a_version >= 2) {
				float castTime = 0.0f;
				a_intfc->ReadRecordData(castTime);
			}

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

		// ---- gear sets ---------------------------------------------------------------------------------------------

		enum class GearKind
		{
			kNone,
			kWeapon,  // one-handed: two-handed weapons, bows and crossbows can't be in a set
			kArmor
		};

		GearKind KindOf(const RE::TESForm* a_form)
		{
			if (const auto weapon = a_form ? a_form->As<RE::TESObjectWEAP>() : nullptr) {
				using Type = RE::WEAPON_TYPE;
				switch (weapon->GetWeaponType()) {
				case Type::kTwoHandSword:
				case Type::kTwoHandAxe:
				case Type::kBow:
				case Type::kCrossbow:
					return GearKind::kNone;
				default:
					return GearKind::kWeapon;
				}
			}
			return a_form && a_form->Is(RE::FormType::Armor) ? GearKind::kArmor : GearKind::kNone;
		}

		// the slot holds a_items (first one = `form`), hand and mode kept; no items empties it
		void SetItems(SlotBinding& a_binding, const std::vector<RE::FormID>& a_items)
		{
			if (a_items.empty()) {
				a_binding = {};
				return;
			}
			a_binding.form = a_items[0];
			a_binding.set = {};
			for (std::size_t i = 1; i < a_items.size() && i < kMaxGearSet; ++i) {
				a_binding.set[i - 1] = a_items[i];
			}
		}

		bool RemoveItem(SlotBinding& a_binding, RE::FormID a_form)
		{
			auto items = Items(a_binding);
			if (std::erase(items, a_form) == 0) {
				return false;
			}
			if (a_binding.IsGearSet()) {
				a_binding.hand = Hand::kAuto;  // a weapon set's hands were for the two of them
			}
			SetItems(a_binding, items);
			return true;
		}

		// nullopt: not a gear set bind, the slot is bound as without the option
		std::optional<MenuBind> BindToSet(SlotBinding& a_binding, const RE::TESForm* a_form)
		{
			const auto kind = KindOf(a_form);
			if (kind == GearKind::kNone || KindOf(RE::TESForm::LookupByID(a_binding.form)) != kind) {
				return std::nullopt;
			}
			const auto id = a_form->GetFormID();
			auto       items = Items(a_binding);
			if (std::ranges::find(items, id) != items.end()) {
				if (!a_binding.IsGearSet()) {
					return std::nullopt;  // one item: its hand cycles / it's cleared as before
				}
				RemoveItem(a_binding, id);
				return MenuBind::kRemovedFromSet;
			}
			if (items.size() >= static_cast<std::size_t>(kind == GearKind::kWeapon ? kMaxWeaponSet : kMaxGearSet)) {
				return MenuBind::kSetRefused;
			}
			if (kind == GearKind::kArmor) {
				const auto mask = a_form->As<RE::TESObjectARMO>()->GetSlotMask().underlying();
				for (const auto other : items) {
					const auto armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(other);
					if (armor && (armor->GetSlotMask().underlying() & mask) != 0) {
						return MenuBind::kSetRefused;  // they can't be worn together
					}
				}
			}
			items.push_back(id);
			SetItems(a_binding, items);
			a_binding.hand = Hand::kAuto;  // weapons: the first one in the right hand, the second in the left
			return MenuBind::kAddedToSet;
		}

		void OnSave(SKSE::SerializationInterface* a_intfc)
		{
			std::scoped_lock lock(Config::Lock());

			std::vector<std::tuple<std::uint8_t, std::uint8_t, SlotBinding>> entries;
			for (int page = 0; page < kPageCount; ++page) {
				for (int slot = 0; slot < kMaxSlots; ++slot) {
					const auto& binding = slots[page][slot];
					if (!binding.Empty()) {
						entries.emplace_back(static_cast<std::uint8_t>(page), static_cast<std::uint8_t>(slot), binding);
					}
				}
			}

			if (!a_intfc->OpenRecord(kRecordBindings, kBindingsVersion)) {
				logs::error("Failed to open bindings record");
				return;
			}
			const auto count = static_cast<std::uint32_t>(entries.size());
			a_intfc->WriteRecordData(count);
			for (const auto& [page, slot, binding] : entries) {
				a_intfc->WriteRecordData(page);
				a_intfc->WriteRecordData(slot);
				a_intfc->WriteRecordData(binding.form);
				a_intfc->WriteRecordData(static_cast<std::uint8_t>(binding.hand));
				a_intfc->WriteRecordData(static_cast<std::uint8_t>(binding.mode));
				const auto items = Items(binding);
				a_intfc->WriteRecordData(static_cast<std::uint8_t>(items.size() - 1));
				for (std::size_t i = 1; i < items.size(); ++i) {
					a_intfc->WriteRecordData(items[i]);
				}
			}
			logs::info("Saved {} slot bindings", count);
			WriteReady(a_intfc);
			ShoutCooldowns::Save(a_intfc);
			SpellTiming::Save(a_intfc);
		}

		void OnLoad(SKSE::SerializationInterface* a_intfc)
		{
			std::scoped_lock lock(Config::Lock());
			ClearAll();

			std::uint32_t type, version, length;
			while (a_intfc->GetNextRecordInfo(type, version, length)) {
				if (type == kRecordReady && version >= 1 && version <= 2) {
					ReadReady(a_intfc, version);
					continue;
				}
				if (type == ShoutCooldowns::kRecord) {
					ShoutCooldowns::Load(a_intfc, version);
					continue;
				}
				if (type == SpellTiming::kRecord) {
					SpellTiming::LoadRecord(a_intfc, version);
					continue;
				}
				// v3 (a 1.9.5 test build) also had a cast time per slot, which is skipped
				if (type != kRecordBindings || version < 1 || version > kBindingsVersion) {
					logs::warn("Unknown co-save record {:X} v{}", type, version);
					continue;
				}
				std::uint32_t count = 0;
				a_intfc->ReadRecordData(count);
				std::uint32_t restored = 0;
				for (std::uint32_t i = 0; i < count; ++i) {
					std::uint8_t page = 0, slot = 0, hand = 0, mode = 0;
					RE::FormID   form = 0;
					a_intfc->ReadRecordData(page);
					a_intfc->ReadRecordData(slot);
					a_intfc->ReadRecordData(form);
					a_intfc->ReadRecordData(hand);
					if (version >= 2) {
						a_intfc->ReadRecordData(mode);
					}
					if (version == 3) {
						float castTime = 0.0f;
						a_intfc->ReadRecordData(castTime);
					}
					std::vector<RE::FormID> items{ form };
					if (version >= 4) {
						std::uint8_t others = 0;
						a_intfc->ReadRecordData(others);
						for (std::uint8_t k = 0; k < others; ++k) {
							RE::FormID other = 0;
							a_intfc->ReadRecordData(other);
							items.push_back(other);
						}
					}

					// items of removed plugins drop out, the rest of a set stays
					std::erase_if(items, [&](RE::FormID& a_item) {
						RE::FormID resolved = 0;
						if (!a_intfc->ResolveFormID(a_item, resolved) || !RE::TESForm::LookupByID(resolved)) {
							return true;
						}
						a_item = resolved;
						return false;
					});
					if (items.empty() || !Valid(static_cast<Page>(page), slot)) {
						continue;
					}
					auto& binding = slots[page][slot];
					binding = { 0, static_cast<Hand>(std::min<std::uint8_t>(hand, 3)), static_cast<SlotMode>(std::min<std::uint8_t>(mode, 2)) };
					SetItems(binding, items);
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
			SpellTiming::Clear();
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
		favoriteSeen.clear();
	}

	void UpdateFavorites(float a_delta)
	{
		if (!Config::Get().unfavoriteRemoves) {
			favoriteSeen.clear();
			return;
		}
		favoriteTimer += a_delta;
		if (favoriteTimer < kFavoriteCheckInterval) {
			return;
		}
		favoriteTimer = 0.0f;

		std::vector<SlotBinding*> bound;
		for (auto& page : slots) {
			for (auto& binding : page) {
				bound.push_back(&binding);
			}
		}
		bound.push_back(&readySpell);
		bound.push_back(&readyPotion);

		std::vector<RE::FormID> ids;
		for (const auto binding : bound) {
			std::ranges::copy(Items(*binding), std::back_inserter(ids));
		}

		std::unordered_map<RE::FormID, bool> seen;
		for (const auto id : ids) {
			if (seen.contains(id)) {
				continue;
			}
			const auto form = RE::TESForm::LookupByID(id);
			const auto now = form ? IsFavorite(form) : std::nullopt;
			const auto before = favoriteSeen.find(id);
			const bool wasFavorite = before != favoriteSeen.end() && before->second;
			if (!now) {
				seen[id] = wasFavorite;  // can't tell right now: remember what it was
				continue;
			}
			seen[id] = *now;
			if (wasFavorite && !*now) {
				// unfavorited, like a vanilla favorite hotkey: off the bar (every slot holding it, also in gear sets)
				int cleared = 0;
				for (const auto other : bound) {
					cleared += RemoveItem(*other, id) ? 1 : 0;
				}
				logs::info("{} was unfavorited: removed from {} slot(s)", form->GetName(), cleared);
			}
		}
		favoriteSeen = std::move(seen);
	}

	MenuBind BindFromMenu(Page a_page, int a_slot, RE::TESForm* a_form)
	{
		if (!a_form || !(Valid(a_page, a_slot) || IsReadySlot(a_slot))) {
			return MenuBind::kCleared;
		}
		auto& binding = Get(a_page, a_slot);
		if (Config::Get().gearSets && !IsReadySlot(a_slot) && !binding.Empty()) {
			if (const auto result = BindToSet(binding, a_form)) {
				return *result;
			}
		}
		if (binding.form != a_form->GetFormID() || binding.IsGearSet()) {
			binding = { a_form->GetFormID(), Hand::kAuto };
			return MenuBind::kBound;
		}
		if (!UsesHand(a_form)) {
			binding = {};
			return MenuBind::kCleared;
		}
		switch (binding.hand) {
		case Hand::kLeft:
			binding.hand = a_form->Is(RE::FormType::Weapon) ? Hand::kAuto : Hand::kBoth;
			break;
		case Hand::kBoth:
			binding.hand = Hand::kAuto;
			break;
		default:
			binding.hand = Hand::kLeft;
			break;
		}
		return MenuBind::kHandChanged;
	}

	std::vector<RE::FormID> Items(const SlotBinding& a_binding)
	{
		std::vector<RE::FormID> out;
		if (a_binding.Empty()) {
			return out;
		}
		out.push_back(a_binding.form);
		for (const auto id : a_binding.set) {
			if (id != 0) {
				out.push_back(id);
			}
		}
		return out;
	}

	std::vector<RE::TESForm*> ItemForms(const SlotBinding& a_binding)
	{
		std::vector<RE::TESForm*> out;
		for (const auto id : Items(a_binding)) {
			if (const auto form = RE::TESForm::LookupByID(id)) {
				out.push_back(form);
			}
		}
		return out;
	}

	bool UsesHand(const RE::TESForm* a_form)
	{
		if (const auto spell = a_form ? a_form->As<RE::SpellItem>() : nullptr) {
			return spell->GetSpellType() == RE::MagicSystem::SpellType::kSpell && !spell->IsTwoHanded();
		}
		return a_form && (a_form->Is(RE::FormType::Scroll) || a_form->Is(RE::FormType::Weapon));
	}

	bool UsesMode(const RE::TESForm* a_form)
	{
		return a_form && (a_form->Is(RE::FormType::Spell) || a_form->Is(RE::FormType::Scroll) || a_form->Is(RE::FormType::Shout));
	}

	KeyMode ModeOf(const SlotBinding& a_binding)
	{
		switch (a_binding.mode) {
		case SlotMode::kCast:
			return KeyMode::kCast;
		case SlotMode::kEquip:
			return KeyMode::kEquip;
		default:
			return Config::Get().keyMode;
		}
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
				json entry{ { "page", page }, { "slot", slot }, { "form", key }, { "hand", static_cast<int>(binding.hand) },
					{ "mode", static_cast<int>(binding.mode) } };
				if (binding.IsGearSet()) {
					json set = json::array();
					for (const auto id : binding.set) {
						if (auto other = id ? Util::ToPluginKey(id) : std::string{}; !other.empty()) {
							set.push_back(std::move(other));
						}
					}
					entry["set"] = std::move(set);
				}
				out.push_back(std::move(entry));
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
			const auto mode = std::clamp(entry.value("mode", 0), 0, 2);
			if (form && RE::TESForm::LookupByID(form) && Valid(static_cast<Page>(page), slot)) {
				auto& binding = slots[page][slot];
				binding = { form, static_cast<Hand>(hand), static_cast<SlotMode>(mode) };
				if (const auto set = entry.find("set"); set != entry.end() && set->is_array()) {
					std::vector<RE::FormID> items{ form };
					for (const auto& other : *set) {
						const auto id = other.is_string() ? Util::FromPluginKey(other.get<std::string>()) : 0;
						if (id && RE::TESForm::LookupByID(id)) {
							items.push_back(id);
						}
					}
					SetItems(binding, items);
				}
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
