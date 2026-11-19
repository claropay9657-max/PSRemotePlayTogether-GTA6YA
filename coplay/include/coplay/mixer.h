// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef COPLAY_MIXER_H
#define COPLAY_MIXER_H

#include <cstdint>
#include <map>

#include "protocol.h"

namespace coplay {

/** Which parts of a controller a player is allowed to contribute. */
struct InputMask
{
	uint32_t buttons = 0xffffffffu;
	bool left_stick = true;
	bool right_stick = true;
	bool l2 = true;
	bool r2 = true;
	bool touch = true;
	bool motion = true;

	static InputMask All() { return InputMask(); }
	static InputMask None()
	{
		InputMask m;
		m.buttons = 0;
		m.left_stick = m.right_stick = m.l2 = m.r2 = m.touch = m.motion = false;
		return m;
	}
};

PadState ApplyMask(const PadState &state, const InputMask &mask);

/**
 * Combines two controller states the way two people sharing one controller
 * would expect: a button is down if either holds it, a trigger is as far in as
 * the one pressed further, and each stick follows whoever pushes it further.
 * Touches and motion come from the first state that has them.
 */
PadState MergePadStates(const PadState &a, const PadState &b);

/**
 * Turns the controller states of the host and all guests into the one state
 * sent to the console. Not thread safe.
 */
class InputMixer
{
	public:
		static constexpr uint32_t kHostSource = 0;

		struct Config
		{
			/** A guest that sent nothing for this long counts as having let go of everything. */
			uint32_t stale_ms = 500;
			/** LastActive: how long the player in control must rest before someone else can take over. */
			uint32_t handoff_ms = 350;
			int16_t stick_deadzone = 6000;
			uint8_t trigger_threshold = 16;
		};

		InputMixer();
		explicit InputMixer(const Config &config);

		void SetMode(MixMode mode);
		MixMode Mode() const { return mode; }

		void SetSource(uint32_t id, const PadState &state, uint64_t now_ms);
		void RemoveSource(uint32_t id);
		void SetMask(uint32_t id, const InputMask &mask);

		PadState Mix(uint64_t now_ms);

		/** After Mix(): whether this source's input currently reaches the console. */
		bool InControl(uint32_t id) const;

		/** Whether the state shows deliberate input (a button, a trigger, a stick outside the deadzone, a touch). */
		bool IsActive(const PadState &state) const;

	private:
		struct Source
		{
			PadState state;
			InputMask mask;
			uint64_t updated_ms = 0;
			uint64_t last_active_ms = 0;
			bool has_state = false;
		};

		PadState Effective(uint32_t id, const Source &source, uint64_t now_ms) const;

		Config config;
		MixMode mode = MixMode::Together;
		std::map<uint32_t, Source> sources;
		bool has_holder = false;
		uint32_t holder = 0;
};

} // namespace coplay

#endif // COPLAY_MIXER_H
