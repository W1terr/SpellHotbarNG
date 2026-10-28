#include "casting/BeastCast.h"

#include "casting/PlayerControl.h"
#include "core/Util.h"

namespace BeastCast
{
	namespace
	{
		using CasterState = RE::MagicCaster::State;
		using Source = RE::MagicSystem::CastingSource;

		enum class Phase
		{
			kPrepare,   // vampire lord rising into the air / lich raising its hands
			kSettle,    // after rising: the transformation script equips its own hand spells, let it finish first
			kEquip,     // the bar's spell went into the hand(s): give the equip a moment
			kPress,     // attack button(s) held for the player
			kFinish     // released: the release animation plays, the casters go back to idle
		};

		struct Job
		{
			RE::SpellItem* spell{ nullptr };
			Page           page{ Page::kMain };
			int            slot{ -1 };
			std::uint32_t  key{ 0 };
			bool           keyHeld{ false };
			bool           right{ false };  // attack buttons pressed for these hands
			bool           left{ false };
			bool           concentration{ false };
			Phase          phase{ Phase::kPrepare };
			float          phaseTime{ 0.0f };
			float          held{ 0.0f };
			bool           raised{ false };   // the bar started the rise / hand raise
			bool           started{ false };  // a caster took the spell
		};

		std::optional<Job> job;

		// The hands before the bar put its spell in; they get it back once the bar has been idle a moment (a quick next
		// cast keeps them)
		struct Saved
		{
			bool         valid{ false };
			RE::TESForm* right{ nullptr };
			RE::TESForm* left{ nullptr };
			std::vector<RE::SpellItem*> placed;  // spells the bar put into a hand that didn't hold them
		};
		Saved saved;
		float idleTime{ 0.0f };

		RE::ButtonEvent* rightEvent{ nullptr };  // "Right Attack/Block" = right hand (left mouse button)
		RE::ButtonEvent* leftEvent{ nullptr };

		constexpr float kPrepareTimeout = 3.0f;
		constexpr float kLevitateDrawWait = 1.5f;  // a risen vampire lord casts even if the draw state lags behind
		constexpr float kSettleTime = 0.5f;
		constexpr float kEquipDelay = 0.2f;
		constexpr float kNoStartTimeout = 0.6f;   // nothing charged: the game refused (magicka, ...)
		constexpr float kChargeTimeout = 2.0f;    // beyond the expected charge time
		constexpr float kMinHold = 0.1f;
		constexpr float kFinishMin = 0.2f;
		constexpr float kFinishTimeout = 3.0f;
		constexpr float kRestoreDelay = 0.3f;

		RE::PlayerCharacter* Player() { return RE::PlayerCharacter::GetSingleton(); }

		RE::BGSEquipSlot* EquipSlot(RE::DEFAULT_OBJECT a_id)
		{
			const auto manager = RE::BGSDefaultObjectManager::GetSingleton();
			return manager ? manager->GetObject<RE::BGSEquipSlot>(a_id) : nullptr;
		}

		RE::BGSEquipSlot* HandSlot(bool a_left)
		{
			return EquipSlot(a_left ? RE::DEFAULT_OBJECT::kLeftHandEquip : RE::DEFAULT_OBJECT::kRightHandEquip);
		}

		bool Levitating()
		{
			bool levitating = false;
			Player()->GetGraphVariableBool("isLevitating"sv, levitating);
			return levitating;
		}

		bool HandsDrawn()
		{
			return Player()->AsActorState()->GetWeaponState() == RE::WEAPON_STATE::kDrawn;
		}

		// Can cast with the hands now: a vampire lord only casts while levitating, a lich with its hands raised
		bool Ready(Util::CastingForm a_form, float a_waited)
		{
			if (a_form == Util::CastingForm::kVampireLord) {
				return Levitating() && (HandsDrawn() || a_waited >= kLevitateDrawWait);
			}
			return HandsDrawn();
		}

		void Raise(Util::CastingForm a_form)
		{
			const auto player = Player();
			if (a_form == Util::CastingForm::kVampireLord) {
				// the vampire lord's Sneak key: its behavior plays "LevitationToggle" for the Sneak action
				PlayerControl::PerformAction(RE::DEFAULT_OBJECT::kActionSneak);
			} else {
				// what Undeath's own hands key does
				player->DrawWeaponMagicHands(true);
				player->NotifyAnimationGraph("Magic_Equip"sv);
			}
		}

		void CreateEvents()
		{
			const auto userEvents = RE::UserEvents::GetSingleton();
			if (!rightEvent) {
				rightEvent = RE::ButtonEvent::Create(RE::INPUT_DEVICE::kMouse, userEvents->rightAttack, 0, 0.0f, 0.0f);
			}
			if (!leftEvent) {
				leftEvent = RE::ButtonEvent::Create(RE::INPUT_DEVICE::kMouse, userEvents->leftAttack, 1, 0.0f, 0.0f);
			}
		}

		// Feeds an attack button state to the attack handler like PlayerControls does for real input
		void SendAttack(bool a_left, float a_value, float a_held)
		{
			const auto controls = RE::PlayerControls::GetSingleton();
			const auto handler = controls ? controls->attackBlockHandler : nullptr;
			const auto event = a_left ? leftEvent : rightEvent;
			if (!handler || !event) {
				return;
			}
			event->GetRuntimeData().value = a_value;
			event->GetRuntimeData().heldDownSecs = a_held;
			if (handler->IsInputEventHandlingEnabled() && handler->CanProcess(event)) {
				handler->ProcessButton(event, std::addressof(controls->data));
			}
			handler->UpdateHeldStateActive(event);
		}

		void SendAttacks(float a_value, float a_held)
		{
			if (job->right) {
				SendAttack(false, a_value, a_held);
			}
			if (job->left) {
				SendAttack(true, a_value, a_held);
			}
		}

		std::vector<RE::MagicCaster*> Casters()
		{
			std::vector<RE::MagicCaster*> out;
			const auto                    player = Player();
			for (const auto [used, source] : { std::pair{ job->right, Source::kRightHand }, std::pair{ job->left, Source::kLeftHand } }) {
				if (const auto caster = used ? player->GetMagicCaster(source) : nullptr) {
					out.push_back(caster);
				}
			}
			return out;
		}

		bool AnyCasterBusy()
		{
			return std::ranges::any_of(Casters(), [](const RE::MagicCaster* a_caster) { return a_caster->state.get() != CasterState::kNone; });
		}

		bool AllCastersCharged()
		{
			const auto casters = Casters();
			return !casters.empty() &&
			       std::ranges::all_of(casters, [](const RE::MagicCaster* a_caster) { return a_caster->state.get() == CasterState::kReady; });
		}

		void SetPhase(Phase a_phase)
		{
			job->phase = a_phase;
			job->phaseTime = 0.0f;
		}

		// a_slot: a hand, or the both-hands slot of a two-handed spell
		void EquipToHand(RE::SpellItem* a_spell, bool a_left, const RE::BGSEquipSlot* a_slot)
		{
			const auto player = Player();
			if (player->GetEquippedObject(a_left) == a_spell) {
				return;
			}
			RE::ActorEquipManager::GetSingleton()->EquipSpell(player, a_spell, a_slot);
			if (std::ranges::find(saved.placed, a_spell) == saved.placed.end()) {
				saved.placed.push_back(a_spell);
			}
		}

		void Equip()
		{
			const auto player = Player();
			if (!saved.valid) {
				saved = { true, player->GetEquippedObject(false), player->GetEquippedObject(true), {} };
			}
			const auto spell = job->spell;
			if (spell->IsTwoHanded()) {
				EquipToHand(spell, false, spell->GetEquipSlot());
				return;
			}
			if (job->right) {
				EquipToHand(spell, false, HandSlot(false));
			}
			if (job->left) {
				EquipToHand(spell, true, HandSlot(true));
			}
		}

		// The hands get what they held before the bar's casts. A vampire lord that landed meanwhile has empty hands
		// (its script takes them away), so only the bar's spells are removed then.
		void Restore()
		{
			const auto player = Player();
			const auto form = Util::CurrentCastingForm();
			if (player && form != Util::CastingForm::kNone) {
				for (const auto spell : saved.placed) {
					if (player->GetEquippedObject(false) == spell || player->GetEquippedObject(true) == spell) {
						player->DeselectSpell(spell);
					}
				}
				const bool handsBack = form != Util::CastingForm::kVampireLord || Levitating();
				const auto manager = RE::ActorEquipManager::GetSingleton();
				for (const auto [left, before] : { std::pair{ false, saved.right }, std::pair{ true, saved.left } }) {
					if (!handsBack || !before || player->GetEquippedObject(left) == before) {
						continue;
					}
					if (const auto spell = before->As<RE::SpellItem>()) {
						manager->EquipSpell(player, spell, spell->IsTwoHanded() ? spell->GetEquipSlot() : HandSlot(left));
					} else if (const auto weapon = before->As<RE::TESObjectWEAP>()) {
						manager->EquipObject(player, weapon, nullptr, 1, HandSlot(left));  // a lich's staff
					}
				}
			}
			saved = {};
		}

		void Release()
		{
			SendAttacks(0.0f, job->held);
			SetPhase(Phase::kFinish);
		}
	}

	StartResult Start(RE::SpellItem* a_spell, const SlotBinding& a_binding, Page a_page, int a_slot, std::uint32_t a_key, bool a_keyHeld)
	{
		const auto player = Player();
		if (!player || !a_spell || Util::CurrentCastingForm() == Util::CastingForm::kNone) {
			return StartResult::kFailed;
		}
		if (job) {
			return StartResult::kBusy;
		}
		CreateEvents();

		Job next;
		next.spell = a_spell;
		next.page = a_page;
		next.slot = a_slot;
		next.key = a_key;
		next.keyHeld = a_keyHeld;
		next.concentration = a_spell->GetCastingType() == RE::MagicSystem::CastingType::kConcentration;
		// two-handed spells are cast with the right hand's button
		next.right = a_spell->IsTwoHanded() || a_binding.hand != Hand::kLeft;
		next.left = !a_spell->IsTwoHanded() && (a_binding.hand == Hand::kLeft || a_binding.hand == Hand::kBoth);
		job = next;
		idleTime = 0.0f;
		logs::info("Beast form cast: {} ({} hand)", a_spell->GetName(), next.right && next.left ? "both" : next.left ? "left" : "right");
		return StartResult::kStarted;
	}

	void OnKeyUp(std::uint32_t a_key)
	{
		if (job && job->key == a_key) {
			job->keyHeld = false;
		}
	}

	void Update(float a_delta)
	{
		const auto player = Player();
		const auto form = Util::CurrentCastingForm();
		if (!player || player->IsDead() || form == Util::CastingForm::kNone) {
			// changed back (the transformation gives the hands back itself) or died
			if (job && job->phase == Phase::kPress) {
				SendAttacks(0.0f, job->held);
			}
			job.reset();
			saved = {};
			return;
		}

		if (!job) {
			if (saved.valid) {
				idleTime += a_delta;
				if (idleTime >= kRestoreDelay) {
					Restore();
				}
			}
			return;
		}

		job->phaseTime += a_delta;
		switch (job->phase) {
		case Phase::kPrepare:
			if (Ready(form, job->phaseTime)) {
				SetPhase(job->raised ? Phase::kSettle : Phase::kEquip);
				if (!job->raised) {
					Equip();
				}
			} else if (!job->raised) {
				job->raised = true;
				Raise(form);
			} else if (job->phaseTime > kPrepareTimeout) {
				logs::info("Beast form cast of {}: the {} didn't get ready to cast", job->spell->GetName(),
					form == Util::CastingForm::kVampireLord ? "vampire lord" : "lich");
				job.reset();
			}
			break;
		case Phase::kSettle:
			if (job->phaseTime >= kSettleTime) {
				SetPhase(Phase::kEquip);
				Equip();
			}
			break;
		case Phase::kEquip:
			if (job->phaseTime >= kEquipDelay) {
				SetPhase(Phase::kPress);
				job->held = 0.0f;
				SendAttacks(1.0f, 0.0f);
			}
			break;
		case Phase::kPress:
			{
				job->held += a_delta;
				if (!job->started && AnyCasterBusy()) {
					job->started = true;
				}
				bool release = false;
				if (!job->started) {
					if (job->held >= kNoStartTimeout) {
						logs::info("Beast form cast of {}: the game didn't cast it", job->spell->GetName());
						release = true;
					}
				} else if (job->concentration) {
					release = !job->keyHeld && job->held >= kMinHold;
				} else {
					// fully charged (or the game never got there)
					const float charge = std::max(0.0f, job->spell->GetChargeTime());
					release = AllCastersCharged() || job->held >= charge + kChargeTimeout;
				}
				if (release) {
					Release();
				} else {
					SendAttacks(1.0f, job->held);
				}
			}
			break;
		case Phase::kFinish:
			if ((job->phaseTime >= kFinishMin && !AnyCasterBusy()) || job->phaseTime >= kFinishTimeout) {
				job.reset();
				idleTime = 0.0f;
			}
			break;
		}
	}

	void Reset()
	{
		job.reset();
		saved = {};
		idleTime = 0.0f;
	}

	std::optional<Info> Current()
	{
		if (!job) {
			return std::nullopt;
		}
		Info info;
		info.page = job->page;
		info.slot = job->slot;
		info.channeling = job->concentration && job->phase == Phase::kPress && job->started;
		if (job->phase == Phase::kFinish || info.channeling) {
			info.progress = 1.0f;
		} else if (job->phase == Phase::kPress) {
			const float total = std::max(job->spell->GetChargeTime(), 0.01f);
			info.progress = std::clamp(job->held / total, 0.0f, 1.0f);
		}
		return info;
	}
}
