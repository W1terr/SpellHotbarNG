#include "ui/InventoryIcons.h"

#include "core/Util.h"

namespace InventoryIcons
{
	namespace
	{
		// ---- item data -------------------------------------------------------------------------------
		// What the menus' list entry of an item holds: SKSE's extended data, SkyUI's data setters, I4's extras.
		// Members missing from the entry are undefined.

		struct Undefined
		{
			bool operator==(const Undefined&) const = default;
		};
		using Keywords = std::shared_ptr<const std::unordered_set<std::string>>;  // a keyword object: editor ids as members
		using Value = std::variant<Undefined, std::nullptr_t, double, bool, std::string, Keywords>;
		using Entry = std::unordered_map<std::string, Value>;

		const Value& Member(const Entry& a_entry, const std::string& a_name)
		{
			static const Value undefined{};
			const auto         it = a_entry.find(a_name);
			return it != a_entry.end() ? it->second : undefined;
		}

		void SetMember(Entry& a_entry, const std::string& a_name, const Value& a_value)
		{
			if (std::holds_alternative<Undefined>(a_value)) {
				a_entry.erase(a_name);
			} else {
				a_entry[a_name] = a_value;
			}
		}

		std::optional<double> Number(const Entry& a_entry, const std::string& a_name)
		{
			const auto& value = Member(a_entry, a_name);
			return std::holds_alternative<double>(value) ? std::optional(std::get<double>(value)) : std::nullopt;
		}

		bool Is(const Entry& a_entry, const std::string& a_name, double a_value)
		{
			return Number(a_entry, a_name) == a_value;
		}

		template <class E>
		double Num(E a_enum)
		{
			return static_cast<double>(std::to_underlying(a_enum));
		}

		bool IEquals(std::string_view a_lhs, std::string_view a_rhs)
		{
			return std::ranges::equal(a_lhs, a_rhs, [](char a, char b) {
				return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
			});
		}

		// ---- SkyUI / I4 enumerations -----------------------------------------------------------------

		struct Named
		{
			std::string_view name;
			std::int64_t     value;
		};

		std::optional<std::int64_t> Find(std::span<const Named> a_table, std::string_view a_name)
		{
			for (const auto& [name, value] : a_table) {
				if (IEquals(name, a_name)) {
					return value;
				}
			}
			return std::nullopt;
		}

		template <class E>
		constexpr Named N(std::string_view a_name, E a_value)
		{
			return { a_name, static_cast<std::int64_t>(std::to_underlying(a_value)) };
		}

		using FT = RE::FormType;
		constexpr Named kFormTypes[]{ N("", FT::None), N("Ammo", FT::Ammo), N("Armor", FT::Armor), N("Book", FT::Book),
			N("Enchantment", FT::Enchantment), N("Ingredient", FT::Ingredient), N("Key", FT::KeyMaster), N("Light", FT::Light),
			N("MiscItem", FT::Misc), N("Potion", FT::AlchemyItem), N("Scroll", FT::Scroll), N("Spell", FT::Spell),
			N("SoulGem", FT::SoulGem), N("Weapon", FT::Weapon) };

		using ST = RE::MagicSystem::SpellType;
		constexpr Named kSpellTypes[]{ N("Spell", ST::kSpell), N("Disease", ST::kDisease), N("Power", ST::kPower),
			N("LesserPower", ST::kLesserPower), N("Ability", ST::kAbility), N("Poison", ST::kPoison), N("Enchantment", ST::kEnchantment),
			N("Potion", ST::kPotion), N("WortCraft", ST::kWortCraft), N("LeveledSpell", ST::kLeveledSpell), N("Addiction", ST::kAddiction),
			N("VoicePower", ST::kVoicePower), N("StaffEnchantment", ST::kStaffEnchantment), N("Scroll", ST::kScroll) };

		using DL = RE::MagicSystem::Delivery;
		constexpr Named kDeliveries[]{ N("Self", DL::kSelf), N("Touch", DL::kTouch), N("Aimed", DL::kAimed),
			N("TargetActor", DL::kTargetActor), N("TargetLocation", DL::kTargetLocation) };

		using CT = RE::MagicSystem::CastingType;
		constexpr Named kCastingTypes[]{ N("ConstantEffect", CT::kConstantEffect), N("FireAndForget", CT::kFireAndForget),
			N("Concentration", CT::kConcentration), N("Scroll", CT::kScroll) };

		using AR = RE::EffectArchetypes::ArchetypeID;
		constexpr Named kArchetypes[]{ N("ValueModifier", AR::kValueModifier), N("Script", AR::kScript), N("Dispel", AR::kDispel),
			N("CureDisease", AR::kCureDisease), N("Absorb", AR::kAbsorb), N("DualValueModifier", AR::kDualValueModifier),
			N("Calm", AR::kCalm), N("Demoralize", AR::kDemoralize), N("Frenzy", AR::kFrenzy), N("Disarm", AR::kDisarm),
			N("CommandSummoned", AR::kCommandSummoned), N("Invisibility", AR::kInvisibility), N("Light", AR::kLight),
			N("Darkness", AR::kDarkness), N("NightEye", AR::kNightEye), N("Lock", AR::kLock), N("Open", AR::kOpen),
			N("BoundWeapon", AR::kBoundWeapon), N("SummonCreature", AR::kSummonCreature), N("DetectLife", AR::kDetectLife),
			N("Telekinesis", AR::kTelekinesis), N("Paralysis", AR::kParalysis), N("Reanimate", AR::kReanimate),
			N("SoulTrap", AR::kSoulTrap), N("TurnUndead", AR::kTurnUndead), N("Guide", AR::kGuide), N("WerewolfFeed", AR::kWerewolfFeed),
			N("CureParalysis", AR::kCureParalysis), N("CureAddiction", AR::kCureAddiction), N("CurePoison", AR::kCurePoison),
			N("Concussion", AR::kConcussion), N("ValueAndParts", AR::kValueAndParts), N("AccumulateMagnitude", AR::kAccumulateMagnitude),
			N("Stagger", AR::kStagger), N("PeakValueModifier", AR::kPeakValueModifier), N("Cloak", AR::kCloak), N("Werewolf", AR::kWerewolf),
			N("SlowTime", AR::kSlowTime), N("Rally", AR::kRally), N("EnhanceWeapon", AR::kEnhanceWeapon), N("SpawnHazard", AR::kSpawnHazard),
			N("Etherealize", AR::kEtherealize), N("Banish", AR::kBanish), N("SpawnScriptedRef", AR::kSpawnScriptedRef),
			N("Disguise", AR::kDisguise), N("GrabActor", AR::kGrabActor), N("VampireLord", AR::kVampireLord) };

		using EF = RE::EffectSetting::EffectSettingData::Flag;
		constexpr Named kEffectFlags[]{ N("Hostile", EF::kHostile), N("Recover", EF::kRecover), N("Detrimental", EF::kDetrimental),
			N("SnapToNavMesh", EF::kSnapToNavMesh), N("NoHitEvent", EF::kNoHitEvent), N("DispelWithKeywords", EF::kDispelWithKeywords),
			N("NoDuration", EF::kNoDuration), N("NoArea", EF::kNoArea), N("FXPersist", EF::kFXPersist), N("GoryVisuals", EF::kGoryVisuals),
			N("HideInUI", EF::kHideInUI), N("NoRecast", EF::kNoRecast), N("PowerAffectsMagnitude", EF::kPowerAffectsMagnitude),
			N("PowerAffectsDuration", EF::kPowerAffectsDuration), N("Painless", EF::kPainless), N("NoHitEffect", EF::kNoHitEffect),
			N("NoDeathDispel", EF::kNoDeathDispel) };

		using AV = RE::ActorValue;
		constexpr Named kActorValues[]{ N("Aggression", AV::kAggression), N("Confidence", AV::kConfidence), N("Energy", AV::kEnergy),
			N("Morality", AV::kMorality), N("Mood", AV::kMood), N("Assistance", AV::kAssistance), N("OneHanded", AV::kOneHanded),
			N("TwoHanded", AV::kTwoHanded), N("Marksman", AV::kArchery), N("Block", AV::kBlock), N("Smithing", AV::kSmithing),
			N("HeavyArmor", AV::kHeavyArmor), N("LightArmor", AV::kLightArmor), N("Pickpocket", AV::kPickpocket),
			N("Lockpicking", AV::kLockpicking), N("Sneak", AV::kSneak), N("Alchemy", AV::kAlchemy), N("Speechcraft", AV::kSpeech),
			N("Alteration", AV::kAlteration), N("Conjuration", AV::kConjuration), N("Destruction", AV::kDestruction),
			N("Illusion", AV::kIllusion), N("Restoration", AV::kRestoration), N("Enchanting", AV::kEnchanting), N("Health", AV::kHealth),
			N("Magicka", AV::kMagicka), N("Stamina", AV::kStamina), N("HealRate", AV::kHealRate), N("MagickaRate", AV::kMagickaRate),
			N("StaminaRate", AV::kStaminaRate), N("SpeedMult", AV::kSpeedMult), N("InventoryWeight", AV::kInventoryWeight),
			N("CarryWeight", AV::kCarryWeight), N("CritChance", AV::kCriticalChance), N("MeleeDamage", AV::kMeleeDamage),
			N("UnarmedDamage", AV::kUnarmedDamage), N("Mass", AV::kMass), N("VoicePoints", AV::kVoicePoints), N("VoiceRate", AV::kVoiceRate),
			N("DamageResist", AV::kDamageResist), N("PoisonResist", AV::kPoisonResist), N("FireResist", AV::kResistFire),
			N("ElectricResist", AV::kResistShock), N("FrostResist", AV::kResistFrost), N("MagicResist", AV::kResistMagic),
			N("DiseaseResist", AV::kResistDisease), N("Paralysis", AV::kParalysis), N("Invisibility", AV::kInvisibility),
			N("NightEye", AV::kNightEye), N("DetectLifeRange", AV::kDetectLifeRange), N("WaterBreathing", AV::kWaterBreathing),
			N("WaterWalking", AV::kWaterWalking), N("JumpingBonus", AV::kJumpingBonus), N("WardPower", AV::kWardPower),
			N("WardDeflection", AV::kWardDeflection), N("AbsorbChance", AV::kAbsorbChance), N("Blindness", AV::kBlindness),
			N("WeaponSpeedMult", AV::kWeaponSpeedMult), N("ShoutRecoveryMult", AV::kShoutRecoveryMult),
			N("BowStaggerBonus", AV::kBowStaggerBonus), N("Telekinesis", AV::kTelekinesis), N("MovementNoiseMult", AV::kMovementNoiseMult),
			N("OneHandedMod", AV::kOneHandedModifier), N("TwoHandedMod", AV::kTwoHandedModifier), N("MarksmanMod", AV::kMarksmanModifier),
			N("BlockMod", AV::kBlockModifier), N("SmithingMod", AV::kSmithingModifier), N("HeavyArmorMod", AV::kHeavyArmorModifier),
			N("LightArmorMod", AV::kLightArmorModifier), N("PickpocketMod", AV::kPickpocketModifier),
			N("LockpickingMod", AV::kLockpickingModifier), N("SneakMod", AV::kSneakingModifier), N("AlchemyMod", AV::kAlchemyModifier),
			N("SpeechcraftMod", AV::kSpeechcraftModifier), N("AlterationMod", AV::kAlterationModifier),
			N("ConjurationMod", AV::kConjurationModifier), N("DestructionMod", AV::kDestructionModifier),
			N("IllusionMod", AV::kIllusionModifier), N("RestorationMod", AV::kRestorationModifier), N("EnchantingMod", AV::kEnchantingModifier),
			N("OneHandedPowerMod", AV::kOneHandedPowerModifier), N("TwoHandedPowerMod", AV::kTwoHandedPowerModifier),
			N("MarksmanPowerMod", AV::kMarksmanPowerModifier), N("BlockPowerMod", AV::kBlockPowerModifier),
			N("SmithingPowerMod", AV::kSmithingPowerModifier), N("HeavyArmorPowerMod", AV::kHeavyArmorPowerModifier),
			N("LightArmorPowerMod", AV::kLightArmorPowerModifier), N("PickpocketPowerMod", AV::kPickpocketPowerModifier),
			N("LockpickingPowerMod", AV::kLockpickingPowerModifier), N("SneakPowerMod", AV::kSneakingPowerModifier),
			N("AlchemyPowerMod", AV::kAlchemyPowerModifier), N("SpeechcraftPowerMod", AV::kSpeechcraftPowerModifier),
			N("AlterationPowerMod", AV::kAlterationPowerModifier), N("ConjurationPowerMod", AV::kConjurationPowerModifier),
			N("DestructionPowerMod", AV::kDestructionPowerModifier), N("IllusionPowerMod", AV::kIllusionPowerModifier),
			N("RestorationPowerMod", AV::kRestorationPowerModifier), N("EnchantingPowerMod", AV::kEnchantingPowerModifier),
			N("DragonRend", AV::kDragonRend), N("AttackDamageMult", AV::kAttackDamageMult), N("HealRateMult", AV::kHealRateMult),
			N("MagickaRateMult", AV::kMagickaRateMult), N("StaminaRateMult", AV::kStaminaRateMult), N("WerewolfPerks", AV::kWerewolfPerks),
			N("VampirePerks", AV::kVampirePerks), N("ReflectDamage", AV::kReflectDamage) };

		// SkyUI's armor subType (Armor.EQUIP_*) and weight classes (Armor.WEIGHT_*)
		enum class Equip
		{
			kHead = 0, kHair, kLongHair, kBody, kForearms, kHands, kShield, kCalves, kFeet, kCirclet, kAmulet, kEars, kRing, kTail
		};
		constexpr Named kEquipTypes[]{ N("Head", Equip::kHead), N("Hair", Equip::kHair), N("LongHair", Equip::kLongHair),
			N("Body", Equip::kBody), N("Forearms", Equip::kForearms), N("Hands", Equip::kHands), N("Shield", Equip::kShield),
			N("Calves", Equip::kCalves), N("Feet", Equip::kFeet), N("Circlet", Equip::kCirclet), N("Amulet", Equip::kAmulet),
			N("Ears", Equip::kEars), N("Ring", Equip::kRing), N("Tail", Equip::kTail) };

		enum class Weight
		{
			kLight = 0, kHeavy = 1, kNone = 2, kClothing = 3, kJewelry = 4
		};
		constexpr Named kWeightClasses[]{ N("Light", Weight::kLight), N("Heavy", Weight::kHeavy), N("Clothing", Weight::kClothing),
			N("Jewelry", Weight::kJewelry) };

		// SkyUI's weapon subType (Weapon.TYPE_*)
		enum class Weap
		{
			kMelee = 0, kSword, kDagger, kWarAxe, kMace, kGreatsword, kBattleaxe, kWarhammer, kBow, kCrossbow, kStaff, kPickaxe, kWoodAxe
		};
		constexpr Named kWeaponTypes[]{ N("Melee", Weap::kMelee), N("Sword", Weap::kSword), N("Dagger", Weap::kDagger),
			N("WarAxe", Weap::kWarAxe), N("Mace", Weap::kMace), N("Greatsword", Weap::kGreatsword), N("Battleaxe", Weap::kBattleaxe),
			N("Warhammer", Weap::kWarhammer), N("Bow", Weap::kBow), N("Crossbow", Weap::kCrossbow), N("Staff", Weap::kStaff),
			N("Pickaxe", Weap::kPickaxe), N("WoodAxe", Weap::kWoodAxe) };

		using WT = RE::WEAPON_TYPE;
		constexpr Named kAnimTypes[]{ N("HandToHandMelee", WT::kHandToHandMelee), N("OneHandSword", WT::kOneHandSword),
			N("OneHandDagger", WT::kOneHandDagger), N("OneHandAxe", WT::kOneHandAxe), N("OneHandMace", WT::kOneHandMace),
			N("TwoHandSword", WT::kTwoHandSword), N("TwoHandAxe", WT::kTwoHandAxe), N("Bow", WT::kBow), N("Staff", WT::kStaff),
			N("Crossbow", WT::kCrossbow) };

		// SkyUI's potion subType (Item.POTION_*)
		enum class Potion
		{
			kHealth = 0, kHealRate, kHealRateMult, kMagicka, kMagickaRate, kMagickaRateMult, kStamina, kStaminaRate, kStaminaRateMult,
			kFireResist, kElectricResist, kFrostResist, kPotion, kDrink, kFood, kPoison
		};
		constexpr Named kPotionTypes[]{ N("Health", Potion::kHealth), N("HealRate", Potion::kHealRate),
			N("HealRateMult", Potion::kHealRateMult), N("Magicka", Potion::kMagicka), N("MagickaRate", Potion::kMagickaRate),
			N("MagickaRateMult", Potion::kMagickaRateMult), N("Stamina", Potion::kStamina), N("StaminaRate", Potion::kStaminaRate),
			N("StaminaRateMult", Potion::kStaminaRateMult), N("FireResist", Potion::kFireResist),
			N("ElectricResist", Potion::kElectricResist), N("FrostResist", Potion::kFrostResist), N("Potion", Potion::kPotion),
			N("Drink", Potion::kDrink), N("Food", Potion::kFood), N("Poison", Potion::kPoison) };

		using PF = RE::AlchemyItem::AlchemyFlag;
		constexpr Named kPotionFlags[]{ N("ManualCalc", PF::kCostOverride), N("Food", PF::kFoodItem), N("Medicine", PF::kMedicine),
			N("Poison", PF::kPoison) };

		constexpr Named kAmmoTypes[]{ { "Arrow", 0 }, { "Bolt", 1 } };

		using AF = RE::AMMO_DATA::Flag;
		constexpr Named kAmmoFlags[]{ N("IgnoresNormalWeaponResistance", AF::kIgnoresNormalWeaponResistance),
			N("NonPlayable", AF::kNonPlayable), N("NonBolt", AF::kNonBolt) };

		// Armor slots by importance (SkyUI Armor.PARTMASK_PRECEDENCE, I4 PartMaskPrecedence)
		constexpr std::uint32_t kPartPrecedence[]{ 1u << 2, 1u << 1, 1u << 3, 1u << 4, 1u << 7, 1u << 8, 1u << 9, 1u << 5, 1u << 6,
			1u << 11, 1u << 13, 1u << 0, 1u << 12, 1u << 10, 1u << 14, 1u << 15, 1u << 16, 1u << 17, 1u << 18, 1u << 19, 1u << 20, 1u << 21,
			1u << 22, 1u << 23, 1u << 24, 1u << 25, 1u << 26, 1u << 27, 1u << 28, 1u << 29, 1u << 30, 1u << 31 };

		std::uint32_t MainPart(std::uint32_t a_mask)
		{
			for (const auto slot : kPartPrecedence) {
				if (a_mask & slot) {
					return slot;
				}
			}
			return 0;
		}

		// ---- the entry of a form ---------------------------------------------------------------------

		Keywords KeywordsOf(std::span<RE::BGSKeyword* const> a_keywords, std::unordered_set<std::string>& a_into)
		{
			for (const auto keyword : a_keywords) {
				if (keyword && keyword->GetFormEditorID() && *keyword->GetFormEditorID()) {
					a_into.emplace(keyword->GetFormEditorID());
				}
			}
			return std::make_shared<const std::unordered_set<std::string>>(a_into);
		}

		// SKSE's MagicItemData (costliest effect) and I4's effectKeywords; SkyUI calls magicType "resistance"
		void MagicData(Entry& a_entry, RE::MagicItem* a_item)
		{
			a_entry["spellName"] = std::string(a_item->GetName());
			const auto effect = a_item->GetCostliestEffectItem();
			if (const auto mgef = effect ? effect->baseEffect : nullptr) {
				a_entry["magnitude"] = static_cast<double>(effect->effectItem.magnitude);
				a_entry["duration"] = static_cast<double>(effect->effectItem.duration);
				a_entry["area"] = static_cast<double>(effect->effectItem.area);
				const auto& data = mgef->data;
				a_entry["effectName"] = std::string(mgef->GetName());
				a_entry["subType"] = Num(data.associatedSkill);
				a_entry["effectFlags"] = static_cast<double>(data.flags.underlying());
				a_entry["school"] = Num(data.associatedSkill);
				a_entry["skillLevel"] = static_cast<double>(data.minimumSkill);
				a_entry["archetype"] = Num(data.archetype);
				a_entry["deliveryType"] = Num(data.delivery);
				a_entry["castTime"] = static_cast<double>(data.spellmakingChargeTime);
				a_entry["delayTime"] = static_cast<double>(data.aiDelayTimer);
				a_entry["actorValue"] = Num(data.primaryAV);
				a_entry["castType"] = Num(data.castingType);
				a_entry["resistance"] = Num(data.resistVariable);
			}
			std::unordered_set<std::string> keywords;
			for (const auto item : a_item->effects) {
				if (item && item->baseEffect) {
					KeywordsOf(item->baseEffect->GetKeywords(), keywords);
				}
			}
			a_entry["effectKeywords"] = std::make_shared<const std::unordered_set<std::string>>(std::move(keywords));
		}

		void ArmorData(Entry& a_entry, RE::TESObjectARMO* a_armor)
		{
			const auto mask = a_armor->GetSlotMask().underlying();
			a_entry["partMask"] = static_cast<double>(mask);

			// InventoryDataSetter.processArmorClass
			auto weight = static_cast<Weight>(std::to_underlying(a_armor->GetArmorType()));
			if (weight != Weight::kLight && weight != Weight::kHeavy) {
				weight = a_armor->HasKeywordString("VendorItemClothing") ? Weight::kClothing :
				         a_armor->HasKeywordString("VendorItemJewelry")  ? Weight::kJewelry :
				                                                           Weight::kNone;
			}
			// processArmorPartMask: subType from the most important slot
			const auto main = MainPart(mask);
			std::optional<Equip> equip;
			switch (main) {
			case 1u << 0: equip = Equip::kHead; break;
			case 1u << 1: equip = Equip::kHair; break;
			case 1u << 11: equip = Equip::kLongHair; break;
			case 1u << 2: equip = Equip::kBody; break;
			case 1u << 3: equip = Equip::kHands; break;
			case 1u << 4: equip = Equip::kForearms; break;
			case 1u << 5: equip = Equip::kAmulet; break;
			case 1u << 6: equip = Equip::kRing; break;
			case 1u << 7: equip = Equip::kFeet; break;
			case 1u << 8: equip = Equip::kCalves; break;
			case 1u << 9: equip = Equip::kShield; break;
			case 1u << 12: equip = Equip::kCirclet; break;
			case 1u << 13: equip = Equip::kEars; break;
			case 1u << 10: equip = Equip::kTail; break;
			default: break;
			}
			if (equip) {
				a_entry["subType"] = Num(*equip);
			} else if (main) {
				a_entry["subType"] = static_cast<double>(main);
			}
			if (main) {
				a_entry["mainPartMask"] = static_cast<double>(main);
			}
			// processArmorOther: no weight class -> clothing or jewelry by slot
			if (weight == Weight::kNone && equip) {
				switch (*equip) {
				case Equip::kAmulet:
				case Equip::kRing:
				case Equip::kCirclet:
				case Equip::kEars:
					weight = Weight::kJewelry;
					break;
				default:
					weight = Weight::kClothing;
					break;
				}
			}
			// processArmorBaseId
			switch (a_armor->GetFormID() & 0x00FFFFFF) {
			case 0x08895A:  // ClothesWeddingWreath
				weight = Weight::kJewelry;
				break;
			case 0x011A84:  // DLC1ClothesVampireLordArmor
				a_entry["subType"] = Num(Equip::kBody);
				break;
			default:
				break;
			}
			if (weight != Weight::kNone) {
				a_entry["weightClass"] = Num(weight);
			}
		}

		void WeaponData(Entry& a_entry, RE::TESObjectWEAP* a_weapon)
		{
			const auto type = a_weapon->GetWeaponType();
			a_entry["weaponType"] = Num(type);
			a_entry["speed"] = static_cast<double>(a_weapon->weaponData.speed);
			a_entry["reach"] = static_cast<double>(a_weapon->weaponData.reach);
			a_entry["stagger"] = static_cast<double>(a_weapon->weaponData.staggerValue);
			a_entry["critDamage"] = static_cast<double>(a_weapon->criticalData.damage);
			a_entry["minRange"] = static_cast<double>(a_weapon->weaponData.minRange);
			a_entry["maxRange"] = static_cast<double>(a_weapon->weaponData.maxRange);
			a_entry["baseDamage"] = static_cast<double>(a_weapon->attackDamage);
			if (const auto slot = a_weapon->GetEquipSlot()) {
				a_entry["equipSlot"] = static_cast<double>(slot->GetFormID());
			}

			// InventoryDataSetter.processWeaponType / processWeaponBaseId
			std::optional<Weap> sub;
			switch (type) {
			case WT::kHandToHandMelee: sub = Weap::kMelee; break;
			case WT::kOneHandSword: sub = Weap::kSword; break;
			case WT::kOneHandDagger: sub = Weap::kDagger; break;
			case WT::kOneHandAxe: sub = Weap::kWarAxe; break;
			case WT::kOneHandMace: sub = Weap::kMace; break;
			case WT::kTwoHandSword: sub = Weap::kGreatsword; break;
			case WT::kTwoHandAxe: sub = a_weapon->HasKeywordString("WeapTypeWarhammer") ? Weap::kWarhammer : Weap::kBattleaxe; break;
			case WT::kBow: sub = Weap::kBow; break;
			case WT::kStaff: sub = Weap::kStaff; break;
			case WT::kCrossbow: sub = Weap::kCrossbow; break;
			default: break;
			}
			switch (a_weapon->GetFormID() & 0x00FFFFFF) {
			case 0x0E3C16:  // WeapPickaxe
			case 0x06A707:  // SSDRocksplinterPickaxe
			case 0x1019D4:  // dunVolunruudPickaxe
				sub = Weap::kPickaxe;
				break;
			case 0x02F2F4:  // Axe01
			case 0x0AE086:  // dunHaltedStreamPoachersAxe
				sub = Weap::kWoodAxe;
				break;
			default:
				break;
			}
			if (sub) {
				a_entry["subType"] = Num(*sub);
			}
		}

		void PotionData(Entry& a_entry, RE::AlchemyItem* a_potion)
		{
			a_entry["flags"] = static_cast<double>(a_potion->data.flags.underlying());
			// InventoryDataSetter.processPotionType
			auto sub = Potion::kPotion;
			if (a_potion->data.flags.any(PF::kFoodItem)) {
				constexpr RE::FormID kITMPotionUse = 0x000B6435;
				const auto           sound = a_potion->data.consumptionSound;
				sub = sound && sound->GetFormID() == kITMPotionUse ? Potion::kDrink : Potion::kFood;
			} else if (a_potion->data.flags.any(PF::kPoison)) {
				sub = Potion::kPoison;
			} else if (const auto av = Number(a_entry, "actorValue")) {
				constexpr std::pair<AV, Potion> kByValue[]{ { AV::kHealth, Potion::kHealth }, { AV::kMagicka, Potion::kMagicka },
					{ AV::kStamina, Potion::kStamina }, { AV::kHealRate, Potion::kHealRate }, { AV::kMagickaRate, Potion::kMagickaRate },
					{ AV::kStaminaRate, Potion::kStaminaRate }, { AV::kHealRateMult, Potion::kHealRateMult },
					{ AV::kMagickaRateMult, Potion::kMagickaRateMult }, { AV::kStaminaRateMult, Potion::kStaminaRateMult },
					{ AV::kResistFire, Potion::kFireResist }, { AV::kResistShock, Potion::kElectricResist },
					{ AV::kResistFrost, Potion::kFrostResist } };
				for (const auto& [value, type] : kByValue) {
					if (*av == Num(value)) {
						sub = type;
					}
				}
			}
			a_entry["subType"] = Num(sub);
		}

		Entry MakeEntry(RE::TESForm* a_form)
		{
			Entry entry;
			entry["formType"] = Num(a_form->GetFormType());
			entry["formId"] = static_cast<double>(a_form->GetFormID());
			entry["baseId"] = static_cast<double>(a_form->GetFormID() & 0x00FFFFFF);
			entry["text"] = std::string(a_form->GetName());
			// keywords: SKSE gives them for inventory items, the magic menu's spells have none
			if (const auto keywordForm = skyrim_cast<RE::BGSKeywordForm*>(a_form); keywordForm && !a_form->Is(FT::Spell, FT::Shout)) {
				std::unordered_set<std::string> keywords;
				entry["keywords"] = KeywordsOf(keywordForm->GetKeywords(), keywords);
			}
			switch (a_form->GetFormType()) {
			case FT::Armor:
				ArmorData(entry, a_form->As<RE::TESObjectARMO>());
				break;
			case FT::Weapon:
				WeaponData(entry, a_form->As<RE::TESObjectWEAP>());
				break;
			case FT::Ammo:
				{
					const auto flags = a_form->As<RE::TESAmmo>()->GetRuntimeData().data.flags;
					entry["flags"] = static_cast<double>(flags.underlying());
					entry["subType"] = flags.any(AF::kNonBolt) ? 0.0 : 1.0;  // processAmmoType: arrow / bolt
					break;
				}
			case FT::AlchemyItem:
				MagicData(entry, a_form->As<RE::MagicItem>());
				PotionData(entry, a_form->As<RE::AlchemyItem>());
				break;
			case FT::Scroll:
				MagicData(entry, a_form->As<RE::MagicItem>());
				break;
			case FT::Spell:
				{
					const auto spell = a_form->As<RE::SpellItem>();
					MagicData(entry, spell);
					entry["spellType"] = Num(spell->GetSpellType());
					if (const auto slot = spell->GetEquipSlot()) {
						entry["equipSlot"] = static_cast<double>(slot->GetFormID());
					}
					if (spell->GetSpellType() != ST::kSpell) {
						entry["skillLevel"] = nullptr;  // MagicDataSetter: not for powers
					}
					break;
				}
			default:
				break;
			}
			return entry;
		}

		// ---- SkyUI's default icons -------------------------------------------------------------------

		constexpr std::uint32_t kFire = 0xC73636;
		constexpr std::uint32_t kShockScroll = 0xFFFF00;
		constexpr std::uint32_t kShock = 0xEAAB00;
		constexpr std::uint32_t kFrost = 0x1FFBFF;

		void SetIcon(Entry& a_entry, std::string_view a_label, std::optional<std::uint32_t> a_color = std::nullopt)
		{
			a_entry["iconLabel"] = std::string(a_label);
			if (a_color) {
				a_entry["iconColor"] = static_cast<double>(*a_color);
			}
		}

		std::optional<Equip> SubEquip(const Entry& a_entry)
		{
			const auto sub = Number(a_entry, "subType");
			return sub && *sub >= 0 && *sub <= Num(Equip::kTail) ? std::optional(static_cast<Equip>(static_cast<int>(*sub))) : std::nullopt;
		}

		void JewelryIcon(Entry& a_entry)
		{
			switch (SubEquip(a_entry).value_or(Equip::kEars)) {
			case Equip::kAmulet:
				SetIcon(a_entry, "armor_amulet");
				break;
			case Equip::kRing:
				SetIcon(a_entry, "armor_ring");
				break;
			case Equip::kCirclet:
				SetIcon(a_entry, "armor_circlet");
				break;
			default:
				break;
			}
		}

		// "<prefix>_<piece>" for light / heavy armor and clothing; jewelry slots of light / heavy armor use the jewelry icons
		void PieceIcon(Entry& a_entry, std::string_view a_prefix, bool a_jewelry)
		{
			const auto equip = SubEquip(a_entry);
			if (!equip) {
				return;
			}
			std::string_view piece;
			switch (*equip) {
			case Equip::kHead:
			case Equip::kHair:
			case Equip::kLongHair:
				piece = "head";
				break;
			case Equip::kBody:
			case Equip::kTail:
				piece = "body";
				break;
			case Equip::kHands:
				piece = "hands";
				break;
			case Equip::kForearms:
				piece = "forearms";
				break;
			case Equip::kFeet:
				piece = "feet";
				break;
			case Equip::kCalves:
				piece = "calves";
				break;
			case Equip::kShield:
				piece = "shield";
				break;
			default:
				if (a_jewelry) {
					JewelryIcon(a_entry);
				}
				return;
			}
			SetIcon(a_entry, std::format("{}_{}", a_prefix, piece));
		}

		void ArmorIcon(Entry& a_entry)
		{
			SetIcon(a_entry, "default_armor", 0xEDDA87);
			const auto weight = Number(a_entry, "weightClass");
			if (weight == Num(Weight::kLight)) {
				SetIcon(a_entry, "default_armor", 0x756000);
				PieceIcon(a_entry, "lightarmor", true);
			} else if (weight == Num(Weight::kHeavy)) {
				SetIcon(a_entry, "default_armor", 0x6B7585);
				PieceIcon(a_entry, "armor", true);
			} else if (weight == Num(Weight::kJewelry)) {
				JewelryIcon(a_entry);
			} else {
				PieceIcon(a_entry, "clothing", false);
			}
		}

		void WeaponIcon(Entry& a_entry)
		{
			SetIcon(a_entry, "default_weapon", 0xA4A5BF);
			constexpr std::pair<Weap, std::string_view> kLabels[]{ { Weap::kSword, "weapon_sword" }, { Weap::kDagger, "weapon_dagger" },
				{ Weap::kWarAxe, "weapon_waraxe" }, { Weap::kMace, "weapon_mace" }, { Weap::kGreatsword, "weapon_greatsword" },
				{ Weap::kBattleaxe, "weapon_battleaxe" }, { Weap::kWarhammer, "weapon_hammer" }, { Weap::kBow, "weapon_bow" },
				{ Weap::kStaff, "weapon_staff" }, { Weap::kCrossbow, "weapon_crossbow" }, { Weap::kPickaxe, "weapon_pickaxe" },
				{ Weap::kWoodAxe, "weapon_woodaxe" } };
			for (const auto& [type, label] : kLabels) {
				if (Is(a_entry, "subType", Num(type))) {
					SetIcon(a_entry, label);
				}
			}
		}

		void PotionIcon(Entry& a_entry)
		{
			SetIcon(a_entry, "default_potion");
			const auto sub = Number(a_entry, "subType");
			if (!sub) {
				return;
			}
			switch (static_cast<Potion>(static_cast<int>(*sub))) {
			case Potion::kDrink:
				SetIcon(a_entry, "food_wine");
				break;
			case Potion::kFood:
				SetIcon(a_entry, "default_food");
				break;
			case Potion::kPoison:
				SetIcon(a_entry, "potion_poison", 0xAD00B3);
				break;
			case Potion::kHealth:
			case Potion::kHealRate:
			case Potion::kHealRateMult:
				SetIcon(a_entry, "potion_health", 0xDB2E73);
				break;
			case Potion::kMagicka:
			case Potion::kMagickaRate:
			case Potion::kMagickaRateMult:
				SetIcon(a_entry, "potion_magic", 0x2E9FDB);
				break;
			case Potion::kStamina:
			case Potion::kStaminaRate:
			case Potion::kStaminaRateMult:
				SetIcon(a_entry, "potion_stam", 0x51DB2E);
				break;
			case Potion::kFireResist:
				SetIcon(a_entry, "potion_fire", kFire);
				break;
			case Potion::kElectricResist:
				SetIcon(a_entry, "potion_shock", kShock);
				break;
			case Potion::kFrostResist:
				SetIcon(a_entry, "potion_frost", kFrost);
				break;
			default:
				break;
			}
		}

		// MagicIconSetter: school icons, destruction by element
		void SpellIcon(Entry& a_entry)
		{
			SetIcon(a_entry, "default_power");
			const auto school = Number(a_entry, "school");
			if (school == Num(AV::kAlteration)) {
				SetIcon(a_entry, "default_alteration");
			} else if (school == Num(AV::kConjuration)) {
				SetIcon(a_entry, "default_conjuration");
			} else if (school == Num(AV::kIllusion)) {
				SetIcon(a_entry, "default_illusion");
			} else if (school == Num(AV::kRestoration)) {
				SetIcon(a_entry, "default_restoration");
			} else if (school == Num(AV::kDestruction)) {
				SetIcon(a_entry, "default_destruction");
				const auto resist = Number(a_entry, "resistance");
				if (resist == Num(AV::kResistFire)) {
					SetIcon(a_entry, "magic_fire", kFire);
				} else if (resist == Num(AV::kResistShock)) {
					SetIcon(a_entry, "magic_shock", kShock);
				} else if (resist == Num(AV::kResistFrost)) {
					SetIcon(a_entry, "magic_frost", kFrost);
				}
			}
		}

		void ScrollIcon(Entry& a_entry)
		{
			SetIcon(a_entry, "default_scroll");
			const auto resist = Number(a_entry, "resistance");
			if (resist == Num(AV::kResistFire)) {
				a_entry["iconColor"] = static_cast<double>(kFire);
			} else if (resist == Num(AV::kResistShock)) {
				a_entry["iconColor"] = static_cast<double>(kShockScroll);
			} else if (resist == Num(AV::kResistFrost)) {
				a_entry["iconColor"] = static_cast<double>(kFrost);
			}
		}

		// The icon SkyUI's list processors give the entry. a_anySpell: I4 redoing it after its rules changed the
		// entry calls SkyUI's spell icon function for powers too (the magic menu itself gives powers "default_power").
		void DefaultIcon(Entry& a_entry, bool a_anySpell)
		{
			const auto type = Number(a_entry, "formType").value_or(0.0);
			if (type == Num(FT::Spell)) {
				if (a_anySpell || Is(a_entry, "spellType", Num(ST::kSpell))) {
					SpellIcon(a_entry);
				} else {
					SetIcon(a_entry, "default_power");
				}
			} else if (type == Num(FT::Shout)) {
				SetIcon(a_entry, "default_shout");
			} else if (type == Num(FT::Scroll)) {
				ScrollIcon(a_entry);
			} else if (type == Num(FT::Armor)) {
				ArmorIcon(a_entry);
			} else if (type == Num(FT::Weapon)) {
				WeaponIcon(a_entry);
			} else if (type == Num(FT::Ammo)) {
				SetIcon(a_entry, Is(a_entry, "subType", 1.0) ? "weapon_bolt" : "weapon_arrow", 0xA89E8C);
			} else if (type == Num(FT::Light)) {
				SetIcon(a_entry, "misc_torch");
			} else if (type == Num(FT::AlchemyItem)) {
				PotionIcon(a_entry);
			} else if (type == Num(FT::Ingredient)) {
				SetIcon(a_entry, "default_ingredient");
			}
		}

		// ---- I4 rules ------------------------------------------------------------------------------

		struct Property
		{
			enum class Kind
			{
				kMatch,     // equal to value (an undefined value also matches null)
				kRange,     // number within min / max
				kBits,      // number with one of the bits set
				kKeyword,   // keyword object with the keyword
				kMainPart,  // armor slot mask whose most important slot is bits
				kAnyOf
			};
			Kind                  kind{ Kind::kMatch };
			Value                 value{};
			std::optional<double> min;
			std::optional<double> max;
			std::uint32_t         bits{ 0 };
			std::string           keyword;
			std::vector<Property> anyOf;

			bool Matches(const Value& a_value) const
			{
				switch (kind) {
				case Kind::kMatch:
					if (std::holds_alternative<std::nullptr_t>(a_value)) {
						return std::holds_alternative<Undefined>(value) || std::holds_alternative<std::nullptr_t>(value);
					}
					return a_value == value;
				case Kind::kRange:
					{
						if (!std::holds_alternative<double>(a_value)) {
							return false;
						}
						const auto number = std::get<double>(a_value);
						return (!min || number >= *min) && (!max || number <= *max);
					}
				case Kind::kBits:
					return std::holds_alternative<double>(a_value) && (static_cast<std::uint32_t>(std::get<double>(a_value)) & bits) != 0;
				case Kind::kKeyword:
					return std::holds_alternative<Keywords>(a_value) && std::get<Keywords>(a_value)->contains(keyword);
				case Kind::kMainPart:
					{
						if (!std::holds_alternative<double>(a_value)) {
							return false;
						}
						const auto mask = static_cast<std::uint32_t>(std::get<double>(a_value));
						return (mask & bits) != 0 && MainPart(mask) == bits;
					}
				case Kind::kAnyOf:
					return std::ranges::any_of(anyOf, [&](const Property& a_sub) { return a_sub.Matches(a_value); });
				default:
					return false;
				}
			}
		};

		struct Rule
		{
			std::map<std::string, Property> match;   // entry member -> what it must be (later ones of a name replace earlier)
			std::map<std::string, Value>    assign;  // entry members set when it matches
			bool                            info{ false };  // assigns more than the icon (the default icon is made again)

			bool Matches(const Entry& a_entry) const
			{
				return std::ranges::all_of(match, [&](const auto& a_pair) { return a_pair.second.Matches(Member(a_entry, a_pair.first)); });
			}
		};

		std::vector<Rule> rules;

		bool IsIcon(const std::string& a_name)
		{
			return a_name.starts_with("icon");
		}

		// How a match value of a member is read
		struct Reading
		{
			enum class Kind
			{
				kPlain,
				kFormType,
				kFormId,
				kKeywords,
				kMainPart,
				kParts,
				kColor,
				kEnum,
				kBits
			};
			Kind                   kind{ Kind::kPlain };
			std::span<const Named> table{};
		};

		Reading ReadingOf(const std::string& a_name, double a_formType)
		{
			using K = Reading::Kind;
			static const std::map<std::string, Reading> kByName{ { "formType", { K::kFormType } }, { "formId", { K::kFormId } },
				{ "equipSlot", { K::kFormId } }, { "teachesSpell", { K::kFormId } }, { "keywords", { K::kKeywords } },
				{ "effectKeywords", { K::kKeywords } }, { "mainPart", { K::kMainPart } }, { "parts", { K::kParts } },
				{ "iconColor", { K::kColor } }, { "weightClass", { K::kEnum, kWeightClasses } }, { "weaponType", { K::kEnum, kAnimTypes } },
				{ "teachesSkill", { K::kEnum, kActorValues } }, { "actorValue", { K::kEnum, kActorValues } },
				{ "resistance", { K::kEnum, kActorValues } }, { "school", { K::kEnum, kActorValues } },
				{ "spellType", { K::kEnum, kSpellTypes } }, { "archetype", { K::kEnum, kArchetypes } },
				{ "deliveryType", { K::kEnum, kDeliveries } }, { "castType", { K::kEnum, kCastingTypes } },
				{ "effectFlags", { K::kBits, kEffectFlags } } };
			if (const auto it = kByName.find(a_name); it != kByName.end()) {
				return it->second;
			}
			if (a_name == "flags") {
				return a_formType == Num(FT::Ammo) ? Reading{ K::kBits, kAmmoFlags } :
				       a_formType == Num(FT::AlchemyItem) ? Reading{ K::kBits, kPotionFlags } :
				                                            Reading{};
			}
			if (a_name == "subType") {
				return a_formType == Num(FT::Armor)       ? Reading{ K::kEnum, kEquipTypes } :
				       a_formType == Num(FT::Weapon)      ? Reading{ K::kEnum, kWeaponTypes } :
				       a_formType == Num(FT::Ammo)        ? Reading{ K::kEnum, kAmmoTypes } :
				       a_formType == Num(FT::AlchemyItem) ? Reading{ K::kEnum, kPotionTypes } :
				                                            Reading{};
			}
			return {};
		}

		std::uint32_t ParseColor(std::string_view a_hex)
		{
			if (a_hex.starts_with('#')) {
				a_hex.remove_prefix(1);
			}
			std::uint32_t color = 0;
			std::from_chars(a_hex.data(), a_hex.data() + a_hex.size(), color, 16);
			return color;
		}

		Property Match(Value a_value)
		{
			return Property{ .kind = Property::Kind::kMatch, .value = std::move(a_value) };
		}

		using AddProperty = std::function<void(const std::string&, Property)>;

		// I4's PropertyParser: one json match value -> properties (arrays add each element under the same name, so
		// the last one counts; { "anyOf": [...] } and { "min", "max" } make one property)
		void ParseMatch(const std::string& a_name, const json& a_spec, const Reading& a_reading, const AddProperty& a_add)
		{
			using K = Reading::Kind;
			if (a_spec.is_string()) {
				const auto text = a_spec.get<std::string>();
				switch (a_reading.kind) {
				case K::kFormType:
					a_add(a_name, Match(static_cast<double>(Find(kFormTypes, text).value_or(-1))));
					return;
				case K::kFormId:
					a_add(a_name, Match(static_cast<double>(Util::FromPluginKey(text))));
					return;
				case K::kColor:
					a_add(a_name, Match(static_cast<double>(ParseColor(text))));
					return;
				case K::kKeywords:
					a_add(a_name, Property{ .kind = Property::Kind::kKeyword, .keyword = text });
					return;
				case K::kEnum:
					if (const auto value = Find(a_reading.table, text)) {
						a_add(a_name, Match(static_cast<double>(*value)));
					} else {
						a_add(a_name, Match(IEquals(text, "Other") ? Value{} : Value{ text }));  // "Other": no weight class
					}
					return;
				case K::kBits:
					if (const auto value = Find(a_reading.table, text)) {
						a_add(a_name, Property{ .kind = Property::Kind::kBits, .bits = static_cast<std::uint32_t>(*value) });
					} else {
						a_add(a_name, Match(text));
					}
					return;
				default:
					a_add(a_name, Match(text));
					return;
				}
			}
			if (a_spec.is_number()) {
				const auto number = a_spec.get<double>();
				if (a_reading.kind == K::kMainPart || a_reading.kind == K::kParts) {
					const auto part = static_cast<int>(number);
					if (part < 30 || part > 61) {
						logs::warn("I4 rules: armor slot {} is out of range", part);
					}
					const auto bit = 1u << ((part - 30) & 31);
					a_add("partMask", Property{ .kind = a_reading.kind == K::kMainPart ? Property::Kind::kMainPart : Property::Kind::kBits, .bits = bit });
				} else {
					a_add(a_name, Match(number));
				}
				return;
			}
			if (a_spec.is_boolean()) {
				a_add(a_name, Match(a_spec.get<bool>()));
				return;
			}
			if (a_spec.is_array()) {
				for (const auto& element : a_spec) {
					ParseMatch(a_name, element, a_reading, a_add);
				}
				return;
			}
			if (a_spec.is_object()) {
				if (const auto anyOf = a_spec.find("anyOf"); anyOf != a_spec.end() && anyOf->is_array()) {
					Property any{ .kind = Property::Kind::kAnyOf };
					for (const auto& element : *anyOf) {
						ParseMatch(a_name, element, a_reading, [&](const std::string&, Property a_sub) { any.anyOf.push_back(std::move(a_sub)); });
					}
					a_add(a_name, std::move(any));
				}
				Property range{ .kind = Property::Kind::kRange };
				if (const auto min = a_spec.find("min"); min != a_spec.end() && min->is_number()) {
					range.min = min->get<double>();
				}
				if (const auto max = a_spec.find("max"); max != a_spec.end() && max->is_number()) {
					range.max = max->get<double>();
				}
				if (range.min || range.max) {
					a_add(a_name, std::move(range));
				}
				return;
			}
			if (a_spec.is_null()) {
				a_add(a_name, Match(Value{}));
			}
		}

		// I4's CustomDataParser: one json assign value
		void ParseAssign(Rule& a_rule, const std::string& a_name, const json& a_value, double a_formType)
		{
			if (a_value.is_boolean()) {
				a_rule.assign[a_name] = a_value.get<bool>();
			} else if (a_value.is_number()) {
				a_rule.assign[a_name] = a_value.get<double>();
			} else if (a_value.is_string()) {
				const auto text = a_value.get<std::string>();
				if (a_name == "iconColor") {
					a_rule.assign[a_name] = static_cast<double>(ParseColor(text));
					return;
				}
				// a named subType / weightClass is stored as SkyUI's number (I4 reads weight classes with the armor
				// slot names, so those stay text like there)
				const auto reading = a_name == "subType" ? ReadingOf(a_name, a_formType) : a_name == "weightClass" ? Reading{ Reading::Kind::kEnum, kEquipTypes } : Reading{};
				if (const auto value = reading.kind == Reading::Kind::kEnum ? Find(reading.table, text) : std::nullopt) {
					a_rule.assign[a_name] = static_cast<double>(*value);
				} else {
					a_rule.assign[a_name] = text;
				}
			}
		}

		bool GameFileExists(const std::string& a_path);

		// I4's Rule::Validate: a missing icon movie drops the rule's icon, a frame without a movie means SkyUI's movie
		bool Validate(Rule& a_rule)
		{
			static std::unordered_map<std::string, bool> checked;
			if (const auto it = a_rule.assign.find("iconSource"); it != a_rule.assign.end()) {
				bool valid = false;
				if (const auto source = std::get_if<std::string>(&it->second)) {
					auto key = *source;
					std::ranges::transform(key, key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					const auto found = checked.find(key);
					valid = found != checked.end() ? found->second : checked.emplace(key, GameFileExists("Interface/" + *source)).first->second;
					if (!valid && found == checked.end()) {
						logs::warn("I4 rules: icon movie Interface/{} is missing", *source);
					}
				}
				if (!valid) {
					a_rule.assign.erase("iconSource");
					a_rule.assign.erase("iconLabel");
				}
			} else if (a_rule.assign.contains("iconLabel")) {
				a_rule.assign["iconSource"] = Value{};
			}
			a_rule.info = std::ranges::any_of(a_rule.assign, [](const auto& a_pair) { return !IsIcon(a_pair.first); });
			return !a_rule.match.empty() && !a_rule.assign.empty();
		}

		std::optional<Rule> ParseRule(const json& a_rule)
		{
			const auto match = a_rule.find("match");
			const auto assign = a_rule.find("assign");
			if (match == a_rule.end() || assign == a_rule.end() || !match->is_object() || !assign->is_object()) {
				return std::nullopt;
			}
			double formType = Num(FT::None);
			if (const auto type = match->find("formType"); type != match->end() && type->is_string()) {
				formType = static_cast<double>(Find(kFormTypes, type->get<std::string>()).value_or(0));
			}
			Rule rule;
			for (const auto& [name, spec] : match->items()) {
				if (!name.empty() && name[0] != '$') {
					ParseMatch(name, spec, ReadingOf(name, formType), [&](const std::string& a_name, Property a_property) {
						rule.match.insert_or_assign(a_name, std::move(a_property));
					});
				}
			}
			for (const auto& [name, value] : assign->items()) {
				if (!name.empty() && name[0] != '$') {
					ParseAssign(rule, name, value, formType);
				}
			}
			if (!Validate(rule)) {
				return std::nullopt;
			}
			return rule;
		}

		// I4's CustomDataManager::ProcessEntry: rules set their item data in order (later rules see it), the default
		// icon is made again if any did, then the rules set their icons
		void ApplyRules(Entry& a_entry)
		{
			bool redoIcon = false;
			for (const auto& rule : rules) {
				if (!rule.Matches(a_entry)) {
					continue;
				}
				for (const auto& [name, value] : rule.assign) {
					if (!IsIcon(name)) {
						SetMember(a_entry, name, value);
					}
				}
				redoIcon = redoIcon || rule.info;
			}
			if (redoIcon) {
				a_entry.erase("iconLabel");
				a_entry.erase("iconColor");
				DefaultIcon(a_entry, true);
			}
			for (const auto& rule : rules) {
				if (!rule.Matches(a_entry)) {
					continue;
				}
				for (const auto& [name, value] : rule.assign) {
					if (IsIcon(name)) {
						SetMember(a_entry, name, value);
					}
				}
			}
		}

		// ---- files ---------------------------------------------------------------------------------

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
			std::string text(stream.stream->totalSize, '\0');
			if (!text.empty() && !stream.read(text.data(), static_cast<std::uint32_t>(text.size()))) {
				return {};
			}
			return text;
		}

		bool GameFileExists(const std::string& a_path)
		{
			RE::BSResourceNiBinaryStream stream(a_path);
			return stream.good();
		}

		// Data\SKSE\Plugins\InventoryInjector\<plugin>.json of every active plugin, in load order (like I4)
		void LoadRules()
		{
			const auto dataHandler = RE::TESDataHandler::GetSingleton();
			if (!dataHandler) {
				return;
			}
			for (const auto file : dataHandler->files) {
				if (!file || file->recordFlags.none(RE::TESFile::RecordFlag::kChecked)) {
					continue;
				}
				auto name = std::filesystem::path(file->GetFilename()).replace_extension("json").string();
				const auto text = ReadGameFile("SKSE/Plugins/InventoryInjector/" + name);
				if (text.empty()) {
					continue;
				}
				const auto root = json::parse(text, nullptr, false, true);
				const auto list = root.is_object() ? root.find("rules") : root.end();
				if (root.is_discarded() || list == root.end() || !list->is_array()) {
					logs::warn("I4 rules: could not read {}", name);
					continue;
				}
				std::size_t count = 0;
				for (const auto& entry : *list) {
					if (auto rule = entry.is_object() ? ParseRule(entry) : std::nullopt) {
						rules.push_back(std::move(*rule));
						++count;
					}
				}
				logs::info("I4 rules: {} from {}", count, name);
			}
		}

		const std::vector<Rule>& Rules()
		{
			static const bool loaded = [] {
				LoadRules();
				return true;
			}();
			(void)loaded;
			return rules;
		}

		struct SkyUIConfig
		{
			std::string movie;  // under Interface, without ".swf"; empty if missing
			bool        noColor{ false };
		};

		std::string WithoutSwf(std::string a_path)
		{
			std::ranges::replace(a_path, '\\', '/');
			if (a_path.size() > 4 && IEquals(std::string_view(a_path).substr(a_path.size() - 4), ".swf")) {
				a_path.resize(a_path.size() - 4);
			}
			return a_path;
		}

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
					out.movie = WithoutSwf(source);
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
		case FT::Weapon:
		case FT::Armor:
		case FT::Ammo:
		case FT::Light:
		case FT::AlchemyItem:
			return true;
		default:
			return false;
		}
	}

	const std::string& MoviePath()
	{
		return Config().movie;
	}

	std::size_t RuleCount()
	{
		return Rules().size();
	}

	std::optional<Look> For(RE::TESForm* a_form)
	{
		if (!a_form || MoviePath().empty()) {
			return std::nullopt;
		}
		switch (a_form->GetFormType()) {
		case FT::Weapon:
		case FT::Armor:
		case FT::Ammo:
		case FT::Light:
		case FT::AlchemyItem:
		case FT::Scroll:
		case FT::Shout:
		case FT::Spell:
			break;
		default:
			return std::nullopt;
		}
		auto entry = MakeEntry(a_form);
		DefaultIcon(entry, false);
		if (!Rules().empty()) {
			ApplyRules(entry);
		}

		const auto label = std::get_if<std::string>(&Member(entry, "iconLabel"));
		if (!label || label->empty()) {
			return std::nullopt;
		}
		Look look{ .movie = MoviePath(), .label = *label };
		if (const auto source = std::get_if<std::string>(&Member(entry, "iconSource")); source && !source->empty()) {
			look.movie = WithoutSwf(*source);
		}
		if (const auto color = Number(entry, "iconColor"); color && !Config().noColor) {
			look.rgb = static_cast<std::uint32_t>(*color) & 0xFFFFFF;
		}
		return look;
	}
}
