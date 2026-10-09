#include "core/Input.h"

#include "casting/Actions.h"
#include "core/Bindings.h"
#include "core/Keys.h"
#include "core/Util.h"
#include "ui/UI.h"

namespace Input
{
	namespace
	{
		enum class Mode
		{
			kNone,
			kGameplay,
			kBind
		};

		// a slot key that went down while we handled it, so its release is swallowed as well
		struct SlotState
		{
			bool down{ false };
			Mode mode{ Mode::kNone };
		};

		// slot keys, then the Oblivion style cast key and potion key
		std::array<SlotState, kMaxSlots + 2> slotStates{};
		std::chrono::steady_clock::time_point lastSlotKey{};
		std::chrono::steady_clock::time_point lastHotbarKey{};  // slot keys and the Oblivion style cast / potion keys
		std::array<bool, kModifierCount> modifierDown{};
		int                              switchedBar{ -1 };  // extra bar picked with a press-mode key, -1 = main bar
		bool                             mainBarOpen{ false };      // opened with the main bar key (press mode)
		bool                             mainBarKeyDown{ false };
		bool                             mainBarModifierDown{ false };
		bool                             mainBarKeyUsed{ false };  // the main bar key's press opened / closed the bar: its release is ours too
		std::uint32_t*                   captureTarget{ nullptr };

		// Keyboard modifiers can miss their release (alt-tab), so double check them with the OS
		bool KeyboardKeyDown(std::uint32_t a_key)
		{
			const auto scan = a_key >= 0x80 ? (0xE000 | (a_key & 0x7F)) : a_key;
			const auto vk = ::MapVirtualKeyW(scan, MAPVK_VSC_TO_VK_EX);
			return vk != 0 && (::GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) != 0;
		}

		bool TextEntryActive()
		{
			const auto controlMap = RE::ControlMap::GetSingleton();
			return controlMap && controlMap->GetRuntimeData().textEntryCount > 0;
		}

		bool GameplayActive()
		{
			const auto ui = RE::UI::GetSingleton();
			const auto player = RE::PlayerCharacter::GetSingleton();
			if (!ui || !player || player->IsDead() || ui->GameIsPaused() || TextEntryActive()) {
				return false;
			}
			// (the Favorites menu, if it isn't for binding, keeps its keys for the game's hotkeys)
			if (ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME) || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) ||
				ui->IsMenuOpen(RE::MainMenu::MENU_NAME) || ui->IsMenuOpen(RE::FavoritesMenu::MENU_NAME) || UI::IsBlockingWindowOpen()) {
				return false;
			}
			const auto controlMap = RE::ControlMap::GetSingleton();
			return !controlMap || controlMap->IsFightingControlsEnabled();
		}

		bool SneakAllows()
		{
			const auto player = RE::PlayerCharacter::GetSingleton();
			return !Config::Get().onlyWhileSneaking || (player && player->AsActorState()->IsSneaking());
		}

		Mode CurrentMode()
		{
			if (InBindMenu()) {
				return Mode::kBind;
			}
			return GameplayActive() && SneakAllows() && !Util::HotbarOff() ? Mode::kGameplay : Mode::kNone;
		}

		// Slot keys of the main bar (and extra bar switching) do something: always while binding, in gameplay only
		// while the main bar is open
		bool MainBarUsable(Mode a_mode)
		{
			return a_mode == Mode::kBind || (a_mode == Mode::kGameplay && MainBarOpen());
		}

		// The main bar key opens / closes the main bar (press mode; key combo mode only while its modifier is held) or
		// keeps it open while held (hold mode, the key also reaches the game like extra bar keys). true = swallow the event.
		bool OnMainBarKey(const RE::ButtonEvent* a_button, std::uint32_t a_key)
		{
			const auto& settings = Config::Get();
			if (!settings.mainBarKeyEnabled || settings.mainBarKey == Keys::kNone) {
				return false;
			}
			const bool pressed = a_button->IsPressed();
			if (a_key == settings.mainBarModifier) {
				mainBarModifierDown = pressed;
			}
			if (a_key != settings.mainBarKey) {
				return false;
			}
			mainBarKeyDown = pressed;
			if (settings.mainBarKeyMode == MainBarKeyMode::kHold) {
				return false;
			}
			if (a_button->IsDown()) {
				const bool modifierHeld = settings.mainBarKeyMode != MainBarKeyMode::kCombo || settings.mainBarModifier == Keys::kNone ||
				                          mainBarModifierDown;
				mainBarKeyUsed = modifierHeld && CurrentMode() == Mode::kGameplay;
				if (mainBarKeyUsed) {
					mainBarOpen = !mainBarOpen;
					logs::info("Main bar {} ({})", mainBarOpen ? "opened" : "closed", Keys::Name(a_key));
				}
				return mainBarKeyUsed;
			}
			const bool used = mainBarKeyUsed;
			if (!pressed) {
				mainBarKeyUsed = false;
			}
			return used;
		}

		// Extra bar whose key is held (hold mode); they win over a bar picked with a press
		std::optional<int> HeldBar()
		{
			const auto& settings = Config::Get();
			for (int i = 0; i < kModifierCount; ++i) {
				if (settings.modifierEnabled[i] && !settings.modifierToggle[i] && settings.modifierKeys[i] != Keys::kNone && modifierDown[i]) {
					return i;
				}
			}
			return std::nullopt;
		}

		// Extra bar picked with a press-mode key, while that bar is still on and in press mode
		std::optional<int> SwitchedBar()
		{
			const auto& settings = Config::Get();
			if (switchedBar >= 0 && settings.modifierEnabled[switchedBar] && settings.modifierToggle[switchedBar] &&
				settings.modifierKeys[switchedBar] != Keys::kNone) {
				return switchedBar;
			}
			return std::nullopt;
		}

		SlotState& StateOf(int a_slot)
		{
			return a_slot == kReadySpellSlot ? slotStates[kMaxSlots] : a_slot == kReadyPotionSlot ? slotStates[kMaxSlots + 1] : slotStates[a_slot];
		}

		constexpr int kNoSlot = -1;  // not a hotbar key (the pseudo slots are negative too)

		// Slot of a key; the Oblivion style cast / potion keys give their pseudo slots
		int FindSlot(std::uint32_t a_key)
		{
			const auto& settings = Config::Get();
			if (settings.keyMode == KeyMode::kOblivion) {
				if (settings.castKey == a_key) {
					return kReadySpellSlot;
				}
				if (settings.potionKey == a_key) {
					return kReadyPotionSlot;
				}
			}
			for (int i = 0; i < settings.slotCount; ++i) {
				if (settings.slotKeys[i] == a_key) {
					return i;
				}
			}
			return kNoSlot;
		}

		// The Oblivion style cast / potion key bind to their pseudo slots
		void Bind(Page a_page, int a_slot)
		{
			const auto target = a_slot == kReadySpellSlot  ? std::string{ "cast key" } :
			                    a_slot == kReadyPotionSlot ? std::string{ "potion key" } :
			                                                 std::format("slot {} ({})", a_slot + 1, Config::PageName(a_page));
			const auto form = MenuSelection();
			if (!form) {
				logs::info("Bind {}: no selected item found in the open menu", target);
				RE::PlaySound("MAGFailSD");
				return;
			}
			logs::info("Bind {}: {} [{:08X}]", target, form->GetName(), form->GetFormID());
			if (IsReadySlot(a_slot) ? !Bindings::FitsReadySlot(a_slot, form) : !Bindings::IsBindable(form)) {
				RE::PlaySound("MAGFailSD");
				return;
			}
			switch (Bindings::BindFromMenu(a_page, a_slot, form)) {
			case Bindings::MenuBind::kBound:
				RE::PlaySound("UIFavorite");
				break;
			case Bindings::MenuBind::kHandChanged:
				{
					constexpr const char* kHands[] = { "right", "right", "left", "both" };
					logs::info("Bind {}: hand -> {}", target, kHands[static_cast<int>(Bindings::Get(a_page, a_slot).hand)]);
					RE::PlaySound("UIMenuFocus");
				}
				break;
			case Bindings::MenuBind::kCleared:
				RE::PlaySound("UIUnFavorite");
				break;
			}
		}

		// A slot key press uses (or in a menu binds) the slot right away, on the page of the held modifier
		void OnDown(int a_slot, std::uint32_t a_key, Mode a_mode)
		{
			StateOf(a_slot) = { true, a_mode };
			lastHotbarKey = std::chrono::steady_clock::now();
			if (!IsReadySlot(a_slot)) {
				lastSlotKey = lastHotbarKey;
			}
			const auto page = CurrentPage();
			if (a_mode == Mode::kBind) {
				Bind(page, a_slot);
			} else {
				// one line per press: a press missing from the log never reached the hotbar (another mod took the key)
				const auto form = Bindings::GetForm(page, a_slot);
				logs::info("Key {} -> {} ({}): {}", Keys::Name(a_key), IsReadySlot(a_slot) ? "picked spell / potion" : std::format("slot {}", a_slot + 1),
					Config::PageName(page), form ? form->GetName() : "empty");
				Actions::Use(page, a_slot, true, a_key);
			}
		}
	}

	bool OnInputEvent(RE::InputEvent* a_event)
	{
		if (!a_event || a_event->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) {
			return false;
		}
		const auto button = a_event->AsButtonEvent();
		const auto key = button ? Keys::FromEvent(button) : Keys::kNone;
		if (key == Keys::kNone) {
			return false;
		}

		std::scoped_lock lock(Config::Lock());
		auto&            settings = Config::Get();

		if (captureTarget) {
			if (button->IsDown()) {
				if (key != Keys::kEscape) {
					*captureTarget = (key == Keys::kDelete || key == Keys::kBackspace) ? Keys::kNone : key;
					Config::MarkDirty();
				}
				captureTarget = nullptr;
			}
			return true;
		}

		if (OnMainBarKey(button, key)) {
			return true;
		}

		const bool pressed = button->IsPressed();

		// extra bar keys always reach the game too
		for (int i = 0; i < kModifierCount; ++i) {
			if (!settings.modifierEnabled[i] || settings.modifierKeys[i] != key) {
				continue;
			}
			if (!settings.modifierToggle[i]) {
				modifierDown[i] = pressed;
			} else if (button->IsDown() && MainBarUsable(CurrentMode())) {
				// press mode: switches to this extra bar, or back to the main bar if it is the current one
				switchedBar = SwitchedBar() == i ? -1 : i;
			}
		}

		const auto slot = FindSlot(key);
		if (slot == kNoSlot) {
			return false;
		}

		// the cast / potion key only exist for the hotbar, the game never sees them
		const bool block = IsReadySlot(slot) || settings.blockGameInput;

		if (!pressed) {
			Actions::OnKeyUp(key);
			auto&      state = StateOf(slot);
			const bool wasOurs = state.down;
			state.down = false;
			return wasOurs && (state.mode == Mode::kBind || block);
		}

		// a closed main bar leaves its slot keys to the game; the Oblivion style cast / potion keys keep working
		const auto mode = CurrentMode();
		if (mode == Mode::kNone || (!IsReadySlot(slot) && !MainBarUsable(mode))) {
			return false;
		}

		if (button->IsDown()) {
			OnDown(slot, key, mode);
		}
		return mode == Mode::kBind || block;
	}

	void Update()
	{
		std::scoped_lock lock(Config::Lock());
		const auto&      settings = Config::Get();
		const auto       missedRelease = [](bool& a_down, std::uint32_t a_key) {
			if (a_down && a_key != Keys::kNone && a_key < Keys::kMouseOffset && !KeyboardKeyDown(a_key)) {
				a_down = false;
			}
		};
		for (int i = 0; i < kModifierCount; ++i) {
			missedRelease(modifierDown[i], settings.modifierKeys[i]);
		}
		missedRelease(mainBarKeyDown, settings.mainBarKey);
		missedRelease(mainBarModifierDown, settings.mainBarModifier);
	}

	void Reset()
	{
		std::scoped_lock lock(Config::Lock());
		slotStates.fill({});
		modifierDown.fill(false);
		switchedBar = -1;
		mainBarOpen = false;
		mainBarKeyDown = false;
		mainBarModifierDown = false;
		mainBarKeyUsed = false;
	}

	bool MainBarOpen()
	{
		const auto& settings = Config::Get();
		if (!settings.mainBarKeyEnabled || settings.mainBarKey == Keys::kNone) {
			return true;
		}
		return settings.mainBarKeyMode == MainBarKeyMode::kHold ? mainBarKeyDown : mainBarOpen;
	}

	void BeginCapture(std::uint32_t* a_target)
	{
		std::scoped_lock lock(Config::Lock());
		captureTarget = a_target;
	}

	void CancelCapture()
	{
		std::scoped_lock lock(Config::Lock());
		captureTarget = nullptr;
	}

	const std::uint32_t* CaptureTarget()
	{
		return captureTarget;
	}

	Page CurrentPage()
	{
		const auto bar = HeldBar().or_else(SwitchedBar);
		return bar ? static_cast<Page>(static_cast<int>(Page::kModifier1) + *bar) : Page::kMain;
	}

	bool PageSwitched()
	{
		return !HeldBar() && SwitchedBar();
	}

	namespace
	{
		float SecondsSince(std::chrono::steady_clock::time_point a_time)
		{
			if (a_time == std::chrono::steady_clock::time_point{}) {
				return std::numeric_limits<float>::max();
			}
			return std::chrono::duration<float>(std::chrono::steady_clock::now() - a_time).count();
		}
	}

	float SinceSlotKey()
	{
		const bool held = std::ranges::any_of(slotStates | std::views::take(kMaxSlots), &SlotState::down);
		return held ? 0.0f : SecondsSince(lastSlotKey);
	}

	float SinceHotbarKey()
	{
		return std::ranges::any_of(slotStates, &SlotState::down) ? 0.0f : SecondsSince(lastHotbarKey);
	}

	bool InBindMenu()
	{
		const auto ui = RE::UI::GetSingleton();
		if (!ui || TextEntryActive() || UI::IsBlockingWindowOpen() || Util::InBeastForm()) {
			return false;  // a vampire lord can open the Magic menu, but the hotbar is off in beast form
		}
		// Favorites menu only with "Bind in the Favorites menu": otherwise its number keys set the game's own hotkeys
		return ui->IsMenuOpen(RE::MagicMenu::MENU_NAME) || ui->IsMenuOpen(RE::InventoryMenu::MENU_NAME) ||
		       (Config::Get().bindInFavorites && ui->IsMenuOpen(RE::FavoritesMenu::MENU_NAME));
	}

	RE::TESForm* MenuSelection()
	{
		const auto ui = RE::UI::GetSingleton();
		if (!ui) {
			return nullptr;
		}

		if (const auto menu = ui->GetMenu<RE::MagicMenu>(); menu && menu->uiMovie) {
			// SkyUI (SKSE extended data) puts the form id on the entry, approach from SpellHotbar2 / Wheeler
			RE::GFxValue selection;
			menu->uiMovie->GetVariable(&selection, "_root.Menu_mc.inventoryLists.itemList.selectedEntry.formId");
			if (selection.IsNumber()) {
				return RE::TESForm::LookupByID(static_cast<RE::FormID>(selection.GetNumber()));
			}
			// vanilla menu: the game's own list, indexed by the selected entry
			const auto itemList = menu->GetRuntimeData().itemList;
			const auto item = itemList ? itemList->GetSelectedItem() : nullptr;
			return item ? item->data.baseForm : nullptr;
		}

		if (const auto menu = ui->GetMenu<RE::InventoryMenu>()) {
			const auto itemList = menu->GetRuntimeData().itemList;
			const auto item = itemList ? itemList->GetSelectedItem() : nullptr;
			if (item && item->data.objDesc) {
				return item->data.objDesc->GetObject();
			}
			return nullptr;
		}

		if (const auto menu = ui->GetMenu<RE::FavoritesMenu>()) {
			auto& root = menu->GetRuntimeData().root;
			if (!root.IsDisplayObject() || !root.HasMember("itemList")) {
				return nullptr;
			}
			RE::GFxValue itemList;
			root.GetMember("itemList", &itemList);
			if (!itemList.IsDisplayObject() || !itemList.HasMember("selectedEntry")) {
				return nullptr;
			}
			RE::GFxValue entry;
			itemList.GetMember("selectedEntry", &entry);
			if (entry.IsObject() && entry.HasMember("formId")) {
				RE::GFxValue formId;
				entry.GetMember("formId", &formId);
				if (formId.IsNumber()) {
					return RE::TESForm::LookupByID(static_cast<RE::FormID>(formId.GetNumber()));
				}
			}
			// vanilla menu: entries are in the order of the game's favorites array
			RE::GFxValue index;
			if (itemList.GetMember("selectedIndex", &index) && index.IsNumber()) {
				const auto  idx = static_cast<std::int32_t>(index.GetNumber());
				const auto& favorites = menu->GetRuntimeData().favorites;
				if (idx >= 0 && static_cast<std::uint32_t>(idx) < favorites.size()) {
					return favorites[idx].item;
				}
			}
		}
		return nullptr;
	}
}
