#include "core/Bindings.h"
#include "core/Config.h"
#include "ui/Framework.h"
#include "ui/Icons.h"
#include "core/Input.h"
#include "core/Keys.h"

namespace UI
{
	namespace
	{
		constexpr const char* kAnchorNames[] = { "Top Left", "Top", "Top Right", "Left", "Center", "Right", "Bottom Left", "Bottom", "Bottom Right" };
		constexpr const char* kVisibilityNames[] = { "Always", "In combat", "Weapons / magic drawn", "In combat or drawn", "Never (only in menus)", "While sneaking" };
		constexpr const char* kHandNames[] = { "Auto", "Right", "Left", "Both" };

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
				SetTooltip("%s", a_text);
			}
		}

		// Button showing a key; clicking it waits for the next key press (Input stores it and marks the settings dirty)
		void KeyButton(const char* a_id, std::uint32_t* a_key)
		{
			const bool capturing = Input::CaptureTarget() == a_key;
			const auto label = capturing ? std::format("Press a key...##{}", a_id) : std::format("{}##{}", Keys::Name(*a_key), a_id);
			if (Button(label.c_str(), ImVec2{ 130.0f, 0.0f })) {
				if (capturing) {
					Input::CancelCapture();
				} else {
					Input::BeginCapture(a_key);
				}
			}
			if (IsItemHovered()) {
				SetTooltip("Click, then press any key, mouse button or controller button.\nEsc: cancel    Delete: remove the key");
			}
		}

		void ColorOption(const char* a_label, RGBA& a_color, const char* a_help = nullptr)
		{
			Changed(ColorEdit4(a_label, a_color.data(), ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf));
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
					return "Power";
				case RE::MagicSystem::SpellType::kLesserPower:
					return "Lesser Power";
				case RE::MagicSystem::SpellType::kVoicePower:
					return "Voice Power";
				default:
					return "Spell";
				}
			case RE::FormType::Scroll:
				return "Scroll";
			case RE::FormType::Shout:
				return "Shout";
			case RE::FormType::AlchemyItem:
				{
					const auto alch = a_form->As<RE::AlchemyItem>();
					return alch->IsFood() ? "Food" : alch->IsPoison() ? "Poison" : "Potion";
				}
			case RE::FormType::Weapon:
				return "Weapon";
			case RE::FormType::Armor:
				return "Apparel";
			default:
				return "Item";
			}
		}

		// the Hand setting only does something for spells, scrolls and weapons (same rule as the L / R / D marker)
		bool UsesHand(RE::TESForm* a_form)
		{
			if (const auto spell = a_form->As<RE::SpellItem>()) {
				return spell->GetSpellType() == RE::MagicSystem::SpellType::kSpell;
			}
			return a_form->Is(RE::FormType::Scroll) || a_form->Is(RE::FormType::Weapon);
		}

		int bindingsPage{ 0 };  // page tab selected on the Bindings page

		// ---- profiles -----------------------------------------------------------------------

		char                                  profileName[64]{ "My Profile" };
		bool                                  saveBindings{ true };
		bool                                  loadBindings{ true };
		int                                   selectedProfile{ -1 };
		std::string                           profileStatus;
		std::vector<Profiles::Entry>          profiles;
		std::chrono::steady_clock::time_point profilesScanned{};

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

		TextWrapped("The bar is previewed live while this menu is open. Sizes are in pixels at 1080p and scale with your resolution.");
		SeparatorText("Size");
		Changed(SliderInt("Slots", &s.slotCount, 1, kMaxSlots));
		Changed(SliderInt("Columns", &s.columns, 1, kMaxSlots));
		Help("Slots per row. Set it to 1 for a vertical bar.");
		Changed(SliderFloat("Icon size", &s.iconSize, 16.0f, 200.0f, "%.0f px"));
		Changed(SliderFloat("Spacing", &s.spacing, 0.0f, 60.0f, "%.0f px"));

		SeparatorText("Position");
		int anchor = static_cast<int>(s.anchor);
		if (Combo("Anchor", &anchor, kAnchorNames, static_cast<int>(std::size(kAnchorNames)))) {
			s.anchor = static_cast<Anchor>(anchor);
			Changed(true);
		}
		Changed(SliderFloat("Offset X", &s.offsetX, -1920.0f, 1920.0f, "%.0f px"));
		Changed(SliderFloat("Offset Y", &s.offsetY, -1080.0f, 1080.0f, "%.0f px"));
		if (Button("Reset position")) {
			s.anchor = Anchor::kBottom;
			s.offsetX = 0.0f;
			s.offsetY = -100.0f;
			Changed(true);
		}

		if (s.keyMode == KeyMode::kOblivion) {
			SeparatorText("Oblivion style bar");
			TextDisabled("Shows the picked spell, the picked potion and your power. It follows the Show bar setting below.");
			int readyAnchor = static_cast<int>(s.readyAnchor);
			if (Combo("Anchor##ready", &readyAnchor, kAnchorNames, static_cast<int>(std::size(kAnchorNames)))) {
				s.readyAnchor = static_cast<Anchor>(readyAnchor);
				Changed(true);
			}
			Changed(SliderFloat("Offset X##ready", &s.readyOffsetX, -1920.0f, 1920.0f, "%.0f px"));
			Changed(SliderFloat("Offset Y##ready", &s.readyOffsetY, -1080.0f, 1080.0f, "%.0f px"));
			Changed(Checkbox("Show power##ready", &s.readyShowPower));
			Changed(Checkbox("Vertical##ready", &s.readyVertical));
			Changed(Checkbox("Hide the main bar until I pick a spell##ready", &s.readyHideMainBar));
			Help("On: only this small bar stays on screen. The main bar shows up for a moment when you\n"
				 "press a slot key (also when binding in the Magic / Inventory menu) or hold an extra bar's key.\n"
				 "Off: both bars are shown.");
			if (Button("Reset position##ready")) {
				const Settings defaults{};
				s.readyAnchor = defaults.readyAnchor;
				s.readyOffsetX = defaults.readyOffsetX;
				s.readyOffsetY = defaults.readyOffsetY;
				Changed(true);
			}
		}

		SeparatorText("Visibility");
		int visibility = static_cast<int>(s.visibility);
		if (Combo("Show bar", &visibility, kVisibilityNames, static_cast<int>(std::size(kVisibilityNames)))) {
			s.visibility = static_cast<Visibility>(visibility);
			Changed(true);
		}
		Changed(SliderFloat("Opacity", &s.opacity, 0.05f, 1.0f, "%.2f"));
		Changed(Checkbox("Fade out of combat", &s.fadeOutOfCombat));
		if (s.fadeOutOfCombat) {
			Changed(SliderFloat("Faded opacity", &s.fadedOpacity, 0.0f, 1.0f, "%.2f"));
		}
		Changed(Checkbox("Show in magic / inventory / favorites menu", &s.showInMenus));

		SeparatorText("Look");
		Changed(Checkbox("Show empty slots", &s.showEmptySlots));
		Changed(Checkbox("Show key labels", &s.showKeyLabels));
		Changed(Checkbox("Show which extra bar is active", &s.showPageName));
		Changed(Checkbox("Show cooldown seconds", &s.showCooldownText));
		Changed(Checkbox("Show item counts", &s.showItemCount));

		SeparatorText("Colors");
		TextDisabled("Click a color square to open the picker. The alpha bar sets each color's own transparency.");
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
		if (Button("Reset colors")) {
			c = BarColors{};
			Changed(true);
		}
	}

	namespace
	{
		// "Hotkeys" tab of the Bindings page: which key uses which slot, extra bars (modifiers), game keys
		void HotkeysTab(Settings& s)
		{
			SeparatorText("What slot keys do");
			int mode = static_cast<int>(s.keyMode);
			RadioButton("Cast right away", &mode, static_cast<int>(KeyMode::kCast));
			Help("Pressing a slot casts its spell with the casting animation. Nothing gets equipped,\n"
				 "your weapons stay in your hands. Powers and shouts are used right away too.");
			RadioButton("Equip", &mode, static_cast<int>(KeyMode::kEquip));
			Help("Pressing a slot equips its spell or scroll in the slot's hand (set on the bar's tab:\n"
				 "Auto = right hand, Both = both hands). Powers and shouts are equipped to your Shout key.\n"
				 "You then cast them with the game's normal attack / shout buttons.");
			RadioButton("Oblivion style", &mode, static_cast<int>(KeyMode::kOblivion));
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
				AlignTextToFramePadding();
				Text("Cast key");
				SameLine(140.0f);
				KeyButton("castKey", &s.castKey);
				Help("Casts the picked spell. Hold it for concentration spells.");
				AlignTextToFramePadding();
				Text("Potion key");
				SameLine(140.0f);
				KeyButton("potionKey", &s.potionKey);
				Help("Uses the picked potion.");
				TextDisabled("The picked spell and potion have their own small bar, see Bar Layout.");
				Unindent();
			}

			SeparatorText("Slot keys");
			TextWrapped("Click a slot's button, then press the key you want for it. Keyboard keys, mouse buttons and "
						"controller buttons all work.");
			if (Button("Remove all keys")) {
				s.slotKeys.fill(0);
				Changed(true);
			}

			if (BeginTable("##slotkeys", 4, ImGuiTableFlags_SizingFixedFit)) {
				for (int i = 0; i < s.slotCount; ++i) {
					TableNextColumn();
					AlignTextToFramePadding();
					Text("Slot %2d", i + 1);
					SameLine();
					KeyButton(std::format("slot{}", i).c_str(), &s.slotKeys[i]);
				}
				EndTable();
			}
			TextDisabled("Your bar has %d slots. You can change that in Bar Layout.", s.slotCount);

			SeparatorText("Extra bars");
			TextWrapped("Hold a key to switch to another set of spells, like Shift + 1 instead of 1. "
						"Each extra bar you turn on gets its own tab.");
			for (int i = 0; i < kModifierCount; ++i) {
				PushID(i);
				Changed(Checkbox(std::format("Extra bar {}", i + 1).c_str(), &s.modifierEnabled[i]));
				SameLine(160.0f);
				AlignTextToFramePadding();
				TextDisabled("hold");
				SameLine();
				KeyButton("mod", &s.modifierKeys[i]);
				PopID();
			}

			SeparatorText("Game controls");
			Changed(Checkbox("Slot keys only work while sneaking", &s.onlyWhileSneaking));
			Help("On: the bar only reacts while you sneak. Standing up, the keys do what Skyrim normally does\n"
				 "with them. Tip: Bar Layout > Show bar > While sneaking hides the bar until you crouch.");
			Changed(Checkbox("Aim spells at the crosshair", &s.aimAtCrosshair));
			Help("On: aimed spells fly to what's under the crosshair, and in third person your character turns\n"
				 "to face where the camera looks while casting.\n"
				 "Off: they fly where your character faces, or at the enemy you're fighting.");
			Changed(Checkbox("Slot keys only use the hotbar", &s.blockGameInput));
			Help("On: pressing a slot key only uses that slot.\n"
				 "Off: the key also does what Skyrim normally does with it (for example 1 - 8 also use your favorites).\n"
				 "The keys you hold for extra bars always keep working in the game.");
		}

		constexpr int kHotkeysTab = -1;  // bindingsPage value of the Hotkeys tab

		std::string PageTabLabel(const Settings& a_settings, Page a_page)
		{
			if (a_page == Page::kMain) {
				return "Main bar";
			}
			return std::format("Hold {}", Keys::Name(a_settings.modifierKeys[static_cast<int>(a_page) - static_cast<int>(Page::kModifier1)]));
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
				if (BeginTabItem(std::format("{}##page{}", PageTabLabel(s, page), p).c_str())) {
					bindingsPage = p;
					EndTabItem();
				}
			}
			if (BeginTabItem("Hotkeys##hotkeys")) {
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
			TextWrapped("Load a save to see what's on your bar.");
			return;
		}

		TextWrapped("To put something on the bar: open the Magic menu (or Inventory / Favorites), select a spell or item "
					"and press a slot key. Hold an extra bar's key at the same time to put it on that bar. "
					"Doing it again with the same spell removes it.");
		Spacing();

		const auto page = static_cast<Page>(bindingsPage);
		const float iconSize = GetFrameHeight() * 1.3f;
		if (BeginTable("##bindings", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
			TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 0.0f);
			TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 0.0f);
			TableSetupColumn("Spell / item", ImGuiTableColumnFlags_WidthStretch, 0.0f);
			TableSetupColumn("Hand", ImGuiTableColumnFlags_WidthFixed, 0.0f);
			TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 0.0f);
			TableHeadersRow();

			for (int slot = 0; slot < s.slotCount; ++slot) {
				PushID(slot);
				TableNextRow();
				auto&      binding = Bindings::Get(page, slot);
				const auto form = Bindings::GetForm(page, slot);

				TableNextColumn();
				AlignTextToFramePadding();
				Text("%d", slot + 1);
				TableNextColumn();
				AlignTextToFramePadding();
				Text("%s", Keys::Name(s.slotKeys[slot]).c_str());

				TableNextColumn();
				IconImage(form, iconSize);
				SameLine();
				AlignTextToFramePadding();
				if (form) {
					Text("%s", FormLabel(form).c_str());
					SameLine();
					TextColored(kGrey, "%s", TypeName(form));
				} else {
					TextDisabled("empty");
				}

				TableNextColumn();
				if (form && UsesHand(form)) {
					int hand = static_cast<int>(binding.hand);
					SetNextItemWidth(90.0f);
					if (Combo("##hand", &hand, kHandNames, static_cast<int>(std::size(kHandNames)))) {
						binding.hand = static_cast<Hand>(hand);
					}
				}

				TableNextColumn();
				if (form && SmallButton("Clear")) {
					Bindings::Clear(page, slot);
				}
				PopID();
			}
			EndTable();
		}
		Spacing();
		if (Button("Clear this bar")) {
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

		TextWrapped("A profile stores all your settings (layout, colors, keys, extra bars) and, if you want, what's on this character's bar.");

		SeparatorText("Save");
		SetNextItemWidth(260.0f);
		InputText("Name", profileName, sizeof(profileName));
		BeginDisabled(!inGame);
		Checkbox("Include what's on my bar##save", &saveBindings);
		EndDisabled();
		if (Button("Save profile")) {
			profileStatus = Profiles::Save(profileName, saveBindings && inGame) ? std::format("Saved '{}'", profileName) : "Could not save (check the name)";
			RefreshProfiles(true);
		}

		SeparatorText("Load");
		if (BeginListBox("##profiles", ImVec2{ -1.0f, 200.0f })) {
			for (int i = 0; i < static_cast<int>(profiles.size()); ++i) {
				if (Selectable(profiles[i].name.c_str(), selectedProfile == i)) {
					selectedProfile = i;
				}
			}
			EndListBox();
		}
		if (profiles.empty()) {
			TextDisabled("No profiles yet.");
		}

		const bool valid = selectedProfile >= 0 && selectedProfile < static_cast<int>(profiles.size());
		BeginDisabled(!inGame);
		Checkbox("Also load what's on the bar (if the profile has it)", &loadBindings);
		EndDisabled();
		BeginDisabled(!valid);
		if (Button("Load")) {
			profileStatus = Profiles::Load(profiles[selectedProfile], loadBindings && inGame) ? std::format("Loaded '{}'", profiles[selectedProfile].name) : "Could not load profile";
		}
		SameLine();
		if (Button("Delete")) {
			profileStatus = Profiles::Delete(profiles[selectedProfile]) ? "Deleted" : "Could not delete";
			selectedProfile = -1;
			RefreshProfiles(true);
		}
		EndDisabled();

		SeparatorText("Defaults");
		if (Button("Reset all settings to defaults")) {
			Config::ResetToDefaults();
			profileStatus = "Settings reset";
		}

		if (!profileStatus.empty()) {
			Spacing();
			TextColored(kGold, "%s", profileStatus.c_str());
		}
		Spacing();
		TextDisabled("Profiles are saved in %s (with Mod Organizer 2: in the overwrite folder).", Profiles::Dir().make_preferred().string().c_str());
	}
}
