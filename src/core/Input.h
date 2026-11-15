#pragma once

#include "core/Config.h"

namespace Input
{
	// Called by SKSE Menu Framework for every input event before the game sees it. true = swallow it.
	bool OnInputEvent(RE::InputEvent* a_event);

	// Real time tick (drops modifiers whose key-up was missed, e.g. alt-tab). Runs every frame, also in menus.
	void Update();

	void Reset();

	// Next pressed button is written to a_target. Esc cancels, Delete / Backspace clears.
	void               BeginCapture(std::uint32_t* a_target);
	void               CancelCapture();
	const std::uint32_t* CaptureTarget();

	// Page currently selected: by slot picking (while its key is held), a held extra bar key, else by the last press
	// of a press-mode extra bar key
	Page CurrentPage();

	// Slot picking: the hold key is held (the bar shows the picked slot, D-pad moves it). nullopt otherwise.
	std::optional<int> PickedSlot();

	// The current page comes from a press-mode extra bar key (nothing has to be held for it)
	bool PageSwitched();

	// With the main bar key option: the main bar was opened with its key (or combo), or the key is held in hold mode.
	// Without it always true. Binding in menus works either way.
	bool MainBarOpen();

	// The bar in the inventory / magic / favorites menu: closed until the menu bar key opens it when that option is on
	// (closed, slot keys there are the game's), always open otherwise
	bool MenuBarOpen();

	// Seconds since a slot key (not the Oblivion style cast / potion key) was last pressed (also to bind), 0 while one is
	// held. Large if none was pressed yet.
	float SinceSlotKey();

	// The same for slot keys and the Oblivion style cast / potion keys (the "After pressing a hotbar key" visibility)
	float SinceHotbarKey();

	// Binding happens by pressing slot keys while one of these menus has an item selected
	bool         InBindMenu();
	RE::TESForm* MenuSelection();
}
