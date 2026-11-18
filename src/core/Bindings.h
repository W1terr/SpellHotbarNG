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

// Option "Gear sets in slots": a slot can hold up to 2 one-handed weapons (right hand, left hand) or up to 4 armor
// pieces, equipped together
inline constexpr int kMaxGearSet = 4;
inline constexpr int kMaxWeaponSet = 2;

struct SlotBinding
{
	RE::FormID form{ 0 };
	Hand       hand{ Hand::kAuto };
	SlotMode   mode{ SlotMode::kDefault };
	std::array<RE::FormID, kMaxGearSet - 1> set{};  // a gear set's other items after `form`, packed from the front

	[[nodiscard]] bool Empty() const { return form == 0; }
	[[nodiscard]] bool IsGearSet() const { return form != 0 && set[0] != 0; }
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

	// Option "Unfavoriting removes it from the bar": a bound spell, item, power or shout that stops being a favorite
	// leaves the bar, like a vanilla favorites hotkey. Call every frame.
	void UpdateFavorites(float a_delta);

	enum class MenuBind
	{
		kBound,
		kHandChanged,
		kCleared,
		kAddedToSet,
		kRemovedFromSet,
		kSetRefused  // gear set full, or an armor piece covering the same body part as one in the set
	};

	// Bind from a menu (like SpellHotbar2): the same form again cycles its hand right -> left -> both -> right, or
	// clears the slot for forms without a hand. With "Gear sets in slots" a weapon / armor on a slot holding that
	// kind is added to its set, and the same item again leaves the set.
	MenuBind BindFromMenu(Page a_page, int a_slot, RE::TESForm* a_form);

	// Every item of the slot: `form` and then a gear set's others (form ids, nothing for an empty slot)
	std::vector<RE::FormID> Items(const SlotBinding& a_binding);

	// The forms of Items() that exist
	std::vector<RE::TESForm*> ItemForms(const SlotBinding& a_binding);

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
