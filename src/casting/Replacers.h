#pragma once

#include "casting/CastAnim.h"

// Animation patches for magic casting animation mods in Dynamic Animation Replacer folders (also loaded by OAR), e.g.
// "Smooth Magic Casting Animation". Hotbar casts play shout clips, so a mod that replaces mrh_chargeloop.hkx & co.
// never shows on them. A patch is only a list of the mod's DAR folders ("darFolders" in an animations\*.json timing
// file, see tools/patches.py). At game start Generate() writes OAR submods that play those folders' clips in place of
// the shout clips (copies of the user's installed files, nothing of those mods is shipped):
//   Data\meshes\actors\character\OpenAnimationReplacer\SpellHotbarNG_Replacers
// The folders' conditions are evaluated by the DLL when a cast starts (spell school instead of the equipped spell type,
// see Choose), and the generated submods only match through the "Variant" component of our OAR condition.
namespace Replacers
{
	// Scan + write the OAR submods. Call in SKSEPluginLoad: OAR reads its folders at kInputLoaded.
	void Generate();

	// Picks the replacer clips for a cast that is about to start
	void Choose(CastAnim::Type a_type, CastAnim::Side a_side, RE::MagicItem* a_item);

	using State = std::array<int, 8>;  // per view (2) and role (4)
	State Save();
	void  Restore(const State& a_state);
	void  Clear();

	// The "Variant" value of a generated submod is the chosen clip of the running cast. Safe on any thread.
	bool Matches(int a_variant);

	// Length of the chosen release clip, if a replacer clip is chosen for that view
	std::optional<float> ReleaseDuration(bool a_firstPerson);
}
