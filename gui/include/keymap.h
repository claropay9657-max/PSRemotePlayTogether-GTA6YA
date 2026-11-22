// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef PSRPT_KEYMAP_H
#define PSRPT_KEYMAP_H

// Keys as USB HID usages (keyboard page), the way mouse and keyboard travel from a guest to
// a game on the host's computer (coplay::KeyMouseState), and each system's own key codes.
// The physical key counts, not the letter on it: a game's WASD stay where they are on any layout.

#include <cstdint>

namespace KeyMap {

/** Windows' scan code (set 1; extended keys as 0xE0xx), or 0. */
uint16_t HidToWindows(uint8_t hid);
/** Linux's evdev key code (KEY_*), or 0. */
uint16_t HidToEvdev(uint8_t hid);
/** macOS's virtual key code (kVK_*), or 0xffff. */
uint16_t HidToMac(uint8_t hid);

/** From Windows' scan code; Qt's form (extended as 0x1xx) is taken too. 0 if unknown. */
uint8_t WindowsToHid(uint32_t scan);
/** From Linux's evdev key code (an X11/xkb key code is that plus 8). */
uint8_t EvdevToHid(uint32_t code);
uint8_t MacToHid(uint32_t vk);

/** Modifier keys: Ctrl, Shift, Alt/Option, GUI (Windows key, Command), left and right. */
bool IsModifier(uint8_t hid);

constexpr uint8_t kLeftCtrl = 0xe0;
constexpr uint8_t kLeftShift = 0xe1;
constexpr uint8_t kLeftAlt = 0xe2;
constexpr uint8_t kLeftGui = 0xe3;
constexpr uint8_t kRightCtrl = 0xe4;
constexpr uint8_t kRightShift = 0xe5;
constexpr uint8_t kRightAlt = 0xe6;
constexpr uint8_t kRightGui = 0xe7;
constexpr uint8_t kTab = 0x2b;
constexpr uint8_t kEscape = 0x29;
constexpr uint8_t kF4 = 0x3d;
constexpr uint8_t kDelete = 0x4c;

} // namespace KeyMap

#endif // PSRPT_KEYMAP_H
