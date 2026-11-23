// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Works out with the app's own code (gui/src/keymousepad.cpp) what the controller looks like
// at a number of scripted moments, and writes script and results as JSON. The browser page
// has the same conversion in JavaScript (web/js/keypad.js); test/web/keypad.test.mjs plays
// the script on it and expects the same numbers. Made again after a change to either side:
//
//   c++ -std=c++17 -Igui/include test/web/keypad-vectors.cpp gui/src/keymousepad.cpp -o /tmp/keypad-vectors
//   /tmp/keypad-vectors > test/web/keypad-vectors.json
//
// Not part of the build: the JSON it writes is in the repository.

#include "keymousepad.h"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace keymouse;

namespace
{

/** The functions as the page names them, in the order of keymouse::Function. */
const char *const kIds[] = {
	"cross", "circle", "square", "triangle", "dpad_up", "dpad_down", "dpad_left", "dpad_right",
	"l1", "r1", "l2", "r2", "l3", "r3", "options", "create", "touchpad", "ps",
	"left_up", "left_down", "left_left", "left_right", "right_up", "right_down", "right_left", "right_right", "walk", "mouse",
};
const size_t kIdCount = sizeof(kIds) / sizeof(kIds[0]);

using Layout = std::map<std::string, std::vector<std::string>>;

/** An input as the page writes it ("k:KeyW", "m:1", "w:up"), as a code for the app's converter. */
uint32_t CodeOf(const std::string &input)
{
	static std::map<std::string, uint32_t> keys;
	if(input == "w:up")
		return kWheelUp;
	if(input == "w:down")
		return kWheelDown;
	if(input.rfind("m:", 0) == 0)
		return kMouseButton + static_cast<uint32_t>(input[2] - '0');
	auto it = keys.find(input);
	if(it == keys.end())
		it = keys.emplace(input, 0x1000u + static_cast<uint32_t>(keys.size())).first;
	return it->second;
}

struct PageOptions
{
	Options o;
};

std::string Number(double v)
{
	char text[64];
	std::snprintf(text, sizeof(text), "%.17g", v);
	return text;
}

std::string OptionsJson(const Options &o)
{
	std::string s = "{";
	s += "\"aim\":" + std::string(o.aim ? "true" : "false");
	s += ",\"rightStick\":" + std::string(o.right_stick ? "true" : "false");
	s += ",\"sensX\":" + Number(o.sens_x) + ",\"sensY\":" + Number(o.sens_y);
	s += ",\"curve\":[" + Number(o.curve[0]) + "," + Number(o.curve[1]) + "," + Number(o.curve[2]) + "," + Number(o.curve[3]) + "]";
	s += ",\"deadZone\":" + Number(o.dead_zone) + ",\"smoothingMs\":" + std::to_string(o.smoothing_ms);
	s += ",\"aimFactor\":" + Number(o.aim_factor);
	s += std::string(",\"aimButton\":\"") + (o.aim_button == Function::L2 ? "l2" : o.aim_button == Function::L1 ? "l1" : "") + "\"";
	s += ",\"invertY\":" + std::string(o.invert_y ? "true" : "false") + ",\"walk\":" + Number(o.walk) + "}";
	return s;
}

class Run
{
	public:
		explicit Run(const std::string &name) { text = "{\"name\":\"" + name + "\",\"ops\":["; }

		void SetOptions(const Options &o)
		{
			converter.SetOptions(o);
			Op("[\"o\"," + OptionsJson(o) + "]");
		}

		void SetBindings(const Layout &layout)
		{
			Bindings b;
			std::string json = "{";
			bool first = true;
			for(size_t f = 0; f < kIdCount; f++)
			{
				auto it = layout.find(kIds[f]);
				if(it == layout.end())
					continue;
				json += std::string(first ? "" : ",") + "\"" + kIds[f] + "\":[";
				first = false;
				for(size_t i = 0; i < it->second.size(); i++)
				{
					b[f].push_back({ CodeOf(it->second[i]), 0 });
					json += std::string(i ? "," : "") + "\"" + it->second[i] + "\"";
				}
				json += "]";
			}
			converter.SetBindings(b);
			Op("[\"b\"," + json + "}]");
		}

		void Press(const std::string &input, bool down)
		{
			converter.Press(CodeOf(input), 0, down);
			Op("[\"p\",\"" + input + "\"," + (down ? "1" : "0") + "]");
		}

		void Move(double dx, double dy, uint64_t t)
		{
			converter.Move(dx, dy, t);
			Op("[\"m\"," + Number(dx) + "," + Number(dy) + "," + std::to_string(t) + "]");
		}

		void Wheel(int notches, uint64_t t)
		{
			converter.Wheel(notches, t);
			Op("[\"w\"," + std::to_string(notches) + "," + std::to_string(t) + "]");
		}

		void ReleaseAll()
		{
			converter.ReleaseAll();
			Op("[\"r\"]");
		}

		/** What the controller looks like at `t`: written down as what the page has to say too. */
		void State(uint64_t t)
		{
			const Pad p = converter.State(t);
			Op("[\"s\"," + std::to_string(t) + ",[" + std::to_string(p.buttons) + "," + std::to_string(p.l2) + "," + std::to_string(p.r2) + ","
				+ std::to_string(p.left_x) + "," + std::to_string(p.left_y) + "," + std::to_string(p.right_x) + "," + std::to_string(p.right_y) + "],"
				+ (converter.Busy(t) ? "1" : "0") + "]");
			states++;
		}

		std::string Done() const { return text + "]}"; }
		int states = 0;

	private:
		void Op(const std::string &op)
		{
			text += (ops++ ? "," : "") + op;
		}

		Converter converter;
		std::string text;
		int ops = 0;
};

const Layout kClassic = {
	{ "left_up", { "k:KeyW" } }, { "left_left", { "k:KeyA" } }, { "left_down", { "k:KeyS" } }, { "left_right", { "k:KeyD" } },
	{ "dpad_up", { "k:ArrowUp" } }, { "dpad_left", { "k:ArrowLeft" } }, { "dpad_down", { "k:ArrowDown" } }, { "dpad_right", { "k:ArrowRight" } },
	{ "cross", { "k:KeyK" } }, { "circle", { "k:KeyL" } }, { "square", { "k:KeyJ" } }, { "triangle", { "k:KeyI" } },
	{ "l1", { "k:KeyQ" } }, { "r1", { "k:KeyE" } }, { "l2", { "k:KeyZ" } }, { "r2", { "k:KeyC" } },
	{ "l3", { "k:KeyF" } }, { "r3", { "k:KeyG" } },
	{ "right_up", { "k:KeyT" } }, { "right_down", { "k:KeyV" } }, { "right_left", { "k:KeyR" } }, { "right_right", { "k:KeyY" } },
	{ "options", { "k:Enter" } }, { "create", { "k:Backspace" } }, { "ps", { "k:KeyP" } }, { "touchpad", { "k:KeyM" } },
	{ "mouse", { "k:F8" } },
};

const Layout kShooter = {
	{ "left_up", { "k:KeyW" } }, { "left_left", { "k:KeyA" } }, { "left_down", { "k:KeyS" } }, { "left_right", { "k:KeyD" } },
	{ "r2", { "m:1" } }, { "l2", { "m:2" } },
	{ "cross", { "k:Space" } }, { "circle", { "k:KeyC" } }, { "square", { "k:KeyR", "k:KeyE" } }, { "triangle", { "k:Digit1", "w:up", "w:down" } },
	{ "l1", { "k:KeyQ", "m:4" } }, { "r1", { "k:KeyG", "m:5" } },
	{ "l3", { "k:ShiftLeft" } }, { "r3", { "k:KeyF", "m:3" } },
	{ "dpad_up", { "k:ArrowUp" } }, { "dpad_left", { "k:ArrowLeft" } }, { "dpad_down", { "k:ArrowDown" } }, { "dpad_right", { "k:ArrowRight" } },
	{ "options", { "k:Enter" } }, { "touchpad", { "k:Tab", "k:KeyM" } }, { "ps", { "k:F1" } }, { "create", { "k:F2" } },
	{ "walk", { "k:AltLeft" } },
	{ "mouse", { "k:F8" } },
};

/** The mouse moving steadily for a while, then resting: the stick goes out and comes back. */
void Sweep(Run &run, uint64_t &t, double dx, double dy, int steps)
{
	for(int i = 0; i < steps; i++)
	{
		t += 4;
		run.Move(dx, dy, t);
		run.State(t);
	}
	for(int i = 0; i < 12; i++)
	{
		t += 5;
		run.State(t);
	}
}

/** The same numbers every time. */
struct Dice
{
	uint32_t s;
	uint32_t Next()
	{
		s = s * 1664525u + 1013904223u;
		return s >> 8;
	}
	int Upto(int n) { return static_cast<int>(Next() % static_cast<uint32_t>(n)); }
};

} // namespace

int main()
{
	std::vector<std::string> runs;
	int states = 0;
	auto done = [&](const Run &run) {
		runs.push_back(run.Done());
		states += run.states;
	};

	{
		// Keys alone: sticks, buttons, triggers, two opposite keys, two neighbours, walking
		Run run("the keys as before");
		run.SetBindings(kClassic);
		uint64_t t = 0;
		const char *const order[] = { "k:KeyW", "k:KeyD", "k:KeyS", "k:KeyA", "k:KeyK", "k:KeyZ", "k:KeyC", "k:KeyT", "k:KeyY",
			"k:ArrowUp", "k:ArrowLeft", "k:Enter", "k:KeyP", "k:KeyM", "k:KeyF", "k:KeyG", "k:KeyQ", "k:KeyE", "k:KeyJ", "k:KeyI", "k:KeyL", "k:Backspace" };
		for(const char *input : order)
		{
			run.Press(input, true);
			run.State(t += 3);
		}
		run.Press("k:KeyW", true); // a key that repeats
		run.State(t += 3);
		for(const char *input : order)
		{
			run.Press(input, false);
			run.State(t += 3);
		}
		run.Press("k:KeyX", true); // bound to nothing
		run.Press("m:1", true);    // the mouse does not aim: not a button
		run.State(t += 3);
		run.Press("k:KeyW", true);
		run.Press("k:KeyA", true);
		run.ReleaseAll();
		run.State(t += 3);
		done(run);
	}

	{
		// Mouse and WASD: the stick follows the mouse's speed, its buttons and the wheel count
		Run run("mouse and WASD");
		run.SetBindings(kShooter);
		Options o;
		o.aim = true;
		run.SetOptions(o);
		uint64_t t = 100;
		const double speeds[][2] = { { 0.25, 0 }, { 1, 0 }, { 3, 0 }, { 8, 0 }, { 20, 0 }, { 60, 0 }, { 0, 2.5 }, { 0, -7 }, { 4, 4 }, { -6, 2 }, { -1.5, -9 }, { 0.5, -0.25 } };
		for(const auto &speed : speeds)
			Sweep(run, t, speed[0], speed[1], 10);
		// Walking while aiming, firing, and a slower mouse while the aim button is held
		run.Press("k:KeyW", true);
		run.Press("k:KeyD", true);
		run.Press("m:1", true);
		Sweep(run, t, 5, -3, 8);
		run.Press("k:AltLeft", true);
		Sweep(run, t, 5, -3, 8);
		run.Press("m:2", true);
		Sweep(run, t, 5, -3, 8);
		run.Press("m:2", false);
		run.Press("m:1", false);
		run.Press("k:AltLeft", false);
		run.Press("k:KeyW", false);
		run.Press("k:KeyD", false);
		run.State(t += 5);
		// The wheel: a press for each notch, one after the other, and no more than a few waiting
		for(int notches : { 1, -1, 3, -2, 12 })
		{
			run.Wheel(notches, t);
			for(int i = 0; i < 80; i++)
				run.State(t += 9);
		}
		// The mouse stops aiming while a button is held: it is let go
		run.Press("m:1", true);
		run.Press("k:KeyQ", true);
		run.Move(9, 9, t);
		run.State(t += 2);
		o.aim = false;
		run.SetOptions(o);
		run.State(t += 2);
		run.Move(9, 9, t);
		run.Wheel(2, t);
		run.Press("m:1", false);
		run.State(t += 2);
		done(run);
	}

	{
		// Everything that can be set, each with the same movements
		struct Setting
		{
			const char *name;
			void (*set)(Options &);
		};
		const Setting settings[] = {
			{ "fine at slow speed", [](Options &o) { o.curve = { 0.08, 0.23, 0.44, 0.70 }; } },
			{ "quick at slow speed", [](Options &o) { o.curve = { 0.38, 0.58, 0.74, 0.87 }; } },
			{ "a curve out of order", [](Options &o) { o.curve = { 0.5, 0.2, 1.4, -1 }; } },
			{ "no dead zone", [](Options &o) { o.dead_zone = 0; } },
			{ "a wide dead zone", [](Options &o) { o.dead_zone = 0.3; } },
			{ "a dead zone too wide", [](Options &o) { o.dead_zone = 0.9; } },
			{ "hardly smoothed", [](Options &o) { o.smoothing_ms = 8; } },
			{ "much smoothed", [](Options &o) { o.smoothing_ms = 60; } },
			{ "slow across, fast up and down", [](Options &o) { o.sens_x = 0.5; o.sens_y = 2; } },
			{ "very fast", [](Options &o) { o.sens_x = 10; o.sens_y = 25; } },
			{ "up and down the other way round", [](Options &o) { o.invert_y = true; } },
			{ "the mouse moves the left stick", [](Options &o) { o.right_stick = false; } },
			{ "much slower while aiming", [](Options &o) { o.aim_factor = 0.25; } },
			{ "aiming is L1", [](Options &o) { o.aim_button = Function::L1; } },
			{ "no aim button", [](Options &o) { o.aim_button = Function::Count; } },
			{ "walking nearly at full speed", [](Options &o) { o.walk = 0.9; } },
		};
		for(const Setting &setting : settings)
		{
			Run run(std::string("set: ") + setting.name);
			run.SetBindings(kShooter);
			Options o;
			o.aim = true;
			setting.set(o);
			run.SetOptions(o);
			uint64_t t = 50;
			Sweep(run, t, 0.5, 0.25, 8);
			Sweep(run, t, 3, -2, 8);
			Sweep(run, t, -11, 6, 8);
			Sweep(run, t, 40, 40, 8);
			run.Press("k:KeyW", true);
			run.Press("k:KeyA", true);
			run.Press("k:AltLeft", true);
			run.Press("m:2", true);
			Sweep(run, t, 3, -2, 8);
			run.Press("m:2", false);
			run.Press("k:KeyQ", true);
			Sweep(run, t, 3, -2, 8);
			done(run);
		}
	}

	{
		// Whatever comes: keys, buttons, the mouse and the wheel in no order
		const char *const inputs[] = { "k:KeyW", "k:KeyA", "k:KeyS", "k:KeyD", "k:Space", "k:KeyC", "k:KeyR", "k:KeyE", "k:Digit1", "k:KeyQ",
			"k:KeyG", "k:ShiftLeft", "k:KeyF", "k:ArrowUp", "k:ArrowLeft", "k:ArrowDown", "k:ArrowRight", "k:Enter", "k:Tab", "k:AltLeft",
			"m:1", "m:2", "m:3", "m:4", "m:5", "k:KeyX", "k:F1", "k:F2", "k:F8" };
		const int count = static_cast<int>(sizeof(inputs) / sizeof(inputs[0]));
		for(uint32_t seed : { 7u, 99u, 2026u })
		{
			Run run("whatever comes, " + std::to_string(seed));
			run.SetBindings(kShooter);
			Options o;
			o.aim = true;
			run.SetOptions(o);
			Dice dice{ seed };
			uint64_t t = 1000;
			std::map<std::string, bool> held;
			for(int step = 0; step < 700; step++)
			{
				t += static_cast<uint64_t>(1 + dice.Upto(9));
				const int what = dice.Upto(10);
				if(what < 4)
				{
					const std::string input = inputs[dice.Upto(count)];
					held[input] = !held[input];
					run.Press(input, held[input]);
				}
				else if(what < 8)
					run.Move((dice.Upto(321) - 160) * 0.25, (dice.Upto(321) - 160) * 0.25, t);
				else if(what == 8)
					run.Wheel(dice.Upto(7) - 3, t);
				run.State(t);
			}
			done(run);
		}
	}

	std::printf("{\"made_by\":\"test/web/keypad-vectors.cpp\",\"states\":%d,\"runs\":[\n", states);
	for(size_t i = 0; i < runs.size(); i++)
		std::printf("%s%s\n", runs[i].c_str(), i + 1 < runs.size() ? "," : "");
	std::printf("]}\n");
	return 0;
}
