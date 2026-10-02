#include "ui/Icons.h"

#include "core/Config.h"
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
		logs::info("Icon database: {} atlases, {} forms, {} named", atlases.size(), byForm.size(), named.size());
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
		const auto id = a_form->GetFormID();
		if (const auto it = cache.find(id); it != cache.end()) {
			return it->second;
		}
		Icon icon;
		if (const auto it = byForm.find(id); it != byForm.end()) {
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
		cache[id] = icon;
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
}
