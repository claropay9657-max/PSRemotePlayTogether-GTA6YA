// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_KEYMOUSEPAD_H
#define CHIAKI_KEYMOUSEPAD_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <vector>

/**
 * Mouse and keyboard as a controller: keys, mouse buttons and the wheel are buttons of the
 * controller or directions of its sticks, and the mouse's movement moves a stick, so that a
 * game that only knows controllers can be played the way a game on a computer is.
 *
 * This is the part that works it out, and nothing else: it knows no window and no settings,
 * is told what was pressed and how far the mouse moved (with the time, in milliseconds, of
 * whoever asks), and says what the controller looks like at a given time. KeyMouse (keymouse.h)
 * feeds it in the app; test/keymousepad.cpp tries it.
 */
namespace keymouse
{

/** What an input can be bound to. */
enum class Function
{
	Cross, Circle, Square, Triangle,
	DpadUp, DpadDown, DpadLeft, DpadRight,
	L1, R1, L2, R2, L3, R3,
	Options, Create, Touchpad, Ps,
	LeftUp, LeftDown, LeftLeft, LeftRight,
	RightUp, RightDown, RightLeft, RightRight,
	/** While held, the left stick goes only part of the way (Options::walk). */
	Walk,
	/** Catches the mouse for aiming and lets it go again: for whoever feeds this, here it does nothing. */
	MouseCatch,
	Count
};

constexpr size_t kFunctions = static_cast<size_t>(Function::Count);
/** How many inputs one function takes. */
constexpr size_t kSlots = 3;

/**
 * An input's code: a key as the window system names it (the app: Qt::Key), or one of these.
 * Mouse buttons count from 1: left, right, middle, back, forward.
 */
constexpr uint32_t kMouseButton = 0x40000000u;
constexpr uint32_t kWheelUp = 0x40001001u;
constexpr uint32_t kWheelDown = 0x40001002u;

inline bool IsMouse(uint32_t code) { return (code & 0x40000000u) != 0; }

/** The controller's buttons, with the bits the stream library gives them (chiaki/controller.h). */
enum : uint32_t
{
	kCross = 1u << 0, kCircle = 1u << 1, kSquare = 1u << 2, kTriangle = 1u << 3,
	kDpadLeft = 1u << 4, kDpadRight = 1u << 5, kDpadUp = 1u << 6, kDpadDown = 1u << 7,
	kL1 = 1u << 8, kR1 = 1u << 9, kL3 = 1u << 10, kR3 = 1u << 11,
	kOptions = 1u << 12, kCreate = 1u << 13, kTouchpad = 1u << 14, kPs = 1u << 15
};

/** One thing that can be pressed. */
struct Input
{
	/** A key's code, or kMouseButton + n, kWheelUp, kWheelDown. 0: nothing. */
	uint32_t code = 0;
	/**
	 * For a key, where it sits on the keyboard as the system numbers it, or 0 if not known.
	 * A key is found by this first: holding Shift or Alt changes what a key is called (1
	 * becomes !, W becomes something else on a Mac), never where it sits.
	 */
	uint32_t native = 0;

	bool operator==(const Input &o) const { return code == o.code; }
};

using Bindings = std::array<std::vector<Input>, kFunctions>;

/** How the mouse aims, and the rest that can be set. */
struct Options
{
	/** The mouse aims: its movement moves a stick, and its buttons and wheel count. */
	bool aim = false;
	/** The stick the mouse moves: the right one, or the left one. */
	bool right_stick = true;
	/** 1 is as it comes; across, and up and down. */
	double sens_x = 1.0;
	double sens_y = 1.0;
	/**
	 * Acceleration: how far the stick goes at 20, 40, 60 and 80 % of the speed that pushes it
	 * all the way (0 at rest and 1 at full speed are fixed). Each between 0 and 1, none below
	 * the one before it. { 0.2, 0.4, 0.6, 0.8 } is even: twice as fast, twice as far.
	 */
	std::array<double, 4> curve = { 0.2, 0.4, 0.6, 0.8 };
	/**
	 * Games ignore a stick that is only a little off its middle. The smallest movement of the
	 * mouse already pushes the stick this far, so that it is past that; 0 to 0.5.
	 */
	double dead_zone = 0.12;
	/** The mouse's speed is taken over this long: longer is calmer and later. 8 to 80. */
	int smoothing_ms = 24;
	/** While the aim button is held, the mouse counts this much; 1 is no difference. */
	double aim_factor = 0.6;
	/** The button that means aiming: L2 or L1, or Function::Count for none. */
	Function aim_button = Function::L2;
	bool invert_y = false;
	/** How far the left stick goes while Walk is held; 0.2 to 0.9. */
	double walk = 0.5;
};

/** The controller: buttons, triggers (0 to 255) and sticks (-32767 to 32767, up and left negative). */
struct Pad
{
	uint32_t buttons = 0;
	uint8_t l2 = 0;
	uint8_t r2 = 0;
	int16_t left_x = 0;
	int16_t left_y = 0;
	int16_t right_x = 0;
	int16_t right_y = 0;

	bool operator==(const Pad &o) const
	{
		return buttons == o.buttons && l2 == o.l2 && r2 == o.r2 && left_x == o.left_x && left_y == o.left_y
			&& right_x == o.right_x && right_y == o.right_y;
	}
	bool operator!=(const Pad &o) const { return !(*this == o); }
};

/** A mouse speed, in the mouse's own steps a millisecond, that pushes the stick all the way at sensitivity 1. */
constexpr double kFullSpeed = 3.0;
/** One notch of the wheel is a press this long, and the next one waits this long after it. */
constexpr uint64_t kWheelPressMs = 50;
constexpr uint64_t kWheelGapMs = 40;
/** No more notches than this wait to be pressed: a spin of the wheel is not a minute of presses. */
constexpr size_t kWheelWaiting = 6;

/** Where the curve puts a speed between 0 and 1. */
double CurveAt(const std::array<double, 4> &curve, double speed);
/** The curve as it may be: every point between 0 and 1 and none below the one before it. */
std::array<double, 4> CurveMadeValid(std::array<double, 4> curve);

class Converter
{
	public:
		void SetOptions(const Options &options);
		const Options &GetOptions() const { return options; }
		/** New bindings: everything held is let go, since what it was held for may be gone. */
		void SetBindings(const Bindings &bindings);
		const Bindings &GetBindings() const { return bindings; }

		/**
		 * A key or a mouse button went down or came up. Says what it is bound to
		 * (Function::Count: nothing, or a mouse button while the mouse does not aim).
		 */
		Function Press(uint32_t code, uint32_t native, bool down);
		/** The wheel turned by this many notches, up counting positive. */
		void Wheel(int notches, uint64_t now_ms);
		/** The mouse moved by this much; does nothing while it does not aim. */
		void Move(double dx, double dy, uint64_t now_ms);
		/** Lets go of everything: the window lost the keyboard, a menu opened. */
		void ReleaseAll();

		/** The controller as it is now. */
		Pad State(uint64_t now_ms);
		/** Whether the controller changes without anything more being pressed: the mouse just moved, a notch of the wheel is still pressed. */
		bool Busy(uint64_t now_ms) const;

		/**
		 * Keys found by their name got to know where they sit: true once after that happened,
		 * for whoever keeps the bindings to keep them again.
		 */
		bool TakeLearned();

	private:
		struct Move_
		{
			uint64_t at;
			double dx;
			double dy;
		};
		struct Pulse
		{
			Function function;
			uint64_t from;
			uint64_t until;
		};

		bool Held(Function f, uint64_t now_ms) const;
		Function Find(uint32_t code, uint32_t native, bool learn);

		Options options;
		Bindings bindings;
		/** What is held down, by where it sits (or by its code where that is not known), and what for. */
		std::map<uint64_t, Function> down;
		std::array<int, kFunctions> held = {};
		std::deque<Move_> moves;
		std::deque<Pulse> pulses;
		bool learned = false;
};

} // namespace keymouse

#endif // CHIAKI_KEYMOUSEPAD_H
