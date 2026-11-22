// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef PSRPT_VIRTUALPADS_H
#define PSRPT_VIRTUALPADS_H

// Game controllers on this computer that the players' controllers drive, for a game on
// the host's computer (docs/pc-source.md). On Windows these are Xbox 360 controllers of the
// ViGEmBus driver, which games, Steam and the Xbox app all take; elsewhere there are none yet.

#include <coplay/protocol.h>

#include <QString>

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
		/** Where the driver comes from, for the Host page. */
		static QString DriverUrl();
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
