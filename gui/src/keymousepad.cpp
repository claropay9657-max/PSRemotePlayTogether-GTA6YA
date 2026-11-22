// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "keymousepad.h"

#include <algorithm>
#include <cmath>

namespace keymouse
{

namespace
{

const double kHalfDiagonal = 0.70710678118654752440; // 1 / sqrt(2)

double Clamped(double v, double low, double high)
{
	// Not a number counts as the lowest
	if(!(v >= low))
		return low;
	return v > high ? high : v;
}

int16_t StickValue(double v)
{
	return static_cast<int16_t>(std::lround(Clamped(v, -1.0, 1.0) * 32767.0));
}

/** A stick may be anywhere inside its circle, not in the corners of the square around it. */
void IntoCircle(double &x, double &y)
{
	const double length = std::hypot(x, y);
	if(length > 1.0)
	{
		x /= length;
		y /= length;
	}
}

struct ButtonBit
{
	Function function;
	uint32_t bit;
};

const ButtonBit kButtons[] = {
	{ Function::Cross, kCross }, { Function::Circle, kCircle }, { Function::Square, kSquare }, { Function::Triangle, kTriangle },
	{ Function::DpadUp, kDpadUp }, { Function::DpadDown, kDpadDown }, { Function::DpadLeft, kDpadLeft }, { Function::DpadRight, kDpadRight },
	{ Function::L1, kL1 }, { Function::R1, kR1 }, { Function::L3, kL3 }, { Function::R3, kR3 },
	{ Function::Options, kOptions }, { Function::Create, kCreate }, { Function::Touchpad, kTouchpad }, { Function::Ps, kPs },
};

/** What is held down is kept by where the key sits; a key whose place is not known, and the mouse, by the code. */
uint64_t DownId(uint32_t code, uint32_t native)
{
	if(!IsMouse(code) && native)
		return (static_cast<uint64_t>(1) << 32) | native;
	return code;
}

} // namespace

std::array<double, 4> CurveMadeValid(std::array<double, 4> curve)
{
	double floor = 0.0;
	for(double &point : curve)
	{
		point = Clamped(point, floor, 1.0);
		floor = point;
	}
	return curve;
}

double CurveAt(const std::array<double, 4> &curve, double speed)
{
	if(!(speed > 0.0))
		return 0.0;
	if(speed >= 1.0)
		return 1.0;
	// Six points a fifth apart, straight lines between them
	const double points[6] = { 0.0, curve[0], curve[1], curve[2], curve[3], 1.0 };
	const double at = speed * 5.0;
	const int whole = static_cast<int>(at);
	const int part = whole < 4 ? whole : 4;
	return points[part] + (points[part + 1] - points[part]) * (at - part);
}

void Converter::SetOptions(const Options &o)
{
	const bool was_aiming = options.aim;
	options = o;
	options.sens_x = Clamped(o.sens_x, 0.05, 10.0);
	options.sens_y = Clamped(o.sens_y, 0.05, 10.0);
	options.curve = CurveMadeValid(o.curve);
	options.dead_zone = Clamped(o.dead_zone, 0.0, 0.5);
	options.smoothing_ms = static_cast<int>(Clamped(o.smoothing_ms, 1, 500));
	options.aim_factor = Clamped(o.aim_factor, 0.05, 2.0);
	options.walk = Clamped(o.walk, 0.1, 1.0);
	if(options.aim_button != Function::L2 && options.aim_button != Function::L1)
		options.aim_button = Function::Count;
	if(was_aiming && !options.aim)
	{
		// The mouse is somebody else's again: its buttons will not be seen coming up
		for(auto it = down.begin(); it != down.end();)
		{
			if(it->first <= 0xffffffffu && IsMouse(static_cast<uint32_t>(it->first)))
			{
				int &count = held[static_cast<size_t>(it->second)];
				if(count > 0)
					count--;
				it = down.erase(it);
			}
			else
				++it;
		}
		moves.clear();
		pulses.clear();
	}
}

void Converter::SetBindings(const Bindings &b)
{
	ReleaseAll();
	for(size_t f = 0; f < kFunctions; f++)
	{
		bindings[f].clear();
		for(const Input &input : b[f])
		{
			if(!input.code || bindings[f].size() >= kSlots)
				continue;
			Input kept = input;
			if(IsMouse(kept.code))
				kept.native = 0;
			bindings[f].push_back(kept);
		}
	}
	learned = false;
}

Function Converter::Find(uint32_t code, uint32_t native, bool learn)
{
	const bool mouse = IsMouse(code);
	if(!mouse && native)
	{
		for(size_t f = 0; f < kFunctions; f++)
		{
			for(const Input &input : bindings[f])
			{
				if(!IsMouse(input.code) && input.native == native)
					return static_cast<Function>(f);
			}
		}
	}
	for(size_t f = 0; f < kFunctions; f++)
	{
		for(Input &input : bindings[f])
		{
			if(input.code != code)
				continue;
			// Found by its name: from now on it is found by where it sits, whatever else is held
			if(!mouse && learn && native && !input.native)
			{
				input.native = native;
				learned = true;
			}
			return static_cast<Function>(f);
		}
	}
	return Function::Count;
}

Function Converter::Press(uint32_t code, uint32_t native, bool is_down)
{
	if(!code || code == kWheelUp || code == kWheelDown)
		return Function::Count;
	if(IsMouse(code) && !options.aim)
		return Function::Count;
	const uint64_t id = DownId(code, native);
	if(is_down)
	{
		auto it = down.find(id);
		if(it != down.end())
			return it->second; // held already: a key that repeats
		const Function f = Find(code, native, true);
		if(f == Function::Count)
			return f;
		down[id] = f;
		held[static_cast<size_t>(f)]++;
		return f;
	}
	auto it = down.find(id);
	if(it == down.end() && id != code)
		it = down.find(code); // went down before its place was known
	if(it == down.end())
		return Function::Count;
	const Function f = it->second;
	down.erase(it);
	int &count = held[static_cast<size_t>(f)];
	if(count > 0)
		count--;
	return f;
}

void Converter::Wheel(int notches, uint64_t now_ms)
{
	if(!options.aim || !notches)
		return;
	const Function f = Find(notches > 0 ? kWheelUp : kWheelDown, 0, false);
	if(f == Function::Count)
		return;
	for(int i = std::abs(notches); i > 0 && pulses.size() < kWheelWaiting; i--)
	{
		// One press for each notch, one after the other, so that the game counts them
		uint64_t from = now_ms;
		for(const Pulse &pulse : pulses)
		{
			if(pulse.function == f && pulse.until + kWheelGapMs > from)
				from = pulse.until + kWheelGapMs;
		}
		pulses.push_back({ f, from, from + kWheelPressMs });
	}
}

void Converter::Move(double dx, double dy, uint64_t now_ms)
{
	if(!options.aim || (dx == 0.0 && dy == 0.0))
		return;
	moves.push_back({ now_ms, dx, dy });
	// Whatever asks for the state drops what is old; this only keeps a mouse nobody asks about from piling up
	while(moves.size() > 4096)
		moves.pop_front();
}

void Converter::ReleaseAll()
{
	down.clear();
	held.fill(0);
	moves.clear();
	pulses.clear();
}

bool Converter::Held(Function f, uint64_t now_ms) const
{
	if(f == Function::Count)
		return false;
	if(held[static_cast<size_t>(f)] > 0)
		return true;
	for(const Pulse &pulse : pulses)
	{
		if(pulse.function == f && pulse.from <= now_ms && now_ms < pulse.until)
			return true;
	}
	return false;
}

Pad Converter::State(uint64_t now_ms)
{
	pulses.erase(std::remove_if(pulses.begin(), pulses.end(), [now_ms](const Pulse &p) { return p.until <= now_ms; }), pulses.end());
	const uint64_t window = static_cast<uint64_t>(options.smoothing_ms);
	while(!moves.empty() && moves.front().at + window <= now_ms)
		moves.pop_front();

	Pad pad;
	for(const ButtonBit &button : kButtons)
	{
		if(Held(button.function, now_ms))
			pad.buttons |= button.bit;
	}
	pad.l2 = Held(Function::L2, now_ms) ? 255 : 0;
	pad.r2 = Held(Function::R2, now_ms) ? 255 : 0;

	// The sticks from keys: two opposite ones cancel, two neighbours meet on the circle's edge
	auto from_keys = [this, now_ms](Function up, Function down_, Function left, Function right, double &x, double &y) {
		x = (Held(right, now_ms) ? 1.0 : 0.0) - (Held(left, now_ms) ? 1.0 : 0.0);
		y = (Held(down_, now_ms) ? 1.0 : 0.0) - (Held(up, now_ms) ? 1.0 : 0.0);
		if(x != 0.0 && y != 0.0)
		{
			x *= kHalfDiagonal;
			y *= kHalfDiagonal;
		}
	};
	double left_x, left_y, right_x, right_y;
	from_keys(Function::LeftUp, Function::LeftDown, Function::LeftLeft, Function::LeftRight, left_x, left_y);
	from_keys(Function::RightUp, Function::RightDown, Function::RightLeft, Function::RightRight, right_x, right_y);
	if(Held(Function::Walk, now_ms))
	{
		left_x *= options.walk;
		left_y *= options.walk;
	}

	// The mouse: how fast it is over the last moments says how far the stick goes
	if(options.aim && !moves.empty())
	{
		double moved_x = 0.0, moved_y = 0.0;
		for(const Move_ &move : moves)
		{
			moved_x += move.dx;
			moved_y += move.dy;
		}
		const double slower = Held(options.aim_button, now_ms) ? options.aim_factor : 1.0;
		const double x = moved_x / window * options.sens_x * slower / kFullSpeed;
		const double y = moved_y / window * options.sens_y * slower / kFullSpeed * (options.invert_y ? -1.0 : 1.0);
		const double speed = std::hypot(x, y);
		if(speed > 1e-9)
		{
			const double reach = options.dead_zone + (1.0 - options.dead_zone) * CurveAt(options.curve, speed < 1.0 ? speed : 1.0);
			double &stick_x = options.right_stick ? right_x : left_x;
			double &stick_y = options.right_stick ? right_y : left_y;
			stick_x += x / speed * reach;
			stick_y += y / speed * reach;
		}
	}

	IntoCircle(left_x, left_y);
	IntoCircle(right_x, right_y);
	pad.left_x = StickValue(left_x);
	pad.left_y = StickValue(left_y);
	pad.right_x = StickValue(right_x);
	pad.right_y = StickValue(right_y);
	return pad;
}

bool Converter::Busy(uint64_t now_ms) const
{
	for(const Pulse &pulse : pulses)
	{
		if(pulse.until > now_ms)
			return true;
	}
	return options.aim && !moves.empty() && moves.back().at + static_cast<uint64_t>(options.smoothing_ms) > now_ms;
}

bool Converter::TakeLearned()
{
	const bool was = learned;
	learned = false;
	return was;
}

} // namespace keymouse
