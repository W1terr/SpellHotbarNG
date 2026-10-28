#pragma once

namespace Util
{
	// "Plugin.esp|0x00ABCD" <-> form id (0 / empty if the plugin is not loaded). Light plugins are supported.
	std::string ToPluginKey(RE::FormID a_form);
	RE::FormID  FromPluginKey(std::string_view a_key);

	// Lower case, spaces to underscores, some punctuation dropped (matches the icon file naming)
	std::string NormalizeName(std::string_view a_name);

	// The player is a werewolf / vampire lord / lich (or another transformation)
	bool InBeastForm();

	// Beast forms whose spells the bar casts through the game's own hand casting (BeastCast)
	enum class CastingForm
	{
		kNone,
		kVampireLord,  // Dawnguard
		kLich          // Undeath (its lich race has the dragon priest body)
	};
	CastingForm CurrentCastingForm();

	// A beast form the bar can't cast in (werewolf, other transformations)
	bool HotbarOffInBeastForm();

	// The player rides a horse (or a dragon), also while getting on / off: the hotbar is off meanwhile
	bool OnMount();

	// A beast form without bar casting, or riding: hotbar keys go to the game and magic can't be used; the bar stays in
	// view. Binding in menus stays possible while riding.
	bool HotbarOff();
}
