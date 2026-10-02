#include "casting/Replacers.h"

#include "core/Config.h"

#include <random>

namespace Replacers
{
	namespace
	{
		namespace fs = std::filesystem;

		enum Role : int
		{
			kCharge = 0,
			kRelease = 1,
			kReady = 2,       // 1st person walk / run after the release only
			kLocomotion = 3,  // 3rd person walk / run while the shout state runs (any cast type)
			kRoleCount = 4
		};
		constexpr std::array<std::string_view, kRoleCount> kRoleNames{ "charge"sv, "release"sv, "ready"sv, "locomotion"sv };

		constexpr int kViewCount = 2;  // 0 = 3rd person, 1 = 1st person

		// DAR folders scanned per view, and where a submod keeps that view's clips
		constexpr std::array<std::string_view, kViewCount> kDarRoots{
			"Data/meshes/actors/character/animations/DynamicAnimationReplacer/_CustomConditions"sv,
			"Data/meshes/actors/character/_1stperson/animations/DynamicAnimationReplacer/_CustomConditions"sv
		};
		constexpr std::array<std::string_view, kViewCount> kViewDirs{ "animations"sv, "_1stperson/animations"sv };

		constexpr std::string_view kOutRoot = "Data/meshes/actors/character/OpenAnimationReplacer/SpellHotbarNG_Replacers"sv;
		constexpr std::string_view kManifest = "manifest.txt"sv;
		constexpr int              kGeneratorVersion = 2;  // bump when the output layout changes

		// above Spell Hotbar NG's own submods (2140000000 + ...), below the animation patches (2141000000 + ...)
		constexpr int kPriorityBase = 2140100000;

		constexpr int        kHandAny = 0, kHandLeft = 2;
		constexpr float      kMoveBlendTime = 0.1f;
		constexpr const char* kConditionVersion = "1.4.0";

		// The same rows as tools/build_anims.py SUBMODS: cast type + hand -> the vanilla clips our shout clips become
		struct Row
		{
			std::string_view folder;
			CastAnim::Type   type;
			int              hand;
			std::string_view charge;
			std::string_view release;
		};
		constexpr std::array kRows{
			Row{ "1_aimed"sv, CastAnim::Type::kAimed, kHandAny, "mrh_chargeloop.hkx"sv, "mrh_release.hkx"sv },
			Row{ "1_aimed_left"sv, CastAnim::Type::kAimed, kHandLeft, "mlh_chargeloop.hkx"sv, "mlh_release.hkx"sv },
			Row{ "2_self"sv, CastAnim::Type::kSelf, kHandAny, "mrh_selfchargeloop.hkx"sv, "mrh_selfrelease.hkx"sv },
			Row{ "2_self_left"sv, CastAnim::Type::kSelf, kHandLeft, "mlh_selfchargeloop.hkx"sv, "mlh_selfrelease.hkx"sv },
			Row{ "3_aimed_concentration"sv, CastAnim::Type::kAimedConc, kHandAny, "mrh_aimedconcentration.hkx"sv, "mrh_release.hkx"sv },
			Row{ "3_aimed_concentration_left"sv, CastAnim::Type::kAimedConc, kHandLeft, "mlh_aimedconcentration.hkx"sv, "mlh_release.hkx"sv },
			Row{ "4_self_concentration"sv, CastAnim::Type::kSelfConc, kHandAny, "mrh_selfconcentration.hkx"sv, "mrh_selfrelease.hkx"sv },
			Row{ "4_self_concentration_left"sv, CastAnim::Type::kSelfConc, kHandLeft, "mlh_selfconcentration.hkx"sv, "mlh_selfrelease.hkx"sv },
			Row{ "5_dual_aimed"sv, CastAnim::Type::kDualAimed, kHandAny, "ritualspell_charge.hkx"sv, "ritualspell_aimrelease.hkx"sv },
			Row{ "6_dual_self"sv, CastAnim::Type::kDualSelf, kHandAny, "ritualspell_charge.hkx"sv, "ritualspell_release.hkx"sv },
			Row{ "7_dual_concentration"sv, CastAnim::Type::kDualConc, kHandAny, "mlhmrh_aimedconcentrationloop.hkx"sv, "dmagaimrelease.hkx"sv },
			Row{ "8_ritual"sv, CastAnim::Type::kRitual, kHandAny, "ritualspell_charge.hkx"sv, "ritualspell_release.hkx"sv },
			Row{ "9_dual_self_concentration"sv, CastAnim::Type::kDualSelfConc, kHandAny, "dmagselfconloop.hkx"sv, "dmagselfrelease.hkx"sv },
		};

		std::string_view ReadyClip(const Row& a_row)
		{
			switch (a_row.type) {
			case CastAnim::Type::kDualAimed:
			case CastAnim::Type::kDualSelf:
			case CastAnim::Type::kDualConc:
			case CastAnim::Type::kDualSelfConc:
			case CastAnim::Type::kRitual:
				return "ritualspell_ready.hkx"sv;
			case CastAnim::Type::kSelf:
			case CastAnim::Type::kSelfConc:
				return a_row.hand == kHandLeft ? "mlh_selfreadyloop.hkx"sv : "mrh_selfreadyloop.hkx"sv;
			default:
				return a_row.hand == kHandLeft ? "mlh_readyloop.hkx"sv : "mrh_readyloop.hkx"sv;
			}
		}

		std::string_view Clip(const Row& a_row, int a_role)
		{
			return a_role == kCharge ? a_row.charge : a_role == kRelease ? a_row.release : ReadyClip(a_row);
		}

		// the shout clips a role replaces (see tools/build_anims.py)
		constexpr std::array<std::string_view, 3> kInhaleTargets{ "mt_shout_inhale.hkx"sv, "1hm_shout_inhale.hkx"sv, "sneak1hm_shout_inhale.hkx"sv };
		constexpr std::array<std::string_view, 3> kExhaleTargets{ "mt_shout_exhale.hkx"sv, "1hm_shout_exhale.hkx"sv, "sneak1hm_shout_exhale.hkx"sv };

		// 3rd person: moving, the shout behavior blends the shout clip (arms half) with these unarmed walk / run clips. While a
		// hotbar cast runs they become the magic casting locomotion (arms held for casting), like the magic behavior does.
		// (target in animations\male + animations\female, casting clip)
		constexpr std::array<std::pair<std::string_view, std::string_view>, 16> kLocomotionClips{ {
			{ "mt_walkforward.hkx"sv, "magcast_walkforward.hkx"sv },
			{ "mt_walkforwardright.hkx"sv, "magcast_walkfrwrdright.hkx"sv },
			{ "mt_walkright.hkx"sv, "magcast_walkright.hkx"sv },
			{ "mt_walkbackwardright.hkx"sv, "magcast_walkbckwrdrht.hkx"sv },
			{ "mt_walkbackward.hkx"sv, "magcast_walkbackward.hkx"sv },
			{ "mt_walkbackwardleft.hkx"sv, "magcast_walkbckwrdleft.hkx"sv },
			{ "mt_walkleft.hkx"sv, "magcast_walkleft.hkx"sv },
			{ "mt_walkforwardleft.hkx"sv, "magcast_walkforwrdleft.hkx"sv },
			{ "mt_runforward.hkx"sv, "magcast_runforward.hkx"sv },
			{ "mt_runforwardright.hkx"sv, "magcast_runfrwrdright.hkx"sv },
			{ "mt_runright.hkx"sv, "magcast_runright.hkx"sv },
			{ "mt_runbackwardright.hkx"sv, "magcast_runbckwrdright.hkx"sv },
			{ "mt_runbackward.hkx"sv, "magcast_runbackward.hkx"sv },
			{ "mt_runbackwardleft.hkx"sv, "magcast_runbackwrdleft.hkx"sv },
			{ "mt_runleft.hkx"sv, "magcast_runleft.hkx"sv },
			{ "mt_runforwardleft.hkx"sv, "magcast_runforwardleft.hkx"sv },
		} };
		constexpr std::array<std::string_view, 2> kLocomotionDirs{ "animations/male"sv, "animations/female"sv };

		std::vector<std::string> MoveTargets()
		{
			std::vector<std::string> out;
			for (const auto gait : { "walk"sv, "run"sv }) {
				for (const auto dir : { "forward"sv, "forwardleft"sv, "forwardright"sv, "left"sv, "right"sv, "backward"sv, "backwardleft"sv, "backwardright"sv }) {
					out.push_back(std::format("mt_{}{}.hkx", gait, dir));
				}
			}
			return out;
		}

		// ---- DAR conditions -------------------------------------------------------------------------------------

		struct Condition
		{
			enum class Kind
			{
				kActorBase,
				kRightType,
				kLeftType,
				kRandom,
				kFemale,
				kInCombat,
				kSneaking
			};
			Kind        kind;
			bool        negated{ false };
			std::string plugin;
			RE::FormID  localID{ 0 };
			int         value{ 0 };
			float       chance{ 0.0f };
		};

		struct Folder
		{
			int                                          view{ 0 };
			int                                          priority{ 0 };
			fs::path                                     dir;
			std::vector<std::vector<Condition>>          groups;  // all groups must hold, one condition per group
			std::unordered_map<std::string, fs::path>    clips;   // lower case file name -> path
		};

		// one replacer clip a cast can pick: a DAR folder's clip for a row, view and role. Its "Variant" id is index + 1.
		struct Source
		{
			std::size_t row;
			int         view;
			int         role;
			std::size_t folder;
			fs::path    file;
			float       duration{ 0.0f };  // release clips only
		};

		// built once in SKSEPluginLoad, read-only afterwards (Matches runs on OAR's threads)
		std::vector<Folder> folders;
		std::vector<Source> sources;
		// candidates[row][view][role]: source indices, highest DAR priority first
		std::array<std::array<std::array<std::vector<std::size_t>, kRoleCount>, kViewCount>, kRows.size()> candidates;
		// 3rd person locomotion sources (any row), highest DAR priority first
		std::vector<std::size_t> locomotionCandidates;

		// chosen Variant per view * kRoleCount + role for the running cast (0 = vanilla clip)
		std::array<std::atomic<int>, kViewCount * kRoleCount> chosen{};

		std::string Lower(std::string_view a_text)
		{
			std::string out(a_text);
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		std::string_view Trim(std::string_view a_text)
		{
			const auto first = a_text.find_first_not_of(" \t\r\n"sv);
			if (first == std::string_view::npos) {
				return {};
			}
			return a_text.substr(first, a_text.find_last_not_of(" \t\r\n"sv) - first + 1);
		}

		// strips a trailing "AND" / "OR" keyword, returns whether it was OR
		bool StripOperator(std::string_view& a_line)
		{
			const auto upper = Lower(a_line);
			for (const auto [keyword, isOr] : { std::pair{ "or"sv, true }, std::pair{ "and"sv, false } }) {
				if (upper.ends_with(keyword) && upper.size() > keyword.size()) {
					const char before = upper[upper.size() - keyword.size() - 1];
					if (before == ' ' || before == '\t' || before == ')') {
						a_line = Trim(a_line.substr(0, a_line.size() - keyword.size()));
						return isOr;
					}
				}
			}
			return false;
		}

		std::optional<Condition> ParseCondition(std::string_view a_line, std::string& a_error)
		{
			Condition condition{};
			if (const auto prefix = Lower(a_line.substr(0, 4)); prefix == "not " || prefix == "not(") {
				condition.negated = true;
				a_line = Trim(a_line.substr(3));
			}
			const auto open = a_line.find('(');
			const auto name = Lower(Trim(a_line.substr(0, open)));
			std::string_view args;
			if (open != std::string_view::npos) {
				const auto close = a_line.rfind(')');
				args = Trim(a_line.substr(open + 1, close == std::string_view::npos || close < open ? std::string_view::npos : close - open - 1));
			}

			auto number = [&](auto& a_out) {
				return std::from_chars(args.data(), args.data() + args.size(), a_out).ec == std::errc{};
			};
			auto unreadable = [&]() {
				a_error = std::format("can't read \"{}\"", a_line);
				return std::nullopt;
			};

			if (name == "isactorbase") {
				// "Skyrim.esm" | 0x00000007
				const auto q1 = args.find('"');
				const auto q2 = q1 == std::string_view::npos ? q1 : args.find('"', q1 + 1);
				const auto bar = args.find('|');
				if (q2 == std::string_view::npos || bar == std::string_view::npos) {
					return unreadable();
				}
				condition.kind = Condition::Kind::kActorBase;
				condition.plugin = std::string(args.substr(q1 + 1, q2 - q1 - 1));
				auto id = Trim(args.substr(bar + 1));
				if (id.starts_with("0x"sv) || id.starts_with("0X"sv)) {
					id.remove_prefix(2);
				}
				if (std::from_chars(id.data(), id.data() + id.size(), condition.localID, 16).ec != std::errc{}) {
					return unreadable();
				}
			} else if (name == "isequippedrighttype" || name == "isequippedlefttype") {
				condition.kind = name == "isequippedrighttype" ? Condition::Kind::kRightType : Condition::Kind::kLeftType;
				if (!number(condition.value)) {
					return unreadable();
				}
			} else if (name == "random") {
				condition.kind = Condition::Kind::kRandom;
				if (!number(condition.chance)) {
					return unreadable();
				}
			} else if (name == "isfemale") {
				condition.kind = Condition::Kind::kFemale;
			} else if (name == "isincombat") {
				condition.kind = Condition::Kind::kInCombat;
			} else if (name == "issneaking") {
				condition.kind = Condition::Kind::kSneaking;
			} else {
				a_error = std::format("condition \"{}\" isn't supported", name);
				return std::nullopt;
			}
			return condition;
		}

		// DAR: lines ending in OR form a group with the next line, groups are ANDed
		bool ParseConditions(const fs::path& a_file, std::vector<std::vector<Condition>>& a_out, std::string& a_error)
		{
			std::ifstream file(a_file);
			if (!file) {
				a_error = "no _conditions.txt";
				return false;
			}
			std::vector<Condition> group;
			std::string            line;
			while (std::getline(file, line)) {
				auto text = Trim(line);
				if (text.empty() || text.starts_with(';')) {
					continue;
				}
				const bool orNext = StripOperator(text);
				auto       condition = ParseCondition(text, a_error);
				if (!condition) {
					return false;
				}
				group.push_back(std::move(*condition));
				if (!orNext) {
					a_out.push_back(std::move(group));
					group.clear();
				}
			}
			if (!group.empty()) {
				a_out.push_back(std::move(group));
			}
			return true;
		}

		// ---- evaluation ------------------------------------------------------------------------------------------

		// The equipped type numbers of DAR / OAR's IsEquippedRightType / IsEquippedLeftType
		int EquippedType(RE::TESForm* a_form)
		{
			if (!a_form) {
				return 0;  // unarmed
			}
			if (const auto weapon = a_form->As<RE::TESObjectWEAP>()) {
				using W = RE::WEAPON_TYPE;
				switch (weapon->GetWeaponType()) {
				case W::kHandToHandMelee:
					return 0;
				case W::kOneHandSword:
					return 1;
				case W::kOneHandDagger:
					return 2;
				case W::kOneHandAxe:
					return 3;
				case W::kOneHandMace:
					return 4;
				case W::kTwoHandSword:
					return 5;
				case W::kTwoHandAxe:
					return weapon->HasKeywordString("WeapTypeWarhammer"sv) ? 10 : 6;
				case W::kBow:
					return 7;
				case W::kStaff:
					return 8;
				case W::kCrossbow:
					return 9;
				default:
					return -1;
				}
			}
			if (a_form->Is(RE::FormType::Armor)) {
				return 11;  // shield
			}
			if (a_form->Is(RE::FormType::Scroll)) {
				return 17;
			}
			if (a_form->Is(RE::FormType::Light)) {
				return 18;
			}
			if (const auto spell = a_form->As<RE::SpellItem>()) {
				switch (spell->GetAssociatedSkill()) {
				case RE::ActorValue::kAlteration:
					return 12;
				case RE::ActorValue::kIllusion:
					return 13;
				case RE::ActorValue::kDestruction:
					return 14;
				case RE::ActorValue::kConjuration:
					return 15;
				case RE::ActorValue::kRestoration:
					return 16;
				default:
					return -1;
				}
			}
			return -1;
		}

		struct CastContext
		{
			RE::PlayerCharacter* player;
			RE::MagicItem*       item;
			CastAnim::Side       side;

			// the casting hand(s) count as holding the hotbar spell, like a vanilla cast; the other hand what it holds
			int HandType(bool a_left) const
			{
				const bool casting = side == CastAnim::Side::kBoth || (side == CastAnim::Side::kLeft) == a_left;
				return EquippedType(casting ? item : player->GetEquippedObject(a_left));
			}
		};

		std::mt19937& Rng()
		{
			static std::mt19937 rng{ std::random_device{}() };
			return rng;
		}

		bool Evaluate(const Condition& a_condition, const CastContext& a_context)
		{
			bool result = false;
			switch (a_condition.kind) {
			case Condition::Kind::kActorBase:
				{
					const auto base = a_context.player->GetActorBase();
					const auto dataHandler = RE::TESDataHandler::GetSingleton();
					result = base && dataHandler && base->GetFormID() == dataHandler->LookupFormID(a_condition.localID, a_condition.plugin);
				}
				break;
			case Condition::Kind::kRightType:
				result = a_context.HandType(false) == a_condition.value;
				break;
			case Condition::Kind::kLeftType:
				result = a_context.HandType(true) == a_condition.value;
				break;
			case Condition::Kind::kRandom:
				result = std::uniform_real_distribution<float>(0.0f, 1.0f)(Rng()) < a_condition.chance;
				break;
			case Condition::Kind::kFemale:
				{
					const auto base = a_context.player->GetActorBase();
					result = base && base->GetSex() == RE::SEX::kFemale;
				}
				break;
			case Condition::Kind::kInCombat:
				result = a_context.player->IsInCombat();
				break;
			case Condition::Kind::kSneaking:
				result = a_context.player->IsSneaking();
				break;
			}
			return result != a_condition.negated;
		}

		bool Evaluate(const Folder& a_folder, const CastContext& a_context)
		{
			return std::ranges::all_of(a_folder.groups, [&](const auto& a_group) {
				return std::ranges::any_of(a_group, [&](const Condition& a_condition) { return Evaluate(a_condition, a_context); });
			});
		}

		// ---- files -----------------------------------------------------------------------------------------------

		std::string ReadFile(const fs::path& a_path)
		{
			std::ifstream file(a_path, std::ios::binary);
			return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
		}

		// Duration of a Skyrim SE animation packfile (hk_2010, 64 bit), like tools/hkx_duration.py
		std::optional<float> HkxDuration(std::string_view a_data)
		{
			auto read = [&](std::size_t a_offset, auto& a_out) {
				if (a_offset + sizeof(a_out) > a_data.size()) {
					return false;
				}
				std::memcpy(&a_out, a_data.data() + a_offset, sizeof(a_out));
				return true;
			};
			std::int32_t sectionCount = 0;
			if (!read(0x14, sectionCount) || sectionCount <= 0 || sectionCount > 16) {
				return std::nullopt;
			}
			std::optional<std::int32_t>     classStart;
			std::optional<std::array<std::int32_t, 7>> data;
			for (std::int32_t i = 0; i < sectionCount; ++i) {
				const std::size_t base = 0x40 + static_cast<std::size_t>(i) * 0x30;
				if (base + 0x30 > a_data.size()) {
					return std::nullopt;
				}
				const auto                  name = std::string_view(a_data.data() + base, 20);
				std::array<std::int32_t, 7> header{};
				read(base + 0x14, header);
				if (name.starts_with("__classnames__"sv)) {
					classStart = header[0];
				} else if (name.starts_with("__data__"sv)) {
					data = header;
				}
			}
			if (!classStart || !data) {
				return std::nullopt;
			}
			// virtual fixups: (object offset, section, class name offset) up to the exports
			const auto dataStart = static_cast<std::size_t>((*data)[0]);
			for (auto pos = dataStart + (*data)[3]; pos + 12 <= dataStart + (*data)[4]; pos += 12) {
				std::array<std::int32_t, 3> fixup{};
				read(pos, fixup);
				if (fixup[0] == -1) {
					continue;
				}
				const auto nameStart = static_cast<std::size_t>(*classStart) + fixup[2];
				if (nameStart >= a_data.size()) {
					continue;
				}
				const auto rest = a_data.substr(nameStart);
				const auto cls = rest.substr(0, rest.find('\0'));
				if (cls.starts_with("hka"sv) && cls.ends_with("Animation"sv) && cls.find("Binding"sv) == std::string_view::npos) {
					float duration = 0.0f;  // hkReferencedObject (0x10) + type (int32)
					if (read(dataStart + fixup[0] + 0x14, duration) && duration > 0.0f && duration < 60.0f) {
						return duration;
					}
					return std::nullopt;
				}
			}
			return std::nullopt;
		}

		std::uint64_t Fnv1a(std::string_view a_text, std::uint64_t a_hash = 14695981039346656037ull)
		{
			for (const unsigned char c : a_text) {
				a_hash = (a_hash ^ c) * 1099511628211ull;
			}
			return a_hash;
		}

		// The DAR folders animation patches enable: "darFolders": [ 996, ... ] in Data\SKSE\Plugins\SpellHotbarNG\animations\*.json
		std::set<int> PatchedFolders()
		{
			std::set<int>   out;
			std::error_code ec;
			const auto      dir = Config::DataDir() / "animations";
			if (!fs::is_directory(dir, ec)) {
				return out;
			}
			for (const auto& entry : fs::directory_iterator(dir, ec)) {
				if (!entry.is_regular_file(ec) || entry.path().extension() != ".json") {
					continue;
				}
				try {
					std::ifstream file(entry.path());
					const auto    j = json::parse(file, nullptr, true, true);
					for (const auto& folder : j.value("darFolders", json::array())) {
						if (folder.is_number_integer()) {
							out.insert(folder.get<int>());
						}
					}
				} catch (const std::exception& e) {
					logs::error("Failed to read animation patch {}: {}", entry.path().string(), e.what());
				}
			}
			return out;
		}

		void ScanFolders(const std::set<int>& a_patched)
		{
			std::array<std::unordered_set<std::string>, kViewCount> wanted;
			for (const auto& row : kRows) {
				for (const int role : { kCharge, kRelease, kReady }) {
					wanted[1].insert(std::string(Clip(row, role)));
					if (role != kReady) {
						wanted[0].insert(std::string(Clip(row, role)));
					}
				}
			}
			for (const auto& clip : kLocomotionClips) {
				wanted[0].insert(std::string(clip.second));
			}

			std::error_code ec;
			for (int view = 0; view < kViewCount; ++view) {
				const fs::path root{ kDarRoots[view] };
				if (!fs::is_directory(root, ec)) {
					continue;
				}
				for (const auto& entry : fs::directory_iterator(root, ec)) {
					if (!entry.is_directory(ec)) {
						continue;
					}
					const auto name = entry.path().filename().string();
					int        priority = 0;
					if (name.find_first_not_of("-0123456789"sv) != std::string::npos ||
						std::from_chars(name.data(), name.data() + name.size(), priority).ec != std::errc{} || !a_patched.contains(priority)) {
						continue;
					}
					Folder folder{ .view = view, .priority = priority, .dir = entry.path() };
					for (const auto& file : fs::directory_iterator(entry.path(), ec)) {
						auto lower = Lower(file.path().filename().string());
						if (file.is_regular_file(ec) && wanted[view].contains(lower)) {
							folder.clips.emplace(std::move(lower), file.path());
						}
					}
					if (folder.clips.empty()) {
						continue;
					}
					// a submod the user turned off in OAR's menu
					if (const auto user = entry.path() / "user.json"; fs::exists(user, ec)) {
						try {
							std::ifstream file(user);
							if (json::parse(file, nullptr, true, true).value("disabled", false)) {
								logs::info("Casting animations: {} is disabled in OAR, skipped", entry.path().string());
								continue;
							}
						} catch (const std::exception&) {
						}
					}
					std::string error;
					if (!ParseConditions(entry.path() / "_conditions.txt", folder.groups, error)) {
						logs::warn("Casting animations: {} skipped ({})", entry.path().string(), error);
						continue;
					}
					logs::info("Casting animations: {} ({} clips)", entry.path().string(), folder.clips.size());
					folders.push_back(std::move(folder));
				}
			}
			std::ranges::stable_sort(folders, std::greater{}, &Folder::priority);

			for (std::size_t row = 0; row < kRows.size(); ++row) {
				for (int view = 0; view < kViewCount; ++view) {
					for (int role = 0; role < kRoleCount; ++role) {
						if (role == kLocomotion || (view == 0 && role == kReady)) {
							continue;
						}
						const auto clip = std::string(Clip(kRows[row], role));
						for (std::size_t f = 0; f < folders.size(); ++f) {
							const auto it = folders[f].clips.find(clip);
							if (folders[f].view != view || it == folders[f].clips.end()) {
								continue;
							}
							Source source{ .row = row, .view = view, .role = role, .folder = f, .file = it->second };
							if (role == kRelease) {
								source.duration = HkxDuration(ReadFile(source.file)).value_or(0.0f);
							}
							candidates[row][view][role].push_back(sources.size());
							sources.push_back(std::move(source));
						}
					}
				}
			}

			for (std::size_t f = 0; f < folders.size(); ++f) {
				const bool hasLocomotion = folders[f].view == 0 && std::ranges::any_of(kLocomotionClips, [&](const auto& a_clip) {
					return folders[f].clips.contains(std::string(a_clip.second));
				});
				if (hasLocomotion) {
					locomotionCandidates.push_back(sources.size());
					sources.push_back({ .row = 0, .view = 0, .role = kLocomotion, .folder = f });
				}
			}
		}

		// a_type 0 = any cast; a_phase 0 = no shout state check, -1 = shout state in any phase
		json CastingCondition(int a_type, int a_hand, int a_variant, int a_phase)
		{
			json condition{
				{ "condition", "SpellHotbarNG_Casting" },
				{ "requiredPlugin", "SpellHotbarNG" },
				{ "requiredVersion", kConditionVersion },
				{ "Animation", { { "value", static_cast<float>(a_type) } } },
				{ "Variant", { { "value", static_cast<float>(a_variant) } } },
			};
			if (a_hand != kHandAny) {
				condition["Hand"] = { { "value", static_cast<float>(a_hand) } };
			}
			if (a_phase) {
				condition["ShoutState"] = { { "value", 1.0f } };
			}
			if (a_phase > 0) {
				condition["Phase"] = { { "value", static_cast<float>(a_phase) } };
			}
			return condition;
		}

		struct OutFile
		{
			fs::path    path;     // relative to kOutRoot
			std::string text;     // config.json
			fs::path    source;   // or a copied clip
		};

		std::vector<OutFile> PlanOutput()
		{
			std::vector<OutFile> out;
			out.push_back({ "config.json", json{
				{ "name", "Spell Hotbar NG - installed casting animations" },
				{ "author", "Spell Hotbar NG" },
				{ "description", "Generated by Spell Hotbar NG at game start from the casting animation mods you have installed, so "
				                 "hotbar casts use them too. Rebuilt automatically, don't edit." },
			}.dump(4) });

			const auto moveTargets = MoveTargets();
			int        count = 0;
			for (std::size_t i = 0; i < sources.size(); ++i) {
				const auto& source = sources[i];
				const auto& row = kRows[source.row];
				const auto& folder = folders[source.folder];
				const auto  variant = static_cast<int>(i) + 1;

				if (source.role == kLocomotion) {
					const auto name = std::format("locomotion_{}", folder.priority);
					const json config{
						{ "name", name },
						{ "description", std::format("walk / run while casting, from {}", folder.dir.generic_string()) },
						{ "priority", kPriorityBase + count++ },
						{ "interruptible", false },
						{ "conditions", json::array({ CastingCondition(0, kHandAny, variant, -1) }) },
					};
					out.push_back({ fs::path(name) / "config.json", config.dump(4) });
					for (const auto& [target, clip] : kLocomotionClips) {
						if (const auto it = folder.clips.find(std::string(clip)); it != folder.clips.end()) {
							for (const auto dir : kLocomotionDirs) {
								out.push_back({ .path = fs::path(name) / dir / target, .source = it->second });
							}
						}
					}
					continue;
				}

				// the shout clip (charge / release), and in 1st person also the walk / run clips while the shout state runs
				for (const bool move : { false, true }) {
					if (move ? source.view != 1 : source.role == kReady) {
						continue;
					}
					const auto name = std::format("{}_{}{}{}_{}", row.folder, kRoleNames[source.role], source.view == 1 ? "_1st"sv : ""sv,
						move ? "_move"sv : ""sv, folder.priority);
					json config{
						{ "name", name },
						{ "description", std::format("{} clip from {}", kRoleNames[source.role], folder.dir.generic_string()) },
						{ "priority", kPriorityBase + count++ },
						{ "interruptible", move },
						{ "conditions", json::array({ CastingCondition(static_cast<int>(row.type), row.hand, variant, move ? source.role + 1 : 0) }) },
					};
					if (move) {
						config["hasCustomBlendTimeOnInterrupt"] = true;
						config["blendTimeOnInterrupt"] = kMoveBlendTime;
					}
					out.push_back({ fs::path(name) / "config.json", config.dump(4) });

					const auto dir = fs::path(name) / kViewDirs[source.view];
					if (move) {
						for (const auto& target : moveTargets) {
							out.push_back({ .path = dir / target, .source = source.file });
						}
					} else {
						for (const auto target : source.role == kCharge ? kInhaleTargets : kExhaleTargets) {
							out.push_back({ .path = dir / target, .source = source.file });
						}
					}
				}
			}
			return out;
		}

		// skips the rewrite when nothing changed since the last start (same files, sizes and dates)
		std::string Signature(const std::vector<OutFile>& a_files)
		{
			std::uint64_t   hash = Fnv1a(std::to_string(kGeneratorVersion));
			std::error_code ec;
			for (const auto& file : a_files) {
				hash = Fnv1a(file.path.generic_string(), hash);
				if (file.source.empty()) {
					hash = Fnv1a(file.text, hash);
				} else {
					hash = Fnv1a(file.source.generic_string(), hash);
					hash = Fnv1a(std::to_string(fs::file_size(file.source, ec)), hash);
					hash = Fnv1a(std::to_string(fs::last_write_time(file.source, ec).time_since_epoch().count()), hash);
				}
			}
			return std::format("{:016x}", hash);
		}

		void WriteOutput()
		{
			const fs::path  root{ kOutRoot };
			std::error_code ec;
			if (sources.empty()) {
				if (fs::exists(root, ec)) {
					fs::remove_all(root, ec);
					logs::info("Casting animations: no replacers installed, removed {}", root.string());
				}
				return;
			}

			const auto files = PlanOutput();
			const auto signature = Signature(files);
			if (std::ifstream manifest(root / kManifest); manifest) {
				std::string previous;
				std::getline(manifest, previous);
				if (previous == signature) {
					logs::info("Casting animations: {} is up to date", root.string());
					return;
				}
			}

			fs::remove_all(root, ec);
			std::unordered_map<std::string, std::string> clipCache;  // source path -> bytes
			std::size_t                                  written = 0;
			for (const auto& file : files) {
				const auto path = root / file.path;
				fs::create_directories(path.parent_path(), ec);
				std::ofstream out(path, std::ios::binary | std::ios::trunc);
				if (file.source.empty()) {
					out << file.text;
				} else {
					auto& bytes = clipCache[file.source.string()];
					if (bytes.empty()) {
						bytes = ReadFile(file.source);
					}
					out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
				}
				written += static_cast<bool>(out);
			}
			std::ofstream(root / kManifest) << signature << '\n';
			logs::info("Casting animations: wrote {} of {} files to {}", written, files.size(), root.string());
		}
	}

	void Generate()
	{
		try {
			if (const auto patched = PatchedFolders(); !patched.empty()) {
				ScanFolders(patched);
			}
			logs::info("Casting animations: {} replacer clips in {} folders", sources.size(), folders.size());
			WriteOutput();
		} catch (const std::exception& e) {
			logs::error("Casting animations: generating the OAR submods failed: {}", e.what());
		}
	}

	void Choose(CastAnim::Type a_type, CastAnim::Side a_side, RE::MagicItem* a_item)
	{
		Clear();
		const auto player = RE::PlayerCharacter::GetSingleton();
		if (sources.empty() || !player || !a_item) {
			return;
		}
		// the hand specific row when there is one, like OAR's priorities do with our own submods
		const auto wantedHand = a_side == CastAnim::Side::kLeft ? kHandLeft : kHandAny;
		std::optional<std::size_t> row;
		for (std::size_t i = 0; i < kRows.size(); ++i) {
			if (kRows[i].type == a_type && (kRows[i].hand == wantedHand || (!row && kRows[i].hand == kHandAny))) {
				row = i;
			}
		}
		if (!row) {
			return;
		}

		const CastContext                 context{ player, a_item, a_side };
		std::unordered_map<std::size_t, bool> results;  // each folder once per cast (Random rolls once)
		for (int view = 0; view < kViewCount; ++view) {
			for (int role = 0; role < kRoleCount; ++role) {
				if (role == kLocomotion && view != 0) {
					continue;
				}
				const auto& list = role == kLocomotion ? locomotionCandidates : candidates[*row][view][role];
				for (const auto index : list) {
					const auto folder = sources[index].folder;
					auto       it = results.find(folder);
					if (it == results.end()) {
						it = results.emplace(folder, Evaluate(folders[folder], context)).first;
					}
					if (it->second) {
						chosen[view * kRoleCount + role] = static_cast<int>(index) + 1;
						logs::debug("Casting animation {} ({}): {} from {}", kRows[*row].folder, view ? "1st person"sv : "3rd person"sv,
							kRoleNames[role], folders[folder].dir.string());
						break;
					}
				}
			}
		}
	}

	State Save()
	{
		State state{};
		for (std::size_t i = 0; i < chosen.size(); ++i) {
			state[i] = chosen[i].load();
		}
		return state;
	}

	void Restore(const State& a_state)
	{
		for (std::size_t i = 0; i < chosen.size(); ++i) {
			chosen[i] = a_state[i];
		}
	}

	void Clear()
	{
		for (auto& value : chosen) {
			value = 0;
		}
	}

	bool Matches(int a_variant)
	{
		if (a_variant < 1 || static_cast<std::size_t>(a_variant) > sources.size()) {
			return false;
		}
		const auto& source = sources[a_variant - 1];
		return chosen[source.view * kRoleCount + source.role].load() == a_variant;
	}

	std::optional<float> ReleaseDuration(bool a_firstPerson)
	{
		const auto variant = chosen[(a_firstPerson ? 1 : 0) * kRoleCount + kRelease].load();
		if (variant < 1 || static_cast<std::size_t>(variant) > sources.size() || sources[variant - 1].duration <= 0.0f) {
			return std::nullopt;
		}
		return sources[variant - 1].duration;
	}
}
