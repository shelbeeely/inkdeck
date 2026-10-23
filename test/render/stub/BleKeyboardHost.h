#pragma once
#include <cstdint>
namespace freeink {
enum class SpecialKey : uint8_t { None, Enter, Backspace, Delete, Tab, Left, Right, Up, Down, Home, End, PageUp, PageDown, Escape };
struct KeyEvent { char ch = 0; uint8_t keycode = 0; uint8_t mods = 0; SpecialKey special = SpecialKey::None; };
}
