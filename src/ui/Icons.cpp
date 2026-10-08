#include "ui/Icons.h"

#include "core/Config.h"
#include "ui/InventoryIcons.h"
#include "ui/ItemIcons.h"
#include "core/Util.h"

namespace Icons
{
	namespace
	{
		std::vector<std::string>                     atlases;
		std::unordered_map<RE::FormID, Icon>         byForm;
		std::unordered_map<std::string, Icon>        byName;     // "name:<normalized item name>"
		std::unordered_map<std::string, Icon>        named;      // "BAR_EMPTY", ...
		std::unordered_map<std::string, Icon>        namedNordic;
		std::unordered_map<RE::FormID, Icon>         cache;      // resolved results incl. fallbacks
		IconStyle                                    cacheStyle{ IconStyle::kOwn };

		// icons the player picked: every atlas key (also of plugins that aren't loaded) and the choices file
		std::unordered_map<std::string, Icon>        byKey;
		std::vector<Choice>                          choices;
		std::map<std::string, std::string>           customs;        // "Plugin.esp|0xID" of the form -> icon key
		std::unordered_map<RE::FormID, std::string>  customByForm;   // the same for loaded forms

		// UI pieces in the icon lists that aren't pictures for a spell
		constexpr std::string_view kNotChoices[] = { "@BAR_EMPTY", "@BAR_OVERLAY", "@BAR_HIGHLIGHT", "@SCROLL_OVERLAY" };

		std::filesystem::path CustomPath()
		{
			return Config::DataDir() / "custom_icons.json";
		}

		// "DESTRUCTION_FIRE_NOVICE" -> "Destruction Fire Novice"
		std::string PrettyName(std::string_view a_name)
		{
			std::string out;
			bool        wordStart = true;
			for (const char c : a_name) {
				if (c == '_') {
					out += ' ';
					wordStart = true;
					continue;
				}
				out += wordStart ? c : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				wordStart = false;
			}
			return out;
		}

		std::vector<std::string_view> Split(std::string_view a_line, char a_sep)
		{
			std::vector<std::string_view> out;
			std::size_t                   start = 0;
			while (true) {
				const auto pos = a_line.find(a_sep, start);
				out.push_back(a_line.substr(start, pos == std::string_view::npos ? std::string_view::npos : pos - start));
				if (pos == std::string_view::npos) {
					break;
				}
				start = pos + 1;
			}
			return out;
		}

		float ToFloat(std::string_view a_text)
		{
			float value = 0.0f;
			std::from_chars(a_text.data(), a_text.data() + a_text.size(), value);
			return value;
		}

		// Vanilla atlases first so spell packs can override vanilla forms (e.g. Mysticism)
		int LoadOrder(const std::filesystem::path& a_path)
		{
			const auto stem = a_path.stem().string();
			if (stem.starts_with("ui")) {
				return 0;
			}
			if (stem.starts_with("vanilla_")) {
				return 1;
			}
			return 2;
		}

		void LoadAtlas(const std::filesystem::path& a_csv)
		{
			auto dds = a_csv;
			dds.replace_extension(".dds");
			std::error_code ec;
			if (!std::filesystem::exists(dds, ec)) {
				logs::warn("Icon list {} has no texture next to it", a_csv.string());
				return;
			}

			std::ifstream file(a_csv);
			if (!file) {
				return;
			}

			const int  atlas = static_cast<int>(atlases.size());
			const bool nordic = a_csv.stem() == "ui_nordic";
			atlases.push_back(dds.string());
			std::set<std::pair<float, float>> pictures;  // one choice per picture, several forms often share one

			std::string line;
			std::getline(file, line);  // header
			int forms = 0, missing = 0;
			while (std::getline(file, line)) {
				if (!line.empty() && line.back() == '\r') {
					line.pop_back();
				}
				const auto cols = Split(line, '\t');
				if (cols.size() < 5 || cols[0].empty()) {
					continue;
				}
				const Icon icon{ atlas, ToFloat(cols[1]), ToFloat(cols[2]), ToFloat(cols[3]), ToFloat(cols[4]) };
				const auto key = cols[0];
				if (!nordic) {
					byKey[std::string(key)] = icon;
					if (std::ranges::find(kNotChoices, key) == std::end(kNotChoices) && pictures.emplace(icon.u0, icon.v0).second) {
						const auto name = cols.size() > 5 && !cols[5].empty() ? cols[5] : key;
						choices.push_back({ std::string(key), key.starts_with('@') ? PrettyName(name) : std::string(name),
							a_csv.stem().string(), icon });
					}
				}
				if (key.starts_with('@')) {
					(nordic ? namedNordic : named)[std::string(key.substr(1))] = icon;
				} else if (key.starts_with("name:")) {
					byName[std::string(key)] = icon;
				} else if (const auto form = Util::FromPluginKey(key)) {
					byForm[form] = icon;
					++forms;
				} else {
					++missing;
				}
			}
			logs::info("Icons {}: {} forms ({} from plugins that are not loaded)", a_csv.filename().string(), forms, missing);
		}

		const char* LevelName(std::int32_t a_minSkill)
		{
			if (a_minSkill < 25) {
				return "NOVICE";
			}
			if (a_minSkill < 50) {
				return "APPRENTICE";
			}
			if (a_minSkill < 75) {
				return "ADEPT";
			}
			if (a_minSkill < 100) {
				return "EXPERT";
			}
			return "MASTER";
		}

		Icon MagicFallback(RE::MagicItem* a_item)
		{
			const auto effect = a_item->GetCostliestEffectItem();
			const auto base = effect ? effect->baseEffect : nullptr;
			if (!base) {
				return Named("UNKNOWN");
			}
			const auto level = LevelName(base->GetMinimumSkillLevel());
			const auto hostile = base->IsHostile() ? "HOSTILE" : "FRIENDLY";
			switch (base->GetMagickSkill()) {
			case RE::ActorValue::kDestruction:
				{
					const char* element = "GENERIC";
					switch (base->data.resistVariable) {
					case RE::ActorValue::kResistFire:
						element = "FIRE";
						break;
					case RE::ActorValue::kResistFrost:
						element = "FROST";
						break;
					case RE::ActorValue::kResistShock:
						element = "SHOCK";
						break;
					default:
						break;
					}
					return Named(std::format("DESTRUCTION_{}_{}", element, level));
				}
			case RE::ActorValue::kAlteration:
				return Named(std::format("ALTERATION_{}", level));
			case RE::ActorValue::kRestoration:
				return Named(std::format("RESTORATION_{}_{}", hostile, level));
			case RE::ActorValue::kIllusion:
				return Named(std::format("ILLUSION_{}_{}", hostile, level));
			case RE::ActorValue::kConjuration:
				return Named(std::format("CONJURATION_{}_{}",
					base->HasArchetype(RE::EffectSetting::Archetype::kBoundWeapon) ? "BOUND_WEAPON" : "SUMMON", level));
			default:
				return Named("UNKNOWN");
			}
		}

		// SkyUI's icon if the icon style wants one for this form; a_final = false while it's still being drawn
		Icon InventoryIcon(RE::TESForm* a_form, IconStyle a_style, bool& a_final)
		{
			if (a_style == IconStyle::kOwn || (a_style == IconStyle::kInventoryItems && !InventoryIcons::IsItem(a_form)) ||
				InventoryIcons::MoviePath().empty()) {
				return {};
			}
			const auto look = InventoryIcons::For(a_form);
			if (!look) {
				return {};
			}
			bool pending = false;
			if (const auto texture = ItemIcons::MenuIcon(look->label, pending)) {
				return Icon{ .texture = texture, .rgb = look->rgb };
			}
			a_final = !pending;
			return {};
		}

		Icon Fallback(RE::TESForm* a_form)
		{
			switch (a_form->GetFormType()) {
			case RE::FormType::Spell:
				{
					const auto spell = a_form->As<RE::SpellItem>();
					switch (spell->GetSpellType()) {
					case RE::MagicSystem::SpellType::kLesserPower:
						return Named("LESSER_POWER");
					case RE::MagicSystem::SpellType::kPower:
						return Named("GREATER_POWER");
					case RE::MagicSystem::SpellType::kVoicePower:
						return Named("SHOUT_GENERIC");
					default:
						return MagicFallback(spell);
					}
				}
			case RE::FormType::Scroll:
				return MagicFallback(a_form->As<RE::MagicItem>());
			case RE::FormType::Shout:
				return Named("SHOUT_GENERIC");
			case RE::FormType::AlchemyItem:
				{
					const auto alch = a_form->As<RE::AlchemyItem>();
					if (alch->IsFood()) {
						const auto it = byName.find("name:" + Util::NormalizeName(alch->GetName()));
						return it != byName.end() ? it->second : Named("GENERIC_FOOD");
					}
					const auto value = alch->GetGoldValue();
					const auto size = value <= 30 ? "_SMALL" : value >= 150 ? "_LARGE" : "";
					return Named(std::format("{}{}", alch->IsPoison() ? "GENERIC_POISON" : "GENERIC_POTION", size));
				}
			default:
				return Named("UNKNOWN");
			}
		}
	}

	void Load()
	{
		atlases.clear();
		byForm.clear();
		byName.clear();
		named.clear();
		namedNordic.clear();
		cache.clear();
		byKey.clear();
		choices.clear();
		customs.clear();
		customByForm.clear();

		const auto dir = Config::DataDir() / "icons";
		std::error_code ec;
		if (!std::filesystem::is_directory(dir, ec)) {
			logs::error("Icon folder {} is missing", dir.string());
			return;
		}

		std::vector<std::filesystem::path> lists;
		for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
			if (entry.is_regular_file() && entry.path().extension() == ".csv") {
				lists.push_back(entry.path());
			}
		}
		std::ranges::sort(lists, [](const auto& a, const auto& b) {
			const auto oa = LoadOrder(a), ob = LoadOrder(b);
			return oa != ob ? oa < ob : a.filename() < b.filename();
		});
		for (const auto& list : lists) {
			LoadAtlas(list);
		}
		logs::info("Icon database: {} atlases, {} forms, {} named, {} pictures", atlases.size(), byForm.size(), named.size(), choices.size());

		if (const auto j = Config::ReadJsonFile(CustomPath()); j && j->contains("icons") && (*j)["icons"].is_object()) {
			for (const auto& [formKey, iconKey] : (*j)["icons"].items()) {
				if (iconKey.is_string()) {
					customs[formKey] = iconKey.get<std::string>();
				}
			}
		}
		for (const auto& [formKey, iconKey] : customs) {
			if (const auto form = Util::FromPluginKey(formKey)) {
				customByForm[form] = iconKey;
			}
		}
		if (!customs.empty()) {
			logs::info("{} icon(s) picked by the player ({} for loaded forms)", customs.size(), customByForm.size());
		}
	}

	const std::string& AtlasPath(int a_atlas)
	{
		static const std::string empty;
		return a_atlas >= 0 && a_atlas < static_cast<int>(atlases.size()) ? atlases[a_atlas] : empty;
	}

	int AtlasCount()
	{
		return static_cast<int>(atlases.size());
	}

	Icon ForForm(RE::TESForm* a_form)
	{
		if (!a_form) {
			return Named("UNKNOWN");
		}
		const auto style = Config::Get().iconStyle;
		if (style != cacheStyle) {
			cache.clear();
			cacheStyle = style;
		}
		const auto id = a_form->GetFormID();
		if (const auto it = cache.find(id); it != cache.end()) {
			return it->second;
		}
		Icon icon;
		bool final = true;  // false: show this for now, ask again next frame
		if (const auto custom = customByForm.find(id); custom != customByForm.end() && byKey.contains(custom->second)) {
			icon = byKey[custom->second];
		} else if (const auto inventory = InventoryIcon(a_form, style, final); inventory.Valid()) {
			icon = inventory;
		} else if (const auto it = byForm.find(id); it != byForm.end()) {
			icon = it->second;
		} else if (ItemIcons::Supports(a_form)) {
			// not cached: the model picture shows up once it's made
			if (const auto texture = ItemIcons::Get(a_form)) {
				icon = Icon{ .texture = texture };
				cache[id] = icon;
				return icon;
			}
			return Named("UNKNOWN");
		} else {
			icon = Fallback(a_form);
		}
		if (final) {
			cache[id] = icon;
		}
		return icon;
	}

	Icon Named(std::string_view a_name, bool a_nordic)
	{
		const std::string key(a_name);
		if (a_nordic) {
			if (const auto it = namedNordic.find(key); it != namedNordic.end()) {
				return it->second;
			}
		}
		if (const auto it = named.find(key); it != named.end()) {
			return it->second;
		}
		if (key != "UNKNOWN") {
			return Named("UNKNOWN");
		}
		return {};
	}

	const std::vector<Choice>& Choices()
	{
		return choices;
	}

	bool HasOwnIcon(RE::TESForm* a_form)
	{
		return a_form && (byForm.contains(a_form->GetFormID()) || ItemIcons::Supports(a_form));
	}

	std::string CustomKey(RE::TESForm* a_form)
	{
		const auto it = a_form ? customByForm.find(a_form->GetFormID()) : customByForm.end();
		return it != customByForm.end() ? it->second : std::string{};
	}

	void SetCustom(RE::TESForm* a_form, std::string_view a_key)
	{
		const auto formKey = a_form ? Util::ToPluginKey(a_form->GetFormID()) : std::string{};
		if (formKey.empty()) {
			return;  // made in game (0xFF...): no stable id to save it under
		}
		const auto id = a_form->GetFormID();
		if (a_key.empty()) {
			customs.erase(formKey);
			customByForm.erase(id);
		} else {
			customs[formKey] = a_key;
			customByForm[id] = a_key;
		}
		cache.erase(id);
		logs::info("Icon of {} ({}): {}", a_form->GetName(), formKey, a_key.empty() ? "own icon" : a_key);

		json icons = json::object();
		for (const auto& [form, icon] : customs) {
			icons[form] = icon;
		}
		Config::WriteJsonFile(CustomPath(), json{ { "version", 1 }, { "icons", icons } });
	}
}
