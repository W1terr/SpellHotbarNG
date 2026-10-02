#pragma once

// Menu and HUD text in the languages Skyrim ships with. English is built in (the English texts are
// the keys), the others come from Data\SKSE\Plugins\SpellHotbarNG\lang\<language>.json.
// Only switch languages and translate on the render thread (menu and HUD): T() returns pointers into
// the loaded table.
namespace Lang
{
	struct Language
	{
		const char* id;      // file name, same names as Skyrim's sLanguage
		const char* name;    // shown in the language list
		const char* glyphs;  // SKSE Menu Framework "Character Glyphs" option the language needs, nullptr if the default font has its letters
	};

	std::span<const Language> All();
	const Language&           Current();

	// Unknown ids fall back to English
	void Set(std::string_view a_id);

	// Translation of an English text, the text itself if there is none. "Context|Text" keys tell apart
	// texts that are the same in English but not in every language; without a translation only "Text" is shown.
	const char* T(const char* a_english);

	// T() of a std::format string, formatted with the arguments
	template <class... Args>
	std::string F(const char* a_english, const Args&... a_args)
	{
		try {
			return std::vformat(T(a_english), std::make_format_args(a_args...));
		} catch (const std::format_error&) {
			return std::vformat(a_english, std::make_format_args(a_args...));  // broken translation
		}
	}
}
