#include "ui/InventoryIcons.h"

namespace InventoryIcons
{
	namespace
	{
		// SkyUI's colors (InventoryIconSetter / MagicIconSetter)
		constexpr std::uint32_t kWhite = 0xFFFFFF;
		constexpr std::uint32_t kFire = 0xC73636;
		constexpr std::uint32_t kShockScroll = 0xFFFF00;
		constexpr std::uint32_t kShock = 0xEAAB00;
		constexpr std::uint32_t kFrost = 0x1FFBFF;

		// Armor slots by importance (SkyUI Armor.PARTMASK_PRECEDENCE)
		using Slot = RE::BIPED_MODEL::BipedObjectSlot;
		constexpr Slot kSlotPrecedence[] = { Slot::kBody, Slot::kHair, Slot::kHands, Slot::kForearms, Slot::kFeet, Slot::kCalves,
			Slot::kShield, Slot::kAmulet, Slot::kRing, Slot::kLongHair, Slot::kEars, Slot::kHead, Slot::kCirclet, Slot::kTail };

		enum class Part
		{
			kOther,
			kHead,
			kBody,
			kHands,
			kForearms,
			kFeet,
			kCalves,
			kShield,
			kAmulet,
			kRing,
			kCirclet,
			kEars
		};

		Part MainPart(const RE::TESObjectARMO* a_armor)
		{
			const auto mask = a_armor->GetSlotMask();
			for (const auto slot : kSlotPrecedence) {
				if (!mask.any(slot)) {
					continue;
				}
				switch (slot) {
				case Slot::kHead:
				case Slot::kHair:
				case Slot::kLongHair:
					return Part::kHead;
				case Slot::kBody:
				case Slot::kTail:
					return Part::kBody;
				case Slot::kHands:
					return Part::kHands;
				case Slot::kForearms:
					return Part::kForearms;
				case Slot::kFeet:
					return Part::kFeet;
				case Slot::kCalves:
					return Part::kCalves;
				case Slot::kShield:
					return Part::kShield;
				case Slot::kAmulet:
					return Part::kAmulet;
				case Slot::kRing:
					return Part::kRing;
				case Slot::kCirclet:
					return Part::kCirclet;
				case Slot::kEars:
					return Part::kEars;
				default:
					return Part::kOther;
				}
			}
			return Part::kOther;
		}

		std::uint32_t BaseId(const RE::TESForm* a_form)
		{
			return a_form->GetFormID() & 0x00FFFFFF;
		}

		// empty for parts that aren't jewelry
		std::string_view JewelryLabel(Part a_part)
		{
			switch (a_part) {
			case Part::kAmulet:
				return "armor_amulet";
			case Part::kRing:
				return "armor_ring";
			case Part::kCirclet:
				return "armor_circlet";
			default:
				return {};
			}
		}

		// "<prefix>_<part>", empty for parts without an icon; a_prefix: "lightarmor", "armor" or "clothing"
		std::string PieceLabel(Part a_part, std::string_view a_prefix)
		{
			std::string_view piece;
			switch (a_part) {
			case Part::kHead:
				piece = "head";
				break;
			case Part::kBody:
				piece = "body";
				break;
			case Part::kHands:
				piece = "hands";
				break;
			case Part::kForearms:
				piece = "forearms";
				break;
			case Part::kFeet:
				piece = "feet";
				break;
			case Part::kCalves:
				piece = "calves";
				break;
			case Part::kShield:
				piece = "shield";
				break;
			default:
				return {};
			}
			return std::format("{}_{}", a_prefix, piece);
		}

		Look Armor(RE::TESObjectARMO* a_armor)
		{
			enum class Weight
			{
				kLight,
				kHeavy,
				kClothing,
				kJewelry,
				kNone
			};
			auto weight = a_armor->IsLightArmor() ? Weight::kLight : a_armor->IsHeavyArmor() ? Weight::kHeavy : Weight::kNone;
			if (weight == Weight::kNone) {
				if (a_armor->HasKeywordString("VendorItemClothing")) {
					weight = Weight::kClothing;
				} else if (a_armor->HasKeywordString("VendorItemJewelry")) {
					weight = Weight::kJewelry;
				}
			}
			auto part = MainPart(a_armor);
			if (weight == Weight::kNone) {
				switch (part) {
				case Part::kAmulet:
				case Part::kRing:
				case Part::kCirclet:
				case Part::kEars:
					weight = Weight::kJewelry;
					break;
				default:
					weight = Weight::kClothing;
					break;
				}
			}
			switch (BaseId(a_armor)) {
			case 0x08895A:  // ClothesWeddingWreath
				weight = Weight::kJewelry;
				break;
			case 0x011A84:  // DLC1ClothesVampireLordArmor
				part = Part::kBody;
				break;
			default:
				break;
			}

			// light / heavy jewelry gets the jewelry icon in the armor color
			const auto armorPiece = [&](std::string_view a_prefix) {
				const auto jewelry = JewelryLabel(part);
				return jewelry.empty() ? PieceLabel(part, a_prefix) : std::string(jewelry);
			};
			Look        look{ "default_armor", 0xEDDA87 };
			std::string label;
			switch (weight) {
			case Weight::kLight:
				look.rgb = 0x756000;
				label = armorPiece("lightarmor");
				break;
			case Weight::kHeavy:
				look.rgb = 0x6B7585;
				label = armorPiece("armor");
				break;
			case Weight::kJewelry:
				label = JewelryLabel(part);
				break;
			default:
				label = PieceLabel(part, "clothing");
				break;
			}
			if (!label.empty()) {
				look.label = std::move(label);
			}
			return look;
		}

		Look Weapon(RE::TESObjectWEAP* a_weapon)
		{
			Look look{ "default_weapon", 0xA4A5BF };
			switch (a_weapon->GetWeaponType()) {
			case RE::WEAPON_TYPE::kOneHandSword:
				look.label = "weapon_sword";
				break;
			case RE::WEAPON_TYPE::kOneHandDagger:
				look.label = "weapon_dagger";
				break;
			case RE::WEAPON_TYPE::kOneHandAxe:
				look.label = "weapon_waraxe";
				break;
			case RE::WEAPON_TYPE::kOneHandMace:
				look.label = "weapon_mace";
				break;
			case RE::WEAPON_TYPE::kTwoHandSword:
				look.label = "weapon_greatsword";
				break;
			case RE::WEAPON_TYPE::kTwoHandAxe:
				look.label = a_weapon->HasKeywordString("WeapTypeWarhammer") ? "weapon_hammer" : "weapon_battleaxe";
				break;
			case RE::WEAPON_TYPE::kBow:
				look.label = "weapon_bow";
				break;
			case RE::WEAPON_TYPE::kStaff:
				look.label = "weapon_staff";
				break;
			case RE::WEAPON_TYPE::kCrossbow:
				look.label = "weapon_crossbow";
				break;
			default:
				break;
			}
			switch (BaseId(a_weapon)) {
			case 0x0E3C16:  // WeapPickaxe
			case 0x06A707:  // SSDRocksplinterPickaxe
			case 0x1019D4:  // dunVolunruudPickaxe
				look.label = "weapon_pickaxe";
				break;
			case 0x02F2F4:  // Axe01
			case 0x0AE086:  // dunHaltedStreamPoachersAxe
				look.label = "weapon_woodaxe";
				break;
			default:
				break;
			}
			return look;
		}

		RE::EffectSetting* CostliestEffect(RE::MagicItem* a_item)
		{
			const auto effect = a_item->GetCostliestEffectItem();
			return effect ? effect->baseEffect : nullptr;
		}

		Look Potion(RE::AlchemyItem* a_potion)
		{
			if (a_potion->IsFood()) {
				constexpr RE::FormID kITMPotionUse = 0x000B6435;
				const auto           sound = a_potion->data.consumptionSound;
				return sound && sound->GetFormID() == kITMPotionUse ? Look{ "food_wine" } : Look{ "default_food" };
			}
			if (a_potion->IsPoison()) {
				return { "potion_poison", 0xAD00B3 };
			}
			const auto effect = CostliestEffect(a_potion);
			switch (effect ? effect->data.primaryAV : RE::ActorValue::kNone) {
			case RE::ActorValue::kHealth:
			case RE::ActorValue::kHealRate:
			case RE::ActorValue::kHealRateMult:
				return { "potion_health", 0xDB2E73 };
			case RE::ActorValue::kMagicka:
			case RE::ActorValue::kMagickaRate:
			case RE::ActorValue::kMagickaRateMult:
				return { "potion_magic", 0x2E9FDB };
			case RE::ActorValue::kStamina:
			case RE::ActorValue::kStaminaRate:
			case RE::ActorValue::kStaminaRateMult:
				return { "potion_stam", 0x51DB2E };
			case RE::ActorValue::kResistFire:
				return { "potion_fire", kFire };
			case RE::ActorValue::kResistShock:
				return { "potion_shock", kShock };
			case RE::ActorValue::kResistFrost:
				return { "potion_frost", kFrost };
			default:
				return { "default_potion" };
			}
		}

		// Magic menu: school icons, destruction by element
		Look Spell(RE::MagicItem* a_spell)
		{
			const auto effect = CostliestEffect(a_spell);
			if (!effect) {
				return { "default_power" };
			}
			switch (effect->GetMagickSkill()) {
			case RE::ActorValue::kAlteration:
				return { "default_alteration" };
			case RE::ActorValue::kConjuration:
				return { "default_conjuration" };
			case RE::ActorValue::kIllusion:
				return { "default_illusion" };
			case RE::ActorValue::kRestoration:
				return { "default_restoration" };
			case RE::ActorValue::kDestruction:
				switch (effect->data.resistVariable) {
				case RE::ActorValue::kResistFire:
					return { "magic_fire", kFire };
				case RE::ActorValue::kResistShock:
					return { "magic_shock", kShock };
				case RE::ActorValue::kResistFrost:
					return { "magic_frost", kFrost };
				default:
					return { "default_destruction" };
				}
			default:
				return { "default_power" };
			}
		}

		Look Scroll(RE::ScrollItem* a_scroll)
		{
			const auto effect = CostliestEffect(a_scroll);
			switch (effect ? effect->data.resistVariable : RE::ActorValue::kNone) {
			case RE::ActorValue::kResistFire:
				return { "default_scroll", kFire };
			case RE::ActorValue::kResistShock:
				return { "default_scroll", kShockScroll };
			case RE::ActorValue::kResistFrost:
				return { "default_scroll", kFrost };
			default:
				return { "default_scroll" };
			}
		}

		std::string Trim(std::string_view a_text)
		{
			const auto first = a_text.find_first_not_of(" \t\r\n");
			if (first == std::string_view::npos) {
				return {};
			}
			const auto last = a_text.find_last_not_of(" \t\r\n");
			return std::string(a_text.substr(first, last - first + 1));
		}

		// A file from loose files or archives, empty if missing
		std::string ReadGameFile(const std::string& a_path)
		{
			RE::BSResourceNiBinaryStream stream(a_path);
			if (!stream.good()) {
				return {};
			}
			std::string text;
			char        c;
			while (stream.get(c)) {
				text += c;
			}
			return text;
		}

		bool GameFileExists(const std::string& a_path)
		{
			RE::BSResourceNiBinaryStream stream(a_path);
			return stream.good();
		}

		struct SkyUIConfig
		{
			std::string movie;  // under Interface, without ".swf"; empty if missing
			bool        noColor{ false };
		};

		const SkyUIConfig& Config()
		{
			static const SkyUIConfig config = [] {
				SkyUIConfig out;
				std::string source = "skyui/icons_item_psychosteve.swf";
				const auto  text = ReadGameFile("Interface/skyui/config.txt");
				std::size_t start = 0;
				while (start < text.size()) {
					auto end = text.find('\n', start);
					if (end == std::string::npos) {
						end = text.size();
					}
					auto line = std::string_view(text).substr(start, end - start);
					start = end + 1;
					if (const auto comment = line.find(';'); comment != std::string_view::npos) {
						line = line.substr(0, comment);
					}
					const auto eq = line.find('=');
					if (eq == std::string_view::npos) {
						continue;
					}
					const auto key = Trim(line.substr(0, eq));
					auto       value = Trim(line.substr(eq + 1));
					if (value.size() >= 2 && (value.front() == '\'' || value.front() == '"') && value.back() == value.front()) {
						value = value.substr(1, value.size() - 2);
					}
					if (key == "icons.item.source") {
						source = value;
					} else if (key == "icons.item.noColor") {
						out.noColor = value == "true";
					}
				}
				std::ranges::replace(source, '\\', '/');
				if (!text.empty() && GameFileExists("Interface/" + source)) {
					out.movie = source.ends_with(".swf") ? source.substr(0, source.size() - 4) : source;
					logs::info("Inventory icons: SkyUI icon movie Interface/{}.swf{}", out.movie, out.noColor ? " (no colors)" : "");
				} else {
					logs::info("Inventory icons: SkyUI not found");
				}
				return out;
			}();
			return config;
		}
	}

	bool IsItem(const RE::TESForm* a_form)
	{
		if (!a_form) {
			return false;
		}
		switch (a_form->GetFormType()) {
		case RE::FormType::Weapon:
		case RE::FormType::Armor:
		case RE::FormType::Ammo:
		case RE::FormType::Light:
		case RE::FormType::AlchemyItem:
			return true;
		default:
			return false;
		}
	}

	const std::string& MoviePath()
	{
		return Config().movie;
	}

	std::optional<Look> For(RE::TESForm* a_form)
	{
		if (!a_form) {
			return std::nullopt;
		}
		Look look;
		switch (a_form->GetFormType()) {
		case RE::FormType::Weapon:
			look = Weapon(a_form->As<RE::TESObjectWEAP>());
			break;
		case RE::FormType::Armor:
			look = Armor(a_form->As<RE::TESObjectARMO>());
			break;
		case RE::FormType::Ammo:
			look = { a_form->As<RE::TESAmmo>()->IsBolt() ? "weapon_bolt" : "weapon_arrow", 0xA89E8C };
			break;
		case RE::FormType::Light:
			look = { "misc_torch" };
			break;
		case RE::FormType::AlchemyItem:
			look = Potion(a_form->As<RE::AlchemyItem>());
			break;
		case RE::FormType::Scroll:
			look = Scroll(a_form->As<RE::ScrollItem>());
			break;
		case RE::FormType::Shout:
			look = { "default_shout" };
			break;
		case RE::FormType::Spell:
			{
				const auto spell = a_form->As<RE::SpellItem>();
				look = spell->GetSpellType() == RE::MagicSystem::SpellType::kSpell ? Spell(spell) : Look{ "default_power" };
				break;
			}
		default:
			return std::nullopt;
		}
		if (Config().noColor) {
			look.rgb = kWhite;
		}
		return look;
	}
}
