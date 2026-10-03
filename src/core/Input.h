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

	// Page currently selected: by a held extra bar key, else by the last press of a press-mode extra bar key
	Page CurrentPage();

	// The current page comes from a press-mode extra bar key (nothing has to be held for it)
	bool PageSwitched();

	// Seconds since a slot key (not the Oblivion style cast / potion key) was last pressed (also to bind), 0 while one is
	// held. Large if none was pressed yet.
	float SinceSlotKey();

	// Binding happens by pressing slot keys while one of these menus has an item selected
	bool         InBindMenu();
	RE::TESForm* MenuSelection();
}
