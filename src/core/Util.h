#pragma once

namespace Util
{
	// "Plugin.esp|0x00ABCD" <-> form id (0 / empty if the plugin is not loaded). Light plugins are supported.
	std::string ToPluginKey(RE::FormID a_form);
	RE::FormID  FromPluginKey(std::string_view a_key);

	// Lower case, spaces to underscores, some punctuation dropped (matches the icon file naming)
	std::string NormalizeName(std::string_view a_name);
}
