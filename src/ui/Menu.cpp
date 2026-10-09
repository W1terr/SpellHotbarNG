#include "core/Bindings.h"
#include "core/Config.h"
#include "core/Hotkeys.h"
#include "ui/Framework.h"
#include "ui/Icons.h"
#include "ui/InventoryIcons.h"
#include "core/Input.h"
#include "core/Keys.h"
#include "core/Lang.h"
#include "ui/UI.h"

namespace UI
{
	namespace
	{
		using Lang::F;
		using Lang::T;

		constexpr const char* kAnchorNames[] = { "Top Left", "Top", "Top Right", "Left", "Center", "Right", "Bottom Left", "Bottom", "Bottom Right" };
		constexpr const char* kVisibilityNames[] = { "Always", "In combat", "Weapons / magic drawn", "In combat or drawn", "Never (only in menus)", "While sneaking",
			"After pressing a hotbar key" };
		constexpr const char* kExtraBarModeNames[] = { "hold", "press" };
		constexpr const char* kMainBarModeNames[] = { "press", "key combo", "hold" };  // order of MainBarKeyMode
		constexpr const char* kHandNames[] = { "Hand|Auto", "Hand|Right", "Hand|Left", "Hand|Both" };  // order of Hand
		constexpr const char* kKeyModeNames[] = { "Cast right away", "Equip", "Oblivion style" };       // order of KeyMode
		constexpr const char* kPictureSourceNames[] = { "All", "Spell Hotbar NG", "SpellHotbar2 icon packs" };  // icon picker filter
		constexpr const char* kIconStyleNames[] = { "Spell Hotbar icons", "Inventory icons for items", "Inventory icons for everything" };  // order of IconStyle

		// Translated texts of a fixed list (combo items)
		template <std::size_t N>
		std::array<const char*, N> Translated(const char* const (&a_english)[N])
		{
			std::array<const char*, N> out{};
			std::ranges::transform(a_english, out.begin(), T);
			return out;
		}

		// Widest of the texts, for lining up what follows them (translations differ in length)
		float TextWidth(std::initializer_list<std::string_view> a_texts)
		{
			float width = 0.0f;
			for (const auto text : a_texts) {
				width = std::max(width, CalcTextSize(text.data(), text.data() + text.size()).x);
			}
			return width;
		}

		// Translated label with a fixed ImGui id, so the widget keeps its state when the language changes
		std::string Id(const char* a_english, std::string_view a_id)
		{
			return std::format("{}###{}", T(a_english), a_id);
		}

		const ImVec4 kGrey{ 0.6f, 0.6f, 0.6f, 1.0f };
		const ImVec4 kGold{ 1.0f, 0.82f, 0.4f, 1.0f };

		void Changed(bool a_changed)
		{
			if (a_changed) {
				Config::Validate(Config::Get());
				Config::MarkDirty();
			}
		}

		void Help(const char* a_text)
		{
			SameLine();
			TextDisabled("(?)");
			if (IsItemHovered()) {
				SetTooltip("%s", T(a_text));
			}
		}

		// What to press for a key of the settings (also where Hotkey Atlas moved it), "-" for none
		std::string KeyLabel(const std::uint32_t& a_key)
		{
			const auto label = Hotkeys::Label(Config::Get(), a_key);
			return label.empty() ? "-" : label;
		}

		// Button showing a key of the settings; clicking it waits for the next key press (Input stores it
		// and marks the settings dirty)
		void KeyButton(const char* a_id, std::uint32_t* a_key)
		{
			const bool capturing = Input::CaptureTarget() == a_key;
			const auto label = std::format("{}###{}", capturing ? T("Press a key...") : KeyLabel(*a_key), a_id);
			if (Button(label.c_str(), ImVec2{ 130.0f, 0.0f })) {
				if (capturing) {
					Input::CancelCapture();
				} else {
					Input::BeginCapture(a_key);
				}
			}
			if (IsItemHovered()) {
				std::string tip = T("Click, then press any key, mouse button or controller button.\nEsc: cancel    Delete: remove the key");
				if (Hotkeys::MovedInHotkeyAtlas(Config::Get(), *a_key)) {
					tip = std::format("{}\n\n{}", tip, T("Changed in Hotkey Atlas. Picking a key here replaces that change."));
				}
				SetTooltip("%s", tip.c_str());
			}
		}

		void ColorOption(const char* a_label, RGBA& a_color, const char* a_help = nullptr)
		{
			Changed(ColorEdit4(Id(a_label, a_label).c_str(), a_color.data(),
				ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf));
			if (a_help) {
				Help(a_help);
			}
		}

		// modifier pages only exist while their modifier is enabled
		bool PageEnabled(const Settings& a_settings, Page a_page)
		{
			return a_page == Page::kMain || a_settings.modifierEnabled[static_cast<int>(a_page) - static_cast<int>(Page::kModifier1)];
		}

		void IconImage(RE::TESForm* a_form, float a_size)
		{
			const auto icon = a_form ? Icons::ForForm(a_form) : Icons::Named("BAR_EMPTY");
			const auto texture = icon.texture ? icon.texture : AtlasTexture(icon.atlas);
			if (texture) {
				const auto channel = [&](int a_shift) { return ((icon.rgb >> a_shift) & 0xFF) / 255.0f; };
				Image(texture, ImVec2{ a_size, a_size }, ImVec2{ icon.u0, icon.v0 }, ImVec2{ icon.u1, icon.v1 },
					ImVec4{ channel(16), channel(8), channel(0), 1.0f });
			} else {
				Dummy(ImVec2{ a_size, a_size });
			}
		}

		std::string FormLabel(RE::TESForm* a_form)
		{
			if (!a_form) {
				return {};
			}
			const char* name = a_form->GetName();
			return name && *name ? name : std::format("{:08X}", a_form->GetFormID());
		}

		const char* TypeName(RE::TESForm* a_form)
		{
			switch (a_form->GetFormType()) {
			case RE::FormType::Spell:
				switch (a_form->As<RE::SpellItem>()->GetSpellType()) {
				case RE::MagicSystem::SpellType::kPower:
					return T("Power");
				case RE::MagicSystem::SpellType::kLesserPower:
					return T("Lesser Power");
				case RE::MagicSystem::SpellType::kVoicePower:
					return T("Voice Power");
				default:
					return T("Spell");
				}
			case RE::FormType::Scroll:
				return T("Scroll");
			case RE::FormType::Shout:
				return T("Shout");
			case RE::FormType::AlchemyItem:
				{
					const auto alch = a_form->As<RE::AlchemyItem>();
					return T(alch->IsFood() ? "Food" : alch->IsPoison() ? "Poison" : "Potion");
				}
			case RE::FormType::Weapon:
				return T("Weapon");
			case RE::FormType::Armor:
				return T("Apparel");
			default:
				return T("Item");
			}
		}

		int bindingsPage{ 0 };  // page tab selected on the Bindings page

		// ---- profiles -----------------------------------------------------------------------

		char                                  profileName[128]{ "My Profile" };
		bool                                  saveBindings{ true };
		bool                                  loadBindings{ true };
		int                                   selectedProfile{ -1 };
		std::string                           profileStatus;
		std::vector<Profiles::Entry>          profiles;
		std::chrono::steady_clock::time_point profilesScanned{};

		void LanguageOption()
		{
			SeparatorText(T("Language"));
			const auto  languages = Lang::All();
			const auto& current = Lang::Current();

			std::vector<const char*> names;
			for (const auto& language : languages) {
				names.push_back(language.name);
			}
			int index = static_cast<int>(&current - languages.data());
			SetNextItemWidth(260.0f);
			if (Combo("###language", &index, names.data(), static_cast<int>(names.size()))) {
				Lang::Set(languages[index].id);
				Config::MarkDirty();
				PageNamesOutdated();
			}
			if (!CanRenamePages()) {
				Help("The names of the pages on the left change after you restart the game.");
			}

			// without the right glyphs the framework draws "?" for every letter, so this is also shown in English
			if (const auto glyphs = Lang::Current().glyphs) {
				constexpr auto kGlyphHint = "If you see ? instead of letters: in SKSE Menu Framework open Options > Open Settings and set Character Glyphs to {}.";
				TextWrapped("%s", F(kGlyphHint, glyphs).c_str());
				TextDisabled("%s", std::vformat(kGlyphHint, std::make_format_args(glyphs)).c_str());
			}
		}

		// rescanned every few seconds (files may be added by hand) and after every change
		void RefreshProfiles(bool a_force)
		{
			const auto now = std::chrono::steady_clock::now();
			if (a_force || now - profilesScanned > 2s) {
				profiles = Profiles::List();
				profilesScanned = now;
			}
		}
	}

	namespace
	{
		bool previewMenuPosition{ false };

		// The widget just drawn belongs to the in-game position (false) or the menu position (true): while the player
		// points at or drags it, the settings preview shows the bars at that place
		void PreviewGroup(bool a_menu)
		{
			if (IsItemHovered() || IsItemActive()) {
				previewMenuPosition = a_menu;
			}
		}

		// Anchor combo + offset sliders; a_id prefixes the widget ids ("" = the main bar's own ids)
		void PositionOptions(Anchor& a_anchor, float& a_x, float& a_y, std::string_view a_id, bool a_menu)
		{
			const auto anchorNames = Translated(kAnchorNames);
			const auto id = [&](std::string_view a_name) {
				return a_id.empty() ? std::string(a_name) : std::format("{}{}{}", a_id, static_cast<char>(std::toupper(a_name[0])), a_name.substr(1));
			};
			int anchor = static_cast<int>(a_anchor);
			if (Combo(Id("Anchor", id("anchor")).c_str(), &anchor, anchorNames.data(), static_cast<int>(anchorNames.size()))) {
				a_anchor = static_cast<Anchor>(anchor);
				Changed(true);
			}
			PreviewGroup(a_menu);
			Changed(SliderFloat(Id("Offset X", id("offsetX")).c_str(), &a_x, -1920.0f, 1920.0f, "%.0f px"));
			PreviewGroup(a_menu);
			Changed(SliderFloat(Id("Offset Y", id("offsetY")).c_str(), &a_y, -1080.0f, 1080.0f, "%.0f px"));
			PreviewGroup(a_menu);
		}
	}

	bool PreviewMenuPosition()
	{
		return previewMenuPosition;
	}

	void __stdcall RenderBarPage()
	{
		std::scoped_lock lock(Config::Lock());
		auto&            s = Config::Get();

		TextWrapped("%s", T("The bar is previewed live while this menu is open. Sizes are in pixels at 1080p and scale with your resolution."));
		SeparatorText(T("Size"));
		Changed(SliderInt(Id("Slots", "slots").c_str(), &s.slotCount, 1, kMaxSlots));
		Changed(SliderInt(Id("Columns", "columns").c_str(), &s.columns, 1, kMaxSlots));
		Help("Slots per row. Set it to 1 for a vertical bar.");
		Changed(SliderFloat(Id("Icon size", "iconSize").c_str(), &s.iconSize, 16.0f, 200.0f, "%.0f px"));
		Changed(SliderFloat(Id("Spacing", "spacing").c_str(), &s.spacing, 0.0f, 60.0f, "%.0f px"));

		SeparatorText(T("Position"));
		PositionOptions(s.anchor, s.offsetX, s.offsetY, "", false);
		if (Button(Id("Reset position", "resetPosition").c_str())) {
			s.anchor = Anchor::kBottom;
			s.offsetX = 0.0f;
			s.offsetY = -100.0f;
			Changed(true);
		}
		PreviewGroup(false);

		if (s.keyMode == KeyMode::kOblivion) {
			SeparatorText(T("Oblivion style bar"));
			TextDisabled("%s", T("Shows the picked spell, the picked potion and your power. It follows the Show bar setting below."));
			PositionOptions(s.readyAnchor, s.readyOffsetX, s.readyOffsetY, "ready", false);
			Changed(Checkbox(Id("Show power", "readyShowPower").c_str(), &s.readyShowPower));
			Help("A third slot with your current power or shout, labelled with the game's Shout key.");
			Changed(Checkbox(Id("Vertical", "readyVertical").c_str(), &s.readyVertical));
			Changed(Checkbox(Id("Hide the main bar until I pick a spell", "readyHideMainBar").c_str(), &s.readyHideMainBar));
			Help("On: only this small bar stays on screen. The main bar shows up for a moment when you\n"
				 "press a slot key (also when binding in the Magic / Inventory menu).\n"
				 "Off: both bars are shown.");
			if (Button(Id("Reset position", "readyResetPosition").c_str())) {
				const Settings defaults{};
				s.readyAnchor = defaults.readyAnchor;
				s.readyOffsetX = defaults.readyOffsetX;
				s.readyOffsetY = defaults.readyOffsetY;
				Changed(true);
			}
			PreviewGroup(false);
		}

		SeparatorText(T("Visibility"));
		const auto visibilityNames = Translated(kVisibilityNames);
		int        visibility = static_cast<int>(s.visibility);
		if (Combo(Id("Show bar", "visibility").c_str(), &visibility, visibilityNames.data(), static_cast<int>(visibilityNames.size()))) {
			s.visibility = static_cast<Visibility>(visibility);
			Changed(true);
		}
		if (s.visibility == Visibility::kKeyPress) {
			Changed(SliderFloat(Id("Show for", "keyPressShowTime").c_str(), &s.keyPressShowTime, 0.5f, 10.0f, "%.1f s"));
			Help("The bar shows up while you hold a slot key (or the Oblivion style Cast / Potion key)\n"
				 "and while a hotbar spell charges, then stays this long before it fades out.");
		}
		Changed(SliderFloat(Id("Opacity", "opacity").c_str(), &s.opacity, 0.05f, 1.0f, "%.2f"));
		Changed(Checkbox(Id("Fade out of combat", "fadeOutOfCombat").c_str(), &s.fadeOutOfCombat));
		if (s.fadeOutOfCombat) {
			Changed(SliderFloat(Id("Faded opacity", "fadedOpacity").c_str(), &s.fadedOpacity, 0.0f, 1.0f, "%.2f"));
		}
		Changed(Checkbox(Id("Show in magic / inventory / favorites menu", "showInMenus").c_str(), &s.showInMenus));
		if (s.showInMenus) {
			if (Checkbox(Id("Own position in these menus", "menuPosition").c_str(), &s.menuPosition)) {
				if (s.menuPosition) {
					// start from where the bars are now
					s.menuAnchor = s.anchor;
					s.menuOffsetX = s.offsetX;
					s.menuOffsetY = s.offsetY;
					s.menuReadyAnchor = s.readyAnchor;
					s.menuReadyOffsetX = s.readyOffsetX;
					s.menuReadyOffsetY = s.readyOffsetY;
				}
				Changed(true);
			}
			Help("On: in the magic, inventory and favorites menus the bar moves to the place set here,\n"
				 "so it doesn't cover the item list. The preview shows that place while you change it.");
			PreviewGroup(true);
			if (s.menuPosition) {
				Indent();
				PositionOptions(s.menuAnchor, s.menuOffsetX, s.menuOffsetY, "menu", true);
				if (s.keyMode == KeyMode::kOblivion) {
					TextDisabled("%s", T("Oblivion style bar"));
					PositionOptions(s.menuReadyAnchor, s.menuReadyOffsetX, s.menuReadyOffsetY, "menuReady", true);
				}
				Unindent();
			}
		}

		SeparatorText(T("Look"));
		Changed(Checkbox(Id("Show empty slots", "showEmptySlots").c_str(), &s.showEmptySlots));
		Changed(Checkbox(Id("Show key labels", "showKeyLabels").c_str(), &s.showKeyLabels));
		Changed(Checkbox(Id("Show which extra bar is active", "showPageName").c_str(), &s.showPageName));
		Changed(Checkbox(Id("Show cooldown seconds", "showCooldownText").c_str(), &s.showCooldownText));
		Changed(Checkbox(Id("Show item counts", "showItemCount").c_str(), &s.showItemCount));

		SeparatorText(T("Colors"));
		TextDisabled("%s", T("Click a color square to open the picker. The alpha bar sets each color's own transparency."));
		auto& c = s.colors;
		ColorOption("Frame", c.frame, "Tint of the slot border. White keeps the original look.");
		ColorOption("Slot background", c.slotBackground, "Color behind the icons. Black is the original look.");
		ColorOption("Key labels", c.keyLabel);
		ColorOption("Text", c.text, "Item counts, cooldown seconds and the extra bar's key above the bar.");
		ColorOption("Hand marker", c.handMarker, "The L / R / D letter of slots bound to a specific hand.");
		ColorOption("Equipped highlight", c.equipped);
		ColorOption("Cooldown overlay", c.cooldown);
		ColorOption("Charge bar", c.chargeBar);
		ColorOption("Channeling glow", c.channeling, "Highlight of a running concentration spell.");
		ColorOption("Not enough magicka", c.noMagicka, "Icon tint when the spell costs more magicka than you have.");
		if (Button(Id("Reset colors", "resetColors").c_str())) {
			c = BarColors{};
			Changed(true);
		}
	}

	namespace
	{
		// "Hotkeys" tab of the Bindings page: which key uses which slot, extra bars (modifiers), game keys
		void HotkeysTab(Settings& s)
		{
			SeparatorText(T("What slot keys do"));
			int mode = static_cast<int>(s.keyMode);
			RadioButton(Id("Cast right away", "modeCast").c_str(), &mode, static_cast<int>(KeyMode::kCast));
			Help("Pressing a slot casts its spell with the casting animation. Nothing gets equipped,\n"
				 "your weapons stay in your hands. Powers and shouts are used right away too.");
			RadioButton(Id("Equip", "modeEquip").c_str(), &mode, static_cast<int>(KeyMode::kEquip));
			Help("Pressing a slot equips its spell or scroll in the slot's hand (right unless you changed it\n"
				 "in the Magic menu). Powers and shouts are equipped to your Shout key.\n"
				 "You then cast them with the game's normal attack / shout buttons.");
			RadioButton(Id("Oblivion style", "modeOblivion").c_str(), &mode, static_cast<int>(KeyMode::kOblivion));
			Help("Pressing a slot picks its spell, then the Cast key casts the picked spell with the\n"
				 "casting animation. Your hands keep their weapons - great with two-handed weapons.\n"
				 "Potions are picked for the Potion key. Powers and shouts are equipped to your Shout key.\n"
				 "Weapons, armor and food always work as usual.");
			if (mode != static_cast<int>(s.keyMode)) {
				s.keyMode = static_cast<KeyMode>(mode);
				Changed(true);
			}
			if (s.keyMode == KeyMode::kOblivion) {
				Indent();
				const float keyColumn = GetCursorPosX() + TextWidth({ T("Cast key"), T("Potion key") }) + GetStyle()->ItemSpacing.x * 2.0f;
				AlignTextToFramePadding();
				Text("%s", T("Cast key"));
				SameLine(keyColumn);
				KeyButton("castKey", &s.castKey);
				Help("Casts the picked spell. Hold it for concentration spells.");
				AlignTextToFramePadding();
				Text("%s", T("Potion key"));
				SameLine(keyColumn);
				KeyButton("potionKey", &s.potionKey);
				Help("Uses the picked potion.");
				TextDisabled("%s", T("The picked spell and potion have their own small bar, see Bar Layout."));
				Unindent();
			}

			SeparatorText(T("Slot keys"));
			TextWrapped("%s", T("Click a slot's button, then press the key you want for it. Keyboard keys, mouse buttons and "
								"controller buttons all work."));
			if (Button(Id("Remove all keys", "removeAllKeys").c_str())) {
				s.slotKeys.fill(0);
				Changed(true);
			}

			if (BeginTable("##slotkeys", 4, ImGuiTableFlags_SizingFixedFit)) {
				for (int i = 0; i < s.slotCount; ++i) {
					TableNextColumn();
					AlignTextToFramePadding();
					Text("%s", F("Slot {}", i + 1).c_str());
					SameLine();
					KeyButton(std::format("slot{}", i).c_str(), &s.slotKeys[i]);
				}
				EndTable();
			}
			TextDisabled("%s", F("Your bar has {} slots. You can change that in Bar Layout.", s.slotCount).c_str());

			SeparatorText(T("Main bar key"));
			Changed(Checkbox(Id("Open the bar with a key", "mainBarKeyEnabled").c_str(), &s.mainBarKeyEnabled));
			Help("On: the bar stays hidden and the slot keys do what Skyrim normally does with them\n"
				 "until you open the bar with this key.\n"
				 "Binding in the Magic / Inventory menu works the same as always.\n"
				 "In Oblivion style the Cast and Potion keys keep working while the bar is closed.");
			if (s.mainBarKeyEnabled) {
				Indent();
				const auto mainModeNames = Translated(kMainBarModeNames);
				int        mainMode = static_cast<int>(s.mainBarKeyMode);
				SetNextItemWidth(TextWidth({ mainModeNames[0], mainModeNames[1], mainModeNames[2] }) + GetFrameHeight() +
								 GetStyle()->FramePadding.x * 2.0f);
				if (Combo("##mainBarMode", &mainMode, mainModeNames.data(), static_cast<int>(mainModeNames.size()))) {
					s.mainBarKeyMode = static_cast<MainBarKeyMode>(mainMode);
					Changed(true);
				}
				SameLine();
				if (s.mainBarKeyMode == MainBarKeyMode::kCombo) {
					KeyButton("mainBarModifier", &s.mainBarModifier);
					SameLine();
					AlignTextToFramePadding();
					Text("+");
					SameLine();
				}
				KeyButton("mainBarKey", &s.mainBarKey);
				Help("press: one press of the key opens the bar, the next press closes it.\n"
					 "key combo: the same, but only while you hold the first key, like Shift + H.\n"
					 "hold: the bar is open while you hold the key, like holding Shift to use Shift + 1.\n"
					 "The held key also keeps working in the game.");
				Unindent();
			}

			SeparatorText(T("Extra bars"));
			TextWrapped("%s", T("Hold a key to switch to another set of spells, like Shift + 1 instead of 1. "
								"Each extra bar you turn on gets its own tab."));
			const auto  modeNames = Translated(kExtraBarModeNames);
			const float modeColumn = GetCursorPosX() + GetFrameHeight() + GetStyle()->ItemInnerSpacing.x +
			                         TextWidth({ F("Extra bar {}", kModifierCount) }) + GetStyle()->ItemSpacing.x * 2.0f;
			const float modeWidth = TextWidth({ modeNames[0], modeNames[1] }) + GetFrameHeight() + GetStyle()->FramePadding.x * 2.0f;
			for (int i = 0; i < kModifierCount; ++i) {
				PushID(i);
				Changed(Checkbox(std::format("{}###enabled", F("Extra bar {}", i + 1)).c_str(), &s.modifierEnabled[i]));
				SameLine(modeColumn);
				int toggle = s.modifierToggle[i] ? 1 : 0;
				SetNextItemWidth(modeWidth);
				if (Combo("##mode", &toggle, modeNames.data(), static_cast<int>(modeNames.size()))) {
					s.modifierToggle[i] = toggle == 1;
					Changed(true);
				}
				SameLine();
				KeyButton("mod", &s.modifierKeys[i]);
				if (i == 0) {
					Help("hold: the extra bar is used while you hold its key.\n"
						 "press: one press switches to the extra bar, the next press goes back to the main bar.\n"
						 "Another extra bar's key switches straight to that bar.");
				}
				PopID();
			}

			SeparatorText(T("Game controls"));
			Changed(Checkbox(Id("Slot keys only work while sneaking", "onlyWhileSneaking").c_str(), &s.onlyWhileSneaking));
			Help("On: the bar only reacts while you sneak. Standing up, the keys do what Skyrim normally does\n"
				 "with them. Tip: Bar Layout > Show bar > While sneaking hides the bar until you crouch.");
			Changed(Checkbox(Id("Aim spells at the crosshair", "aimAtCrosshair").c_str(), &s.aimAtCrosshair));
			Help("On: aimed spells fly to what's under the crosshair, and in third person your character turns\n"
				 "to face where the camera looks while casting.\n"
				 "Off: they fly where your character faces, or at the enemy you're fighting.");
			Changed(Checkbox(Id("Each shout has its own cooldown", "individualShoutCooldowns").c_str(), &s.individualShoutCooldowns));
			Help("On: a shout only puts itself on cooldown, so you can use your other shouts right away.\n"
				 "Each shout slot shows its own cooldown, and it carries over when you save and load.\n"
				 "Off: one cooldown for all shouts, like the base game.");
			Changed(Checkbox(Id("Cast during weapon attacks", "castDuringAttacks").c_str(), &s.castDuringAttacks));
			Help("On: a spell, scroll, power or shout cast right away from the bar stops your weapon\n"
				 "attack (swing, power attack, bash) and is cast at once, so you can cancel attacks with it.\n"
				 "Off: the bar's magic can't be used while you attack, like in the base game.");
			Changed(Checkbox(Id("Bind in the Favorites menu", "bindInFavorites").c_str(), &s.bindInFavorites));
			Help("On: in the Favorites menu, pressing a slot key puts the selected favorite on the bar,\n"
				 "like in the Magic and Inventory menus.\n"
				 "Off: the Favorites menu works like in the base game: number keys set its own hotkeys, and the bar\n"
				 "isn't shown there. Tip: give the bar other slot keys, or turn off \"Slot keys only use the hotbar\",\n"
				 "so those hotkeys also work in game.");
			Changed(Checkbox(Id("Unfavoriting removes it from the bar", "unfavoriteRemoves").c_str(), &s.unfavoriteRemoves));
			Help("On: when you unfavorite a spell, item, power or shout that's on the bar, it's taken off the bar,\n"
				 "like a favorites hotkey in the base game.\n"
				 "Off: the bar keeps it until you clear the slot yourself.");
			Changed(Checkbox(Id("Slot keys only use the hotbar", "blockGameInput").c_str(), &s.blockGameInput));
			Help("On: pressing a slot key only uses that slot.\n"
				 "Off: the key also does what Skyrim normally does with it (for example 1 - 8 also use your favorites).\n"
				 "Extra bar keys always keep working in the game.");
		}

		constexpr int kHotkeysTab = -1;  // bindingsPage value of the Hotkeys tab

		std::string PageTabLabel(const Settings& a_settings, Page a_page)
		{
			if (a_page == Page::kMain) {
				return T("Main bar");
			}
			const int bar = static_cast<int>(a_page) - static_cast<int>(Page::kModifier1);
			const auto key = KeyLabel(a_settings.modifierKeys[bar]);
			return a_settings.modifierToggle[bar] ? F("Press {}", key) : F("Hold {}", key);
		}
	}

	void __stdcall RenderBindingsPage()
	{
		std::scoped_lock lock(Config::Lock());
		auto&            s = Config::Get();

		if (bindingsPage != kHotkeysTab && !PageEnabled(s, static_cast<Page>(bindingsPage))) {
			bindingsPage = static_cast<int>(Page::kMain);
		}
		if (BeginTabBar("##pages")) {
			for (int p = 0; p < kPageCount; ++p) {
				const auto page = static_cast<Page>(p);
				if (!PageEnabled(s, page)) {
					continue;
				}
				if (BeginTabItem(std::format("{}###page{}", PageTabLabel(s, page), p).c_str())) {
					bindingsPage = p;
					EndTabItem();
				}
			}
			if (BeginTabItem(Id("Hotkeys", "hotkeys").c_str())) {
				bindingsPage = kHotkeysTab;
				EndTabItem();
			}
			EndTabBar();
		}

		if (bindingsPage == kHotkeysTab) {
			HotkeysTab(s);
			return;
		}

		const auto player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->Is3DLoaded()) {
			TextWrapped("%s", T("Load a save to see what's on your bar."));
			return;
		}

		TextWrapped("%s", T("To put something on the bar: open the Magic menu (or Inventory / Favorites), select a spell or item "
							"and press a slot key. Hold an extra bar's key at the same time to put it on that bar. "
							"Press the key again on the same spell to change its hand: right, left (L), both hands (D). "
							"Other items are removed that way. Clear removes anything."));
		Spacing();

		const auto  page = static_cast<Page>(bindingsPage);
		const float iconSize = GetFrameHeight() * 1.3f;
		const auto  handNames = Translated(kHandNames);
		const float handWidth = TextWidth({ handNames[0], handNames[1], handNames[2], handNames[3] }) + GetFrameHeight() +
		                        GetStyle()->FramePadding.x * 2.0f;
		// order of SlotMode; "Default" names the mode it follows
		const auto  defaultMode = F("Default ({})", T(kKeyModeNames[static_cast<int>(s.keyMode)]));
		const char* modeNames[] = { defaultMode.c_str(), T(kKeyModeNames[0]), T(kKeyModeNames[1]) };
		const float modeWidth = TextWidth({ modeNames[0], modeNames[1], modeNames[2] }) + GetFrameHeight() + GetStyle()->FramePadding.x * 2.0f;
		if (BeginTable("##bindings", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
			TableSetupColumn(T("Slot"), ImGuiTableColumnFlags_WidthFixed, 0.0f);
			TableSetupColumn(T("Key"), ImGuiTableColumnFlags_WidthFixed, 0.0f);
			TableSetupColumn(T("Spell / item"), ImGuiTableColumnFlags_WidthStretch, 0.0f);
			TableSetupColumn(T("Hand"), ImGuiTableColumnFlags_WidthFixed, 0.0f);
			TableSetupColumn(T("On key press"), ImGuiTableColumnFlags_WidthFixed, 0.0f);
			TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 0.0f);
			TableHeadersRow();

			for (int slot = 0; slot < s.slotCount; ++slot) {
				PushID(slot);
				TableNextRow();
				const auto form = Bindings::GetForm(page, slot);

				TableNextColumn();
				AlignTextToFramePadding();
				Text("%d", slot + 1);
				TableNextColumn();
				AlignTextToFramePadding();
				Text("%s", KeyLabel(s.slotKeys[slot]).c_str());

				TableNextColumn();
				IconImage(form, iconSize);
				SameLine();
				AlignTextToFramePadding();
				if (form) {
					Text("%s", FormLabel(form).c_str());
					SameLine();
					TextColored(kGrey, "%s", TypeName(form));
				} else {
					TextDisabled("%s", T("empty"));
				}

				// same choices as pressing the slot key again in the Magic menu (weapons: no "both")
				TableNextColumn();
				if (form && Bindings::UsesHand(form)) {
					auto&     binding = Bindings::Get(page, slot);
					int       hand = static_cast<int>(binding.hand);
					const int count = form->Is(RE::FormType::Weapon) ? 3 : 4;
					SetNextItemWidth(handWidth);
					if (Combo("##hand", &hand, handNames.data(), count)) {
						binding.hand = static_cast<Hand>(hand);
					}
				}

				TableNextColumn();
				if (form && Bindings::UsesMode(form)) {
					auto& binding = Bindings::Get(page, slot);
					int   mode = static_cast<int>(binding.mode);
					SetNextItemWidth(modeWidth);
					if (Combo("##mode", &mode, modeNames, static_cast<int>(std::size(modeNames)))) {
						binding.mode = static_cast<SlotMode>(mode);
					}
				}

				TableNextColumn();
				if (form && SmallButton(Id("Clear", "clear").c_str())) {
					Bindings::Clear(page, slot);
				}
				PopID();
			}
			EndTable();
		}
		TextDisabled("%s", T("On key press: what the slot key does with that spell, power or shout. Default follows Hotkeys > What slot keys do."));
		Spacing();
		if (Button(Id("Clear this bar", "clearBar").c_str())) {
			for (int slot = 0; slot < kMaxSlots; ++slot) {
				Bindings::Clear(page, slot);
			}
		}
		Help("What's on your bar is saved with your save game. Use Profiles to copy it to another character.");
	}

	namespace
	{
		// ---- icons --------------------------------------------------------------------------

		char                                  formSearch[128]{};
		char                                  iconSearch[128]{};
		bool                                  onlyWithoutIcon{ true };
		RE::FormID                            iconTarget{ 0 };  // form whose icon is being picked, 0 = none
		std::vector<RE::FormID>               iconForms;        // the player's spells, powers, shouts and what's on the bar
		std::chrono::steady_clock::time_point iconFormsScanned{};
		int                                   iconSource{ 0 };  // order of kPictureSourceNames
		std::vector<int>                      iconMatches;  // Icons::Choices() indices matching iconSearch and iconSource
		std::string                           iconMatchesFor{ "\x01" };
		int                                   iconMatchesSource{ -1 };

		bool Contains(std::string_view a_text, std::string_view a_part)
		{
			const auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
			return a_part.empty() || !std::ranges::search(a_text, a_part, {}, lower, lower).empty();
		}

		// rescanned every few seconds: spells are learned while the menu isn't open
		void RefreshIconForms()
		{
			const auto now = std::chrono::steady_clock::now();
			if (now - iconFormsScanned < 2s) {
				return;
			}
			iconFormsScanned = now;

			std::vector<RE::TESForm*> forms;
			const auto add = [&](RE::TESForm* a_form) {
				if (a_form && std::ranges::find(forms, a_form) == forms.end()) {
					forms.push_back(a_form);
				}
			};
			const auto addList = [&](RE::TESSpellList::SpellData* a_list) {
				if (!a_list) {
					return;
				}
				for (std::uint32_t i = 0; a_list->spells && i < a_list->numSpells; ++i) {
					add(a_list->spells[i]);
				}
				for (std::uint32_t i = 0; a_list->shouts && i < a_list->numShouts; ++i) {
					add(a_list->shouts[i]);
				}
			};
			const auto player = RE::PlayerCharacter::GetSingleton();
			if (const auto base = player->GetActorBase()) {
				addList(base->actorEffects);
			}
			if (const auto race = player->GetRace()) {
				addList(race->actorEffects);
			}
			for (const auto spell : player->GetActorRuntimeData().addedSpells) {
				add(spell);
			}
			std::erase_if(forms, [](RE::TESForm* a_form) { return !Bindings::IsBindable(a_form); });  // abilities, diseases...
			for (int page = 0; page < kPageCount; ++page) {
				for (int slot = 0; slot < kMaxSlots; ++slot) {
					add(Bindings::GetForm(static_cast<Page>(page), slot));
				}
			}
			add(Bindings::GetForm(Page::kMain, kReadySpellSlot));
			add(Bindings::GetForm(Page::kMain, kReadyPotionSlot));

			std::ranges::sort(forms, {}, [](RE::TESForm* a_form) { return FormLabel(a_form); });
			iconForms.clear();
			std::ranges::transform(forms, std::back_inserter(iconForms), &RE::TESForm::GetFormID);
		}

		// The icon grid only draws the rows in view: with spell packs there are well over a thousand pictures
		void IconGrid(RE::TESForm* a_form)
		{
			const auto& choices = Icons::Choices();
			if (iconMatchesFor != iconSearch || iconMatchesSource != iconSource) {
				iconMatchesFor = iconSearch;
				iconMatchesSource = iconSource;
				iconMatches.clear();
				for (int i = 0; i < static_cast<int>(choices.size()); ++i) {
					const bool source = iconSource == 0 || (iconSource == 2) == choices[i].spellHotbar2;
					if (source && (Contains(choices[i].name, iconSearch) || Contains(choices[i].group, iconSearch))) {
						iconMatches.push_back(i);
					}
				}
			}
			if (iconMatches.empty()) {
				TextDisabled("%s", T("Nothing found."));
				return;
			}

			const auto  style = GetStyle();
			const float size = GetFrameHeight() * 2.0f;
			const float cell = size + style->FramePadding.x * 2.0f + style->ItemSpacing.x;
			const int   columns = std::max(1, static_cast<int>((GetContentRegionAvail().x + style->ItemSpacing.x) / cell));
			const int   rows = (static_cast<int>(iconMatches.size()) + columns - 1) / columns;
			const auto  current = Icons::CustomKey(a_form);

			const auto clipper = ImGuiListClipperManager::Create();
			ImGuiListClipperManager::Begin(clipper, rows, size + style->FramePadding.y * 2.0f + style->ItemSpacing.y);
			while (ImGuiListClipperManager::Step(clipper)) {
				for (int row = clipper->DisplayStart; row < clipper->DisplayEnd; ++row) {
					for (int column = 0; column < columns; ++column) {
						const auto index = static_cast<std::size_t>(row * columns + column);
						if (index >= iconMatches.size()) {
							break;
						}
						const auto& choice = choices[iconMatches[index]];
						if (column > 0) {
							SameLine();
						}
						PushID(iconMatches[index]);
						const auto texture = AtlasTexture(choice.icon.atlas);
						const auto background = choice.key == current ? ImVec4{ kGold.x, kGold.y, kGold.z, 0.5f } : ImVec4{ 0, 0, 0, 0 };
						if (!texture) {
							Dummy(ImVec2{ size + style->FramePadding.x * 2.0f, size + style->FramePadding.y * 2.0f });
						} else if (ImageButton("##pick", texture, ImVec2{ size, size }, ImVec2{ choice.icon.u0, choice.icon.v0 },
									   ImVec2{ choice.icon.u1, choice.icon.v1 }, background)) {
							Icons::SetCustom(a_form, choice.key);
						}
						if (IsItemHovered()) {
							SetTooltip("%s\n%s", choice.name.c_str(), choice.group.c_str());
						}
						PopID();
					}
				}
			}
			ImGuiListClipperManager::End(clipper);
			ImGuiListClipperManager::Destroy(clipper);
		}
	}

	void __stdcall RenderIconsPage()
	{
		std::scoped_lock lock(Config::Lock());

		TextWrapped("%s", T("Pick the picture a spell, power, shout or item shows on the bar. Your choices count for every character."));

		auto&      s = Config::Get();
		const auto styleNames = Translated(kIconStyleNames);
		int        style = static_cast<int>(s.iconStyle);
		SetNextItemWidth(TextWidth({ styleNames[0], styleNames[1], styleNames[2] }) + GetFrameHeight() + GetStyle()->FramePadding.x * 2.0f);
		if (Combo(Id("Icon style", "iconStyle").c_str(), &style, styleNames.data(), static_cast<int>(styleNames.size()))) {
			s.iconStyle = static_cast<IconStyle>(style);
			Changed(true);
		}
		Help("Inventory icons are the ones SkyUI shows in your inventory, favorites and magic menu\n"
			 "(also those of an icon replacer you have installed, and the icons of mods for\n"
			 "Inventory Interface Information Injector (I4) like KIT).\n"
			 "Items: weapons, armor, ammo, torches, potions and food. Spells, scrolls, powers and shouts keep their icons.\n"
			 "Everything: spells, scrolls, powers and shouts get SkyUI's icons too.\n"
			 "An icon you pick below is always used.");
		if (s.iconStyle != IconStyle::kOwn && InventoryIcons::MoviePath().empty()) {
			TextColored(kGold, "%s", T("SkyUI is not installed, so the bar keeps its own icons."));
		} else if (s.iconStyle != IconStyle::kOwn && InventoryIcons::RuleCount() > 0) {
			TextDisabled("%s", F("Uses {} icon rules of Inventory Interface Information Injector mods.", InventoryIcons::RuleCount()).c_str());
		}

		Changed(Checkbox(Id("Use SpellHotbar2 icon packs", "spellHotbar2Icons").c_str(), &s.spellHotbar2Icons));
		Help("Icon packs made for SpellHotbar2 (like SpellHotbar2 - Icon Packs Hub) keep their pictures\n"
			 "in Data\\SKSE\\Plugins\\SpellHotbar\\images.\n"
			 "On: a spell, power or shout one of these packs has a picture for shows that picture on the bar.\n"
			 "Off: the bar's own icons. Either way, Change below lets you pick any of their pictures.");
		const bool packPictures = std::ranges::any_of(Icons::Choices(), [](const Icons::Choice& a_choice) { return a_choice.spellHotbar2; });
		if (const auto count = Icons::SpellHotbar2IconCount(); count > 0) {
			TextDisabled("%s", F("{} spells, powers and shouts have a picture in a SpellHotbar2 icon pack.", count).c_str());
		} else if (!packPictures) {
			TextDisabled("%s", T("No SpellHotbar2 icon packs found."));
		}
		if (packPictures) {
			TextDisabled("%s", T("To give any spell one of their pictures: Change, then Pictures: SpellHotbar2 icon packs."));
		}
		Spacing();

		const auto player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->Is3DLoaded()) {
			TextWrapped("%s", T("Load a save to see your spells."));
			return;
		}
		RefreshIconForms();
		Spacing();

		SetNextItemWidth(260.0f);
		InputTextWithHint("##formSearch", T("Search"), formSearch, sizeof(formSearch));
		SameLine();
		Checkbox(Id("Only ones without their own icon", "onlyWithoutIcon").c_str(), &onlyWithoutIcon);
		Help("Spells from other mods often have no icon of their own. The bar then shows a general one for their school.\n"
			 "Off: everything you know or have on the bar, so you can change any icon.");

		auto        target = iconTarget ? RE::TESForm::LookupByID(iconTarget) : nullptr;
		const float iconSize = GetFrameHeight() * 1.3f;
		if (BeginChild("##iconForms", ImVec2{ 0.0f, target ? GetContentRegionAvail().y * 0.4f : 0.0f }, ImGuiChildFlags_Border)) {
			int shown = 0;
			if (BeginTable("##forms", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
				TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.0f);
				TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 0.0f);
				for (const auto id : iconForms) {
					const auto form = RE::TESForm::LookupByID(id);
					const auto custom = form ? Icons::CustomKey(form) : std::string{};
					if (!form || (onlyWithoutIcon && Icons::HasOwnIcon(form) && custom.empty()) || !Contains(FormLabel(form), formSearch)) {
						continue;
					}
					++shown;
					PushID(static_cast<int>(id));
					TableNextRow();
					if (form == target) {
						TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(255, 210, 100, 60));
					}
					TableNextColumn();
					IconImage(form, iconSize);
					SameLine();
					AlignTextToFramePadding();
					Text("%s", FormLabel(form).c_str());
					SameLine();
					TextColored(kGrey, "%s", TypeName(form));
					if (!custom.empty()) {
						SameLine();
						TextColored(kGold, "%s", T("your pick"));
					}
					TableNextColumn();
					if (SmallButton(Id("Change", "change").c_str())) {
						iconTarget = id;
					}
					if (!custom.empty()) {
						SameLine();
						if (SmallButton(Id("Reset", "reset").c_str())) {
							Icons::SetCustom(form, {});
						}
					}
					PopID();
				}
				EndTable();
			}
			if (shown == 0) {
				TextDisabled("%s", T("Nothing found."));
			}
		}
		EndChild();

		target = iconTarget ? RE::TESForm::LookupByID(iconTarget) : nullptr;
		if (!target) {
			return;
		}
		SeparatorText(F("Icon for {}", FormLabel(target)).c_str());
		IconImage(target, iconSize);
		SameLine();
		AlignTextToFramePadding();
		TextDisabled("%s", T("Click an icon to use it."));
		SameLine();
		SetNextItemWidth(220.0f);
		InputTextWithHint("##iconSearch", T("Search"), iconSearch, sizeof(iconSearch));
		SameLine();
		const auto sourceNames = Translated(kPictureSourceNames);
		SetNextItemWidth(TextWidth({ sourceNames[0], sourceNames[1], sourceNames[2] }) + GetFrameHeight() + GetStyle()->FramePadding.x * 2.0f);
		Combo(Id("Pictures", "iconSource").c_str(), &iconSource, sourceNames.data(), static_cast<int>(sourceNames.size()));
		Help("SpellHotbar2 icon packs: the pictures of the packs you installed from\n"
			 "SpellHotbar2 - Icon Packs Hub (its installer only offers the packs for mods you have).\n"
			 "Hover a picture to see its name and the pack it comes from.");
		SameLine();
		if (Button(Id("Done", "done").c_str())) {
			iconTarget = 0;
			return;
		}
		if (BeginChild("##iconGrid", ImVec2{ 0.0f, 0.0f }, ImGuiChildFlags_Border)) {
			IconGrid(target);
		}
		EndChild();
	}

	void __stdcall RenderProfilesPage()
	{
		RefreshProfiles(false);
		const auto player = RE::PlayerCharacter::GetSingleton();
		const bool inGame = player && player->Is3DLoaded();

		LanguageOption();

		SeparatorText(T("Profiles"));
		TextWrapped("%s", T("A profile stores all your settings (layout, colors, keys, extra bars) and, if you want, what's on this character's bar."));

		SeparatorText(T("Save"));
		SetNextItemWidth(260.0f);
		InputText(Id("Name", "profileName").c_str(), profileName, sizeof(profileName));
		BeginDisabled(!inGame);
		Checkbox(Id("Include what's on my bar", "saveBindings").c_str(), &saveBindings);
		EndDisabled();
		if (Button(Id("Save profile", "saveProfile").c_str())) {
			profileStatus = Profiles::Save(profileName, saveBindings && inGame) ? F("Saved '{}'", profileName) : T("Could not save (check the name)");
			RefreshProfiles(true);
		}

		SeparatorText(T("Load"));
		if (BeginListBox("##profiles", ImVec2{ -1.0f, 200.0f })) {
			for (int i = 0; i < static_cast<int>(profiles.size()); ++i) {
				if (Selectable(profiles[i].name.c_str(), selectedProfile == i)) {
					selectedProfile = i;
				}
			}
			EndListBox();
		}
		if (profiles.empty()) {
			TextDisabled("%s", T("No profiles yet."));
		}

		const bool valid = selectedProfile >= 0 && selectedProfile < static_cast<int>(profiles.size());
		BeginDisabled(!inGame);
		Checkbox(Id("Also load what's on the bar (if the profile has it)", "loadBindings").c_str(), &loadBindings);
		EndDisabled();
		BeginDisabled(!valid);
		if (Button(Id("Load", "loadProfile").c_str())) {
			profileStatus = Profiles::Load(profiles[selectedProfile], loadBindings && inGame) ? F("Loaded '{}'", profiles[selectedProfile].name) : T("Could not load profile");
		}
		SameLine();
		if (Button(Id("Delete", "deleteProfile").c_str())) {
			profileStatus = Profiles::Delete(profiles[selectedProfile]) ? T("Deleted") : T("Could not delete");
			selectedProfile = -1;
			RefreshProfiles(true);
		}
		EndDisabled();

		SeparatorText(T("Defaults"));
		if (Button(Id("Reset all settings to defaults", "resetAll").c_str())) {
			Config::ResetToDefaults();
			profileStatus = T("Settings reset");
		}

		if (!profileStatus.empty()) {
			Spacing();
			TextColored(kGold, "%s", profileStatus.c_str());
		}
		Spacing();
		TextDisabled("%s", F("Profiles are saved in {} (with Mod Organizer 2: in the overwrite folder).", Profiles::Dir().make_preferred().string()).c_str());
	}
}
