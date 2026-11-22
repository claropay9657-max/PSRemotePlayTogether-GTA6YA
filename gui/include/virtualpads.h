// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef PSRPT_VIRTUALPADS_H
#define PSRPT_VIRTUALPADS_H

// Game controllers on this computer that the players' controllers drive, for a game on
// the host's computer (docs/pc-source.md): Xbox 360 controllers, which games, Steam and the
// Xbox app all take. On Windows through the ViGEmBus driver, on Linux through the kernel's
// uinput; macOS has none for apps (friends watch there).

#include <coplay/protocol.h>

#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class VirtualPads
{
	public:
		/** The most controllers there can be (XInput's four, the host's own one among them). */
		static constexpr int kMaxPads = 4;

		/** Whether this build has virtual controllers on this system at all. */
		static bool Built();
		/** Where the driver comes from, for the Host page; empty where there is none to get. */
		static QString DriverUrl();
		/** Linux: the command that lets this app add controllers (a udev rule); empty elsewhere. */
		static QString FixCommand();

		/**
		 * Windows: whether this XInput slot (0..3) is one of this app's controllers right now, so
		 * that the app does not take its friends' controllers for the host's own. (On Linux they
		 * carry "PSRemotePlayTogether" in their name.)
		 */
		static bool IsOwnSlot(int slot);
		/** Counts up each time this app starts plugging controllers in; 0 if it never has. */
		static int PlugGeneration();
		/** Whether any of this app's controllers are plugged in now. */
		static bool AnyPlugged();
		/**
		 * Why there are no virtual controllers right now, in words for the Host page; empty if
		 * there are. Asks the driver, so not every frame.
		 */
		static QString Problem();

		VirtualPads();
		~VirtualPads();
		VirtualPads(const VirtualPads &) = delete;
		VirtualPads &operator=(const VirtualPads &) = delete;

		bool Open(std::string *error);
		void Close();
		bool IsOpen() const;

		/**
		 * Makes sure there are this many controllers, plugged in this order; the game numbers
		 * them as they come (after any real one already there). Fewer unplugs the last ones.
		 */
		void SetCount(int count);
		int Count() const;
		/** What controller `index` presses now. */
		void Set(int index, const coplay::PadState &state);

		/** The game makes a controller rumble (0..255 each); on the driver's thread. */
		std::function<void(int index, uint8_t left, uint8_t right)> on_rumble;

	private:
		struct Private;
		std::unique_ptr<Private> d;
};

#endif // PSRPT_VIRTUALPADS_H
