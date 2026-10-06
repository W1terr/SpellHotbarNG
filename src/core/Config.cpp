#include "core/Config.h"

#include "core/Bindings.h"
#include "core/Hotkeys.h"
#include "core/Lang.h"

namespace
{
	// Profile names can have any letters, path::string() would throw for those missing in the system code page
	std::filesystem::path FromUtf8(const std::string& a_str)
	{
		return std::filesystem::path(std::u8string(a_str.begin(), a_str.end()));
	}

	std::string ToUtf8(const std::filesystem::path& a_path)
	{
		const auto str = a_path.u8string();
		return std::string(str.begin(), str.end());
	}
}

namespace Config
{
	namespace
	{
		Settings settings{};

		bool                                  dirty{ false };
		std::chrono::steady_clock::time_point dirtySince{};

		constexpr int kFileVersion = 4;  // 3: modifiers off by default, 4: casting / double tap / hold / frame options removed

		// json key of every bar color
		constexpr std::pair<const char*, RGBA BarColors::*> kColorFields[]{
			{ "frame", &BarColors::frame },
			{ "slotBackground", &BarColors::slotBackground },
			{ "keyLabel", &BarColors::keyLabel },
			{ "text", &BarColors::text },
			{ "handMarker", &BarColors::handMarker },
			{ "equipped", &BarColors::equipped },
			{ "cooldown", &BarColors::cooldown },
			{ "chargeBar", &BarColors::chargeBar },
			{ "channeling", &BarColors::channeling },
			{ "noMagicka", &BarColors::noMagicka },
		};

		template <class T>
		void Read(const json& a_json, const char* a_key, T& a_out)
		{
			const auto it = a_json.find(a_key);
			if (it == a_json.end()) {
				return;
			}
			try {
				if constexpr (std::is_enum_v<T>) {
					a_out = static_cast<T>(it->get<int>());
				} else {
					a_out = it->get<T>();
				}
			} catch (const std::exception& e) {
				logs::warn("Ignoring setting '{}': {}", a_key, e.what());
			}
		}

		template <class T, std::size_t N>
		void ReadArray(const json& a_json, const char* a_key, std::array<T, N>& a_out)
		{
			const auto it = a_json.find(a_key);
			if (it == a_json.end() || !it->is_array()) {
				return;
			}
			for (std::size_t i = 0; i < N && i < it->size(); ++i) {
				try {
					a_out[i] = (*it)[i].get<T>();
				} catch (...) {
				}
			}
		}

		std::filesystem::path SettingsPath()
		{
			return UserDir() / "settings.json";
		}

		std::optional<json> ReadJsonFile(const std::filesystem::path& a_path)
		{
			std::ifstream file(a_path);
			if (!file) {
				return std::nullopt;
			}
			try {
				return json::parse(file, nullptr, true, true);
			} catch (const std::exception& e) {
				logs::error("Failed to parse {}: {}", ToUtf8(a_path), e.what());
				return std::nullopt;
			}
		}

		bool WriteJsonFile(const std::filesystem::path& a_path, const json& a_json)
		{
			std::error_code ec;
			std::filesystem::create_directories(a_path.parent_path(), ec);
			std::ofstream file(a_path, std::ios::trunc);
			if (!file) {
				logs::error("Failed to write {}", ToUtf8(a_path));
				return false;
			}
			file << a_json.dump(2);
			return true;
		}
	}

	Settings& Get()
	{
		return settings;
	}

	std::recursive_mutex& Lock()
	{
		static std::recursive_mutex lock;
		return lock;
	}

	std::filesystem::path UserDir()
	{
		static const auto dir = [] {
			auto logDir = logs::log_directory();
			return logDir ? *logDir / "SpellHotbarNG" : std::filesystem::path("Data/SKSE/Plugins/SpellHotbarNG/user");
		}();
		return dir;
	}

	std::filesystem::path DataDir()
	{
		return std::filesystem::path("Data/SKSE/Plugins/SpellHotbarNG");
	}

	json ToJson(const Settings& s)
	{
		json j;
		j["slotCount"] = s.slotCount;
		j["columns"] = s.columns;
		j["iconSize"] = s.iconSize;
		j["spacing"] = s.spacing;
		j["anchor"] = static_cast<int>(s.anchor);
		j["offsetX"] = s.offsetX;
		j["offsetY"] = s.offsetY;
		j["opacity"] = s.opacity;
		j["visibility"] = static_cast<int>(s.visibility);
		j["keyPressShowTime"] = s.keyPressShowTime;
		j["fadeOutOfCombat"] = s.fadeOutOfCombat;
		j["fadedOpacity"] = s.fadedOpacity;
		j["showEmptySlots"] = s.showEmptySlots;
		j["showKeyLabels"] = s.showKeyLabels;
		j["showPageName"] = s.showPageName;
		j["showCooldownText"] = s.showCooldownText;
		j["showItemCount"] = s.showItemCount;
		j["showInMenus"] = s.showInMenus;
		auto& colors = j["colors"];
		for (const auto& [key, member] : kColorFields) {
			colors[key] = s.colors.*member;
		}

		j["slotKeys"] = s.slotKeys;
		j["modifierKeys"] = s.modifierKeys;
		j["modifierEnabled"] = s.modifierEnabled;
		j["modifierToggle"] = s.modifierToggle;
		j["mainBarKeyEnabled"] = s.mainBarKeyEnabled;
		j["mainBarKeyMode"] = static_cast<int>(s.mainBarKeyMode);
		j["mainBarKey"] = s.mainBarKey;
		j["mainBarModifier"] = s.mainBarModifier;
		j["blockGameInput"] = s.blockGameInput;
		j["onlyWhileSneaking"] = s.onlyWhileSneaking;
		j["aimAtCrosshair"] = s.aimAtCrosshair;
		j["individualShoutCooldowns"] = s.individualShoutCooldowns;

		j["keyMode"] = static_cast<int>(s.keyMode);
		j["castKey"] = s.castKey;
		j["potionKey"] = s.potionKey;
		j["readyAnchor"] = static_cast<int>(s.readyAnchor);
		j["readyOffsetX"] = s.readyOffsetX;
		j["readyOffsetY"] = s.readyOffsetY;
		j["readyShowPower"] = s.readyShowPower;
		j["readyVertical"] = s.readyVertical;
		j["readyHideMainBar"] = s.readyHideMainBar;
		return j;
	}

	void FromJson(const json& j, Settings& s)
	{
		if (!j.is_object()) {
			return;
		}
		Read(j, "slotCount", s.slotCount);
		Read(j, "columns", s.columns);
		Read(j, "iconSize", s.iconSize);
		Read(j, "spacing", s.spacing);
		Read(j, "anchor", s.anchor);
		Read(j, "offsetX", s.offsetX);
		Read(j, "offsetY", s.offsetY);
		Read(j, "opacity", s.opacity);
		Read(j, "visibility", s.visibility);
		Read(j, "keyPressShowTime", s.keyPressShowTime);
		Read(j, "fadeOutOfCombat", s.fadeOutOfCombat);
		Read(j, "fadedOpacity", s.fadedOpacity);
		Read(j, "showEmptySlots", s.showEmptySlots);
		Read(j, "showKeyLabels", s.showKeyLabels);
		Read(j, "showPageName", s.showPageName);
		Read(j, "showCooldownText", s.showCooldownText);
		Read(j, "showItemCount", s.showItemCount);
		Read(j, "showInMenus", s.showInMenus);
		if (const auto colors = j.find("colors"); colors != j.end() && colors->is_object()) {
			for (const auto& [key, member] : kColorFields) {
				ReadArray(*colors, key, s.colors.*member);
			}
		}

		ReadArray(j, "slotKeys", s.slotKeys);
		ReadArray(j, "modifierKeys", s.modifierKeys);
		ReadArray(j, "modifierEnabled", s.modifierEnabled);
		ReadArray(j, "modifierToggle", s.modifierToggle);
		Read(j, "mainBarKeyEnabled", s.mainBarKeyEnabled);
		Read(j, "mainBarKeyMode", s.mainBarKeyMode);
		Read(j, "mainBarKey", s.mainBarKey);
		Read(j, "mainBarModifier", s.mainBarModifier);
		Read(j, "blockGameInput", s.blockGameInput);
		Read(j, "onlyWhileSneaking", s.onlyWhileSneaking);
		Read(j, "aimAtCrosshair", s.aimAtCrosshair);
		Read(j, "individualShoutCooldowns", s.individualShoutCooldowns);

		Read(j, "keyMode", s.keyMode);
		Read(j, "castKey", s.castKey);
		Read(j, "potionKey", s.potionKey);
		Read(j, "readyAnchor", s.readyAnchor);
		Read(j, "readyOffsetX", s.readyOffsetX);
		Read(j, "readyOffsetY", s.readyOffsetY);
		Read(j, "readyShowPower", s.readyShowPower);
		Read(j, "readyVertical", s.readyVertical);
		Read(j, "readyHideMainBar", s.readyHideMainBar);
		Validate(s);
	}

	void Validate(Settings& s)
	{
		s.slotCount = std::clamp(s.slotCount, 1, kMaxSlots);
		s.columns = std::clamp(s.columns, 1, kMaxSlots);
		s.iconSize = std::clamp(s.iconSize, 16.0f, 200.0f);
		s.spacing = std::clamp(s.spacing, 0.0f, 100.0f);
		s.anchor = static_cast<Anchor>(std::clamp(static_cast<int>(s.anchor), 0, 8));
		s.opacity = std::clamp(s.opacity, 0.05f, 1.0f);
		s.visibility = static_cast<Visibility>(std::clamp(static_cast<int>(s.visibility), 0, 6));
		s.keyPressShowTime = std::clamp(s.keyPressShowTime, 0.5f, 10.0f);
		s.fadedOpacity = std::clamp(s.fadedOpacity, 0.0f, 1.0f);
		s.keyMode = static_cast<KeyMode>(std::clamp(static_cast<int>(s.keyMode), 0, 2));
		s.mainBarKeyMode = static_cast<MainBarKeyMode>(std::clamp(static_cast<int>(s.mainBarKeyMode), 0, 2));
		s.readyAnchor = static_cast<Anchor>(std::clamp(static_cast<int>(s.readyAnchor), 0, 8));
		for (const auto& [key, member] : kColorFields) {
			for (auto& channel : s.colors.*member) {
				channel = std::clamp(channel, 0.0f, 1.0f);
			}
		}
	}

	void Load()
	{
		std::scoped_lock lock(Lock());
		if (auto j = ReadJsonFile(SettingsPath())) {
			FromJson(j->value("settings", json::object()), settings);
			Lang::Set(j->value("language", "english"));  // outside "settings": profiles and resets keep the language
			const int version = j->value("version", 1);
			if (version < 3) {
				settings.modifierEnabled.fill(false);
			}
			if (version < kFileVersion) {
				MarkDirty();  // rewrite without the removed options
			}
			logs::info("Loaded settings from {}", ToUtf8(SettingsPath()));
		} else {
			logs::info("No settings file yet, using defaults");
			Lang::Set("english");
		}
		Hotkeys::Read(settings);
		Hotkeys::Write(settings);  // there before hotkey tools scan (Hotkey Atlas: after kDataLoaded)
	}

	void Save()
	{
		std::scoped_lock lock(Lock());
		json j;
		j["version"] = kFileVersion;
		j["settings"] = ToJson(settings);
		j["language"] = Lang::Current().id;
		if (WriteJsonFile(SettingsPath(), j)) {
			dirty = false;
		}
		Hotkeys::Write(settings);
	}

	void MarkDirty()
	{
		dirty = true;
		dirtySince = std::chrono::steady_clock::now();
	}

	void SaveIfDirty()
	{
		if (dirty && std::chrono::steady_clock::now() - dirtySince > 1s) {
			Save();
		}
	}

	void ResetToDefaults()
	{
		std::scoped_lock lock(Lock());
		settings = Settings{};
		MarkDirty();
	}

	const char* PageName(Page a_page)
	{
		switch (a_page) {
		case Page::kMain:
			return "Main";
		case Page::kModifier1:
			return "Modifier 1";
		case Page::kModifier2:
			return "Modifier 2";
		case Page::kModifier3:
			return "Modifier 3";
		default:
			return "?";
		}
	}
}

namespace Profiles
{
	namespace
	{
		// Names are UTF-8 (typed in the menu); bytes >= 0x80 are parts of non-English letters
		std::string SanitizeName(const std::string& a_name)
		{
			std::string out;
			for (const char c : a_name) {
				const auto u = static_cast<unsigned char>(c);
				if (u >= 0x80 || std::isalnum(u) || c == ' ' || c == '-' || c == '_' || c == '.' || c == '(' || c == ')') {
					out.push_back(c);
				}
			}
			while (!out.empty() && (out.back() == ' ' || out.back() == '.')) {
				out.pop_back();
			}
			return out;
		}
	}

	std::filesystem::path Dir()
	{
		return Config::DataDir() / "profiles";
	}

	std::vector<Entry> List()
	{
		std::vector<Entry> out;
		std::error_code    ec;
		if (!std::filesystem::is_directory(Dir(), ec)) {
			return out;
		}
		for (const auto& file : std::filesystem::directory_iterator(Dir(), ec)) {
			if (file.is_regular_file() && file.path().extension() == ".json") {
				out.push_back({ ToUtf8(file.path().stem()), file.path() });
			}
		}
		std::ranges::sort(out, {}, &Entry::name);
		return out;
	}

	bool Save(const std::string& a_name, bool a_includeBindings)
	{
		const auto name = SanitizeName(a_name);
		if (name.empty()) {
			return false;
		}
		std::scoped_lock lock(Config::Lock());
		json j;
		j["version"] = 1;
		j["settings"] = Config::ToJson(Config::Get());
		if (a_includeBindings) {
			j["bindings"] = Bindings::ToJson();
		}
		if (!Config::WriteJsonFile(Dir() / FromUtf8(name + ".json"), j)) {
			return false;
		}
		logs::info("Saved profile '{}' (bindings: {})", name, a_includeBindings);
		return true;
	}

	bool Load(const Entry& a_entry, bool a_loadBindings)
	{
		const auto j = Config::ReadJsonFile(a_entry.path);
		if (!j) {
			return false;
		}

		std::scoped_lock lock(Config::Lock());
		Settings loaded{};
		Config::FromJson(j->value("settings", json::object()), loaded);
		Config::Get() = loaded;
		if (a_loadBindings && j->contains("bindings")) {
			Bindings::FromJson((*j)["bindings"]);
		}
		Config::Save();
		logs::info("Loaded profile '{}'", a_entry.name);
		return true;
	}

	bool Delete(const Entry& a_entry)
	{
		std::error_code ec;
		return std::filesystem::remove(a_entry.path, ec);
	}
}
