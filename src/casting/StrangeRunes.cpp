#include "casting/StrangeRunes.h"

#include "core/Config.h"

namespace StrangeRunes
{
	namespace
	{
		using Object = RE::BSScript::Object;

		constexpr std::string_view kPlugin = "StrangeRunes.esp";
		constexpr const char*      kScript = "PO3_RUNEDetectPlayerCasting";
		constexpr float            kRemoveDelay = 0.1f;  // the script also waits this long before taking a rune off

		// The script's rune types (its GetRuneType result) -> its rune spell properties without the "Left" / "Right" /
		// "Ritual" ending ("po3_RUNE_<name><hand>"). kWard uses po3_RUNE_WardFX on either hand.
		constexpr std::string_view kRuneNames[] = { "Fire", "Frost", "Shock", "Drain", "FireRune", "FrostRune", "ShockRune",
			"Destruction", "Heal", "Poison", "MysticHeal", "TurnUndead", "Sun", "", "PoisonRune", "Paralyse", "Armour", "ArmourFire",
			"ArmourFrost", "ArmourPoison", "ArmourShock", "DetectLife", "Guide", "Light", "Telekinesis", "Alteration", "AshRune",
			"Frenzy", "Calm", "Dispel", "Invisibility", "Illusion", "FrenzyRune", "ConjureFire", "ConjureFrost", "ConjureShock",
			"ConjureFamiliar", "Reanimate", "SoulTrap", "Banish", "BoundWeapon", "Conjure", "Weakness", "WeaknessFire", "WeaknessFrost",
			"WeaknessPoison", "WeaknessShock", "WeaknessHeals", "Air", "Water", "EarthRune", "ConjureWater", "ConjureEarth",
			"ConjureHeals", "ArmourHeals" };
		constexpr int kWard = 13;

		std::optional<bool>         installed;
		bool                        warnedNoScript{ false };
		std::uint32_t               castId{ 0 };  // the running hotbar cast; answers for older casts are ignored
		bool                        casting{ false };
		std::vector<RE::SpellItem*> active;   // rune spells added for the running cast
		std::vector<std::pair<RE::SpellItem*, float>> leaving;  // taken off when their time runs out

		RE::PlayerCharacter* Player()
		{
			return RE::PlayerCharacter::GetSingleton();
		}

		bool Installed()
		{
			if (!installed) {
				const auto dataHandler = RE::TESDataHandler::GetSingleton();
				installed = dataHandler && dataHandler->LookupModByName(kPlugin);
				if (*installed) {
					logs::info("Strange Runes found: hotbar casts get its runes");
				}
			}
			return *installed;
		}

		// The rune script running on one of the player's active effects
		RE::BSTSmartPointer<Object> Script()
		{
			const auto player = Player();
			const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			const auto policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
			const auto list = player && policy ? player->AsMagicTarget()->GetActiveEffectList() : nullptr;
			if (!list) {
				return {};
			}
			for (const auto effect : *list) {
				if (!effect) {
					continue;
				}
				const auto handle = policy->GetHandleForObject(RE::ActiveEffect::VMTYPEID, effect);
				RE::BSTSmartPointer<Object> object;
				if (handle != policy->EmptyHandle() && vm->FindBoundObject(handle, kScript, object) && object) {
					return object;
				}
			}
			return {};
		}

		template <class T>
		T* Property(Object& a_script, std::string_view a_name)
		{
			const auto variable = a_script.GetProperty(RE::BSFixedString(a_name));
			return variable && variable->IsObject() ? variable->Unpack<T*>() : nullptr;
		}

		bool Toggle(Object& a_script, std::string_view a_global)
		{
			const auto global = Property<RE::TESGlobal>(a_script, a_global);
			return global && global->value != 0.0f;
		}

		enum class Kind
		{
			kRitual,
			kSpell,
			kScroll
		};

		// The script's GetSpellType with its MCM toggles; nullopt = no rune for this cast
		std::optional<Kind> KindOf(Object& a_script, RE::MagicItem* a_item, bool a_scroll, bool a_dual)
		{
			if (a_scroll) {
				return Toggle(a_script, "po3_RUNE_ScrollsToggle") ? std::optional(Kind::kScroll) : std::nullopt;
			}
			const auto ritual = Property<RE::BGSKeyword>(a_script, "RitualSpellEffect");
			const auto ritualIllusion = Property<RE::BGSKeyword>(a_script, "RitualSpellIllusion");
			if (((ritual && a_item->HasKeyword(ritual)) || (ritualIllusion && a_item->HasKeyword(ritualIllusion))) &&
				Toggle(a_script, "po3_RUNE_MasterSpellsToggle")) {
				return Kind::kRitual;
			}
			const auto effect = a_item->GetCostliestEffectItem();
			const auto base = effect ? effect->baseEffect : nullptr;
			if (!base) {
				return std::nullopt;
			}
			using CT = RE::MagicSystem::CastingType;
			const auto castingType = base->data.castingType;
			if ((castingType == CT::kFireAndForget && Toggle(a_script, "po3_RUNE_FFSpellsTogglePC")) ||
				(castingType == CT::kConcentration && Toggle(a_script, "po3_RUNE_ConcSpellsTogglePC")) ||
				(base->data.delivery == RE::MagicSystem::Delivery::kSelf && Toggle(a_script, "po3_RUNE_SelfSpellsTogglePC")) ||
				(a_dual && Toggle(a_script, "po3_RUNE_DualCastRunesToggle"))) {
				return Kind::kSpell;
			}
			return std::nullopt;
		}

		void AddRune(RE::SpellItem* a_rune)
		{
			std::erase_if(leaving, [&](const auto& a_entry) { return a_entry.first == a_rune; });  // still on: keep it
			if (std::ranges::find(active, a_rune) == active.end()) {
				Player()->AddSpell(a_rune);
				active.push_back(a_rune);
			}
		}

		// The script's answer for a cast: put the rune of that type on the hands
		void OnRuneType(std::uint32_t a_cast, int a_type, const RE::BSTSmartPointer<Object>& a_script, const std::vector<std::string>& a_hands)
		{
			std::scoped_lock lock(Config::Lock());
			if (a_cast != castId || !casting || !a_script || a_type < 0 || a_type >= static_cast<int>(std::size(kRuneNames)) || !Player()) {
				return;
			}
			for (const auto& hand : a_hands) {
				if (a_type == kWard && hand == "Ritual") {
					continue;  // the script has no ritual ward rune
				}
				const auto name = a_type == kWard ? std::string("po3_RUNE_WardFX") : std::format("po3_RUNE_{}{}", kRuneNames[a_type], hand);
				if (const auto rune = Property<RE::SpellItem>(*a_script, name)) {
					AddRune(rune);
				}
			}
		}

		// Result of the script's GetRuneType, comes on a Papyrus thread: handled in the main thread
		class RuneTypeCallback : public RE::BSScript::IStackCallbackFunctor
		{
		public:
			RuneTypeCallback(std::uint32_t a_cast, RE::BSTSmartPointer<Object> a_script, std::vector<std::string> a_hands) :
				cast(a_cast), script(std::move(a_script)), hands(std::move(a_hands))
			{}

			void operator()(RE::BSScript::Variable a_result) override
			{
				const int type = a_result.IsInt() ? a_result.GetSInt() : -1;
				SKSE::GetTaskInterface()->AddTask([cast = cast, type, script = script, hands = hands] { OnRuneType(cast, type, script, hands); });
			}

			void SetObject(const RE::BSTSmartPointer<Object>&) override {}

		private:
			std::uint32_t               cast;
			RE::BSTSmartPointer<Object> script;
			std::vector<std::string>    hands;
		};
	}

	void Start(RE::MagicItem* a_item, CastAnim::Side a_side, bool a_scroll)
	{
		Stop();  // a cast still running (shouldn't be) loses its runes
		++castId;
		casting = true;
		if (!a_item || !Installed()) {
			return;
		}
		auto script = Script();
		if (!script) {
			if (!warnedNoScript) {
				warnedNoScript = true;
				logs::info("Strange Runes: its rune script isn't running on the player (yet), no runes");
			}
			return;
		}
		const bool both = a_side == CastAnim::Side::kBoth && !a_item->IsTwoHanded();
		const auto kind = KindOf(*script, a_item, a_scroll, both);
		if (!kind) {
			return;
		}
		// ritual runes come from the script's left hand code; one-handed casts on their hand(s)
		std::vector<std::string> hands;
		if (*kind == Kind::kRitual) {
			hands = { "Ritual" };
		} else if (a_side == CastAnim::Side::kLeft) {
			hands = { "Left" };
		} else if (both) {
			hands = { "Left", "Right" };
		} else {
			hands = { "Right" };
		}

		const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback(new RuneTypeCallback(castId, script, std::move(hands)));
		const auto args = RE::MakeFunctionArguments(static_cast<RE::TESForm*>(a_item));
		if (!vm->DispatchMethodCall(script, "GetRuneType", args, callback)) {
			logs::warn("Strange Runes: could not ask its script for the rune of {}", a_item->GetName());
		}
	}

	void Stop()
	{
		casting = false;
		++castId;
		for (const auto rune : active) {
			leaving.emplace_back(rune, kRemoveDelay);
		}
		active.clear();
	}

	void Update(float a_delta)
	{
		if (leaving.empty()) {
			return;
		}
		const auto player = Player();
		std::erase_if(leaving, [&](auto& a_entry) {
			a_entry.second -= a_delta;
			if (a_entry.second > 0.0f) {
				return false;
			}
			if (player) {
				player->RemoveSpell(a_entry.first);
			}
			return true;
		});
	}

	void Reset()
	{
		casting = false;
		++castId;
		active.clear();
		leaving.clear();
	}
}
