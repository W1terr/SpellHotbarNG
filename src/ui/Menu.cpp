#include "core/Bindings.h"
#include "core/Config.h"
#include "core/Hotkeys.h"
#include "ui/Framework.h"
#include "ui/Icons.h"
#include "core/Input.h"
#include "core/Keys.h"
#include "core/Lang.h"

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
				Image(texture, ImVec2{ a_size, a_size }, ImVec2{ icon.u0, icon.v0 }, ImVec2{ icon.u1, icon.v1 });
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
			}
			Help("The names of the pages on the left change after you restart the game.");

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

	void __stdcall RenderBarPage()
	{
		std::scoped_lock lock(Config::Lock());
		auto&            s = Config::Get();

		const auto anchorNames = Translated(kAnchorNames);

		TextWrapped("%s", T("The bar is previewed live while this menu is open. Sizes are in pixels at 1080p and scale with your resolution."));
		SeparatorText(T("Size"));
		Changed(SliderInt(Id("Slots", "slots").c_str(), &s.slotCount, 1, kMaxSlots));
		Changed(SliderInt(Id("Columns", "columns").c_str(), &s.columns, 1, kMaxSlots));
		Help("Slots per row. Set it to 1 for a vertical bar.");
		Changed(SliderFloat(Id("Icon size", "iconSize").c_str(), &s.iconSize, 16.0f, 200.0f, "%.0f px"));
		Changed(SliderFloat(Id("Spacing", "spacing").c_str(), &s.spacing, 0.0f, 60.0f, "%.0f px"));

		SeparatorText(T("Position"));
		int anchor = static_cast<int>(s.anchor);
		if (Combo(Id("Anchor", "anchor").c_str(), &anchor, anchorNames.data(), static_cast<int>(anchorNames.size()))) {
			s.anchor = static_cast<Anchor>(anchor);
			Changed(true);
		}
		Changed(SliderFloat(Id("Offset X", "offsetX").c_str(), &s.offsetX, -1920.0f, 1920.0f, "%.0f px"));
		Changed(SliderFloat(Id("Offset Y", "offsetY").c_str(), &s.offsetY, -1080.0f, 1080.0f, "%.0f px"));
		if (Button(Id("Reset position", "resetPosition").c_str())) {
			s.anchor = Anchor::kBottom;
			s.offsetX = 0.0f;
			s.offsetY = -100.0f;
			Changed(true);
		}

		if (s.keyMode == KeyMode::kOblivion) {
			SeparatorText(T("Oblivion style bar"));
			TextDisabled("%s", T("Shows the picked spell, the picked potion and your power. It follows the Show bar setting below."));
			int readyAnchor = static_cast<int>(s.readyAnchor);
			if (Combo(Id("Anchor", "readyAnchor").c_str(), &readyAnchor, anchorNames.data(), static_cast<int>(anchorNames.size()))) {
				s.readyAnchor = static_cast<Anchor>(readyAnchor);
				Changed(true);
			}
			Changed(SliderFloat(Id("Offset X", "readyOffsetX").c_str(), &s.readyOffsetX, -1920.0f, 1920.0f, "%.0f px"));
			Changed(SliderFloat(Id("Offset Y", "readyOffsetY").c_str(), &s.readyOffsetY, -1080.0f, 1080.0f, "%.0f px"));
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
		ColorOption("Slot background", c.slotBackground, "Tint of the empty slot texture. White keeps the original look.");
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

		const auto page = static_cast<Page>(bindingsPage);
		const float iconSize = GetFrameHeight() * 1.3f;
		if (BeginTable("##bindings", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
			TableSetupColumn(T("Slot"), ImGuiTableColumnFlags_WidthFixed, 0.0f);
			TableSetupColumn(T("Key"), ImGuiTableColumnFlags_WidthFixed, 0.0f);
			TableSetupColumn(T("Spell / item"), ImGuiTableColumnFlags_WidthStretch, 0.0f);
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

				TableNextColumn();
				if (form && SmallButton(Id("Clear", "clear").c_str())) {
					Bindings::Clear(page, slot);
				}
				PopID();
			}
			EndTable();
		}
		Spacing();
		if (Button(Id("Clear this bar", "clearBar").c_str())) {
			for (int slot = 0; slot < kMaxSlots; ++slot) {
				Bindings::Clear(page, slot);
			}
		}
		Help("What's on your bar is saved with your save game. Use Profiles to copy it to another character.");
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
