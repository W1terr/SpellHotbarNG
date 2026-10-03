#include "core/Keys.h"

namespace Keys
{
	namespace
	{
		// XInput button masks as reported by gamepad ButtonEvents, in SKSE key code order (0x10A + index)
		constexpr std::array<std::uint32_t, 16> kGamepadMasks{
			0x0001,  // DPad Up
			0x0002,  // DPad Down
			0x0004,  // DPad Left
			0x0008,  // DPad Right
			0x0010,  // Start
			0x0020,  // Back
			0x0040,  // Left Thumb
			0x0080,  // Right Thumb
			0x0100,  // Left Shoulder
			0x0200,  // Right Shoulder
			0x1000,  // A
			0x2000,  // B
			0x4000,  // X
			0x8000,  // Y
			0x0009,  // Left Trigger
			0x000A,  // Right Trigger
		};

		constexpr std::array<std::string_view, 16> kGamepadNames{
			"DPad Up", "DPad Down", "DPad Left", "DPad Right", "Start", "Back", "LS", "RS",
			"LB", "RB", "A", "B", "X", "Y", "LT", "RT"
		};

		constexpr std::array<std::string_view, 10> kMouseNames{
			"Mouse 1", "Mouse 2", "Mouse 3", "Mouse 4", "Mouse 5", "Mouse 6", "Mouse 7", "Mouse 8", "Wheel Up", "Wheel Down"
		};

		std::string_view KeyboardName(std::uint32_t a_key)
		{
			switch (a_key) {
			case 0x01: return "Esc";
			case 0x02: return "1";
			case 0x03: return "2";
			case 0x04: return "3";
			case 0x05: return "4";
			case 0x06: return "5";
			case 0x07: return "6";
			case 0x08: return "7";
			case 0x09: return "8";
			case 0x0A: return "9";
			case 0x0B: return "0";
			case 0x0C: return "-";
			case 0x0D: return "=";
			case 0x0E: return "Backspace";
			case 0x0F: return "Tab";
			case 0x10: return "Q";
			case 0x11: return "W";
			case 0x12: return "E";
			case 0x13: return "R";
			case 0x14: return "T";
			case 0x15: return "Y";
			case 0x16: return "U";
			case 0x17: return "I";
			case 0x18: return "O";
			case 0x19: return "P";
			case 0x1A: return "[";
			case 0x1B: return "]";
			case 0x1C: return "Enter";
			case 0x1D: return "LCtrl";
			case 0x1E: return "A";
			case 0x1F: return "S";
			case 0x20: return "D";
			case 0x21: return "F";
			case 0x22: return "G";
			case 0x23: return "H";
			case 0x24: return "J";
			case 0x25: return "K";
			case 0x26: return "L";
			case 0x27: return ";";
			case 0x28: return "'";
			case 0x29: return "~";
			case 0x2A: return "LShift";
			case 0x2B: return "\\";
			case 0x2C: return "Z";
			case 0x2D: return "X";
			case 0x2E: return "C";
			case 0x2F: return "V";
			case 0x30: return "B";
			case 0x31: return "N";
			case 0x32: return "M";
			case 0x33: return ",";
			case 0x34: return ".";
			case 0x35: return "/";
			case 0x36: return "RShift";
			case 0x37: return "Num*";
			case 0x38: return "LAlt";
			case 0x39: return "Space";
			case 0x3A: return "Caps";
			case 0x3B: return "F1";
			case 0x3C: return "F2";
			case 0x3D: return "F3";
			case 0x3E: return "F4";
			case 0x3F: return "F5";
			case 0x40: return "F6";
			case 0x41: return "F7";
			case 0x42: return "F8";
			case 0x43: return "F9";
			case 0x44: return "F10";
			case 0x45: return "NumLock";
			case 0x46: return "ScrLock";
			case 0x47: return "Num7";
			case 0x48: return "Num8";
			case 0x49: return "Num9";
			case 0x4A: return "Num-";
			case 0x4B: return "Num4";
			case 0x4C: return "Num5";
			case 0x4D: return "Num6";
			case 0x4E: return "Num+";
			case 0x4F: return "Num1";
			case 0x50: return "Num2";
			case 0x51: return "Num3";
			case 0x52: return "Num0";
			case 0x53: return "Num.";
			case 0x57: return "F11";
			case 0x58: return "F12";
			case 0x9C: return "NumEnter";
			case 0x9D: return "RCtrl";
			case 0xB5: return "Num/";
			case 0xB8: return "RAlt";
			case 0xC5: return "Pause";
			case 0xC7: return "Home";
			case 0xC8: return "Up";
			case 0xC9: return "PgUp";
			case 0xCB: return "Left";
			case 0xCD: return "Right";
			case 0xCF: return "End";
			case 0xD0: return "Down";
			case 0xD1: return "PgDn";
			case 0xD2: return "Ins";
			case 0xD3: return "Del";
			default: return {};
			}
		}
	}

	std::uint32_t FromEvent(const RE::ButtonEvent* a_event)
	{
		const auto id = a_event->GetIDCode();
		switch (a_event->GetDevice()) {
		case RE::INPUT_DEVICE::kKeyboard:
			return id;
		case RE::INPUT_DEVICE::kMouse:
			return kMouseOffset + id;
		case RE::INPUT_DEVICE::kGamepad:
			return FromGamepadMask(id);
		default:
			return kNone;
		}
	}

	std::uint32_t FromGamepadMask(std::uint32_t a_mask)
	{
		const auto it = std::ranges::find(kGamepadMasks, a_mask);
		return it != kGamepadMasks.end() ? kGamepadOffset + static_cast<std::uint32_t>(it - kGamepadMasks.begin()) : kNone;
	}

	std::uint32_t GamepadMask(std::uint32_t a_key)
	{
		return a_key >= kGamepadOffset && a_key < kGamepadOffset + kGamepadMasks.size() ? kGamepadMasks[a_key - kGamepadOffset] : 0;
	}

	std::string Name(std::uint32_t a_key)
	{
		if (a_key == kNone) {
			return "-";
		}
		if (a_key >= kGamepadOffset && a_key < kGamepadOffset + kGamepadNames.size()) {
			return std::string(kGamepadNames[a_key - kGamepadOffset]);
		}
		if (a_key >= kMouseOffset && a_key < kMouseOffset + kMouseNames.size()) {
			return std::string(kMouseNames[a_key - kMouseOffset]);
		}
		if (const auto name = KeyboardName(a_key); !name.empty()) {
			return std::string(name);
		}
		return std::format("Key {:X}", a_key);
	}
}
