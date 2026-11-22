// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Mouse and keyboard as a controller (gui/src/keymousepad.cpp), tried on its own: what is
// pressed and how the mouse moves goes in, the controller comes out.

#include "keymousepad.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace keymouse;

static int failures = 0;
static int checks = 0;

#define CHECK(what) \
	do { \
		checks++; \
		if(!(what)) \
		{ \
			failures++; \
			std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #what); \
		} \
	} while(0)

#define CHECK_NEAR(value, wanted, by) \
	do { \
		checks++; \
		if(std::abs(static_cast<double>(value) - static_cast<double>(wanted)) > (by)) \
		{ \
			failures++; \
			std::printf("FAILED %s:%d: %s is %g, wanted %g\n", __FILE__, __LINE__, #value, static_cast<double>(value), static_cast<double>(wanted)); \
		} \
	} while(0)

// Keys by any numbers: the converter does not care what they mean
enum : uint32_t { KeyW = 'W', KeyA = 'A', KeyS = 'S', KeyD = 'D', KeySpace = ' ', KeyShift = 0x1000020, KeyOne = '1', KeyBang = '!', KeyAlt = 0x1000023, KeyUp = 0x1000013 };
const uint32_t MouseLeft = kMouseButton + 1;
const uint32_t MouseRight = kMouseButton + 2;

static void Bind(Bindings &b, Function f, uint32_t code, uint32_t native = 0)
{
	b[static_cast<size_t>(f)].push_back({ code, native });
}

static Bindings Shooter()
{
	Bindings b;
	Bind(b, Function::LeftUp, KeyW);
	Bind(b, Function::LeftLeft, KeyA);
	Bind(b, Function::LeftDown, KeyS);
	Bind(b, Function::LeftRight, KeyD);
	Bind(b, Function::Cross, KeySpace);
	Bind(b, Function::L3, KeyShift);
	Bind(b, Function::Walk, KeyAlt);
	Bind(b, Function::R2, MouseLeft);
	Bind(b, Function::L2, MouseRight);
	Bind(b, Function::Triangle, KeyOne);
	Bind(b, Function::Triangle, kWheelUp);
	Bind(b, Function::DpadDown, kWheelDown);
	return b;
}

/** Aiming with nothing in the way of the numbers: no dead zone, an even curve, 20 ms. */
static Options Plain()
{
	Options o;
	o.aim = true;
	o.dead_zone = 0.0;
	o.smoothing_ms = 20;
	o.aim_factor = 0.5;
	return o;
}

static void Buttons()
{
	Converter c;
	c.SetBindings(Shooter());
	CHECK(c.State(0).buttons == 0);
	CHECK(c.Press(KeySpace, 0, true) == Function::Cross);
	CHECK(c.State(0).buttons == kCross);
	CHECK(c.Press(KeyShift, 0, true) == Function::L3);
	CHECK(c.State(0).buttons == (kCross | kL3));
	CHECK(c.Press(KeySpace, 0, false) == Function::Cross);
	CHECK(c.State(0).buttons == kL3);
	c.Press(KeyShift, 0, false);
	CHECK(c.State(0) == Pad());
	// A key nothing is bound to
	CHECK(c.Press('Z', 0, true) == Function::Count);
	CHECK(c.State(0) == Pad());
}

static void SeveralInputsForOne()
{
	Bindings b = Shooter();
	Bind(b, Function::Cross, 'J');
	Bind(b, Function::Cross, 'K');
	// A fourth is one too many
	Bind(b, Function::Cross, 'L');
	Converter c;
	c.SetBindings(b);
	CHECK(c.GetBindings()[static_cast<size_t>(Function::Cross)].size() == kSlots);
	CHECK(c.Press('L', 0, true) == Function::Count);
	c.Press(KeySpace, 0, true);
	c.Press('J', 0, true);
	c.Press(KeySpace, 0, false);
	// Still held by the other one
	CHECK(c.State(0).buttons == kCross);
	c.Press('J', 0, false);
	CHECK(c.State(0).buttons == 0);
	// A key that repeats while held counts once
	c.Press('K', 0, true);
	c.Press('K', 0, true);
	c.Press('K', 0, false);
	CHECK(c.State(0).buttons == 0);
}

static void SticksFromKeys()
{
	Converter c;
	c.SetBindings(Shooter());
	c.Press(KeyW, 0, true);
	CHECK(c.State(0).left_y == -32767);
	CHECK(c.State(0).left_x == 0);
	// With its neighbour: on the circle's edge, not in the square's corner
	c.Press(KeyD, 0, true);
	CHECK_NEAR(c.State(0).left_x, 23170, 1);
	CHECK_NEAR(c.State(0).left_y, -23170, 1);
	c.Press(KeyW, 0, false);
	CHECK(c.State(0).left_x == 32767);
	CHECK(c.State(0).left_y == 0);
	// Opposite keys cancel, and letting one go leaves the other
	c.Press(KeyA, 0, true);
	CHECK(c.State(0).left_x == 0);
	c.Press(KeyD, 0, false);
	CHECK(c.State(0).left_x == -32767);
	// Walking: part of the way
	c.Press(KeyAlt, 0, true);
	CHECK_NEAR(c.State(0).left_x, -16384, 1);
	Options o;
	o.walk = 0.25;
	c.SetOptions(o);
	CHECK_NEAR(c.State(0).left_x, -8192, 1);
	c.ReleaseAll();
	CHECK(c.State(0) == Pad());
}

static void MouseNeedsAim()
{
	Converter c;
	c.SetBindings(Shooter());
	// Not aiming: the mouse is not ours
	CHECK(c.Press(MouseLeft, 0, true) == Function::Count);
	c.Move(100, 0, 0);
	c.Wheel(1, 0);
	CHECK(c.State(1) == Pad());
	CHECK(!c.Busy(1));
	c.SetOptions(Plain());
	CHECK(c.Press(MouseLeft, 0, true) == Function::R2);
	CHECK(c.State(1).r2 == 255);
	// Aiming ends: the button will not be seen coming up, so it comes up now
	Options off = Plain();
	off.aim = false;
	c.SetOptions(off);
	CHECK(c.State(1).r2 == 0);
}

static void MouseSpeed()
{
	Converter c;
	c.SetBindings(Shooter());
	c.SetOptions(Plain());
	// 30 steps in 20 ms: 1.5 a millisecond, half of full speed
	c.Move(10, 0, 100);
	c.Move(10, 0, 105);
	c.Move(10, 0, 110);
	Pad pad = c.State(110);
	CHECK_NEAR(pad.right_x, 16384, 1);
	CHECK(pad.right_y == 0);
	CHECK(pad.left_x == 0);
	CHECK(c.Busy(110));
	// What is older than 20 ms no longer counts
	CHECK_NEAR(c.State(121).right_x, 10922, 1);
	CHECK(c.State(130).right_x == 0);
	CHECK(!c.Busy(130));

	// Faster than full speed is full; the direction stays
	c.Move(300, -300, 200);
	pad = c.State(200);
	CHECK_NEAR(pad.right_x, 23170, 1);
	CHECK_NEAR(pad.right_y, -23170, 1);

	// Each direction with its own sensitivity, and up and down the other way round
	Options o = Plain();
	o.sens_x = 2.0;
	o.sens_y = 0.5;
	o.invert_y = true;
	c.SetOptions(o);
	c.ReleaseAll();
	c.Move(15, 30, 300); // 0.75 and 1.5 a millisecond: 0.5 and -0.25 of full
	pad = c.State(300);
	const double length = std::hypot(0.5, 0.25);
	CHECK_NEAR(pad.right_x, 0.5 / length * length * 32767, 2);
	CHECK_NEAR(pad.right_y, -0.25 / length * length * 32767, 2);

	// On the left stick instead
	o = Plain();
	o.right_stick = false;
	c.SetOptions(o);
	c.ReleaseAll();
	c.Move(30, 0, 400);
	pad = c.State(400);
	CHECK_NEAR(pad.left_x, 16384, 1);
	CHECK(pad.right_x == 0);
}

static void DeadZoneAndCurve()
{
	Converter c;
	c.SetBindings(Shooter());
	Options o = Plain();
	o.dead_zone = 0.2;
	c.SetOptions(o);
	c.Move(30, 0, 0); // half of full speed
	CHECK_NEAR(c.State(0).right_x, (0.2 + 0.8 * 0.5) * 32767, 1);
	c.ReleaseAll();
	// The smallest movement is past the dead zone at once
	c.Move(1, 0, 50);
	CHECK(c.State(50).right_x > 0.2 * 32767);
	CHECK(c.State(50).right_x < 0.22 * 32767);

	// A curve that starts slowly
	o = Plain();
	o.curve = { 0.05, 0.15, 0.35, 0.65 };
	c.SetOptions(o);
	c.ReleaseAll();
	c.Move(30, 0, 100); // half of full speed: between 0.15 (at 0.4) and 0.35 (at 0.6)
	CHECK_NEAR(c.State(100).right_x, 0.25 * 32767, 1);
	CHECK_NEAR(CurveAt(o.curve, 0.1), 0.025, 1e-9);
	CHECK_NEAR(CurveAt(o.curve, 0.2), 0.05, 1e-9);
	CHECK_NEAR(CurveAt(o.curve, 0.9), 0.825, 1e-9);
	CHECK(CurveAt(o.curve, 0.0) == 0.0);
	CHECK(CurveAt(o.curve, 1.0) == 1.0);
	CHECK(CurveAt(o.curve, 7.0) == 1.0);

	// A curve never goes down, and stays between 0 and 1
	const std::array<double, 4> valid = CurveMadeValid({ 0.5, 0.3, 1.4, -1.0 });
	CHECK(valid[0] == 0.5 && valid[1] == 0.5 && valid[2] == 1.0 && valid[3] == 1.0);
}

static void SlowerWhileAiming()
{
	Converter c;
	c.SetBindings(Shooter());
	c.SetOptions(Plain());
	c.Press(MouseRight, 0, true); // L2
	c.Move(30, 0, 0);
	Pad pad = c.State(0);
	CHECK(pad.l2 == 255);
	CHECK_NEAR(pad.right_x, 0.25 * 32767, 1);
	c.Press(MouseRight, 0, false);
	CHECK_NEAR(c.State(0).right_x, 0.5 * 32767, 1);
	// Another button means aiming, or none
	Options o = Plain();
	o.aim_button = Function::Count;
	c.SetOptions(o);
	c.Press(MouseRight, 0, true);
	CHECK_NEAR(c.State(0).right_x, 0.5 * 32767, 1);
}

static void WheelPresses()
{
	Converter c;
	c.SetBindings(Shooter());
	c.SetOptions(Plain());
	c.Wheel(2, 1000);
	CHECK(c.State(1000).buttons == kTriangle);
	CHECK(c.State(1000 + kWheelPressMs - 1).buttons == kTriangle);
	// Let go between two notches, so that the game counts two
	CHECK(c.State(1000 + kWheelPressMs).buttons == 0);
	CHECK(c.Busy(1000 + kWheelPressMs));
	const uint64_t second = 1000 + kWheelPressMs + kWheelGapMs;
	CHECK(c.State(second).buttons == kTriangle);
	CHECK(c.State(second + kWheelPressMs).buttons == 0);
	CHECK(!c.Busy(second + kWheelPressMs));
	// The other way is another button
	c.Wheel(-1, 2000);
	CHECK(c.State(2000).buttons == kDpadDown);
	// A spin of the wheel is not presses without end
	c.State(3000);
	c.Wheel(50, 3000);
	int presses = 0;
	bool was = false;
	for(uint64_t t = 3000; t < 6000; t++)
	{
		const bool is = c.State(t).buttons == kTriangle;
		if(is && !was)
			presses++;
		was = is;
	}
	CHECK(presses == static_cast<int>(kWheelWaiting));
}

static void KeysByTheirPlace()
{
	Converter c;
	c.SetBindings(Shooter());
	CHECK(!c.TakeLearned());
	// Pressed by its name, with where it sits: that is remembered
	CHECK(c.Press(KeyOne, 18, true) == Function::Triangle);
	c.Press(KeyOne, 18, false);
	CHECK(c.TakeLearned());
	CHECK(!c.TakeLearned());
	CHECK(c.GetBindings()[static_cast<size_t>(Function::Triangle)][0].native == 18);
	// With Shift held the same key is called "!": found by its place all the same
	c.Press(KeyShift, 56, true);
	CHECK(c.Press(KeyBang, 18, true) == Function::Triangle);
	CHECK(c.State(0).buttons == (kTriangle | kL3));
	// Shift comes up first, and the key comes up under its own name again: not stuck
	c.Press(KeyShift, 56, false);
	CHECK(c.Press(KeyOne, 18, false) == Function::Triangle);
	CHECK(c.State(0).buttons == 0);
	// The other Shift key has the same name and another place
	CHECK(c.Press(KeyShift, 60, true) == Function::L3);
	c.Press(KeyShift, 60, false);
	CHECK(c.State(0).buttons == 0);
	CHECK(c.GetBindings()[static_cast<size_t>(Function::L3)][0].native == 56);
	// Bindings that come with places are found by them at once
	Bindings b;
	Bind(b, Function::Cross, KeySpace, 49);
	c.SetBindings(b);
	CHECK(c.Press(0x2211, 49, true) == Function::Cross);
	CHECK(c.Press(KeySpace, 49, false) == Function::Cross);
	CHECK(c.State(0) == Pad());
}

static void Bounds()
{
	Converter c;
	Options o;
	o.sens_x = -3;
	o.sens_y = 1000;
	o.dead_zone = 4;
	o.smoothing_ms = 0;
	o.aim_factor = -1;
	o.walk = 9;
	o.aim_button = Function::Cross;
	o.curve = { 2, 2, 2, 2 };
	c.SetOptions(o);
	const Options &kept = c.GetOptions();
	CHECK(kept.sens_x == 0.05);
	CHECK(kept.sens_y == 10.0);
	CHECK(kept.dead_zone == 0.5);
	CHECK(kept.smoothing_ms == 1);
	CHECK(kept.aim_factor == 0.05);
	CHECK(kept.walk == 1.0);
	CHECK(kept.aim_button == Function::Count);
	CHECK(kept.curve[0] == 1.0 && kept.curve[3] == 1.0);
}

int main()
{
	Buttons();
	SeveralInputsForOne();
	SticksFromKeys();
	MouseNeedsAim();
	MouseSpeed();
	DeadZoneAndCurve();
	SlowerWhileAiming();
	WheelPresses();
	KeysByTheirPlace();
	Bounds();
	std::printf("%d checks, %d failed\n", checks, failures);
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
