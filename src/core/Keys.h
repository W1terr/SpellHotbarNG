#pragma once

// Key codes use the SKSE / MCM convention so they match what other mods show:
// keyboard = DirectInput scan code, mouse = 0x100 + button, gamepad = 0x10A + button index
namespace Keys
{
	inline constexpr std::uint32_t kNone = 0;
	inline constexpr std::uint32_t kMouseOffset = 0x100;
	inline constexpr std::uint32_t kGamepadOffset = 0x10A;

	inline constexpr std::uint32_t kEscape = 0x01;
	inline constexpr std::uint32_t kBackspace = 0x0E;
	inline constexpr std::uint32_t kDelete = 0xD3;

	// Unified key code of a button event, 0 for devices we don't handle
	std::uint32_t FromEvent(const RE::ButtonEvent* a_event);

	// Gamepad key code <-> XInput button mask (what gamepad ButtonEvents carry); 0 if there is none
	std::uint32_t FromGamepadMask(std::uint32_t a_mask);
	std::uint32_t GamepadMask(std::uint32_t a_key);

	std::string Name(std::uint32_t a_key);
}
