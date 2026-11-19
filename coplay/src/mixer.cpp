// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <coplay/mixer.h>

namespace coplay {

PadState ApplyMask(const PadState &state, const InputMask &mask)
{
	PadState out = state;
	out.buttons &= mask.buttons;
	if(!mask.left_stick)
		out.left_x = out.left_y = 0;
	if(!mask.right_stick)
		out.right_x = out.right_y = 0;
	if(!mask.l2)
		out.l2 = 0;
	if(!mask.r2)
		out.r2 = 0;
	if(!mask.touch)
	{
		out.touch_id_next = 0;
		for(PadTouch &t : out.touches)
			t = PadTouch();
		out.buttons &= ~static_cast<uint32_t>(kButtonTouchpad);
	}
	if(!mask.motion)
	{
		PadState idle;
		for(size_t i = 0; i < 3; i++)
		{
			out.gyro[i] = idle.gyro[i];
			out.accel[i] = idle.accel[i];
		}
		for(size_t i = 0; i < 4; i++)
			out.orient[i] = idle.orient[i];
	}
	return out;
}

static int64_t MagnitudeSquared(int16_t x, int16_t y)
{
	return static_cast<int64_t>(x) * x + static_cast<int64_t>(y) * y;
}

PadState MergePadStates(const PadState &a, const PadState &b)
{
	PadState out;
	out.buttons = a.buttons | b.buttons;
	out.l2 = a.l2 > b.l2 ? a.l2 : b.l2;
	out.r2 = a.r2 > b.r2 ? a.r2 : b.r2;

	// Whole stick from one player: mixing x from one and y from the other gives a
	// direction neither of them is pointing at
	if(MagnitudeSquared(b.left_x, b.left_y) > MagnitudeSquared(a.left_x, a.left_y))
	{
		out.left_x = b.left_x;
		out.left_y = b.left_y;
	}
	else
	{
		out.left_x = a.left_x;
		out.left_y = a.left_y;
	}
	if(MagnitudeSquared(b.right_x, b.right_y) > MagnitudeSquared(a.right_x, a.right_y))
	{
		out.right_x = b.right_x;
		out.right_y = b.right_y;
	}
	else
	{
		out.right_x = a.right_x;
		out.right_y = a.right_y;
	}

	out.touch_id_next = 0;
	for(size_t i = 0; i < 2; i++)
	{
		if(a.touches[i].id >= 0)
			out.touches[i] = a.touches[i];
		else if(b.touches[i].id >= 0)
			out.touches[i] = b.touches[i];
		else
			out.touches[i] = PadTouch();
	}

	// Never blend motion data of two controllers, orientation would be meaningless
	const PadState &motion = a.HasMotion() ? a : b;
	for(size_t i = 0; i < 3; i++)
	{
		out.gyro[i] = motion.gyro[i];
		out.accel[i] = motion.accel[i];
	}
	for(size_t i = 0; i < 4; i++)
		out.orient[i] = motion.orient[i];
	return out;
}

InputMixer::InputMixer() : InputMixer(Config()) {}

InputMixer::InputMixer(const Config &config) : config(config) {}

void InputMixer::SetMode(MixMode new_mode)
{
	if(mode == new_mode)
		return;
	mode = new_mode;
	has_holder = false;
}

void InputMixer::SetSource(uint32_t id, const PadState &state, uint64_t now_ms)
{
	Source &s = sources[id];
	s.state = state;
	s.updated_ms = now_ms;
	s.has_state = true;
}

void InputMixer::RemoveSource(uint32_t id)
{
	sources.erase(id);
	if(has_holder && holder == id)
		has_holder = false;
}

void InputMixer::SetMask(uint32_t id, const InputMask &mask)
{
	sources[id].mask = mask;
}

bool InputMixer::IsActive(const PadState &state) const
{
	if(state.buttons)
		return true;
	if(state.l2 > config.trigger_threshold || state.r2 > config.trigger_threshold)
		return true;
	int64_t dz = static_cast<int64_t>(config.stick_deadzone) * config.stick_deadzone;
	if(MagnitudeSquared(state.left_x, state.left_y) > dz || MagnitudeSquared(state.right_x, state.right_y) > dz)
		return true;
	for(const PadTouch &t : state.touches)
	{
		if(t.id >= 0)
			return true;
	}
	return false;
}

PadState InputMixer::Effective(uint32_t id, const Source &source, uint64_t now_ms) const
{
	if(!source.has_state)
		return PadState();
	// A guest whose packets stopped must not keep holding buttons. The host's own
	// controllers are local and report every change, so they never go stale.
	if(id != kHostSource && now_ms > source.updated_ms && now_ms - source.updated_ms > config.stale_ms)
		return PadState();
	return ApplyMask(source.state, source.mask);
}

PadState InputMixer::Raw(uint32_t id, uint64_t now_ms) const
{
	auto it = sources.find(id);
	if(it == sources.end() || !it->second.has_state)
		return PadState();
	const Source &source = it->second;
	if(id != kHostSource && now_ms > source.updated_ms && now_ms - source.updated_ms > config.stale_ms)
		return PadState();
	return source.state;
}

PadState InputMixer::Mix(uint64_t now_ms)
{
	switch(mode)
	{
		case MixMode::HostOnly: {
			auto it = sources.find(kHostSource);
			return it == sources.end() ? PadState() : Effective(kHostSource, it->second, now_ms);
		}
		case MixMode::Together: {
			PadState out;
			for(auto &entry : sources)
				out = MergePadStates(out, Effective(entry.first, entry.second, now_ms));
			return out;
		}
		case MixMode::LastActive: {
			bool holder_active = false;
			bool have_candidate = false;
			uint32_t candidate = 0;
			for(auto &entry : sources)
			{
				bool active = IsActive(Effective(entry.first, entry.second, now_ms));
				if(active)
					entry.second.last_active_ms = now_ms;
				if(has_holder && entry.first == holder)
					holder_active = active;
				else if(active && !have_candidate)
				{
					have_candidate = true;
					candidate = entry.first;
				}
			}
			if(has_holder && sources.find(holder) == sources.end())
				has_holder = false;
			if(!has_holder)
			{
				if(have_candidate)
				{
					has_holder = true;
					holder = candidate;
				}
			}
			else if(!holder_active && have_candidate
				&& now_ms - sources[holder].last_active_ms >= config.handoff_ms)
			{
				holder = candidate;
			}
			if(!has_holder)
				return PadState();
			return Effective(holder, sources[holder], now_ms);
		}
	}
	return PadState();
}

bool InputMixer::InControl(uint32_t id) const
{
	auto it = sources.find(id);
	if(it == sources.end())
		return false;
	switch(mode)
	{
		case MixMode::HostOnly:
			return id == kHostSource;
		case MixMode::Together:
			return true;
		case MixMode::LastActive:
			return has_holder && holder == id;
	}
	return false;
}

} // namespace coplay
