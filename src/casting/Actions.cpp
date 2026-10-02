#include "casting/Actions.h"

#include "casting/CastAnim.h"
#include "casting/PlayerControl.h"
#include "casting/VanillaCast.h"

namespace Actions
{
	namespace
	{
		using SpellType = RE::MagicSystem::SpellType;
		using CastingType = RE::MagicSystem::CastingType;
		using Delivery = RE::MagicSystem::Delivery;
		using Source = RE::MagicSystem::CastingSource;

		struct ActiveCast
		{
			RE::MagicItem*    item{ nullptr };
			RE::ScrollItem*   scroll{ nullptr };
			Page              page{ Page::kMain };
			int               slot{ -1 };
			std::uint32_t     key{ 0 };
			bool              keyHeld{ false };
			Source            source{ Source::kRightHand };
			bool              dual{ false };
			float             cost{ 0.0f };
			float             charge{ 0.0f };
			float             chargeTotal{ 0.0f };
			bool              channeling{ false };
			bool              animated{ false };  // casting animation through OAR (CastAnim)
			bool              releaseAnimPlayed{ false };
			float             animLoopTimer{ 0.0f };
			RE::BSSoundHandle chargeSound{};
			RE::BSSoundHandle loopSound{};
		};

		std::optional<ActiveCast> cast;
		float                     gcdRemaining{ 0.0f };
		float                     gcdTotal{ 0.0f };
		float                     potionRemaining{ 0.0f };
		float                     potionTotal{ 0.0f };

		// casting animation timings (same idea as Spell Hotbar 2)
		constexpr float kAnimMinCharge = 0.25f;     // shortest fire-and-forget cast with animation (vanilla charge time otherwise)
		constexpr float kAnimReleaseLead = 0.12f;   // release animation starts this long before the spell fires (hand thrust)
		constexpr float kAnimConcWindup = 0.1f;     // hand raise before a concentration spell starts (kept short: holding = casting)
		constexpr float kAnimLoopInterval = 0.5f;   // concentration animation is restarted this often
		constexpr float kMinRecovery = 0.25f;       // shortest time between two casts without animation
		constexpr float kQueueMaxAge = 3.0f;        // a press while busy waits at most this long for the previous cast + animation
		constexpr float kAirDebounce = 0.1f;        // midair this long counts as jumping / falling (slopes and stairs flicker)
		constexpr float kPotionCooldown = 0.5f;     // between two potions (food and poisons have none)
		constexpr float kTurnSpeed = 10.0f;         // radians per second the character turns to the camera while charging
		constexpr auto  kCostCacheTime = 250ms;     // how long the HUD's magicka costs are reused

		// a slot pressed while the previous cast was still charging / recovering
		struct Queued
		{
			Page          page{ Page::kMain };
			int           slot{ -1 };
			std::uint32_t key{ 0 };
			bool          keyHeld{ false };
			float         age{ 0.0f };
		};
		std::optional<Queued> queued;
		float                 airTime{ 0.0f };  // how long the player has been in the air

		RE::PlayerCharacter* Player()
		{
			return RE::PlayerCharacter::GetSingleton();
		}

		// No magic while jumping / falling, swimming (vanilla can't cast while swimming either) or attacking
		// (weapon swings incl. power attacks, bashes, drawing a bow)
		bool CanCastNow()
		{
			const auto player = Player();
			if (!player || airTime >= kAirDebounce) {
				return false;
			}
			const auto state = player->AsActorState();
			return !state->IsSwimming() && state->GetAttackState() == RE::ATTACK_STATE_ENUM::kNone;
		}

		void Queue(Page a_page, int a_slot, std::uint32_t a_key, bool a_keyHeld)
		{
			queued = Queued{ a_page, a_slot, a_key, a_keyHeld, 0.0f };
		}

		RE::BGSEquipSlot* EquipSlot(RE::DEFAULT_OBJECT a_id)
		{
			const auto manager = RE::BGSDefaultObjectManager::GetSingleton();
			return manager ? manager->GetObject<RE::BGSEquipSlot>(a_id) : nullptr;
		}

		RE::BGSEquipSlot* RightSlot() { return EquipSlot(RE::DEFAULT_OBJECT::kRightHandEquip); }
		RE::BGSEquipSlot* LeftSlot() { return EquipSlot(RE::DEFAULT_OBJECT::kLeftHandEquip); }
		RE::BGSEquipSlot* VoiceSlot() { return EquipSlot(RE::DEFAULT_OBJECT::kVoiceEquip); }

		float GameSetting(const char* a_name, float a_default)
		{
			const auto settings = RE::GameSettingCollection::GetSingleton();
			const auto setting = settings ? settings->GetSetting(a_name) : nullptr;
			return setting ? setting->GetFloat() : a_default;
		}

		void FailFeedback()
		{
			RE::PlaySound("MAGFailSD");
		}

		void NotEnoughMagicka()
		{
			RE::HUDMenu::FlashMeter(RE::ActorValue::kMagicka);
			RE::PlaySound("MAGFailSD");
		}

		RE::BGSSoundDescriptorForm* EffectSound(RE::MagicItem* a_item, RE::MagicSystem::SoundID a_id)
		{
			const auto effect = a_item ? a_item->GetCostliestEffectItem() : nullptr;
			const auto base = effect ? effect->baseEffect : nullptr;
			if (!base) {
				return nullptr;
			}
			for (const auto& sound : base->effectSounds) {
				if (sound.id == a_id) {
					return sound.sound;
				}
			}
			return nullptr;
		}

		void PlayEffectSound(RE::MagicItem* a_item, RE::MagicSystem::SoundID a_id, RE::BSSoundHandle* a_handle = nullptr)
		{
			const auto descriptor = EffectSound(a_item, a_id);
			const auto audio = RE::BSAudioManager::GetSingleton();
			const auto player = Player();
			if (!descriptor || !audio || !player) {
				return;
			}
			RE::BSSoundHandle local{};
			auto&             handle = a_handle ? *a_handle : local;
			if (audio->GetSoundHandle(handle, descriptor)) {
				if (const auto root = player->Get3D()) {
					handle.SetObjectToFollow(root);
				}
				handle.Play();
			}
		}

		void StopSound(RE::BSSoundHandle& a_handle)
		{
			if (a_handle.IsValid()) {
				a_handle.FadeOutAndRelease(100);
			}
		}

		bool KnowsSpell(RE::SpellItem* a_spell)
		{
			const auto player = Player();
			if (!player || !a_spell) {
				return false;
			}
			if (player->HasSpell(a_spell)) {
				return true;
			}
			// racial powers
			const auto race = player->GetRace();
			if (race && race->actorEffects && race->actorEffects->spells) {
				for (std::uint32_t i = 0; i < race->actorEffects->numSpells; ++i) {
					if (race->actorEffects->spells[i] == a_spell) {
						return true;
					}
				}
			}
			return false;
		}

		// The vanilla "<school> Dual Casting" perks
		RE::BGSPerk* DualCastPerk(RE::ActorValue a_school)
		{
			static const auto perks = [] {
				const auto lookup = [](RE::FormID a_id) { return RE::TESForm::LookupByID<RE::BGSPerk>(a_id); };
				return std::array{ lookup(0x000153CD), lookup(0x000153CE), lookup(0x000153CF), lookup(0x000153D0), lookup(0x000153D1) };
			}();
			switch (a_school) {
			case RE::ActorValue::kAlteration:
				return perks[0];
			case RE::ActorValue::kConjuration:
				return perks[1];
			case RE::ActorValue::kDestruction:
				return perks[2];
			case RE::ActorValue::kIllusion:
				return perks[3];
			case RE::ActorValue::kRestoration:
				return perks[4];
			default:
				return nullptr;
			}
		}

		bool HasDualCastPerk(RE::MagicItem* a_item)
		{
			const auto effect = a_item->GetCostliestEffectItem();
			const auto base = effect ? effect->baseEffect : nullptr;
			const auto perk = base ? DualCastPerk(base->GetMagickSkill()) : nullptr;
			return perk && Player()->HasPerk(perk);
		}

		bool WantsDualCast(RE::MagicItem* a_item, Hand a_hand)
		{
			if (a_hand != Hand::kBoth || !a_item || a_item->GetNoDualCastModifications()) {
				return false;
			}
			return HasDualCastPerk(a_item);
		}

		float SpellCost(RE::MagicItem* a_item, bool a_dual)
		{
			const auto player = Player();
			if (!player || !a_item || a_item->Is(RE::FormType::Scroll)) {
				return 0.0f;
			}
			auto cost = a_item->CalculateMagickaCost(player);
			if (a_dual) {
				cost *= GameSetting("fMagicDualCastingCostMult", 2.8f);
			}
			return cost;
		}

		// SpellCost for the HUD, which asks for every slot every frame: the perk entry points behind the cost are
		// evaluated at most every kCostCacheTime per spell
		float CachedSpellCost(RE::MagicItem* a_item, bool a_dual)
		{
			struct Cached
			{
				float                                 cost;
				std::chrono::steady_clock::time_point time;
			};
			static std::unordered_map<std::uint64_t, Cached> costs;

			const auto key = (static_cast<std::uint64_t>(a_item->GetFormID()) << 1) | (a_dual ? 1 : 0);
			const auto now = std::chrono::steady_clock::now();
			auto&      entry = costs[key];
			if (entry.time == std::chrono::steady_clock::time_point{} || now - entry.time > kCostCacheTime) {
				entry = { SpellCost(a_item, a_dual), now };
			}
			return entry.cost;
		}

		float Magicka()
		{
			const auto player = Player();
			return player ? player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kMagicka) : 0.0f;
		}

		Source SourceFor(Hand a_hand)
		{
			return a_hand == Hand::kLeft ? Source::kLeftHand : Source::kRightHand;
		}

		// The player's inventory changes entry of an object. Read in place: GetInventory() copies the whole
		// inventory, too slow for the HUD which asks every frame. Worn / enchanted / tempered items always have one.
		RE::InventoryEntryData* ChangesEntry(const RE::TESBoundObject* a_object)
		{
			const auto player = Player();
			const auto changes = player ? player->GetInventoryChanges() : nullptr;
			if (!changes || !changes->entryList) {
				return nullptr;
			}
			for (const auto entry : *changes->entryList) {
				if (entry && entry->object == a_object) {
					return entry;
				}
			}
			return nullptr;
		}

		RE::ExtraDataList* FirstExtraList(const RE::TESBoundObject* a_object)
		{
			const auto entry = ChangesEntry(a_object);
			if (entry && entry->extraLists) {
				for (const auto list : *entry->extraLists) {
					if (list) {
						return list;
					}
				}
			}
			return nullptr;
		}

		// Time before the bar casts again: the rest of the release animation incl. its blend out (the next cast only
		// starts once the hand is back)
		void StartGcd(bool a_animated)
		{
			gcdTotal = gcdRemaining = a_animated ? std::max(kMinRecovery, CastAnim::ReleaseRemaining()) : kMinRecovery;
		}

		void EndCast()
		{
			if (!cast) {
				return;
			}
			StopSound(cast->chargeSound);
			StopSound(cast->loopSound);
			CastAnim::StopHandArt();
			cast.reset();
		}

		void StopChannel()
		{
			if (!cast || !cast->channeling) {
				EndCast();
				return;
			}
			if (const auto player = Player()) {
				if (const auto caster = player->GetMagicCaster(cast->source)) {
					caster->InterruptCast(false);
					if (cast->dual) {
						caster->SetDualCasting(false);
					}
				}
				if (player->IsCasting(cast->item)) {
					player->InterruptCast(false);  // some concentration casts ignore the caster interrupt
				}
			}
			const bool animated = cast->animated;
			if (animated) {
				CastAnim::Stop();
			}
			EndCast();
			StartGcd(animated);
		}

		// Fires the spell. Returns false if it failed (no magicka, bad target location).
		bool Release()
		{
			const auto player = Player();
			if (!player || !cast) {
				return false;
			}
			StopSound(cast->chargeSound);

			const auto item = cast->item;
			const bool concentration = item->GetCastingType() == CastingType::kConcentration;

			if (!cast->scroll) {
				const auto magicka = Magicka();
				if (concentration ? magicka <= 0.0f : magicka < cast->cost) {
					NotEnoughMagicka();
					return false;
				}
			}

			const auto caster = player->GetMagicCaster(cast->source);
			if (!caster) {
				return false;
			}

			const bool targetSelf = item->GetDelivery() == Delivery::kSelf;
			const bool crosshair = PlayerControl::AimsAtCrosshair(item);
			RE::Actor* target = targetSelf ? player :
			                    crosshair  ? nullptr :
			                                 player->GetActorRuntimeData().currentCombatTarget.get().get();

			if (cast->dual) {
				caster->SetDualCasting(true);
			}
			if (concentration) {
				caster->currentSpellCost = cast->cost;  // magicka per second, drained by the caster
			} else if (!cast->scroll) {
				player->AsActorValueOwner()->DamageActorValue(RE::ActorValue::kMagicka, cast->cost);
			}

			if (crosshair) {
				PlayerControl::FaceCamera(-1.0f);
				const PlayerControl::ScopedCrosshairAim aim(caster);
				caster->CastSpellImmediate(item, false, target, 1.0f, false, 0.0f, player);
			} else {
				caster->CastSpellImmediate(item, false, target, 1.0f, false, 0.0f, targetSelf ? nullptr : player);
			}

			if (cast->dual && !concentration) {
				caster->SetDualCasting(false);
			}

			// Target location spells (runes, summons) that found no valid spot keep "casting" forever
			if (item->GetDelivery() == Delivery::kTargetLocation && !concentration && player->IsCasting(item)) {
				player->InterruptCast(false);
				if (!cast->scroll) {
					player->AsActorValueOwner()->RestoreActorValue(RE::ActorValue::kMagicka, cast->cost);
				}
				FailFeedback();
				return false;
			}

			if (cast->scroll) {
				player->RemoveItem(cast->scroll, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			}

			PlayEffectSound(item, RE::MagicSystem::SoundID::kRelease);
			if (concentration) {
				cast->channeling = true;
				PlayEffectSound(item, RE::MagicSystem::SoundID::kCastLoop, &cast->loopSound);
			} else {
				CastAnim::StopHandArt();
			}
			return true;
		}

		void BeginInstantCast(RE::MagicItem* a_item, RE::ScrollItem* a_scroll, Hand a_hand, Page a_page, int a_slot, std::uint32_t a_key, bool a_keyHeld)
		{
			// still recovering, or the previous cast's release animation hasn't finished: cast right after it
			if (gcdRemaining > 0.0f || CastAnim::ReleaseRemaining() > 0.0f) {
				Queue(a_page, a_slot, a_key, a_keyHeld);
				return;
			}

			ActiveCast next{};
			next.item = a_item;
			next.scroll = a_scroll;
			next.page = a_page;
			next.slot = a_slot;
			next.key = a_key;
			next.keyHeld = a_keyHeld;
			next.source = SourceFor(a_hand);
			next.dual = !a_scroll && WantsDualCast(a_item, a_hand);
			next.cost = SpellCost(a_item, next.dual);

			const bool concentration = a_item->GetCastingType() == CastingType::kConcentration;
			if (concentration && !a_keyHeld) {
				return;  // concentration spells only channel while the key is held
			}
			if (!a_scroll && (concentration ? Magicka() <= 0.0f : Magicka() < next.cost)) {
				NotEnoughMagicka();
				return;
			}

			PlayerControl::StopSprinting();

			// with animation whenever Open Animation Replacer is there; spells always take their real charge time
			next.animated = CastAnim::Available();
			next.chargeTotal = concentration ? 0.0f : std::max(0.0f, a_item->GetChargeTime());
			// which hand(s) the animation and hand glow use
			const auto side = next.dual || a_item->IsTwoHanded()    ? CastAnim::Side::kBoth :
			                  next.source == Source::kLeftHand ? CastAnim::Side::kLeft :
			                                                     CastAnim::Side::kRight;
			if (next.animated) {
				next.chargeTotal = concentration ? kAnimConcWindup : std::max(next.chargeTotal, kAnimMinCharge);
				switch (CastAnim::Start(CastAnim::Choose(a_item, next.dual), side)) {
				case CastAnim::StartResult::kBusy:
					// the previous release animation is still playing: wait for it, don't cast over it
					Queue(a_page, a_slot, a_key, a_keyHeld);
					return;
				case CastAnim::StartResult::kRefused:
					logs::info("Casting animation refused for {}, casting without it", a_item->GetName());
					next.animated = false;
					break;
				default:
					break;
				}
			}
			if (next.animated) {
				CastAnim::StartHandArt(a_item, side, concentration ? 120.0f : next.chargeTotal + 0.5f);
			}
			cast = std::move(next);

			if (cast->chargeTotal > 0.0f) {
				PlayEffectSound(a_item, RE::MagicSystem::SoundID::kCharge, &cast->chargeSound);
				return;
			}

			const bool animated = cast->animated;
			const bool ok = Release();
			if (!ok || !cast->channeling) {
				EndCast();
				if (ok) {
					StartGcd(animated);
				}
			}
		}

		void UseSpell(RE::SpellItem* a_spell, const SlotBinding& a_binding, Page a_page, int a_slot, std::uint32_t a_key, bool a_keyHeld)
		{
			if (!KnowsSpell(a_spell)) {
				FailFeedback();
				return;
			}

			switch (a_spell->GetSpellType()) {
			case SpellType::kPower:
			case SpellType::kLesserPower:
			case SpellType::kVoicePower:
				VanillaCast::StartShout(a_spell, a_page, a_slot, a_key, a_keyHeld);
				return;
			case SpellType::kSpell:
				break;
			default:
				return;
			}

			if (cast) {
				// pressing the slot of the running concentration spell again stops it
				if (cast->channeling && cast->page == a_page && cast->slot == a_slot) {
					StopChannel();
				} else if (!cast->channeling) {
					Queue(a_page, a_slot, a_key, a_keyHeld);
				}
				return;
			}
			BeginInstantCast(a_spell, nullptr, a_binding.hand, a_page, a_slot, a_key, a_keyHeld);
		}

		void UseScroll(RE::ScrollItem* a_scroll, const SlotBinding& a_binding, Page a_page, int a_slot, std::uint32_t a_key, bool a_keyHeld)
		{
			if (ItemCount(a_scroll) <= 0) {
				FailFeedback();
				return;
			}
			if (!cast) {
				BeginInstantCast(a_scroll, a_scroll, a_binding.hand, a_page, a_slot, a_key, a_keyHeld);
			} else if (!cast->channeling) {
				Queue(a_page, a_slot, a_key, a_keyHeld);
			}
		}

		void UseShout(RE::TESShout* a_shout, Page a_page, int a_slot, std::uint32_t a_key, bool a_keyHeld)
		{
			if (!Player()->HasShout(a_shout)) {
				FailFeedback();
				return;
			}
			VanillaCast::StartShout(a_shout, a_page, a_slot, a_key, a_keyHeld);
		}

		void UseAlchemy(RE::AlchemyItem* a_item)
		{
			const auto player = Player();
			if (ItemCount(a_item) <= 0) {
				FailFeedback();
				return;
			}
			const bool potion = !a_item->IsFood() && !a_item->IsPoison();
			if (potion && potionRemaining > 0.0f) {
				return;
			}
			RE::ActorEquipManager::GetSingleton()->EquipObject(player, a_item, FirstExtraList(a_item));
			if (potion) {
				potionTotal = potionRemaining = kPotionCooldown;
			}
		}

		// Equip mode: a spell / scroll goes into the slot's hand (Auto = right, Both = both hands), a power / shout into the
		// voice slot. Returns false for forms this doesn't handle (potions, weapons, ... work as always).
		bool EquipMagic(RE::TESForm* a_form, Hand a_hand)
		{
			const auto player = Player();
			const auto manager = RE::ActorEquipManager::GetSingleton();

			if (const auto spell = a_form->As<RE::SpellItem>()) {
				if (!KnowsSpell(spell)) {
					FailFeedback();
				} else if (spell->GetSpellType() != SpellType::kSpell) {
					manager->EquipSpell(player, spell, VoiceSlot());
				} else if (spell->IsTwoHanded()) {
					manager->EquipSpell(player, spell, spell->GetEquipSlot());
				} else {
					if (a_hand != Hand::kLeft) {
						manager->EquipSpell(player, spell, RightSlot());
					}
					if (a_hand == Hand::kLeft || a_hand == Hand::kBoth) {
						manager->EquipSpell(player, spell, LeftSlot());
					}
				}
				return true;
			}
			if (const auto scroll = a_form->As<RE::ScrollItem>()) {
				if (ItemCount(scroll) <= 0) {
					FailFeedback();
				} else {
					manager->EquipObject(player, scroll, nullptr, 1, a_hand == Hand::kLeft ? LeftSlot() : RightSlot());
				}
				return true;
			}
			if (const auto shout = a_form->As<RE::TESShout>()) {
				if (player->HasShout(shout)) {
					manager->EquipShout(player, shout);
				} else {
					FailFeedback();
				}
				return true;
			}
			return false;
		}

		// Oblivion style: a spell / scroll becomes the cast key's spell, a potion the potion key's; powers and shouts are
		// equipped. Returns false for forms this doesn't handle.
		bool Pick(RE::TESForm* a_form, const SlotBinding& a_binding)
		{
			const auto spell = a_form->As<RE::SpellItem>();
			if ((spell && spell->GetSpellType() == SpellType::kSpell) || a_form->Is(RE::FormType::Scroll)) {
				if (!IsAvailable(a_form)) {
					FailFeedback();
				} else {
					Bindings::Set(Page::kMain, kReadySpellSlot, a_form->GetFormID(), a_binding.hand);
				}
				return true;
			}
			if (a_form->Is(RE::FormType::AlchemyItem)) {
				if (ItemCount(a_form) <= 0) {
					FailFeedback();
				} else {
					Bindings::Set(Page::kMain, kReadyPotionSlot, a_form->GetFormID());
				}
				return true;
			}
			if (spell || a_form->Is(RE::FormType::Shout)) {
				return EquipMagic(a_form, a_binding.hand);
			}
			return false;
		}

		void UseEquipment(RE::TESBoundObject* a_object, Hand a_hand)
		{
			const auto player = Player();
			const auto manager = RE::ActorEquipManager::GetSingleton();
			if (ItemCount(a_object) <= 0) {
				FailFeedback();
				return;
			}

			const RE::BGSEquipSlot* slot = nullptr;
			if (a_object->Is(RE::FormType::Weapon)) {
				slot = a_hand == Hand::kLeft ? LeftSlot() : RightSlot();
			}

			if (IsEquipped(a_object)) {
				manager->UnequipObject(player, a_object, nullptr, 1, slot);
			} else {
				manager->EquipObject(player, a_object, FirstExtraList(a_object), 1, slot);
			}
		}
	}

	void Use(Page a_page, int a_slot, bool a_keyHeld, std::uint32_t a_key)
	{
		const auto player = Player();
		const auto form = Bindings::GetForm(a_page, a_slot);
		if (!player || !form || player->IsDead()) {
			return;
		}
		const auto binding = Bindings::Get(a_page, a_slot);

		// slot keys in Equip / Oblivion style mode equip or pick magic instead of casting it (the cast key and potion
		// key of Oblivion style use their pseudo slots normally)
		if (!IsReadySlot(a_slot)) {
			switch (Config::Get().keyMode) {
			case KeyMode::kEquip:
				if (EquipMagic(form, binding.hand)) {
					return;
				}
				break;
			case KeyMode::kOblivion:
				if (Pick(form, binding)) {
					return;
				}
				break;
			default:
				break;
			}
		}

		const bool magic = form->Is(RE::FormType::Spell) || form->Is(RE::FormType::Scroll) || form->Is(RE::FormType::Shout);
		if (magic && !CanCastNow()) {
			return;
		}

		switch (form->GetFormType()) {
		case RE::FormType::Spell:
			UseSpell(form->As<RE::SpellItem>(), binding, a_page, a_slot, a_key, a_keyHeld);
			break;
		case RE::FormType::Scroll:
			UseScroll(form->As<RE::ScrollItem>(), binding, a_page, a_slot, a_key, a_keyHeld);
			break;
		case RE::FormType::Shout:
			UseShout(form->As<RE::TESShout>(), a_page, a_slot, a_key, a_keyHeld);
			break;
		case RE::FormType::AlchemyItem:
			UseAlchemy(form->As<RE::AlchemyItem>());
			break;
		case RE::FormType::Weapon:
		case RE::FormType::Armor:
		case RE::FormType::Ammo:
		case RE::FormType::Light:
			UseEquipment(form->As<RE::TESBoundObject>(), binding.hand);
			break;
		default:
			break;
		}
	}

	void OnKeyUp(std::uint32_t a_key)
	{
		VanillaCast::OnKeyUp(a_key);
		if (queued && queued->key == a_key) {
			queued->keyHeld = false;
		}
		if (cast && cast->key == a_key) {
			cast->keyHeld = false;
		}
	}

	void Update(float a_delta)
	{
		gcdRemaining = std::max(0.0f, gcdRemaining - a_delta);
		potionRemaining = std::max(0.0f, potionRemaining - a_delta);
		if (const auto player = Player(); player && player->IsInMidair()) {
			airTime += a_delta;
		} else {
			airTime = 0.0f;
		}
		VanillaCast::Update(a_delta);
		CastAnim::Update(a_delta);

		// A press while the bar is busy (charging, recovery, release animation playing / blending out) is used as
		// soon as it's free. Use() queues it again while the animation still blocks.
		if (queued) {
			queued->age += a_delta;
			if (queued->age > kQueueMaxAge) {
				queued.reset();
			} else if (!cast && gcdRemaining <= 0.0f) {
				const auto next = *queued;
				queued.reset();
				Use(next.page, next.slot, next.keyHeld, next.key);
				if (queued && queued->page == next.page && queued->slot == next.slot) {
					queued->age = next.age;  // queued again (animation busy): keep the original press time
				}
			}
		}

		if (!cast) {
			return;
		}
		const auto player = Player();
		if (!player || player->IsDead()) {
			EndCast();
			return;
		}

		// no sprinting while charging / channeling, like vanilla casting; third person: turn toward the crosshair
		// while charging, keep facing it while channeling
		PlayerControl::StopSprinting();
		if (PlayerControl::AimsAtCrosshair(cast->item)) {
			PlayerControl::FaceCamera(kTurnSpeed * a_delta);
		}

		// jumped / fell / went into deep water / started an attack: a charging cast fizzles (nothing spent yet), a channel stops
		if (!CanCastNow()) {
			if (cast->channeling) {
				StopChannel();
			} else {
				if (cast->animated) {
					CastAnim::Stop();
				}
				EndCast();
			}
			return;
		}

		if (!cast->channeling) {
			cast->charge += a_delta;
			const bool concentration = cast->item->GetCastingType() == CastingType::kConcentration;
			if (concentration && !cast->keyHeld) {
				// let go during the wind-up: nothing is cast
				if (cast->animated) {
					CastAnim::Stop();
				}
				EndCast();
				return;
			}
			if (cast->animated && !concentration && !cast->releaseAnimPlayed && cast->chargeTotal - cast->charge <= kAnimReleaseLead) {
				cast->releaseAnimPlayed = true;
				CastAnim::Release();
			}
			if (cast->charge >= cast->chargeTotal) {
				const bool animated = cast->animated;
				const bool ok = Release();
				if (animated && !ok) {
					CastAnim::Stop();
				}
				if (!ok || !cast->channeling) {
					EndCast();
					if (ok) {
						StartGcd(animated);
					}
				}
			}
			return;
		}

		if (cast->animated) {
			cast->animLoopTimer += a_delta;
			if (cast->animLoopTimer >= kAnimLoopInterval) {
				cast->animLoopTimer = 0.0f;
				CastAnim::Restart();
			}
		}

		if (!cast->keyHeld) {
			StopChannel();
		} else if (Magicka() <= 0.0f) {
			NotEnoughMagicka();
			StopChannel();
		}
	}

	void Reset()
	{
		VanillaCast::Reset();
		CastAnim::Reset();
		queued.reset();
		cast.reset();
		gcdRemaining = gcdTotal = 0.0f;
		potionRemaining = potionTotal = 0.0f;
	}

	std::optional<CastInfo> CurrentCast()
	{
		if (!cast) {
			if (const auto vanilla = VanillaCast::Current()) {
				return CastInfo{ vanilla->page, vanilla->slot, vanilla->progress, vanilla->channeling };
			}
			return std::nullopt;
		}
		CastInfo info;
		info.page = cast->page;
		info.slot = cast->slot;
		info.channeling = cast->channeling;
		info.progress = cast->channeling || cast->chargeTotal <= 0.0f ? 1.0f : std::clamp(cast->charge / cast->chargeTotal, 0.0f, 1.0f);
		return info;
	}

	std::pair<float, float> Cooldown(RE::TESForm* a_form)
	{
		if (!a_form) {
			return { 0.0f, 0.0f };
		}
		const auto player = Player();
		switch (a_form->GetFormType()) {
		case RE::FormType::Shout:
			{
				const float recovery = player ? player->GetVoiceRecoveryTime() : 0.0f;
				return { recovery, std::max(recovery, 1.0f) };
			}
		case RE::FormType::AlchemyItem:
			{
				const auto alch = a_form->As<RE::AlchemyItem>();
				if (!alch->IsFood() && !alch->IsPoison()) {
					return { potionRemaining, potionTotal };
				}
				return { 0.0f, 0.0f };
			}
		case RE::FormType::Spell:
			{
				const auto type = a_form->As<RE::SpellItem>()->GetSpellType();
				if (type == SpellType::kVoicePower) {
					const float recovery = player ? player->GetVoiceRecoveryTime() : 0.0f;
					return { recovery, std::max(recovery, 1.0f) };
				}
				if (type == SpellType::kSpell) {
					return { gcdRemaining, gcdTotal };
				}
				return { 0.0f, 0.0f };
			}
		case RE::FormType::Scroll:
			return { gcdRemaining, gcdTotal };
		default:
			return { 0.0f, 0.0f };
		}
	}

	bool CanAfford(RE::TESForm* a_form, Hand a_hand)
	{
		if (!a_form || !a_form->Is(RE::FormType::Spell)) {
			return true;
		}
		const auto spell = a_form->As<RE::SpellItem>();
		if (spell->GetSpellType() != SpellType::kSpell) {
			return true;
		}
		if (spell->GetCastingType() == CastingType::kConcentration) {
			return Magicka() > 0.0f;
		}
		return Magicka() >= CachedSpellCost(spell, WantsDualCast(spell, a_hand));
	}

	bool IsAvailable(RE::TESForm* a_form)
	{
		if (!a_form) {
			return false;
		}
		switch (a_form->GetFormType()) {
		case RE::FormType::Spell:
			return KnowsSpell(a_form->As<RE::SpellItem>());
		case RE::FormType::Shout:
			return Player()->HasShout(a_form->As<RE::TESShout>());
		default:
			return ItemCount(a_form) != 0;
		}
	}

	int ItemCount(RE::TESForm* a_form)
	{
		const auto player = Player();
		const auto object = a_form ? a_form->As<RE::TESBoundObject>() : nullptr;
		if (!player || !object || a_form->Is(RE::FormType::Spell) || a_form->Is(RE::FormType::Shout)) {
			return -1;
		}
		const auto changes = player->GetInventoryChanges();
		return changes ? std::max<int>(0, changes->GetItemCount(object)) : 0;
	}

	bool IsEquipped(RE::TESForm* a_form)
	{
		const auto player = Player();
		if (!player || !a_form) {
			return false;
		}
		auto& runtime = player->GetActorRuntimeData();
		if (a_form->Is(RE::FormType::Spell) || a_form->Is(RE::FormType::Shout)) {
			if (runtime.selectedPower == a_form) {
				return true;
			}
			for (const auto spell : runtime.selectedSpells) {
				if (spell == a_form) {
					return true;
				}
			}
			return false;
		}
		if (player->GetEquippedObject(false) == a_form || player->GetEquippedObject(true) == a_form) {
			return true;
		}
		if (a_form->Is(RE::FormType::Armor) || a_form->Is(RE::FormType::Ammo) || a_form->Is(RE::FormType::Light)) {
			const auto entry = ChangesEntry(a_form->As<RE::TESBoundObject>());
			return entry && entry->IsWorn();
		}
		return false;
	}
}
