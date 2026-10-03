#include "core/Hotkeys.h"

#include "core/Keys.h"
#include "core/Lang.h"

namespace Hotkeys
{
	namespace
	{
		constexpr auto kSlotsSection = "Slots";
		constexpr auto kExtraBarsSection = "Extra Bars";
		constexpr auto kOblivionSection = "Oblivion Style";  // Hotkey Atlas shows sections as the Context column

		std::filesystem::path Path()
		{
			return Config::DataDir() / "Hotkeys.ini";
		}

		std::string Lower(std::string_view a_str)
		{
			std::string out(a_str);
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		std::string_view Trim(std::string_view a_str)
		{
			const auto first = a_str.find_first_not_of(" \t\r\n");
			if (first == std::string_view::npos) {
				return {};
			}
			return a_str.substr(first, a_str.find_last_not_of(" \t\r\n") - first + 1);
		}

		std::optional<std::uint32_t> ParseUInt(std::string_view a_str)
		{
			std::uint32_t value = 0;
			const auto [end, ec] = std::from_chars(a_str.data(), a_str.data() + a_str.size(), value);
			return ec == std::errc{} && end == a_str.data() + a_str.size() ? std::optional(value) : std::nullopt;
		}

		struct Setting
		{
			const char* section;
			std::string name;  // Hotkey Atlas shows it as the action: "Slot_1_Key" -> "Slot 1 Key"

			std::string Id() const { return Lower(std::format("{}|{}", section, name)); }
		};

		// Ini section and name of a key member of `a_settings`
		std::optional<Setting> SettingOf(const Settings& a_settings, const std::uint32_t* a_key)
		{
			const auto& slots = a_settings.slotKeys;
			if (a_key >= slots.data() && a_key < slots.data() + slots.size()) {
				return Setting{ kSlotsSection, std::format("Slot_{}_Key", a_key - slots.data() + 1) };
			}
			const auto& modifiers = a_settings.modifierKeys;
			if (a_key >= modifiers.data() && a_key < modifiers.data() + modifiers.size()) {
				return Setting{ kExtraBarsSection, std::format("Extra_Bar_{}_Key", a_key - modifiers.data() + 1) };
			}
			if (a_key == &a_settings.castKey) {
				return Setting{ kOblivionSection, "Cast_Key" };
			}
			if (a_key == &a_settings.potionKey) {
				return Setting{ kOblivionSection, "Potion_Key" };
			}
			return std::nullopt;
		}

		// Calls a_func with every key member, in file order. a_inUse: only the keys that do something
		// now (the bar's slots, extra bars that are on, the Oblivion style keys in that mode).
		template <class S, class Func>
		void ForEachKey(S& a_settings, bool a_inUse, Func a_func)
		{
			for (int i = 0; i < kMaxSlots; ++i) {
				if (!a_inUse || i < a_settings.slotCount) {
					a_func(a_settings.slotKeys[i]);
				}
			}
			for (int i = 0; i < kModifierCount; ++i) {
				if (!a_inUse || a_settings.modifierEnabled[i]) {
					a_func(a_settings.modifierKeys[i]);
				}
			}
			if (!a_inUse || a_settings.keyMode == KeyMode::kOblivion) {
				a_func(a_settings.castKey);
				a_func(a_settings.potionKey);
			}
		}

		// ---- Hotkey Atlas -----------------------------------------------------------------------
		// Its ini keeps a remap of a mod key as "<file under Data>|<section>|<setting> = <input>|<original>"
		// in [ModFiles] (key moved) and [ModFilesGamepad] (controller button added, the key stays).
		// Inputs are its own codes: keyboard = scan code | modifiers << 8, mouse = 0x10000 | button,
		// gamepad = 0x20000 | XInput mask, bits 20-29 an input held first, bits 30-31 double tap / hold.

		constexpr auto          kAtlasIni = "Data/SKSE/Plugins/HotkeyAtlas.ini";
		constexpr auto          kAtlasOrigin = "skse/plugins/spellhotbarng/hotkeys.ini";  // lower case
		constexpr std::uint32_t kAtlasMouse = 0x10000;
		constexpr std::uint32_t kAtlasPad = 0x20000;
		constexpr std::uint32_t kAtlasUnbound = 0xFF;  // key removed

		struct AtlasRemap
		{
			std::uint32_t input;     // what the user presses now
			std::uint32_t original;  // our key it stands for
		};

		struct AtlasRemaps
		{
			std::map<std::string, AtlasRemap> moved;      // Setting::Id() -> remap
			std::map<std::string, AtlasRemap> padAdded;
		};

		AtlasRemaps ReadAtlas()
		{
			AtlasRemaps   out;
			std::ifstream file(kAtlasIni);
			std::string   section, line;
			while (std::getline(file, line)) {
				const auto text = Trim(line);
				if (text.empty() || text.front() == ';' || text.front() == '#') {
					continue;
				}
				if (text.front() == '[') {
					section = Lower(text.substr(1, text.find(']') - 1));
					continue;
				}
				const auto eq = text.rfind('=');
				if (eq == std::string_view::npos || (section != "modfiles" && section != "modfilesgamepad")) {
					continue;
				}
				// older Hotkey Atlas versions wrote "../../MO2/mods/<mod>/SKSE/..." as the file
				const auto id = Lower(Trim(text.substr(0, eq)));
				const auto fileEnd = id.find('|');
				if (fileEnd == std::string::npos || !std::string_view(id).substr(0, fileEnd).ends_with(kAtlasOrigin)) {
					continue;
				}
				const auto value = Trim(text.substr(eq + 1));
				const auto bar = value.find('|');
				const auto input = ParseUInt(Trim(value.substr(0, bar)));
				const auto original = bar != std::string_view::npos ? ParseUInt(Trim(value.substr(bar + 1))) : std::nullopt;
				if (input && original) {
					(section == "modfiles" ? out.moved : out.padAdded)[id.substr(fileEnd + 1)] = { *input, *original };
				}
			}
			if (!out.moved.empty() || !out.padAdded.empty()) {
				logs::info("Hotkey Atlas: {} key(s) moved, {} controller button(s) added", out.moved.size(), out.padAdded.size());
			}
			return out;
		}

		// Reread when Hotkey Atlas saves (checked once a second). Render thread only.
		const AtlasRemaps& Atlas()
		{
			static AtlasRemaps                           remaps;
			static std::filesystem::file_time_type       readTime{};
			static std::chrono::steady_clock::time_point checked{};

			const auto now = std::chrono::steady_clock::now();
			if (now - checked < 1s) {
				return remaps;
			}
			checked = now;
			std::error_code ec;
			const auto      time = std::filesystem::last_write_time(kAtlasIni, ec);
			if (ec) {
				remaps = {};
				readTime = {};
			} else if (time != readTime) {
				readTime = time;
				remaps = ReadAtlas();
			}
			return remaps;
		}

		std::uint32_t AtlasCode(std::uint32_t a_key)
		{
			if (const auto mask = Keys::GamepadMask(a_key)) {
				return kAtlasPad | mask;
			}
			return a_key >= Keys::kMouseOffset ? kAtlasMouse | (a_key - Keys::kMouseOffset) : a_key;
		}

		// A remap made for this key (Hotkey Atlas ignores it too once the key in our file changed)
		const AtlasRemap* FindRemap(const std::map<std::string, AtlasRemap>& a_remaps, const Settings& a_settings, const std::uint32_t& a_key)
		{
			const auto setting = SettingOf(a_settings, &a_key);
			if (!setting || a_key == Keys::kNone) {
				return nullptr;
			}
			const auto it = a_remaps.find(setting->Id());
			return it != a_remaps.end() && it->second.original == AtlasCode(a_key) ? &it->second : nullptr;
		}

		std::string GamepadName(std::uint32_t a_mask)
		{
			switch (a_mask) {
			case 0xB:
				return "L Stick";  // the stick pushed in a direction, used like a button
			case 0xC:
				return "R Stick";
			default:
				return Keys::Name(Keys::FromGamepadMask(a_mask));
			}
		}

		std::string AtlasInputName(std::uint32_t a_input)
		{
			if (a_input >= kAtlasPad) {
				return GamepadName(a_input & 0xFFFF);
			}
			if (a_input >= kAtlasMouse) {
				return Keys::Name(Keys::kMouseOffset + (a_input & 0xFFFF));
			}
			std::string mods;
			if (a_input & 0x200) {
				mods += "Ctrl+";
			}
			if (a_input & 0x100) {
				mods += "Shift+";
			}
			if (a_input & 0x400) {
				mods += "Alt+";
			}
			return mods + Keys::Name(a_input & 0xFF);
		}

		std::string AtlasName(std::uint32_t a_input)
		{
			if (a_input == kAtlasUnbound) {
				return {};
			}
			auto name = AtlasInputName(a_input & 0xFFFFF);

			// "hold G, press F": the held input's kind and index
			const auto held = (a_input >> 20) & 0x3FF;
			const auto index = held & 0xFF;
			constexpr std::uint32_t kPadOrder[] = { 0x1, 0x2, 0x4, 0x8, 0x10, 0x20, 0x40, 0x80, 0x100, 0x200, 0x1000, 0x2000, 0x4000, 0x8000, 0x9, 0xA, 0xB, 0xC };
			switch (held >> 8) {
			case 1:
				name = Keys::Name(index) + "+" + name;
				break;
			case 2:
				name = Keys::Name(Keys::kMouseOffset + index) + "+" + name;
				break;
			case 3:
				if (index < std::size(kPadOrder)) {
					name = GamepadName(kPadOrder[index]) + "+" + name;
				}
				break;
			default:
				break;
			}

			switch (a_input >> 30) {
			case 1:
				return Lang::F("{} (double tap)", name);
			case 2:
				return Lang::F("{} (hold)", name);
			default:
				return name;
			}
		}
	}

	void Read(Settings& a_settings)
	{
		std::ifstream file(Path());
		if (!file) {
			return;
		}

		std::map<std::string, std::uint32_t*> keys;  // Setting::Id() -> member
		ForEachKey(a_settings, false, [&](std::uint32_t& a_key) { keys[SettingOf(a_settings, &a_key)->Id()] = &a_key; });

		std::string section, line;
		int         count = 0;
		while (std::getline(file, line)) {
			auto text = Trim(std::string_view(line).substr(0, line.find_first_of(";#")));
			if (text.empty()) {
				continue;
			}
			if (text.front() == '[') {
				section = std::string(Trim(text.substr(1, text.find(']') - 1)));
				continue;
			}
			const auto eq = text.find('=');
			if (eq == std::string_view::npos) {
				continue;
			}
			const auto name = Trim(text.substr(0, eq));
			const auto it = keys.find(Lower(std::format("{}|{}", section, name)));
			if (it == keys.end()) {
				continue;
			}
			const auto value = ParseUInt(Trim(text.substr(eq + 1)));
			if (!value || *value >= Keys::kGamepadOffset + 16) {
				logs::warn("{}: ignoring {} = {}", Path().string(), name, Trim(text.substr(eq + 1)));
				continue;
			}
			*it->second = *value;
			++count;
		}
		logs::info("Read {} key(s) from {}", count, Path().string());
	}

	void Write(const Settings& a_settings)
	{
		std::string text =
			"; Spell Hotbar NG keys, kept up to date by the mod. Change them in its menu (Bindings > Hotkeys)\n"
			"; or here while the game is closed.\n"
			"; Hotkey tools such as Hotkey Atlas read this file to list and remap the keys.\n"
			"; Codes: keyboard scan codes (DirectInput), 256-265 mouse buttons, 266-281 controller buttons, 0 = no key.\n"
			"; Only the keys in use are listed: the bar's slots, extra bars that are on, the Oblivion style keys.\n";
		const char* section = nullptr;
		ForEachKey(a_settings, true, [&](const std::uint32_t& a_key) {
			const auto setting = SettingOf(a_settings, &a_key);
			if (setting->section != section) {
				section = setting->section;
				text += std::format("\n[{}]\n", section);
			}
			text += std::format("{} = {}", setting->name, a_key);
			if (a_key != Keys::kNone) {
				text += std::format("  ; {}", Keys::Name(a_key));
			}
			text += '\n';
		});

		{
			std::ifstream current(Path());
			if (current && std::string(std::istreambuf_iterator<char>(current), {}) == text) {
				return;
			}
		}
		std::error_code ec;
		std::filesystem::create_directories(Path().parent_path(), ec);
		std::ofstream file(Path(), std::ios::trunc);
		if (!file) {
			logs::error("Failed to write {}", Path().string());
			return;
		}
		file << text;
	}

	std::string Label(const Settings& a_settings, const std::uint32_t& a_key)
	{
		const auto& atlas = Atlas();
		auto        label = a_key == Keys::kNone ? std::string{} : Keys::Name(a_key);
		if (atlas.moved.empty() && atlas.padAdded.empty()) {
			return label;  // the usual case, every frame for every slot
		}
		if (const auto moved = FindRemap(atlas.moved, a_settings, a_key)) {
			label = AtlasName(moved->input);
		}
		if (const auto pad = FindRemap(atlas.padAdded, a_settings, a_key)) {
			label = label.empty() ? AtlasName(pad->input) : std::format("{} / {}", label, AtlasName(pad->input));
		}
		return label;
	}

	bool MovedInHotkeyAtlas(const Settings& a_settings, const std::uint32_t& a_key)
	{
		const auto& atlas = Atlas();
		if (atlas.moved.empty() && atlas.padAdded.empty()) {
			return false;
		}
		return FindRemap(atlas.moved, a_settings, a_key) || FindRemap(atlas.padAdded, a_settings, a_key);
	}
}
