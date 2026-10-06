#pragma once

namespace Util
{
	// "Plugin.esp|0x00ABCD" <-> form id (0 / empty if the plugin is not loaded). Light plugins are supported.
	std::string ToPluginKey(RE::FormID a_form);
	RE::FormID  FromPluginKey(std::string_view a_key);

	// Lower case, spaces to underscores, some punctuation dropped (matches the icon file naming)
	std::string NormalizeName(std::string_view a_name);

	// The player is a werewolf / vampire lord (or another transformation): the hotbar is off meanwhile
	bool InBeastForm();

	// The player rides a horse (or a dragon), also while getting on / off: the hotbar is off meanwhile
	bool OnMount();

	// Beast form or riding: hotbar keys go to the game, the bar fades out, magic can't be used. Binding in menus
	// stays possible while riding.
	bool HotbarOff();
}
