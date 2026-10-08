#pragma once

#include "core/Config.h"

enum class Hand : std::uint8_t
{
	kAuto = 0,  // right hand, without the HUD letter
	kRight,
	kLeft,
	kBoth
};

// What the slot key does with this slot's spell, scroll, power or shout
enum class SlotMode : std::uint8_t
{
	kDefault = 0,  // the "What slot keys do" setting
	kCast,
	kEquip
};

struct SlotBinding
{
	RE::FormID form{ 0 };
	Hand       hand{ Hand::kAuto };
	SlotMode   mode{ SlotMode::kDefault };

	[[nodiscard]] bool Empty() const { return form == 0; }
};

// Oblivion style: the spell / scroll picked for the cast key and the potion picked for the potion key. These pseudo
// slots work with every function taking a slot (the page is ignored).
inline constexpr int kReadySpellSlot = -2;
inline constexpr int kReadyPotionSlot = -3;

inline bool IsReadySlot(int a_slot) { return a_slot == kReadySpellSlot || a_slot == kReadyPotionSlot; }

// Slot bindings belong to the character, so they are stored in the SKSE co-save.
// Profiles can optionally carry them as plugin-relative form ids.
namespace Bindings
{
	SlotBinding&       Get(Page a_page, int a_slot);
	RE::TESForm*       GetForm(Page a_page, int a_slot);
	void               Set(Page a_page, int a_slot, RE::FormID a_form, Hand a_hand = Hand::kAuto);
	void               Clear(Page a_page, int a_slot);
	void               ClearAll();

	enum class MenuBind
	{
		kBound,
		kHandChanged,
		kCleared
	};

	// Bind from a menu (like SpellHotbar2): the same form again cycles its hand right -> left -> both -> right, or
	// clears the slot for forms without a hand
	MenuBind BindFromMenu(Page a_page, int a_slot, RE::TESForm* a_form);

	// Forms that can sit in a slot
	bool IsBindable(const RE::TESForm* a_form);

	// The hand only matters for one-handed spells, scrolls and weapons (weapons: right / left)
	bool UsesHand(const RE::TESForm* a_form);

	// The slot mode only matters for spells, scrolls, powers and shouts
	bool UsesMode(const RE::TESForm* a_form);

	// What the key of this binding does: its own mode, else the setting
	KeyMode ModeOf(const SlotBinding& a_binding);

	// Oblivion style: spells / scrolls for the cast key, potions for the potion key
	bool FitsReadySlot(int a_slot, const RE::TESForm* a_form);

	json ToJson();
	void FromJson(const json& a_json);

	void RegisterSerialization();
}
