#pragma once

// Casting animations the Spell Hotbar 2 way, without an ESP: the shout animation is started with the
// "ShoutStart" graph event, and Open Animation Replacer swaps the shout clips for vanilla magic casting
// clips while the custom OAR condition "SpellHotbarNG_Casting" matches the running cast. Nothing in the
// hands changes. The clips + OAR configs ship in meshes\actors\character\OpenAnimationReplacer\SpellHotbarNG.
//
// Casting animation mods made for the normal magic clips (DAR folders) are used through animation patches, see Replacers.h.
// Other animation mods can target the same condition ("Animation" = Type, "Hand" = Side) with a higher OAR
// priority. If their release clips have other lengths, a timing file tells the bar how long to wait, see
// LoadTimings(). "ShoutState" and "Phase" serve clips that are also used outside of our cast (1st person walk / run
// inside the shout behavior, see tools/build_anims.py).
namespace CastAnim
{
	enum class Type : int
	{
		kNone = 0,
		kAimed = 1,       // one hand fire and forget, aimed
		kSelf = 2,        // one hand fire and forget, on self
		kAimedConc = 3,   // one hand concentration, aimed
		kSelfConc = 4,    // one hand concentration, on self
		kDualAimed = 5,   // dual cast, aimed
		kDualSelf = 6,    // dual cast, on self
		kDualConc = 7,    // dual / two-handed concentration, aimed
		kRitual = 8,      // two-handed (ritual) fire and forget spells
		kDualSelfConc = 9 // dual / two-handed concentration, on self (healing with both hands)
	};

	// The hand(s) a cast comes from: the condition's "Hand" value
	enum class Side : int
	{
		kBoth = 0,  // dual casts and two-handed spells
		kRight = 1,
		kLeft = 2
	};

	// Registers the OAR condition. Call on kPostLoad.
	void RegisterCondition();

	// OAR is installed and accepted the condition
	bool Available();

	// Release clip lengths of animation replacers: every Data\SKSE\Plugins\SpellHotbarNG\animations\*.json.
	// Call on kDataLoaded.
	void LoadTimings();

	Type Choose(RE::MagicItem* a_item, bool a_dual);

	enum class StartResult
	{
		kStarted,
		kBusy,    // the release animation of the previous cast is still playing, try again next frame
		kRefused  // the behavior graph can't play it (swimming, mounted, ...), cast without animation
	};

	// Raise the hand(s) / charge loop. a_item picks the clips of installed casting animation mods (see Replacers.h).
	StartResult Start(Type a_type, Side a_side, RE::MagicItem* a_item);
	// seconds until the running release / stop animation is over and blended back to idle (0 if none);
	// no new cast starts before that
	float ReleaseRemaining();
	void Restart();  // keep a concentration loop going
	void Release();  // release animation
	void Stop();     // cancel / end of concentration

	// The spell's casting art (fire / frost / healing glow ...) on the hand magic node(s), like a spell held in hand
	// a_maxDuration is a safety limit, the art is normally removed with StopHandArt()
	void StartHandArt(RE::MagicItem* a_item, Side a_side, float a_maxDuration);
	void StopHandArt();

	// Clears the condition once the release / stop animation had time to finish
	void Update(float a_delta);
	void Reset();
}
