#include "core/Util.h"

namespace Util
{
	namespace
	{
		std::string ToLower(std::string_view a_text)
		{
			std::string out(a_text);
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		// Loaded plugins by lower case file name, built on first use (the load order doesn't change while playing).
		// Icon lists resolve thousands of keys at startup, so no linear search over the plugin list per key.
		const std::unordered_map<std::string, const RE::TESFile*>& LoadedPlugins()
		{
			static const auto plugins = [] {
				std::unordered_map<std::string, const RE::TESFile*> out;
				if (const auto dataHandler = RE::TESDataHandler::GetSingleton()) {
					for (const auto file : dataHandler->files) {
						if (file && file->GetCompileIndex() != 0xFF) {
							out.emplace(ToLower(file->GetFilename()), file);
						}
					}
				}
				return out;
			}();
			return plugins;
		}

		// Runtime form id of a plugin-local id, 0 if the plugin is not loaded. Light plugins are supported.
		RE::FormID ResolveForm(std::string_view a_plugin, RE::FormID a_localID)
		{
			const auto& plugins = LoadedPlugins();
			const auto  it = plugins.find(ToLower(a_plugin));
			if (it == plugins.end()) {
				return 0;
			}
			const auto file = it->second;
			if (file->IsLight()) {
				return 0xFE000000 | (static_cast<RE::FormID>(file->GetSmallFileCompileIndex()) << 12) | (a_localID & 0xFFF);
			}
			return (static_cast<RE::FormID>(file->GetCompileIndex()) << 24) | (a_localID & 0xFFFFFF);
		}
	}

	std::string ToPluginKey(RE::FormID a_form)
	{
		const auto form = RE::TESForm::LookupByID(a_form);
		if (!form || (a_form >> 24) == 0xFF) {
			return {};
		}
		const auto file = form->GetFile(0);
		if (!file) {
			return {};
		}
		const RE::FormID local = file->IsLight() ? (a_form & 0xFFF) : (a_form & 0xFFFFFF);
		return std::format("{}|0x{:06X}", file->GetFilename(), local);
	}

	RE::FormID FromPluginKey(std::string_view a_key)
	{
		const auto sep = a_key.rfind('|');
		if (sep == std::string_view::npos) {
			return 0;
		}
		auto idText = a_key.substr(sep + 1);
		if (idText.starts_with("0x") || idText.starts_with("0X")) {
			idText.remove_prefix(2);
		}
		RE::FormID local = 0;
		const auto [end, error] = std::from_chars(idText.data(), idText.data() + idText.size(), local, 16);
		if (error != std::errc{} || idText.empty()) {
			return 0;
		}
		return ResolveForm(a_key.substr(0, sep), local);
	}

	std::string NormalizeName(std::string_view a_name)
	{
		std::string out;
		out.reserve(a_name.size());
		for (const unsigned char c : a_name) {
			switch (c) {
			case ' ':
			case '-':
				out.push_back('_');
				break;
			case '\'':
			case ':':
				break;
			default:
				out.push_back(static_cast<char>(std::tolower(c)));
				break;
			}
		}
		return out;
	}

	bool InBeastForm()
	{
		// the game's beast form flag: set by the werewolf and vampire lord scripts (and most transformation mods)
		if (const auto controls = RE::MenuControls::GetSingleton(); controls && controls->InBeastForm()) {
			return true;
		}
		const auto player = RE::PlayerCharacter::GetSingleton();
		const auto race = player ? player->GetRace() : nullptr;
		if (!race) {
			return false;
		}
		static RE::TESRace* werewolf = nullptr;
		static RE::TESRace* vampireLord = nullptr;
		if (!werewolf) {
			const auto defaults = RE::BGSDefaultObjectManager::GetSingleton();
			const auto object = defaults ? defaults->GetObject<RE::TESRace>(RE::DefaultObjectID::kWerewolfRace) : nullptr;
			werewolf = object ? *object : nullptr;
		}
		if (!vampireLord) {
			const auto dataHandler = RE::TESDataHandler::GetSingleton();
			vampireLord = dataHandler ? dataHandler->LookupForm<RE::TESRace>(0x00283A, "Dawnguard.esm"sv) : nullptr;  // DLC1VampireBeastRace
		}
		return race == werewolf || race == vampireLord;
	}

	bool OnMount()
	{
		// the rider keeps the mount interaction from getting on until the end of getting off
		const auto player = RE::PlayerCharacter::GetSingleton();
		return player && player->IsOnMount();
	}

	bool HotbarOff()
	{
		return InBeastForm() || OnMount();
	}
}
