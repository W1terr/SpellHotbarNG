#include "casting/CastAnim.h"

#include "core/Config.h"
#include "casting/GraphWatch.h"
#include "casting/Replacers.h"
#include "casting/ShoutBlend.h"
#include "casting/VanillaCast.h"
#include "OpenAnimationReplacerAPI-Conditions.h"

namespace CastAnim
{
	namespace
	{
		std::atomic<int> current{ 0 };      // Type of the running cast, read by OAR on its own threads
		std::atomic<int> currentSide{ 0 };  // Side of the running cast
		// The graph is in the shout state for our cast: set before "ShoutStart" (OAR picks clips inside
		// NotifyAnimationGraph), cleared when "ShoutStop" goes out. Clips that are also used outside the shout state
		// (1st person walk / run) are only replaced while it's set.
		std::atomic<bool> shoutActive{ false };
		// 1 = charging / channeling, 2 = release animation playing, 3 = release clip done (the "Phase" component).
		// The 1st person walk / run clips are looping slots: a release clip there would start over at its end, so they
		// switch to the ready pose (where the release ends) kReadyLead before that.
		std::atomic<int> phase{ 0 };
		constexpr float  kReadyLead = 0.05f;
		float            readyAt{ 0.0f };  // linger time of phase 3
		bool             available{ false };
		bool             lingering{ false };  // cast over, waiting for the release / stop clip to finish
		float            lingerTime{ 0.0f };
		float            lingerDuration{ 0.0f };

		// The replacement has to stay active while the release clip plays, or OAR would pick the shout clip.
		// After the clip (+ this margin) the graph is sent "ShoutStop": the replaced clips lack the vanilla shout
		// annotations, so the graph can stay in the shout state and refuse the next "ShoutStart".
		constexpr float kLingerMargin = 0.15f;

		// After "ShoutStop" the graph needs a moment to blend back to idle. The next cast starts only after kSettleTime:
		// a "ShoutStart" in the same frame as the "ShoutStop" made every second cast take a different path in the
		// shout behavior (upright pose instead of the casting clip). A start the graph still refuses within
		// kRefusalGrace waits as well instead of casting without animation.
		constexpr float kSettleTime = 0.3f;
		constexpr float kRefusalGrace = 1.0f;
		constexpr float kLongAgo = 10.0f;
		float           sinceStop{ kLongAgo };  // seconds since the last cast's shout state was ended
		bool            stopAtEnd{ false };     // lingering after a release: "ShoutStop" still has to be sent

		// The weapon state reads drawn / sheathed while the draw / sheathe clip still plays, and the graph refuses
		// "ShoutStart" until it ends: a start refused within kDrawGrace of a draw / sheathe waits for it too.
		constexpr float kDrawGrace = 1.5f;
		float           sinceDraw{ kLongAgo };  // seconds since the weapon was last drawing / sheathing

		// The same after a weapon attack was stopped for a cast (option "Cast during attacks")
		constexpr float kAttackStopGrace = 0.5f;
		float           sinceAttackStop{ kLongAgo };

		// Release clip lengths from animation replacer timing files: (first person, type, side) -> seconds.
		// Side 0 entries apply to every side.
		std::map<std::tuple<bool, int, int>, float> timings;

		class CastingCondition : public Conditions::CustomCondition
		{
		public:
			constexpr static inline std::string_view CONDITION_NAME = "SpellHotbarNG_Casting"sv;

			CastingCondition()
			{
				_type = static_cast<Conditions::INumericConditionComponent*>(
					AddBaseComponent(Conditions::ConditionComponentType::kNumeric, "Animation",
						"Casting animation of the running hotbar cast. 0 = any, 1 aimed, 2 self, 3 aimed concentration, "
						"4 self concentration, 5 dual aimed, 6 dual self, 7 dual concentration, 8 ritual (two-handed spell), "
						"9 dual self concentration."));
				_hand = static_cast<Conditions::INumericConditionComponent*>(
					AddBaseComponent(Conditions::ConditionComponentType::kNumeric, "Hand",
						"Hand of the running hotbar cast. 0 = any, 1 right, 2 left. Dual casts and two-handed spells only match 0."));
				_shoutState = static_cast<Conditions::INumericConditionComponent*>(
					AddBaseComponent(Conditions::ConditionComponentType::kNumeric, "ShoutState",
						"1 = only while the hotbar cast keeps the behavior graph in the shout state (for clips that are also "
						"used outside of it, like the 1st person walk / run inside the shout behavior). 0 = also during the "
						"release / stop animation afterwards."));
				_phase = static_cast<Conditions::INumericConditionComponent*>(
					AddBaseComponent(Conditions::ConditionComponentType::kNumeric, "Phase",
						"0 = any, 1 = charging / channeling, 2 = release animation playing, 3 = release animation done. Use it "
						"with Interruptible for looping clips that start while charging (1st person walk / run)."));
				_variant = static_cast<Conditions::INumericConditionComponent*>(
					AddBaseComponent(Conditions::ConditionComponentType::kNumeric, "Variant",
						"0 = any. Otherwise only when the running cast picked this clip of an installed casting animation mod "
						"(set by the submods Spell Hotbar NG generates in SpellHotbarNG_Replacers, don't use it yourself)."));
			}

			RE::BSString GetName() const override { return CONDITION_NAME.data(); }
			RE::BSString GetDescription() const override { return "True while Spell Hotbar NG plays a casting animation of the given type."; }
			REL::Version GetRequiredVersion() const override { return { 1, 4, 0 }; }
			RE::BSString GetArgument() const override
			{
				return std::format("{} / hand {} / shout state {} / phase {}", _type->GetArgument().c_str(), _hand->GetArgument().c_str(),
					_shoutState->GetArgument().c_str(), _phase->GetArgument().c_str())
				    .c_str();
			}
			RE::BSString GetCurrent(RE::TESObjectREFR*) const override
			{
				return std::format("{} / hand {}{}{}", current.load(), currentSide.load(), shoutActive.load() ? " / shout state" : "",
					phase.load() == 2 ? " / releasing" : "")
				    .c_str();
			}

		protected:
			// Runs inside NotifyAnimationGraph / on Havok threads: no logging or game calls here. (Don't read
			// hkbClipGenerator strings either: formatting them crashed on 1.7.)
			bool EvaluateImpl(RE::TESObjectREFR* a_refr, RE::hkbClipGenerator*, void*) const override
			{
				if (!a_refr || !a_refr->IsPlayerRef()) {
					return false;
				}
				const int type = current.load();
				if (type == 0) {
					return false;
				}
				const int wanted = static_cast<int>(_type->GetNumericValue(a_refr));
				if (wanted != 0 && wanted != type) {
					return false;
				}
				if (_shoutState->GetNumericValue(a_refr) >= 1.0f && !shoutActive.load()) {
					return false;
				}
				const int wantedPhase = static_cast<int>(_phase->GetNumericValue(a_refr));
				if (wantedPhase != 0 && wantedPhase != phase.load()) {
					return false;
				}
				const int variant = static_cast<int>(_variant->GetNumericValue(a_refr));
				if (variant != 0 && !Replacers::Matches(variant)) {
					return false;
				}
				const int hand = static_cast<int>(_hand->GetNumericValue(a_refr));
				return (hand != 1 && hand != 2) || hand == currentSide.load();
			}

			Conditions::INumericConditionComponent* _type;
			Conditions::INumericConditionComponent* _hand;
			Conditions::INumericConditionComponent* _shoutState;
			Conditions::INumericConditionComponent* _phase;
			Conditions::INumericConditionComponent* _variant;
		};

		RE::PlayerCharacter* Player() { return RE::PlayerCharacter::GetSingleton(); }

		// Our hand art. ApplyArtObject doesn't always create the effect right away: called from a thread other than the
		// one that owns the effect list, it queues it as a task for a later frame (a glow that showed up late was never
		// found and stayed for its whole safety duration). So the effects are picked up from the game's list on every
		// update, and ones that only show up after the cast ended are removed as soon as they appear. They're only
		// compared against that list, never dereferenced outside of it: the game owns and deletes them.
		// (ApplyArtObject's return value can't be used: on 1.6+/1.7 it returns a success flag, not the effect.)
		struct HandArt
		{
			const RE::BGSArtObject*                      art{ nullptr };
			std::vector<const RE::ModelReferenceEffect*> foreign;  // effects of this art that were there before (not ours)
			std::vector<const RE::ModelReferenceEffect*> found;    // ours, still in the list
			std::size_t                                  expected{ 0 };  // effects asked for
			std::size_t                                  claimed{ 0 };   // effects found so far
			bool                                         stopped{ false };
			float                                        sinceStop{ 0.0f };
		};
		std::vector<HandArt> handArts;  // the running one last; stopped ones stay until all their effects showed up
		constexpr float      kHandArtWait = 1.0f;  // how long a stopped hand art waits for effects not created yet

		bool Contains(const std::vector<const RE::ModelReferenceEffect*>& a_list, const RE::ModelReferenceEffect* a_effect)
		{
			return std::ranges::find(a_list, a_effect) != a_list.end();
		}

		template <class F>
		void ForEachPlayerArtEffect(F&& a_func)
		{
			const auto processLists = RE::ProcessLists::GetSingleton();
			const auto player = Player();
			if (!processLists || !player) {
				return;
			}
			const auto handle = player->GetHandle().native_handle();
			processLists->ForEachModelEffect([&](RE::ModelReferenceEffect* a_effect) {
				if (a_effect && a_effect->artObject && a_effect->target.native_handle() == handle) {
					a_func(a_effect);
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}

		// Claims new effects of our arts (newest hand art first) and ends the ones of stopped hand arts
		void UpdateHandArt(float a_delta)
		{
			if (handArts.empty()) {
				return;
			}
			std::vector<const RE::ModelReferenceEffect*> seen;  // effects of our arts still in the list
			ForEachPlayerArtEffect([&](RE::ModelReferenceEffect* a_effect) {
				HandArt* owner = nullptr;
				HandArt* newest = nullptr;  // a new effect belongs to the newest hand art of its art
				for (auto& entry : handArts) {
					if (entry.art != a_effect->artObject) {
						continue;
					}
					newest = &entry;
					if (Contains(entry.found, a_effect)) {
						owner = &entry;
						break;
					}
				}
				if (!newest) {
					return;
				}
				seen.push_back(a_effect);
				if (!owner && !Contains(newest->foreign, a_effect)) {
					owner = newest;
					owner->found.push_back(a_effect);
					++owner->claimed;
				}
				if (owner && owner->stopped) {
					a_effect->finished = true;
				}
			});
			// forget deleted effects (their addresses can be reused)
			for (auto& entry : handArts) {
				std::erase_if(entry.found, [&](auto a_effect) { return !Contains(seen, a_effect); });
				std::erase_if(entry.foreign, [&](auto a_effect) { return !Contains(seen, a_effect); });
				if (entry.stopped) {
					entry.sinceStop += a_delta;
				}
			}
			std::erase_if(handArts, [](const HandArt& a_entry) {
				return a_entry.stopped && (a_entry.claimed >= a_entry.expected || a_entry.sinceStop > kHandArtWait);
			});
		}

		bool FirstPerson()
		{
			const auto camera = RE::PlayerCamera::GetSingleton();
			return camera && camera->IsInFirstPerson();
		}

		// The hand's magic node on the skeleton that is drawn now (1st or 3rd person). The hand caster's own magicNode
		// is only kept up to date for a hand holding a spell: an empty left hand's points at the 3rd person body, which
		// isn't drawn in 1st person, so the glow didn't show there.
		RE::NiAVObject* MagicNode(bool a_left)
		{
			const auto player = Player();
			const auto name = a_left ? "NPC L MagicNode [LMag]"sv : "NPC R MagicNode [RMag]"sv;
			if (const auto root = player->Get3D(FirstPerson())) {
				if (const auto node = root->GetObjectByName(name)) {
					return node;
				}
			}
			const auto caster = player->GetMagicCaster(a_left ? RE::MagicSystem::CastingSource::kLeftHand : RE::MagicSystem::CastingSource::kRightHand);
			if (const auto actorCaster = skyrim_cast<RE::ActorMagicCaster*>(caster); actorCaster && actorCaster->magicNode) {
				return actorCaster->magicNode;
			}
			return nullptr;
		}

		bool Notify(std::string_view a_event)
		{
			const auto player = Player();
			return player && player->NotifyAnimationGraph(a_event);
		}

		// The shout behavior has a standing and a moving state, picked from the graph variable iSyncIdleLocomotion
		// (set by the engine, 1 while moving) when the shout starts, and switched only by moveStart / moveStop. The moving
		// state blends the shout clip with its own walk / run (arms only half from the shout clip), so in 1st person,
		// where legs aren't drawn, our shout always starts standing: the casting clip gets the arms like a standing cast.
		// The engine's value is put back on the next update.
		constexpr std::string_view kSyncIdleLocomotion = "iSyncIdleLocomotion"sv;
		std::optional<std::int32_t> savedSyncIdleLocomotion;

		bool NotifyShoutStart()
		{
			const auto player = Player();
			if (!player) {
				return false;
			}
			std::int32_t sync = 0;
			if (FirstPerson() && player->GetGraphVariableInt(kSyncIdleLocomotion, sync) && sync != 0) {
				if (!savedSyncIdleLocomotion) {
					savedSyncIdleLocomotion = sync;
				}
				player->SetGraphVariableInt(kSyncIdleLocomotion, 0);
			}
			return player->NotifyAnimationGraph("ShoutStart"sv);
		}

		void RestoreSyncIdleLocomotion()
		{
			if (!savedSyncIdleLocomotion) {
				return;
			}
			// the player may have stopped meanwhile: put back what fits now
			if (const auto player = Player()) {
				player->SetGraphVariableInt(kSyncIdleLocomotion, player->IsMoving() ? *savedSyncIdleLocomotion : 0);
			}
			savedSyncIdleLocomotion.reset();
		}

		// a real shout / power of the player (voice caster busy or a hotbar shout running)
		bool PlayerShouting()
		{
			if (VanillaCast::Current()) {
				return true;
			}
			const auto player = Player();
			const auto voice = player ? player->GetMagicCaster(RE::MagicSystem::CastingSource::kOther) : nullptr;
			return voice && voice->state.get() != RE::MagicCaster::State::kNone;
		}

		void Linger(float a_duration, bool a_stopAtEnd)
		{
			stopAtEnd = a_stopAtEnd;
			lingering = true;
			lingerTime = 0.0f;
			lingerDuration = a_duration + kLingerMargin;
		}

		// takes the graph out of the shout state, unless the player is really shouting
		void EndShoutState()
		{
			shoutActive = false;
			if (!PlayerShouting()) {
				const bool handled = Notify("ShoutStop"sv);
				logs::debug("Casting animation ended (graph {} ShoutStop)", handled ? "took" : "ignored");
				GraphWatch::Note(std::format("ShoutStop after the release ({})", handled ? "taken" : "ignored"));
			}
		}

		// Length of our own (vanilla) release clip of a cast type (measured with tools/hkx_duration.py, the same in
		// 1st and 3rd person). Concentration casts end with a short blend instead.
		float VanillaReleaseDuration(Type a_type)
		{
			// mlh_ (left hand) clips have the same lengths as the mrh_ ones
			switch (a_type) {
			case Type::kAimed:
				return 0.833f;  // mrh_release
			case Type::kSelf:
				return 1.0f;    // mrh_selfrelease
			case Type::kDualAimed:
				return 0.967f;  // dmagaimrelease
			case Type::kDualSelf:
				return 0.8f;    // dmagselfrelease
			case Type::kRitual:
				return 1.0f;    // ritualspell_release
			case Type::kAimedConc:
			case Type::kSelfConc:
			case Type::kDualConc:
			case Type::kDualSelfConc:
				return 0.4f;    // "ShoutStop" blends back to idle
			default:
				return 0.0f;
			}
		}

		bool Concentration(Type a_type)
		{
			return a_type == Type::kAimedConc || a_type == Type::kSelfConc || a_type == Type::kDualConc ||
			       a_type == Type::kDualSelfConc;
		}

		// Length of the release clip that plays: the installed casting animation mod's clip, or ours
		float ClipReleaseDuration(Type a_type, bool a_firstPerson)
		{
			if (!Concentration(a_type)) {
				if (const auto duration = Replacers::ReleaseDuration(a_firstPerson)) {
					return *duration;
				}
			}
			return VanillaReleaseDuration(a_type);
		}

		// Length of the release clip of a cast for the current camera: an animation patch's timing file, or the clip's
		float ReleaseDuration(Type a_type, Side a_side)
		{
			const bool firstPerson = FirstPerson();
			for (const int side : { static_cast<int>(a_side), 0 }) {
				if (const auto it = timings.find({ firstPerson, static_cast<int>(a_type), side }); it != timings.end()) {
					return it->second;
				}
			}
			return ClipReleaseDuration(a_type, firstPerson);
		}

		// a release / stop animation of ours, or a weapon draw / sheathe, is still playing or blending out
		bool Busy()
		{
			return lingering || sinceStop < kRefusalGrace || sinceDraw < kDrawGrace || sinceAttackStop < kAttackStopGrace;
		}

		void UpdateDrawTimer(float a_delta)
		{
			const auto player = Player();
			const auto weapon = player ? player->AsActorState()->GetWeaponState() : RE::WEAPON_STATE::kSheathed;
			const bool moving = weapon != RE::WEAPON_STATE::kSheathed && weapon != RE::WEAPON_STATE::kDrawn;
			sinceDraw = moving ? 0.0f : std::min(sinceDraw + a_delta, kLongAgo);
		}

		float CurrentReleaseDuration()
		{
			return ReleaseDuration(static_cast<Type>(current.load()), static_cast<Side>(currentSide.load()));
		}

		// "releaseTime": { "thirdPerson": { "1": 1.03, "1-left": 1.0, ... }, "firstPerson": { ... } }
		int ReadTimings(const json& a_view, bool a_firstPerson)
		{
			int count = 0;
			if (!a_view.is_object()) {
				return count;
			}
			for (const auto& [key, value] : a_view.items()) {
				const auto dash = key.find('-');
				int        type = 0;
				const auto typeText = std::string_view(key).substr(0, dash);
				if (std::from_chars(typeText.data(), typeText.data() + typeText.size(), type).ec != std::errc{} || type < 1 || type > 9) {
					continue;
				}
				int side = 0;
				if (dash != std::string::npos) {
					const auto suffix = key.substr(dash + 1);
					side = suffix == "left" ? static_cast<int>(Side::kLeft) : suffix == "right" ? static_cast<int>(Side::kRight) : -1;
				}
				if (side < 0 || !value.is_number()) {
					continue;
				}
				const auto seconds = value.get<float>();
				if (seconds > 0.0f && seconds < 10.0f) {
					timings[{ a_firstPerson, type, side }] = seconds;
					++count;
				}
			}
			return count;
		}
	}

	void RegisterCondition()
	{
		const auto result = OAR_API::Conditions::AddCustomCondition<CastingCondition>();
		available = result == OAR_API::Conditions::APIResult::OK || result == OAR_API::Conditions::APIResult::AlreadyRegistered;
		if (available) {
			logs::info("Registered OAR condition {}", CastingCondition::CONDITION_NAME);
		} else {
			logs::warn("Open Animation Replacer not found (result {}), casting animations are disabled", static_cast<int>(result));
		}
	}

	bool Available()
	{
		return available;
	}

	void LoadTimings()
	{
		timings.clear();
		const auto      dir = Config::DataDir() / "animations";
		std::error_code ec;
		if (!std::filesystem::is_directory(dir, ec)) {
			return;
		}
		std::vector<std::filesystem::path> files;
		for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
			if (entry.is_regular_file() && entry.path().extension() == ".json") {
				files.push_back(entry.path());
			}
		}
		std::ranges::sort(files);  // later files win
		for (const auto& path : files) {
			try {
				std::ifstream file(path);
				const auto    j = json::parse(file, nullptr, true, true);
				const auto    release = j.value("releaseTime", json::object());
				const int     count = ReadTimings(release.value("thirdPerson", json::object()), false) +
				                  ReadTimings(release.value("firstPerson", json::object()), true);
				logs::info("Animation timings {}: {} release times", path.filename().string(), count);
			} catch (const std::exception& e) {
				logs::error("Failed to read animation timings {}: {}", path.string(), e.what());
			}
		}
	}

	Type Choose(RE::MagicItem* a_item, bool a_dual)
	{
		const bool self = a_item->GetDelivery() == RE::MagicSystem::Delivery::kSelf;
		const bool conc = a_item->GetCastingType() == RE::MagicSystem::CastingType::kConcentration;
		const bool twoHanded = a_item->IsTwoHanded();
		if (conc) {
			if (a_dual || twoHanded) {
				return self ? Type::kDualSelfConc : Type::kDualConc;
			}
			return self ? Type::kSelfConc : Type::kAimedConc;
		}
		if (twoHanded) {
			return Type::kRitual;
		}
		if (a_dual) {
			return self ? Type::kDualSelf : Type::kDualAimed;
		}
		return self ? Type::kSelf : Type::kAimed;
	}

	StartResult Start(Type a_type, Side a_side, RE::MagicItem* a_item)
	{
		const int previous = current.load();
		const int previousSide = currentSide.load();
		const bool previousShout = shoutActive.load();
		const int  previousPhase = phase.load();
		const auto previousClips = Replacers::Save();
		Replacers::Choose(a_type, a_side, a_item);
		current = static_cast<int>(a_type);
		currentSide = static_cast<int>(a_side);
		shoutActive = true;
		phase = 1;
		if (NotifyShoutStart()) {
			lingering = false;
			sinceStop = kLongAgo;
			logs::debug("Casting animation {} started", static_cast<int>(a_type));
			const auto player = Player();
			GraphWatch::Begin(std::format("ShoutStart, animation {} hand {} ({}, {} person)", static_cast<int>(a_type), static_cast<int>(a_side),
				player && player->IsMoving() ? "moving" : "standing", FirstPerson() ? "1st" : "3rd"));
			return StartResult::kStarted;
		}
		// refused: keep the condition of the clip that may still be playing
		current = previous;
		currentSide = previousSide;
		shoutActive = previousShout;
		phase = previousPhase;
		Replacers::Restore(previousClips);
		// our release animation is still running / blending out: the caller waits for it (Update ends the shout
		// state after the clip) instead of casting without animation
		return Busy() ? StartResult::kBusy : StartResult::kRefused;
	}

	float ReleaseRemaining()
	{
		if (lingering) {
			return std::max(0.0f, lingerDuration - lingerTime) + (stopAtEnd ? kSettleTime : 0.0f);
		}
		return std::max(0.0f, kSettleTime - sinceStop);
	}

	void Restart()
	{
		// refused while the graph is still in the shout state (the normal case)
		if (NotifyShoutStart()) {
			GraphWatch::Note("ShoutStart again: the graph had left the shout state");
		}
	}

	void Release()
	{
		phase = 2;  // before the event: interruptible clips (1st person walk / run) switch to the release clip
		Notify("MT_BreathExhaleShort"sv);
		GraphWatch::Note("MT_BreathExhaleShort (release)");
		Linger(CurrentReleaseDuration(), true);
		readyAt = std::max(0.0f, ClipReleaseDuration(static_cast<Type>(current.load()), FirstPerson()) - kReadyLead);
		logs::debug("Casting animation {} released", current.load());
	}

	void Stop()
	{
		// "ShoutStop" goes out right away here, so there's no extra settle time at the end
		shoutActive = false;
		Notify("ShoutStop"sv);
		GraphWatch::Note("ShoutStop (cast stopped)");
		Linger(CurrentReleaseDuration(), false);
	}

	void AttackStopped()
	{
		sinceAttackStop = 0.0f;
	}

	void Update(float a_delta)
	{
		sinceStop = std::min(sinceStop + a_delta, kLongAgo);
		sinceAttackStop = std::min(sinceAttackStop + a_delta, kLongAgo);
		UpdateDrawTimer(a_delta);
		UpdateHandArt(a_delta);
		RestoreSyncIdleLocomotion();  // the graph picked the shout's start state during the last update
		ShoutBlend::Update(shoutActive.load());  // moving casts: upper body from the casting clip
		GraphWatch::Update(a_delta, current.load() != 0);
		// Something else took the graph out of the shout state while we charge / channel: the arms would drop into the
		// normal walk / run for the rest of the cast. Put the cast animation back.
		if (GraphWatch::TakeLeftShout() && shoutActive.load() && phase.load() == 1 && !lingering) {
			const bool back = NotifyShoutStart();
			GraphWatch::Note(back ? "the graph left the shout state mid-cast: ShoutStart again" : "the graph left the shout state mid-cast, ShoutStart refused");
		}
		if (!lingering) {
			return;
		}
		lingerTime += a_delta;
		if (stopAtEnd && phase.load() == 2 && lingerTime >= readyAt) {
			phase = 3;  // release clip about to end: walk / run clips hold the ready pose instead of looping it
		}
		if (lingerTime > lingerDuration) {
			const bool stop = stopAtEnd;
			if (stop) {
				EndShoutState();
			}
			Reset();
			sinceStop = stop ? 0.0f : kSettleTime;  // already settled after a stop: no wait, refusals still covered
		}
	}

	void StartHandArt(RE::MagicItem* a_item, Side a_side, float a_maxDuration)
	{
		StopHandArt();
		const auto player = Player();
		const auto effect = a_item ? a_item->GetCostliestEffectItem() : nullptr;
		const auto art = effect && effect->baseEffect ? effect->baseEffect->data.castingArt : nullptr;
		if (!player || !art) {
			return;
		}
		HandArt entry{ .art = art };
		ForEachPlayerArtEffect([&](RE::ModelReferenceEffect* a_effect) {
			const bool ours = std::ranges::any_of(handArts, [&](const HandArt& a_other) {
				return Contains(a_other.found, a_effect);
			});
			if (a_effect->artObject == art && !ours) {
				entry.foreign.push_back(a_effect);
			}
		});
		for (const bool left : { false, true }) {
			if (a_side == (left ? Side::kRight : Side::kLeft)) {
				continue;  // one hand casts glow on their hand only
			}
			if (const auto node = MagicNode(left)) {
				player->ApplyArtObject(art, a_maxDuration, nullptr, false, false, node);
				++entry.expected;
			} else {
				logs::debug("Hand art: no {} magic node", left ? "left" : "right");
			}
		}
		if (entry.expected > 0) {
			handArts.push_back(std::move(entry));
			UpdateHandArt(0.0f);  // effects created right away
		}
	}

	void StopHandArt()
	{
		for (auto& entry : handArts) {
			entry.stopped = true;
		}
		UpdateHandArt(0.0f);  // effects that show up later are ended by Update
	}

	void Reset()
	{
		current = 0;
		currentSide = 0;
		shoutActive = false;
		phase = 0;
		lingering = false;
		sinceStop = kLongAgo;
		Replacers::Clear();
		ShoutBlend::Restore();
		StopHandArt();
		RestoreSyncIdleLocomotion();
	}
}
