#pragma once

inline constexpr int kMaxSlots = 20;
inline constexpr int kModifierCount = 3;

// Every slot exists once per page. Holding a modifier shows / uses that modifier's page.
enum class Page : int
{
	kMain = 0,
	kModifier1,
	kModifier2,
	kModifier3,

	kTotal
};
inline constexpr int kPageCount = static_cast<int>(Page::kTotal);

enum class Anchor : int
{
	kTopLeft = 0,
	kTop,
	kTopRight,
	kLeft,
	kCenter,
	kRight,
	kBottomLeft,
	kBottom,
	kBottomRight
};

enum class Visibility : int
{
	kAlways = 0,
	kCombat,
	kWeaponDrawn,
	kCombatOrWeaponDrawn,
	kNever,
	kSneaking,
	kKeyPress  // for a moment after a slot key (or the Oblivion style cast / potion key)
};

// What pressing a slot key does with spells, scrolls, powers and shouts (items always work the same)
enum class KeyMode : int
{
	kCast = 0,  // cast right away with the casting animation, nothing gets equipped
	kEquip,     // equip to the slot's hand (powers / shouts: to the voice slot), cast with the game's own controls
	kOblivion   // pick the spell (or potion) for the cast key / potion key; powers and shouts are equipped
};

// How the main bar key opens the main bar
enum class MainBarKeyMode : int
{
	kPress = 0,  // one press opens it, the next closes it
	kCombo,      // the same, but only while the modifier key is held (Shift + H)
	kHold        // open while the key is held
};

using RGBA = std::array<float, 4>;  // 0..1, the bar opacity multiplies the alpha

struct BarColors
{
	RGBA frame{ 1.0f, 1.0f, 1.0f, 1.0f };           // tint of the slot border texture
	RGBA slotBackground{ 1.0f, 1.0f, 1.0f, 1.0f };  // tint of the slot background texture
	RGBA keyLabel{ 1.0f, 1.0f, 1.0f, 1.0f };
	RGBA text{ 1.0f, 1.0f, 1.0f, 1.0f };  // item counts, cooldown seconds, page name
	RGBA handMarker{ 1.0f, 220 / 255.0f, 120 / 255.0f, 1.0f };
	RGBA equipped{ 1.0f, 1.0f, 1.0f, 0.9f };
	RGBA cooldown{ 0.0f, 0.0f, 0.0f, 0.65f };
	RGBA chargeBar{ 90 / 255.0f, 160 / 255.0f, 1.0f, 1.0f };
	RGBA channeling{ 160 / 255.0f, 200 / 255.0f, 1.0f, 1.0f };
	RGBA noMagicka{ 1.0f, 110 / 255.0f, 110 / 255.0f, 1.0f };  // icon tint when the spell costs more than you have
};

struct Settings
{
	// layout
	int        slotCount{ 10 };
	int        columns{ 10 };
	float      iconSize{ 52.0f };  // pixels at 1080p, scaled with the screen height
	float      spacing{ 6.0f };
	Anchor     anchor{ Anchor::kBottom };
	float      offsetX{ 0.0f };
	float      offsetY{ -100.0f };
	float      opacity{ 1.0f };
	Visibility visibility{ Visibility::kAlways };
	float      keyPressShowTime{ 3.0f };  // Visibility::kKeyPress: seconds the bar stays after the key is let go
	bool       fadeOutOfCombat{ false };
	float      fadedOpacity{ 0.35f };
	bool       showEmptySlots{ true };
	bool       showKeyLabels{ true };
	bool       showPageName{ true };
	bool       showCooldownText{ true };
	bool       showItemCount{ true };
	bool       showInMenus{ true };  // show the bar in the magic / inventory / favorites menu for binding
	BarColors  colors{};

	// keys
	std::array<std::uint32_t, kMaxSlots>      slotKeys{ 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D };
	std::array<std::uint32_t, kModifierCount> modifierKeys{ 0x2A, 0x1D, 0x38 };
	std::array<bool, kModifierCount>          modifierEnabled{ false, false, false };
	std::array<bool, kModifierCount>          modifierToggle{ false, false, false };  // one press switches to the extra bar and back, instead of holding
	bool                                      mainBarKeyEnabled{ false };  // the main bar (slot keys + bar) is closed until its key opens it
	MainBarKeyMode                            mainBarKeyMode{ MainBarKeyMode::kPress };
	std::uint32_t                             mainBarKey{ 0x23 };          // H
	std::uint32_t                             mainBarModifier{ 0x2A };     // key combo mode: held while pressing mainBarKey (LShift)
	bool                                      blockGameInput{ true };  // hotbar keys don't reach the game (vanilla hotkeys 1-8 etc.)
	bool                                      onlyWhileSneaking{ false };  // outside of sneak the hotbar keys are the game's
	bool                                      aimAtCrosshair{ true };      // aimed spells fly to the crosshair, not the combat target
	bool                                      individualShoutCooldowns{ false };  // each shout its own cooldown instead of the game's shared one

	// what slot keys do
	KeyMode       keyMode{ KeyMode::kCast };
	std::uint32_t castKey{ 0x2F };    // Oblivion style: casts the picked spell (V)
	std::uint32_t potionKey{ 0x30 };  // Oblivion style: uses the picked potion (B)

	// Oblivion style "ready" slots: picked spell, picked potion, current power
	Anchor readyAnchor{ Anchor::kBottomLeft };
	float  readyOffsetX{ 60.0f };
	float  readyOffsetY{ -110.0f };
	bool   readyShowPower{ true };
	bool   readyVertical{ false };
	bool   readyHideMainBar{ true };  // main bar only shows while picking (slot key pressed, extra bar held), in menus and in the settings
};

namespace Config
{
	Settings& Get();

	// Lock shared by input, update, HUD and menu code
	std::recursive_mutex& Lock();

	// Documents\My Games\Skyrim Special Edition\SKSE\SpellHotbarNG
	std::filesystem::path UserDir();
	// Data\SKSE\Plugins\SpellHotbarNG
	std::filesystem::path DataDir();

	void Load();
	void Save();
	void MarkDirty();
	void SaveIfDirty();
	void ResetToDefaults();
	void Validate(Settings& a_settings);

	json ToJson(const Settings& a_settings);
	void FromJson(const json& a_json, Settings& a_settings);

	const char* PageName(Page a_page);
}

namespace Profiles
{
	struct Entry
	{
		std::string           name;
		std::filesystem::path path;
	};

	// Data\SKSE\Plugins\SpellHotbarNG\profiles. Under Mod Organizer 2 new files there land in the overwrite folder.
	std::filesystem::path Dir();

	std::vector<Entry> List();
	bool               Save(const std::string& a_name, bool a_includeBindings);
	bool               Load(const Entry& a_entry, bool a_loadBindings);
	bool               Delete(const Entry& a_entry);
}
