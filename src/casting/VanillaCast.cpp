#include "casting/VanillaCast.h"

namespace VanillaCast
{
	namespace
	{
		enum class Phase
		{
			kWaitEquip,    // give the equip a few frames before pressing
			kHolding,      // Shout button held
			kWaitFinish,   // released, shout animation playing
			kRestoreDelay  // short pause before the previous power comes back
		};

		struct Shout
		{
			RE::TESForm*  form{ nullptr };
			Page          page{ Page::kMain };
			int           slot{ -1 };
			std::uint32_t key{ 0 };
			bool          keyHeld{ false };
			Phase         phase{ Phase::kWaitEquip };
			float         phaseTime{ 0.0f };
			float         heldTime{ 0.0f };
			bool          sawShouting{ false };  // the voice caster / shout animation got busy after the release
			bool          timerJumped{ false };  // the shout recovery timer was set: the shout went off
			float         lastTimer{ 0.0f };
			float         idleTime{ 0.0f };      // since the voice went quiet again
		};

		std::optional<Shout> shout;
		bool                 savedValid{ false };
		RE::TESForm*         savedPower{ nullptr };
		RE::ButtonEvent*     shoutEvent{ nullptr };

		constexpr float kEquipDelay = 0.1f;
		constexpr float kMinHold = 0.1f;
		constexpr float kFinishMin = 0.3f;
		constexpr float kQuietTime = 0.2f;      // the voice stays quiet this long before the shout counts as over
		constexpr float kNoShoutTimeout = 1.0f;  // nothing happened after the release: the game refused it (cooldown)
		constexpr float kTimerWait = 1.0f;      // after a shout, how long to wait for the recovery timer
		constexpr float kFinishTimeout = 5.0f;
		constexpr float kRestoreDelay = 0.3f;

		RE::PlayerCharacter* Player() { return RE::PlayerCharacter::GetSingleton(); }

		RE::BGSEquipSlot* VoiceSlot()
		{
			const auto manager = RE::BGSDefaultObjectManager::GetSingleton();
			return manager ? manager->GetObject<RE::BGSEquipSlot>(RE::DEFAULT_OBJECT::kVoiceEquip) : nullptr;
		}

		void CreateEvent()
		{
			if (shoutEvent) {
				return;
			}
			const auto    userEvents = RE::UserEvents::GetSingleton();
			std::uint32_t shoutKey = 0x2C;  // Z
			if (const auto controlMap = RE::ControlMap::GetSingleton()) {
				const auto mapped = controlMap->GetMappedKey(userEvents->shout, RE::INPUT_DEVICE::kKeyboard);
				if (mapped != 0xFF && mapped != static_cast<std::uint32_t>(-1)) {
					shoutKey = mapped;
				}
			}
			shoutEvent = RE::ButtonEvent::Create(RE::INPUT_DEVICE::kKeyboard, userEvents->shout, shoutKey, 0.0f, 0.0f);
		}

		// Feeds a Shout button state to the shout handler like PlayerControls does for real input
		void SendShout(float a_value, float a_held)
		{
			const auto controls = RE::PlayerControls::GetSingleton();
			const auto handler = controls ? controls->shoutHandler : nullptr;
			if (!handler || !shoutEvent) {
				return;
			}
			shoutEvent->GetRuntimeData().value = a_value;
			shoutEvent->GetRuntimeData().heldDownSecs = a_held;
			if (handler->IsInputEventHandlingEnabled() && handler->CanProcess(shoutEvent)) {
				handler->ProcessButton(shoutEvent, std::addressof(controls->data));
			}
		}

		bool IsShouting()
		{
			bool shouting = false;
			Player()->GetGraphVariableBool("IsShouting"sv, shouting);
			return shouting;
		}

		// the voice caster still works on the shout / power (charging words, firing)
		bool VoiceBusy()
		{
			const auto player = Player();
			for (const auto source : { RE::MagicSystem::CastingSource::kOther, RE::MagicSystem::CastingSource::kInstant }) {
				const auto caster = player->GetMagicCaster(source);
				if (caster && caster->state.get() != RE::MagicCaster::State::kNone) {
					return true;
				}
			}
			return IsShouting();
		}

		// shouts and voice powers start the shout recovery timer when they go off
		bool UsesShoutTimer(const RE::TESForm* a_form)
		{
			const auto spell = a_form->As<RE::SpellItem>();
			return a_form->Is(RE::FormType::Shout) || (spell && spell->GetSpellType() == RE::MagicSystem::SpellType::kVoicePower);
		}

		// The shout is over: it went off (with its recovery timer set) and the voice went quiet, or the game never took it.
		// The previous power must not come back earlier: the timer of a shout still going off would land on it (and with
		// "Each shout has its own cooldown" be kept as that shout's cooldown).
		bool Finished(Shout& a_shout, float a_delta)
		{
			const float timer = Player()->GetVoiceRecoveryTime();
			if (timer > a_shout.lastTimer + 0.05f) {
				a_shout.timerJumped = true;
			}
			a_shout.lastTimer = timer;
			if (VoiceBusy()) {
				a_shout.sawShouting = true;
				a_shout.idleTime = 0.0f;
			} else {
				a_shout.idleTime += a_delta;
			}
			if (a_shout.phaseTime >= kFinishTimeout) {
				return true;
			}
			if (a_shout.phaseTime < kFinishMin || a_shout.idleTime < kQuietTime) {
				return false;
			}
			if (!a_shout.sawShouting && !a_shout.timerJumped) {
				return a_shout.phaseTime >= kNoShoutTimeout;
			}
			return a_shout.timerJumped || !UsesShoutTimer(a_shout.form) || a_shout.idleTime >= kTimerWait;
		}

		void Equip(RE::TESForm* a_form)
		{
			const auto manager = RE::ActorEquipManager::GetSingleton();
			if (const auto voice = a_form->As<RE::TESShout>()) {
				manager->EquipShout(Player(), voice);
			} else if (const auto spell = a_form->As<RE::SpellItem>()) {
				manager->EquipSpell(Player(), spell, VoiceSlot());
			}
		}

		void Restore()
		{
			const auto player = Player();
			if (player && savedValid && savedPower && player->GetActorRuntimeData().selectedPower != savedPower) {
				Equip(savedPower);
			}
			savedValid = false;
			savedPower = nullptr;
		}
	}

	bool StartShout(RE::TESForm* a_form, Page a_page, int a_slot, std::uint32_t a_key, bool a_keyHeld)
	{
		const auto player = Player();
		if (!player || !a_form) {
			return false;
		}
		if (shout) {
			// the previous shout must have gone off first, or its cooldown would land on this one
			if (shout->phase != Phase::kRestoreDelay) {
				return false;
			}
			shout.reset();  // chained: the power from before the first one comes back at the end
		}
		CreateEvent();
		if (!savedValid) {
			savedValid = true;
			savedPower = player->GetActorRuntimeData().selectedPower;
		}
		Equip(a_form);

		Shout next;
		next.form = a_form;
		next.page = a_page;
		next.slot = a_slot;
		next.key = a_key;
		next.keyHeld = a_keyHeld;
		shout = next;
		logs::debug("Shout / power {}", a_form->GetName());
		return true;
	}

	void OnKeyUp(std::uint32_t a_key)
	{
		if (shout && shout->key == a_key) {
			shout->keyHeld = false;
		}
	}

	void Update(float a_delta)
	{
		if (!shout) {
			if (savedValid) {
				Restore();
			}
			return;
		}
		const auto player = Player();
		if (!player || player->IsDead()) {
			Reset();
			return;
		}

		shout->phaseTime += a_delta;
		switch (shout->phase) {
		case Phase::kWaitEquip:
			if (shout->phaseTime >= kEquipDelay) {
				shout->phase = Phase::kHolding;
				shout->phaseTime = 0.0f;
				shout->heldTime = 0.0f;
				SendShout(1.0f, 0.0f);
			}
			break;
		case Phase::kHolding:
			shout->heldTime += a_delta;
			if (!shout->keyHeld && shout->heldTime >= kMinHold) {
				SendShout(0.0f, shout->heldTime);
				shout->phase = Phase::kWaitFinish;
				shout->phaseTime = 0.0f;
				shout->lastTimer = player->GetVoiceRecoveryTime();
			} else {
				SendShout(1.0f, shout->heldTime);
			}
			break;
		case Phase::kWaitFinish:
			if (Finished(*shout, a_delta)) {
				shout->phase = Phase::kRestoreDelay;
				shout->phaseTime = 0.0f;
			}
			break;
		case Phase::kRestoreDelay:
			if (shout->phaseTime >= kRestoreDelay) {
				shout.reset();
				Restore();
			}
			break;
		}
	}

	void Reset()
	{
		shout.reset();
		savedValid = false;
		savedPower = nullptr;
	}

	std::optional<Info> Current()
	{
		if (!shout || shout->phase == Phase::kRestoreDelay) {
			return std::nullopt;
		}
		Info info;
		info.page = shout->page;
		info.slot = shout->slot;
		info.channeling = shout->phase == Phase::kHolding;
		info.progress = 1.0f;
		return info;
	}
}
