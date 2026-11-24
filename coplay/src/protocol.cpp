// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <algorithm>
#include <coplay/protocol.h>

#include <cmath>

namespace coplay {

static bool FloatEq(float a, float b)
{
	return a == b || (std::isnan(a) && std::isnan(b));
}

bool PadState::operator==(const PadState &o) const
{
	if(buttons != o.buttons || l2 != o.l2 || r2 != o.r2 || left_x != o.left_x || left_y != o.left_y
		|| right_x != o.right_x || right_y != o.right_y || touch_id_next != o.touch_id_next)
		return false;
	for(size_t i = 0; i < 2; i++)
	{
		if(!(touches[i] == o.touches[i]))
			return false;
	}
	for(size_t i = 0; i < 3; i++)
	{
		if(!FloatEq(gyro[i], o.gyro[i]) || !FloatEq(accel[i], o.accel[i]))
			return false;
	}
	for(size_t i = 0; i < 4; i++)
	{
		if(!FloatEq(orient[i], o.orient[i]))
			return false;
	}
	return true;
}

bool PadState::HasMotion() const
{
	const float eps = 0.0000001f;
	auto differs = [eps](float v, float def) { return v < def - eps || v > def + eps; };
	return differs(gyro[0], 0.0f) || differs(gyro[1], 0.0f) || differs(gyro[2], 0.0f)
		|| differs(accel[0], 0.0f) || differs(accel[1], 1.0f) || differs(accel[2], 0.0f);
}

enum PadStateWireFlags : uint8_t
{
	kPadHasMotion = 1 << 0
};

void WritePadState(ByteWriter &w, const PadState &s)
{
	// Motion data is only sent when it carries information: most controllers have none
	bool motion = s.HasMotion() || s.orient[0] != 0.0f || s.orient[1] != 0.0f || s.orient[2] != 0.0f || s.orient[3] != 1.0f;
	w.U8(motion ? kPadHasMotion : 0);
	w.U32(s.buttons);
	w.U8(s.l2);
	w.U8(s.r2);
	w.I16(s.left_x);
	w.I16(s.left_y);
	w.I16(s.right_x);
	w.I16(s.right_y);
	w.U8(s.touch_id_next);
	for(size_t i = 0; i < 2; i++)
	{
		w.U16(s.touches[i].x);
		w.U16(s.touches[i].y);
		w.I8(s.touches[i].id);
	}
	if(motion)
	{
		for(size_t i = 0; i < 3; i++)
			w.F32(s.gyro[i]);
		for(size_t i = 0; i < 3; i++)
			w.F32(s.accel[i]);
		for(size_t i = 0; i < 4; i++)
			w.F32(s.orient[i]);
	}
}

bool ReadPadState(ByteReader &r, PadState &s)
{
	s = PadState();
	uint8_t flags = r.U8();
	s.buttons = r.U32();
	s.l2 = r.U8();
	s.r2 = r.U8();
	s.left_x = r.I16();
	s.left_y = r.I16();
	s.right_x = r.I16();
	s.right_y = r.I16();
	s.touch_id_next = r.U8();
	for(size_t i = 0; i < 2; i++)
	{
		s.touches[i].x = r.U16();
		s.touches[i].y = r.U16();
		s.touches[i].id = r.I8();
	}
	if(flags & kPadHasMotion)
	{
		for(size_t i = 0; i < 3; i++)
			s.gyro[i] = r.F32();
		for(size_t i = 0; i < 3; i++)
			s.accel[i] = r.F32();
		for(size_t i = 0; i < 4; i++)
			s.orient[i] = r.F32();
		// A hostile or broken peer must not be able to inject NaN/Inf into the console's motion data
		for(float v : s.gyro)
			if(!std::isfinite(v))
				return false;
		for(float v : s.accel)
			if(!std::isfinite(v))
				return false;
		for(float v : s.orient)
			if(!std::isfinite(v))
				return false;
	}
	if(!r.ok())
		return false;
	for(size_t i = 0; i < 2; i++)
	{
		if(s.touches[i].id < -1)
			return false;
	}
	return true;
}

static void WriteStreamInfo(ByteWriter &w, const StreamInfo &s)
{
	w.U8(static_cast<uint8_t>(s.codec));
	w.U16(s.width);
	w.U16(s.height);
	w.U8(s.fps);
	w.U8(static_cast<uint8_t>((s.ps5 ? 1 : 0) | (s.dualsense ? 2 : 0) | ((static_cast<uint8_t>(s.source) & 3) << 2)
		| (s.console_asleep ? 0x10 : 0)));
}

static bool ReadStreamInfo(ByteReader &r, StreamInfo &s)
{
	uint8_t codec = r.U8();
	s.width = r.U16();
	s.height = r.U16();
	s.fps = r.U8();
	uint8_t flags = r.U8();
	s.ps5 = (flags & 1) != 0;
	s.dualsense = (flags & 2) != 0;
	s.console_asleep = (flags & 0x10) != 0;
	uint8_t source = (flags >> 2) & 3;
	if(!r.ok() || codec > static_cast<uint8_t>(VideoCodec::H265Hdr) || source > static_cast<uint8_t>(GameSource::XboxApp))
		return false;
	s.source = static_cast<GameSource>(source);
	s.codec = static_cast<VideoCodec>(codec);
	return true;
}

static void WriteAudioHeader(ByteWriter &w, const AudioHeader &a)
{
	w.U8(a.valid ? 1 : 0);
	w.U8(a.channels);
	w.U8(a.bits);
	w.U32(a.rate);
	w.U32(a.frame_size);
	w.U32(a.unknown);
}

static bool ReadAudioHeader(ByteReader &r, AudioHeader &a)
{
	a.valid = r.U8() != 0;
	a.channels = r.U8();
	a.bits = r.U8();
	a.rate = r.U32();
	a.frame_size = r.U32();
	a.unknown = r.U32();
	return r.ok();
}

static void WriteFeedback(ByteWriter &w, const Feedback &f)
{
	w.U16(f.rumble_seq);
	w.U8(f.rumble_left);
	w.U8(f.rumble_right);
	w.U16(f.trigger_seq);
	w.U8(f.trigger_type_left);
	w.U8(f.trigger_type_right);
	w.Bytes(f.trigger_left.data(), f.trigger_left.size());
	w.Bytes(f.trigger_right.data(), f.trigger_right.size());
	w.U16(f.led_seq);
	w.Bytes(f.led.data(), f.led.size());
	w.U16(f.player_index_seq);
	w.U8(f.player_index);
	w.U16(f.haptic_intensity_seq);
	w.U8(f.haptic_intensity);
	w.U16(f.trigger_intensity_seq);
	w.U8(f.trigger_intensity);
	w.U16(f.motion_reset_seq);
}

static bool ReadFeedback(ByteReader &r, Feedback &f)
{
	f.rumble_seq = r.U16();
	f.rumble_left = r.U8();
	f.rumble_right = r.U8();
	f.trigger_seq = r.U16();
	f.trigger_type_left = r.U8();
	f.trigger_type_right = r.U8();
	r.Bytes(f.trigger_left.data(), f.trigger_left.size());
	r.Bytes(f.trigger_right.data(), f.trigger_right.size());
	f.led_seq = r.U16();
	r.Bytes(f.led.data(), f.led.size());
	f.player_index_seq = r.U16();
	f.player_index = r.U8();
	f.haptic_intensity_seq = r.U16();
	f.haptic_intensity = r.U8();
	f.trigger_intensity_seq = r.U16();
	f.trigger_intensity = r.U8();
	f.motion_reset_seq = r.U16();
	return r.ok();
}

static void WriteBlobRef(ByteWriter &w, const BlobRef &b)
{
	w.Bytes(b.hash.data(), b.hash.size());
	w.U16(b.size);
}

static bool ReadBlobRef(ByteReader &r, BlobRef &b)
{
	r.Bytes(b.hash.data(), b.hash.size());
	b.size = r.U16();
	if(!r.ok() || b.size > kMaxAvatarSize)
		return false;
	return true;
}

PlayerInput ToPlayerInput(uint8_t slot, const PadState &state)
{
	PlayerInput p;
	p.slot = slot;
	p.buttons = state.buttons;
	p.l2 = state.l2;
	p.r2 = state.r2;
	p.left_x = state.left_x;
	p.left_y = state.left_y;
	p.right_x = state.right_x;
	p.right_y = state.right_y;
	return p;
}

static void WritePlayerInput(ByteWriter &w, const PlayerInput &p)
{
	w.U8(p.slot);
	w.U32(p.buttons);
	w.U8(p.l2);
	w.U8(p.r2);
	w.I16(p.left_x);
	w.I16(p.left_y);
	w.I16(p.right_x);
	w.I16(p.right_y);
}

static void ReadPlayerInput(ByteReader &r, PlayerInput &p)
{
	p.slot = r.U8();
	p.buttons = r.U32();
	p.l2 = r.U8();
	p.r2 = r.U8();
	p.left_x = r.I16();
	p.left_y = r.I16();
	p.right_x = r.I16();
	p.right_y = r.I16();
}

void WriteInputs(ByteWriter &w, const InputsSnapshot &s)
{
	w.U32(s.seq);
	size_t count = s.players.size() > kMaxGuests + 1 ? kMaxGuests + 1 : s.players.size();
	w.U8(static_cast<uint8_t>(count));
	for(size_t i = 0; i < count; i++)
		WritePlayerInput(w, s.players[i]);
	WritePlayerInput(w, s.combined);
}

bool ReadInputs(ByteReader &r, InputsSnapshot &s)
{
	s = InputsSnapshot();
	s.seq = r.U32();
	size_t count = r.U8();
	if(!r.ok() || count > kMaxGuests + 1)
		return false;
	s.players.resize(count);
	for(size_t i = 0; i < count; i++)
		ReadPlayerInput(r, s.players[i]);
	ReadPlayerInput(r, s.combined);
	return r.ok();
}

void WriteKeyMouseState(ByteWriter &w, const KeyMouseState &s)
{
	w.U8(s.buttons);
	w.U32(static_cast<uint32_t>(s.move_x));
	w.U32(static_cast<uint32_t>(s.move_y));
	w.U32(static_cast<uint32_t>(s.wheel_y));
	w.U32(static_cast<uint32_t>(s.wheel_x));
	const size_t count = s.keys.size() > KeyMouseState::kMaxKeys ? KeyMouseState::kMaxKeys : s.keys.size();
	w.U8(static_cast<uint8_t>(count));
	for(size_t i = 0; i < count; i++)
		w.U8(s.keys[i]);
}

bool ReadKeyMouseState(ByteReader &r, KeyMouseState &s)
{
	s = KeyMouseState();
	s.buttons = r.U8();
	s.move_x = static_cast<int32_t>(r.U32());
	s.move_y = static_cast<int32_t>(r.U32());
	s.wheel_y = static_cast<int32_t>(r.U32());
	s.wheel_x = static_cast<int32_t>(r.U32());
	const size_t count = r.U8();
	if(!r.ok() || count > KeyMouseState::kMaxKeys)
		return false;
	for(size_t i = 0; i < count; i++)
		s.keys.push_back(r.U8());
	return r.ok();
}

void WriteSessionState(ByteWriter &w, const SessionState &s)
{
	w.U32(s.state_seq);
	WriteStreamInfo(w, s.stream);
	WriteAudioHeader(w, s.audio);
	w.U16(s.video_config_id);
	WriteFeedback(w, s.feedback);
	w.U8(static_cast<uint8_t>(s.mix_mode));
	w.U8(s.your_slot);
	size_t count = s.participants.size() > kMaxGuests + 1 ? kMaxGuests + 1 : s.participants.size();
	w.U8(static_cast<uint8_t>(count));
	for(size_t i = 0; i < count; i++)
	{
		const Participant &p = s.participants[i];
		w.U8(p.slot);
		w.U8(p.flags);
		w.Str8(p.name.substr(0, kMaxNameLength));
		WriteBlobRef(w, p.avatar);
		w.U8(static_cast<uint8_t>(p.controller));
		w.U8(p.media);
		w.U8(p.battery);
		w.U8(p.player);
	}
}

bool ReadSessionState(ByteReader &r, SessionState &s)
{
	s = SessionState();
	s.state_seq = r.U32();
	if(!ReadStreamInfo(r, s.stream) || !ReadAudioHeader(r, s.audio))
		return false;
	s.video_config_id = r.U16();
	if(!ReadFeedback(r, s.feedback))
		return false;
	uint8_t mode = r.U8();
	s.your_slot = r.U8();
	size_t count = r.U8();
	if(!r.ok() || mode > static_cast<uint8_t>(MixMode::EachOwn) || count > kMaxGuests + 1)
		return false;
	s.mix_mode = static_cast<MixMode>(mode);
	for(size_t i = 0; i < count; i++)
	{
		Participant p;
		p.slot = r.U8();
		p.flags = r.U8();
		p.name = r.Str8();
		if(!r.ok() || p.name.size() > kMaxNameLength || !ReadBlobRef(r, p.avatar))
			return false;
		p.controller = ControllerKindFromWire(r.U8());
		p.media = r.U8();
		p.battery = r.U8();
		p.player = r.U8();
		s.participants.push_back(std::move(p));
	}
	return r.ok();
}

void WriteHello(ByteWriter &w, const HelloPayload &h)
{
	w.Bytes(h.guest_nonce.data(), h.guest_nonce.size());
	w.U8(h.want);
	w.U8(static_cast<uint8_t>(h.controller));
	w.Str8(h.name.substr(0, kMaxNameLength));
	WriteBlobRef(w, h.avatar);
	w.U8(h.media);
	w.U8(h.battery);
	w.U64(h.player_id);
}

bool ReadHello(ByteReader &r, HelloPayload &h)
{
	r.Bytes(h.guest_nonce.data(), h.guest_nonce.size());
	h.want = r.U8() & kWantMask;
	uint8_t controller = r.U8();
	h.name = r.Str8();
	if(!r.ok() || h.name.size() > kMaxNameLength || !ReadBlobRef(r, h.avatar))
		return false;
	h.media = r.U8() & kMediaOwnMask;
	h.battery = r.U8();
	h.player_id = r.U64();
	if(!r.ok())
		return false;
	h.controller = ControllerKindFromWire(controller);
	return true;
}

void WriteWelcome(ByteWriter &w, const WelcomePayload &p)
{
	w.Bytes(p.guest_nonce.data(), p.guest_nonce.size());
	w.Bytes(p.host_nonce.data(), p.host_nonce.size());
	w.U32(p.conn_id);
	w.U8(p.slot);
	w.Str8(p.host_name.substr(0, kMaxNameLength));
	WriteStreamInfo(w, p.stream);
	WriteAudioHeader(w, p.audio);
}

bool ReadWelcome(ByteReader &r, WelcomePayload &p)
{
	r.Bytes(p.guest_nonce.data(), p.guest_nonce.size());
	r.Bytes(p.host_nonce.data(), p.host_nonce.size());
	p.conn_id = r.U32();
	p.slot = r.U8();
	p.host_name = r.Str8();
	if(!r.ok() || p.host_name.size() > kMaxNameLength)
		return false;
	return ReadStreamInfo(r, p.stream) && ReadAudioHeader(r, p.audio);
}

void WriteVideoFragmentHeader(ByteWriter &w, const VideoFragmentHeader &h)
{
	w.U32(h.frame_seq);
	w.U16(h.frag_index);
	w.U16(h.frag_count);
	w.U8(h.flags);
	w.U8(h.frames_lost);
	w.U32(h.total_size);
	w.U32(h.capture_ms);
}

bool ReadVideoFragmentHeader(ByteReader &r, VideoFragmentHeader &h)
{
	h.frame_seq = r.U32();
	h.frag_index = r.U16();
	h.frag_count = r.U16();
	h.flags = r.U8();
	h.frames_lost = r.U8();
	h.total_size = r.U32();
	h.capture_ms = r.U32();
	return r.ok();
}

size_t WriteNack(ByteWriter &w, const std::vector<NackEntry> &entries, size_t first, size_t max_payload)
{
	size_t used = 0;
	size_t written = 0;
	for(size_t i = first; i < entries.size(); i++)
	{
		const NackEntry &e = entries[i];
		size_t need = 6 + e.fragments.size() * 2;
		if(used + need > max_payload)
			break;
		w.U32(e.frame_seq);
		if(e.fragments.empty())
			w.U16(kNackWholeFrame);
		else
		{
			w.U16(static_cast<uint16_t>(e.fragments.size()));
			for(uint16_t f : e.fragments)
				w.U16(f);
		}
		used += need;
		written++;
	}
	return written;
}

bool ReadNack(ByteReader &r, std::vector<NackEntry> &entries)
{
	entries.clear();
	while(r.ok() && r.remaining() > 0)
	{
		NackEntry e;
		e.frame_seq = r.U32();
		uint16_t count = r.U16();
		if(!r.ok())
			return false;
		if(count != kNackWholeFrame)
		{
			if(static_cast<size_t>(count) * 2 > r.remaining())
				return false;
			e.fragments.reserve(count);
			for(uint16_t i = 0; i < count; i++)
				e.fragments.push_back(r.U16());
		}
		entries.push_back(std::move(e));
	}
	return r.ok();
}

void WriteCamFragmentHeader(ByteWriter &w, const CamFragmentHeader &h)
{
	w.U32(h.frame_seq);
	w.U16(h.frag_index);
	w.U16(h.frag_count);
	w.U32(h.total_size);
}

bool ReadCamFragmentHeader(ByteReader &r, CamFragmentHeader &h)
{
	h.frame_seq = r.U32();
	h.frag_index = r.U16();
	h.frag_count = r.U16();
	h.total_size = r.U32();
	if(!r.ok() || h.total_size == 0 || h.total_size > kMaxCamFrameSize)
		return false;
	if(h.frag_count != CamFragmentCount(h.total_size) || h.frag_index >= h.frag_count)
		return false;
	// Every fragment but the last is full, the last one carries the rest
	size_t expected = h.frag_index + 1 == h.frag_count
		? h.total_size - static_cast<size_t>(h.frag_index) * kCamFragmentSize : kCamFragmentSize;
	return r.remaining() == expected;
}

bool ValidTwitchLogin(const std::string &login)
{
	if(login.size() < 3 || login.size() > kMaxTwitchLogin)
		return false;
	for(char c : login)
		if(!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
			return false;
	return true;
}

namespace
{
/** Code points emoji are made of (Unicode's emoji blocks and the few symbols outside them). */
bool EmojiCodePoint(uint32_t c)
{
	return c == 0xa9 || c == 0xae || c == 0x203c || c == 0x2049 || c == 0x2122 || c == 0x2139
		|| (c >= 0x2190 && c <= 0x21ff) || (c >= 0x2300 && c <= 0x23ff) || c == 0x24c2
		|| (c >= 0x25a0 && c <= 0x27bf) || (c >= 0x2900 && c <= 0x297f) || (c >= 0x2b00 && c <= 0x2bff)
		|| c == 0x3030 || c == 0x303d || c == 0x3297 || c == 0x3299 || (c >= 0x1f000 && c <= 0x1faff);
}

/** What joins or changes an emoji: zero width joiner, variation selectors, the keycap. */
bool EmojiJoiner(uint32_t c)
{
	return c == 0x200d || c == 0xfe0e || c == 0xfe0f || c == 0x20e3;
}
}

bool ValidReaction(const std::string &emoji)
{
	if(emoji.empty() || emoji.size() > kMaxReactionSize)
		return false;
	// UTF-8 that decodes to emoji code points and what joins them: no letters of any script, no
	// spaces, controls or invisible marks; a digit, # or * only as a keycap (1️⃣, #️⃣)
	std::vector<uint32_t> points;
	for(size_t i = 0; i < emoji.size();)
	{
		const uint8_t c = static_cast<uint8_t>(emoji[i]);
		const size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xe ? 3 : (c >> 3) == 0x1e ? 4 : 0;
		if(n == 0 || i + n > emoji.size())
			return false;
		uint32_t point = n == 1 ? c : n == 2 ? (c & 0x1f) : n == 3 ? (c & 0x0f) : (c & 0x07);
		for(size_t k = 1; k < n; k++)
		{
			const uint8_t b = static_cast<uint8_t>(emoji[i + k]);
			if((b & 0xc0) != 0x80)
				return false;
			point = (point << 6) | (b & 0x3f);
		}
		// The shortest form only (no hidden ASCII in long forms)
		const uint32_t least = n == 1 ? 0 : n == 2 ? 0x80 : n == 3 ? 0x800 : 0x10000;
		if(point < least || point > 0x10ffff)
			return false;
		points.push_back(point);
		i += n;
	}
	bool symbol = false;
	for(size_t i = 0; i < points.size(); i++)
	{
		const uint32_t c = points[i];
		if((c >= '0' && c <= '9') || c == '#' || c == '*')
		{
			// A keycap: the character, perhaps the emoji selector, then the keycap mark
			size_t k = i + 1;
			if(k < points.size() && points[k] == 0xfe0f)
				k++;
			if(k >= points.size() || points[k] != 0x20e3)
				return false;
			symbol = true;
			i = k;
		}
		else if(EmojiCodePoint(c))
			symbol = true;
		else if(!EmojiJoiner(c) || c == 0x20e3)
			return false;
	}
	return symbol;
}

void WriteChatReaction(ByteWriter &w, const ChatReaction &r)
{
	w.U32(r.seq);
	w.U8(r.on ? 1 : 0);
	w.Str8(r.emoji.substr(0, kMaxReactionSize));
}

bool ReadChatReaction(ByteReader &r, ChatReaction &out)
{
	out.seq = r.U32();
	out.on = r.U8() != 0;
	out.emoji = r.Str8();
	return r.ok() && out.seq != 0 && ValidReaction(out.emoji);
}

void WriteChatMark(ByteWriter &w, const ChatMark &m)
{
	w.U32(m.mark);
	w.U8(static_cast<uint8_t>(m.kind));
	w.U8(m.slot);
	w.U32(m.seq);
	if(m.kind == ChatMarkKind::Reaction)
	{
		w.U8(m.on ? 1 : 0);
		w.Str8(m.emoji.substr(0, kMaxReactionSize));
	}
}

bool ReadChatMark(ByteReader &r, ChatMark &m)
{
	m.mark = r.U32();
	const uint8_t kind = r.U8();
	m.slot = r.U8();
	m.seq = r.U32();
	if(!r.ok() || m.slot > kMaxGuests)
		return false;
	if(kind == static_cast<uint8_t>(ChatMarkKind::Read))
	{
		m.kind = ChatMarkKind::Read;
		return true;
	}
	if(kind == static_cast<uint8_t>(ChatMarkKind::Deleted))
	{
		m.kind = ChatMarkKind::Deleted;
		return m.seq != 0;
	}
	if(kind != static_cast<uint8_t>(ChatMarkKind::Reaction))
		return false;
	m.kind = ChatMarkKind::Reaction;
	m.on = r.U8() != 0;
	m.emoji = r.Str8();
	return r.ok() && m.seq != 0 && ValidReaction(m.emoji);
}

bool ApplyChatMark(ChatMarks &marks, const ChatMark &m)
{
	if(m.kind == ChatMarkKind::Read)
	{
		if(m.seq == 0)
			return marks.read.erase(m.slot) > 0;
		uint32_t &read = marks.read[m.slot];
		if(read == m.seq)
			return false;
		read = m.seq;
		return true;
	}
	if(m.kind == ChatMarkKind::Deleted)
	{
		// Gone with its reactions
		const bool reacted = marks.reactions.erase(m.seq) > 0;
		return marks.deleted.insert(m.seq).second || reacted;
	}
	auto &kinds = marks.reactions[m.seq];
	auto &slots = kinds[m.emoji];
	const auto at = std::find(slots.begin(), slots.end(), m.slot);
	bool changed = false;
	if(m.on && at == slots.end())
	{
		slots.push_back(m.slot);
		changed = true;
	}
	else if(!m.on && at != slots.end())
	{
		slots.erase(at);
		changed = true;
	}
	if(slots.empty())
		kinds.erase(m.emoji);
	if(kinds.empty())
		marks.reactions.erase(m.seq);
	return changed;
}

std::string TruncateUtf8(const std::string &s, size_t max)
{
	if(s.size() <= max)
		return s;
	size_t end = max;
	// Back to the first byte of the character that would be cut
	while(end > 0 && (static_cast<uint8_t>(s[end]) & 0xc0) == 0x80)
		end--;
	return s.substr(0, end);
}

void WriteChatMessage(ByteWriter &w, const ChatMessage &m)
{
	w.U32(m.seq);
	w.U32(m.up_id);
	w.U8(m.slot);
	w.U8(m.flags);
	w.U64(m.time_ms);
	w.Str8(TruncateUtf8(m.name, kMaxNameLength));
	w.Str16(TruncateUtf8(m.text, kMaxChatText));
	w.U8(m.embed.Valid() ? 1 : 0);
	if(m.embed.Valid())
	{
		w.Str16(TruncateUtf8(m.embed.url, kMaxChatUrl));
		w.Str8(TruncateUtf8(m.embed.site, kMaxChatSite));
		w.Str8(TruncateUtf8(m.embed.title, kMaxChatTitle));
		w.Str16(TruncateUtf8(m.embed.description, kMaxChatDescription));
		WriteBlobRef(w, m.embed.image);
	}
	// Last, so that versions without replies read the rest as before
	w.U32(m.reply);
}

bool ReadChatMessage(ByteReader &r, ChatMessage &m)
{
	m.seq = r.U32();
	m.up_id = r.U32();
	m.slot = r.U8();
	m.flags = r.U8();
	m.time_ms = r.U64();
	m.name = r.Str8();
	m.text = r.Str16();
	uint8_t has_embed = r.U8();
	if(!r.ok() || m.name.size() > kMaxNameLength || m.text.size() > kMaxChatText)
		return false;
	m.embed = ChatEmbed();
	m.reply = 0;
	if(has_embed)
	{
		m.embed.url = r.Str16();
		m.embed.site = r.Str8();
		m.embed.title = r.Str8();
		m.embed.description = r.Str16();
		if(!r.ok() || !ReadBlobRef(r, m.embed.image))
			return false;
		if(m.embed.url.empty() || m.embed.url.size() > kMaxChatUrl || m.embed.site.size() > kMaxChatSite
				|| m.embed.title.size() > kMaxChatTitle || m.embed.description.size() > kMaxChatDescription
				|| m.embed.image.size > kMaxChatImageSize)
			return false;
	}
	else if(m.text.empty())
		return false;
	// The message it answers, from versions that have replies
	if(r.remaining() >= 4)
		m.reply = r.U32();
	return true;
}

} // namespace coplay
