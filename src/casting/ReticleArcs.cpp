#include "casting/ReticleArcs.h"

#include "casting/Actions.h"
#include "core/Config.h"

#include "ReticleArcsAPI.h"

namespace ReticleArcs
{
	namespace
	{
		using ReticleArcsAPI::BarState;

		ReticleArcsAPI::IReticleArcs* api{ nullptr };
		bool                          canReadBars{ false };  // interface version 2: GetBarState

		constexpr std::uint32_t kFillColor = 0x78BEFF;  // Reticle Arcs' default spell fill
		constexpr std::uint8_t  kFillAlpha = 180;
		constexpr float         kLowMagicka = 0.25f;  // a channel shows the warning colour below this share, like Reticle Arcs'

		struct HandBar
		{
			bool          left;
			bool          shown{ false };
			std::uint32_t ended{ 0 };    // Actions::Events().ended while it showed
			std::uint32_t refused{ 0 };  // refusals already flashed
		};
		HandBar leftBar{ .left = true };
		HandBar rightBar{ .left = false };

		// the magicka left as a share of the most there can be, buffs included (what Reticle Arcs shows for channels)
		float MagickaShare()
		{
			const auto player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return 0.0f;
			}
			const auto  values = player->AsActorValueOwner();
			const float most = values->GetPermanentActorValue(RE::ActorValue::kMagicka) +
			                   player->GetActorValueModifier(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kMagicka);
			return most > 0.0f ? std::clamp(values->GetActorValue(RE::ActorValue::kMagicka) / most, 0.0f, 1.0f) : 0.0f;
		}

		// Reticle Arcs' own bar of that hand already shows the spell (a concentration spell the hand caster channels)
		bool BuiltInShows(bool a_left, RE::FormID a_spell)
		{
			ReticleArcsAPI::BarInfo info{};
			return canReadBars && api->GetBarState(a_left ? ReticleArcsAPI::Bar::kLeftSpell : ReticleArcsAPI::Bar::kRightSpell, info) &&
			       info.active && info.formID == a_spell;
		}

		void Update(HandBar& a_bar, BarState& a_state)
		{
			const auto& events = Actions::Events();
			const auto  cast = Actions::CurrentHandCast();
			// a concentration spell's short wind-up isn't worth a bar: it shows once it channels
			const bool mine = cast && (a_bar.left ? cast->left : cast->right) &&
			                  (cast->channeling || cast->item->GetCastingType() != RE::MagicSystem::CastingType::kConcentration) &&
			                  !BuiltInShows(a_bar.left, cast->item->GetFormID());
			if (mine) {
				a_state.active = true;
				if (cast->channeling) {
					a_state.percent = MagickaShare();
					a_state.drain = true;
					a_state.motion = ReticleArcsAPI::Motion::kPlayers;
					a_state.low = a_state.percent < kLowMagicka;
				} else {
					a_state.percent = cast->progress;
				}
				if (!a_bar.shown) {
					a_bar.shown = true;
					a_bar.ended = events.ended;
				}
			} else if (a_bar.shown) {
				// the frame it stops: released, or broken when the cast ended without going off (fizzled, let go)
				a_bar.shown = false;
				a_state.broken = events.ended != a_bar.ended && !events.lastFired;
			}

			const auto refused = a_bar.left ? events.refusedLeft : events.refusedRight;
			if (refused != a_bar.refused) {
				a_bar.refused = refused;
				a_state.refused = !a_state.active;
			}
		}

		void UpdateLeft(float, BarState& a_state)
		{
			std::scoped_lock lock(Config::Lock());
			Update(leftBar, a_state);
		}

		void UpdateRight(float, BarState& a_state)
		{
			std::scoped_lock lock(Config::Lock());
			Update(rightBar, a_state);
		}

		void Reset()
		{
			std::scoped_lock lock(Config::Lock());
			const auto&      events = Actions::Events();
			for (auto* bar : { &leftBar, &rightBar }) {
				bar->shown = false;
				bar->ended = events.ended;
				bar->refused = bar->left ? events.refusedLeft : events.refusedRight;
			}
		}

		// the settings page: a charge filling up, a short pause, again
		void Preview(float a_time, BarState& a_state)
		{
			constexpr float kCharge = 1.2f, kPause = 0.5f;
			const float     t = std::fmod(a_time, kCharge + kPause);
			a_state.active = t < kCharge;
			a_state.percent = std::min(1.0f, t / kCharge);
		}

		ReticleArcsAPI::BarDefinition Bar(const char* a_id, const char* a_name, const char* a_tooltip, const char* a_placement,
			void (*a_update)(float, BarState&))
		{
			ReticleArcsAPI::BarDefinition bar{};
			bar.id = a_id;
			bar.name = a_name;
			bar.tooltip = a_tooltip;
			bar.color = kFillColor;
			bar.alpha = kFillAlpha;
			bar.placement = a_placement;
			bar.update = a_update;
			bar.reset = Reset;
			bar.preview = Preview;
			return bar;
		}

		// Reticle Arcs 2.1.0 and later (interface 3): hotbar casts shown on its own left / right spell bars, in the spell's
		// colours and with the player's effects. Reticle Arcs drains channels by the magicka left, shows a hand's own cast
		// first and resets after a load
		struct ProvidedCast
		{
			bool          shown{ false };
			std::uint32_t ended{ 0 };  // Actions::Events().ended when it started showing
			std::uint32_t refusedLeft{ 0 };
			std::uint32_t refusedRight{ 0 };
		};
		ProvidedCast provided;

		ReticleArcsAPI::Hand HandOf(bool a_left, bool a_right)
		{
			using Hand = ReticleArcsAPI::Hand;
			return a_left && a_right ? Hand::kBoth : a_left ? Hand::kLeft : Hand::kRight;
		}

		void OnCast(float, ReticleArcsAPI::CastState& a_state)
		{
			std::scoped_lock lock(Config::Lock());
			const auto&      events = Actions::Events();
			const auto       cast = Actions::CurrentHandCast();
			// a concentration spell's short wind-up isn't worth a bar: it shows once it channels
			if (cast && (cast->channeling || cast->item->GetCastingType() != RE::MagicSystem::CastingType::kConcentration)) {
				a_state.active = true;
				a_state.formID = cast->item->GetFormID();
				a_state.percent = cast->progress;  // ignored while channelling
				a_state.channel = cast->channeling;
				a_state.hand = HandOf(cast->left, cast->right);
				if (!provided.shown) {
					provided.shown = true;
					provided.ended = events.ended;
				}
			} else if (provided.shown) {
				// the frame it stops: the break effect when the cast ended without going off (fizzled, let go)
				provided.shown = false;
				a_state.broken = events.ended != provided.ended && !events.lastFired;
			}

			// a press without enough magicka: the warning flash on that hand's bar
			const bool refusedLeft = events.refusedLeft != provided.refusedLeft;
			const bool refusedRight = events.refusedRight != provided.refusedRight;
			if (refusedLeft || refusedRight) {
				if (!a_state.active) {
					a_state.refused = true;
					a_state.hand = HandOf(refusedLeft, refusedRight);
				}
				provided.refusedLeft = events.refusedLeft;
				provided.refusedRight = events.refusedRight;
			}
		}
	}

	void Init()
	{
		if (auto* castApi = ReticleArcsAPI::Request(3)) {
			castApi->SetCastProvider(OnCast);
			logs::info("Reticle Arcs found (interface {}): hotbar casts shown on its spell bars", castApi->GetVersion());
			return;
		}

		// older Reticle Arcs: two add-on bars of our own
		api = ReticleArcsAPI::Request(2);
		canReadBars = api != nullptr;
		if (!api) {
			api = ReticleArcsAPI::Request(1);
		}
		if (!api) {
			return;
		}
		const bool left = api->RegisterBar(Bar("SpellHotbarNG-LeftHand", "Spell Hotbar NG - left hand",
			"Spells and scrolls cast from Spell Hotbar NG with the left hand", "left", UpdateLeft));
		const bool right = api->RegisterBar(Bar("SpellHotbarNG-RightHand", "Spell Hotbar NG - right hand",
			"Spells and scrolls cast from Spell Hotbar NG with the right hand", "right", UpdateRight));
		logs::info("Reticle Arcs found (interface {}): hotbar cast bars {}", api->GetVersion(),
			left && right ? "added"sv : left || right ? "partly added (see ReticleArcs.log)"sv : "refused (see ReticleArcs.log)"sv);
	}
}
