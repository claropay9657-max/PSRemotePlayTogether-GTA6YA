// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef PSRPT_PCINPUT_H
#define PSRPT_PCINPUT_H

// The guests' mouse and keyboard into a game on this computer (docs/pc-source.md, section 7):
// Windows' SendInput while the shared window is in front, Linux's uinput while this app is not,
// on a Mac events for the shared program (with the Accessibility permission). Keys come as USB
// HID usages (keymap.h). Keys that leave the game never go through: the Windows or Command key,
// Alt+Tab, Alt+F4, Ctrl+Esc, Ctrl+Alt+Delete.

#include "pcsource.h"

#include <QString>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class PcInput
{
	public:
		/** Why guests cannot use mouse and keyboard on this computer, in words; empty if they can. */
		static QString Problem();
		/** Asks for what is missing: macOS's Accessibility permission. */
		static void AskPermission();

		explicit PcInput(const PcSource::Target &target);
		~PcInput();
		PcInput(const PcInput &) = delete;
		PcInput &operator=(const PcInput &) = delete;

		bool Open(std::string *error);

		/**
		 * Moves and scrolls by this much, and holds these buttons (coplay::MouseButton) and keys
		 * (USB HID usages) from now on, letting go of the others. While the shared game is not in
		 * front, nothing goes through and everything is let go. On the main thread.
		 */
		void Apply(int dx, int dy, int wheel_y, int wheel_x, uint8_t buttons, const std::vector<uint8_t> &keys);
		void ReleaseAll();

		/** The keys of `keys` that may reach the game (no Windows key, Alt+Tab, ...). */
		static std::vector<uint8_t> Allowed(const std::vector<uint8_t> &keys);

	private:
		struct Private;
		std::unique_ptr<Private> d;

		bool GameInFront();
		void Key(uint8_t hid, bool down);
		void Button(uint8_t button, bool down);
		void Move(int dx, int dy);
		void Wheel(int wheel_y, int wheel_x);
		void Flush();

		PcSource::Target target;
		std::vector<uint8_t> held_keys;
		uint8_t held_buttons = 0;
};

#endif // PSRPT_PCINPUT_H
