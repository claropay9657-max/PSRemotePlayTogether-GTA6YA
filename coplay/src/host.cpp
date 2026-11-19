// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <coplay/host.h>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace coplay {

static const uint32_t kUnconfirmedTimeoutMs = 3000;
static const uint64_t kFastStateWindowMs = 100;
static const uint32_t kFastStateIntervalMs = 30;
static const size_t kMaxSeenNonces = 256;
static const size_t kMaxRetransmitFragmentsPerNack = 256;
/** A requested key frame that has not shown up after this long is requested again. */
static const uint32_t kKeyframeOutstandingMs = 1000;

static std::string CleanName(const std::string &name, uint8_t slot)
{
	std::string out;
	for(char c : name)
	{
		if(static_cast<unsigned char>(c) >= 0x20 && c != 0x7f)
			out.push_back(c);
	}
	if(out.size() > kMaxNameLength)
		out.resize(kMaxNameLength);
	if(out.empty())
		out = "Guest " + std::to_string(slot);
	return out;
}

Host::Host(const HostConfig &config_in, const HostCallbacks &callbacks)
	: config(config_in), callbacks(callbacks), mixer(config_in.mixer)
{
	if(config.max_guests > kMaxGuests)
		config.max_guests = kMaxGuests;
	hello_key = DeriveHelloKey(config.secret);
	welcome_key = DeriveWelcomeKey(config.secret);
	mixer.SetSource(InputMixer::kHostSource, PadState(), NowMs());
}

Host::~Host()
{
	Stop();
}

bool Host::Start(std::string *error)
{
	if(running)
		return true;
	if(!socket.Bind(config.port, error))
		return false;
	stop_requested = false;
	running = true;
	thread = std::thread(&Host::ThreadMain, this);
	return true;
}

void Host::Stop()
{
	if(!running)
		return;
	{
		std::lock_guard<std::mutex> lock(mutex);
		for(auto &entry : guests)
		{
			if(entry.second->confirmed)
			{
				SendBye(*entry.second, ByeReason::HostEnded);
				SendBye(*entry.second, ByeReason::HostEnded);
			}
		}
	}
	stop_requested = true;
	if(thread.joinable())
		thread.join();
	socket.Close();
	{
		std::lock_guard<std::mutex> lock(mutex);
		for(auto &entry : guests)
			mixer.RemoveSource(entry.second->slot);
		guests.clear();
	}
	running = false;
}

uint16_t Host::Port() const
{
	return socket.LocalPort();
}

void Host::ThreadMain()
{
	uint8_t buf[2048];
	while(!stop_requested)
	{
		Address from;
		int n = socket.RecvFrom(buf, sizeof(buf), from, 5);
		Deferred d;
		{
			std::lock_guard<std::mutex> lock(mutex);
			uint64_t now = NowMs();
			// Take what is already queued before doing the periodic work
			for(int drained = 0; n > 0 && drained < 64; drained++)
			{
				HandlePacket(buf, static_cast<size_t>(n), from, now, d);
				n = socket.RecvFrom(buf, sizeof(buf), from, 0);
			}
			Tick(now, d);
		}
		Fire(d);
		if(n < 0)
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
}

void Host::Fire(Deferred &d)
{
	if(callbacks.on_log)
	{
		for(const std::string &line : d.logs)
			callbacks.on_log(line);
	}
	if(d.guests_changed && callbacks.on_guests_changed)
		callbacks.on_guests_changed();
	if(d.keyframe_needed && callbacks.on_keyframe_needed)
		callbacks.on_keyframe_needed();
	if(d.input_changed && callbacks.on_input_changed)
		callbacks.on_input_changed();
}

bool Host::SendToGuest(Guest &g, PacketType type, const std::vector<uint8_t> &payload)
{
	std::vector<uint8_t> packet;
	if(!SealPacket(g.keys.host_to_guest, type, g.conn_id, ++g.send_counter, payload.data(), payload.size(), packet))
		return false;
	return socket.SendTo(packet.data(), packet.size(), g.addr);
}

void Host::SendBye(Guest &g, ByeReason reason)
{
	std::vector<uint8_t> payload;
	ByteWriter w(payload);
	w.U8(static_cast<uint8_t>(reason));
	SendToGuest(g, PacketType::Bye, payload);
}

void Host::BumpSeq(uint16_t &seq)
{
	seq++;
	if(seq == 0)
		seq = 1; // 0 means "never set"
}

Host::Guest *Host::FindBySlot(uint8_t slot)
{
	for(auto &entry : guests)
	{
		if(entry.second->slot == slot)
			return entry.second.get();
	}
	return nullptr;
}

void Host::RequestKeyframe(uint64_t now_ms, Deferred &d)
{
	// One is already on its way from the console; it serves this request as well
	if(keyframe_outstanding && now_ms - last_keyframe_request_ms < kKeyframeOutstandingMs)
		return;
	if(now_ms - last_keyframe_request_ms >= config.keyframe_min_interval_ms || last_keyframe_request_ms == 0)
	{
		last_keyframe_request_ms = now_ms;
		keyframe_pending = false;
		keyframe_outstanding = true;
		d.keyframe_needed = true;
	}
	else
		keyframe_pending = true;
}

void Host::RemoveGuest(uint32_t conn_id, Deferred &d, const std::string &why)
{
	auto it = guests.find(conn_id);
	if(it == guests.end())
		return;
	Guest &g = *it->second;
	bool was_confirmed = g.confirmed;
	if(was_confirmed)
		d.logs.push_back(g.name + " " + why);
	mixer.RemoveSource(g.slot);
	guests.erase(it);
	if(was_confirmed)
	{
		d.guests_changed = true;
		state_seq++;
		BroadcastState(NowMs());
	}
}

void Host::HandleHello(const uint8_t *data, size_t size, const PacketHeader &header, const Address &from,
		uint64_t now_ms, Deferred &d)
{
	std::vector<uint8_t> payload;
	if(!OpenPacket(hello_key, data, size, payload))
		return; // not someone with our invite: stay silent
	if(payload.size() < kHandshakeNonceSize)
		return;

	auto reject = [&](const uint8_t *guest_nonce, RejectReason reason) {
		std::vector<uint8_t> body;
		ByteWriter w(body);
		w.Bytes(guest_nonce, kHandshakeNonceSize);
		w.U8(static_cast<uint8_t>(reason));
		std::vector<uint8_t> packet;
		if(SealHandshakePacket(welcome_key, PacketType::Reject, body.data(), body.size(), packet))
			socket.SendTo(packet.data(), packet.size(), from);
	};

	if(header.version != kProtocolVersion)
	{
		reject(payload.data(), RejectReason::Version);
		return;
	}

	HelloPayload hello;
	ByteReader r(payload.data(), payload.size());
	if(!ReadHello(r, hello))
		return;

	// The guest repeats its Hello until it hears a Welcome
	for(auto &entry : guests)
	{
		Guest &g = *entry.second;
		if(g.guest_nonce == hello.guest_nonce)
		{
			if(!g.confirmed)
				g.addr = from;
			socket.SendTo(g.welcome_packet.data(), g.welcome_packet.size(), from);
			return;
		}
	}

	// A Hello of a guest that is gone is a late duplicate or a replay
	if(std::find(seen_nonces.begin(), seen_nonces.end(), hello.guest_nonce) != seen_nonces.end())
		return;

	if(guests.size() >= config.max_guests)
	{
		reject(hello.guest_nonce.data(), RejectReason::Full);
		return;
	}

	auto g = std::make_unique<Guest>();
	for(uint8_t slot = 1; slot <= config.max_guests; slot++)
	{
		if(!FindBySlot(slot))
		{
			g->slot = slot;
			break;
		}
	}
	if(g->slot == 0)
		return;
	do
	{
		RandomBytes(reinterpret_cast<uint8_t *>(&g->conn_id), sizeof(g->conn_id));
	} while(g->conn_id == 0 || guests.count(g->conn_id));

	HandshakeNonce host_nonce{};
	if(!RandomBytes(host_nonce.data(), host_nonce.size()))
		return;

	g->addr = from;
	g->name = CleanName(hello.name, g->slot);
	g->guest_nonce = hello.guest_nonce;
	g->keys = DeriveSessionKeys(config.secret, hello.guest_nonce, host_nonce);
	g->created_ms = now_ms;
	g->last_rx_ms = now_ms;
	g->want = hello.want;
	g->controller = hello.controller;

	WelcomePayload welcome;
	welcome.guest_nonce = hello.guest_nonce;
	welcome.host_nonce = host_nonce;
	welcome.conn_id = g->conn_id;
	welcome.slot = g->slot;
	welcome.host_name = config.name;
	welcome.stream = stream;
	welcome.audio = audio;
	std::vector<uint8_t> body;
	ByteWriter w(body);
	WriteWelcome(w, welcome);
	if(!SealHandshakePacket(welcome_key, PacketType::Welcome, body.data(), body.size(), g->welcome_packet))
		return;

	seen_nonces.push_back(hello.guest_nonce);
	if(seen_nonces.size() > kMaxSeenNonces)
		seen_nonces.pop_front();

	socket.SendTo(g->welcome_packet.data(), g->welcome_packet.size(), from);
	uint32_t conn_id = g->conn_id;
	guests[conn_id] = std::move(g);
	(void)d;
}

void Host::HandlePacket(const uint8_t *data, size_t size, const Address &from, uint64_t now_ms, Deferred &d)
{
	PacketHeader header;
	if(!ParseHeader(data, size, header))
		return;
	if(header.type == PacketType::Hello)
	{
		HandleHello(data, size, header, from, now_ms, d);
		return;
	}
	if(header.version != kProtocolVersion)
		return;
	auto it = guests.find(header.conn_id);
	if(it == guests.end())
		return;
	Guest &g = *it->second;

	std::vector<uint8_t> payload;
	if(!OpenPacket(g.keys.guest_to_host, data, size, payload))
		return;
	if(!g.replay.Accept(header.counter))
		return;

	// Authenticated and fresh: follow the guest if its address changed (NAT rebinding, network switch)
	g.addr = from;
	g.last_rx_ms = now_ms;

	if(!g.confirmed)
	{
		g.confirmed = true;
		g.awaiting_key = true;
		mixer.SetSource(g.slot, PadState(), now_ms);
		mixer.SetMask(g.slot, g.can_control ? InputMask::All() : InputMask::None());
		d.guests_changed = true;
		d.logs.push_back(g.name + " joined from " + g.addr.ToString());
		if(g.want & kWantVideo)
		{
			SendVideoConfig(g);
			RequestKeyframe(now_ms, d);
		}
		state_seq++;
		BroadcastState(now_ms);
	}

	ByteReader r(payload.data(), payload.size());
	switch(header.type)
	{
		case PacketType::Input: {
			uint32_t seq = r.U32();
			PadState state;
			if(!r.ok() || !ReadPadState(r, state))
				break;
			if(g.have_input && static_cast<int32_t>(seq - g.last_input_seq) <= 0)
				break; // older than what we already have
			g.have_input = true;
			g.last_input_seq = seq;
			mixer.SetSource(g.slot, state, now_ms);
			break;
		}
		case PacketType::Ping: {
			uint32_t ping_id = r.U32();
			uint64_t sent_us = r.U64();
			uint8_t want = r.U8() & kWantAll;
			uint16_t rtt = r.U16();
			if(!r.ok())
				break;
			g.rtt_ms = rtt;
			if(want != g.want)
			{
				bool video_turned_on = (want & kWantVideo) && !(g.want & kWantVideo);
				g.want = want;
				d.guests_changed = true;
				if(video_turned_on)
				{
					g.awaiting_key = true;
					SendVideoConfig(g);
					RequestKeyframe(now_ms, d);
				}
			}
			std::vector<uint8_t> pong;
			ByteWriter w(pong);
			w.U32(ping_id);
			w.U64(sent_us);
			SendToGuest(g, PacketType::Pong, pong);
			break;
		}
		case PacketType::Nack:
			HandleNack(g, payload);
			break;
		case PacketType::KeyframeRequest:
			if(g.want & kWantVideo)
				RequestKeyframe(now_ms, d);
			break;
		case PacketType::Bye:
			RemoveGuest(header.conn_id, d, "left");
			return; // g is gone
		default:
			break;
	}
}

void Host::HandleNack(Guest &g, const std::vector<uint8_t> &payload)
{
	if(!(g.want & kWantVideo))
		return;
	std::vector<NackEntry> entries;
	ByteReader r(payload.data(), payload.size());
	if(!ReadNack(r, entries))
		return;
	size_t budget = kMaxRetransmitFragmentsPerNack;
	std::vector<uint8_t> fragment;
	for(const NackEntry &e : entries)
	{
		const FrameCache::Entry *entry = cache.Find(e.frame_seq);
		if(!entry)
			continue; // too old; the guest will ask for a key frame
		uint32_t count = VideoFragmentCount(entry->data->size());
		if(e.fragments.empty())
		{
			for(uint32_t i = 0; i < count && budget; i++, budget--)
			{
				if(BuildVideoFragment(*entry, i, true, fragment))
					SendToGuest(g, PacketType::Video, fragment);
			}
		}
		else
		{
			for(uint16_t i : e.fragments)
			{
				if(!budget)
					break;
				budget--;
				if(BuildVideoFragment(*entry, i, true, fragment))
					SendToGuest(g, PacketType::Video, fragment);
			}
		}
		if(!budget)
			break;
	}
}

void Host::UpdateControl(uint64_t now_ms, Deferred &d)
{
	PadState mix = mixer.Mix(now_ms);
	if(mix != last_reported_mix)
	{
		last_reported_mix = mix;
		d.input_changed = true;
	}

	bool changed = false;
	bool host_control = mixer.InControl(InputMixer::kHostSource);
	if(host_control != host_in_control)
	{
		host_in_control = host_control;
		changed = true;
	}
	for(auto &entry : guests)
	{
		Guest &g = *entry.second;
		bool control = g.confirmed && g.can_control && mixer.InControl(g.slot);
		if(control != g.in_control)
		{
			g.in_control = control;
			changed = true;
		}
	}
	if(changed)
	{
		state_seq++;
		BroadcastState(now_ms);
		d.guests_changed = true;
	}
}

void Host::Tick(uint64_t now_ms, Deferred &d)
{
	std::vector<uint32_t> timed_out;
	for(auto &entry : guests)
	{
		Guest &g = *entry.second;
		if(!g.confirmed)
		{
			if(now_ms - g.created_ms > kUnconfirmedTimeoutMs)
				timed_out.push_back(entry.first);
		}
		else if(now_ms - g.last_rx_ms > config.guest_timeout_ms)
			timed_out.push_back(entry.first);
	}
	for(uint32_t conn_id : timed_out)
		RemoveGuest(conn_id, d, "timed out");

	if(keyframe_pending && now_ms - last_keyframe_request_ms >= config.keyframe_min_interval_ms)
	{
		keyframe_pending = false;
		keyframe_outstanding = true;
		last_keyframe_request_ms = now_ms;
		d.keyframe_needed = true;
	}

	UpdateControl(now_ms, d);

	// Right after a change the state goes out a few times in quick succession, so that
	// a lost packet does not leave a controller rumbling for long
	uint32_t interval = now_ms < fast_state_until_ms ? kFastStateIntervalMs : config.state_interval_ms;
	for(auto &entry : guests)
	{
		Guest &g = *entry.second;
		if(g.confirmed && now_ms - g.last_state_ms >= interval)
			SendState(g, now_ms);
	}
}

void Host::SendState(Guest &g, uint64_t now_ms)
{
	SessionState s;
	s.state_seq = state_seq;
	s.stream = stream;
	s.audio = audio;
	s.video_config_id = video_config_id;
	s.feedback = feedback;
	s.mix_mode = mixer.Mode();
	s.your_slot = g.slot;

	Participant host;
	host.slot = 0;
	host.flags = static_cast<uint8_t>(kParticipantHost | kParticipantCanControl | (host_in_control ? kParticipantInControl : 0));
	host.name = config.name;
	s.participants.push_back(host);
	for(auto &entry : guests)
	{
		const Guest &other = *entry.second;
		if(!other.confirmed)
			continue;
		Participant p;
		p.slot = other.slot;
		p.flags = static_cast<uint8_t>((other.can_control ? kParticipantCanControl : 0) | (other.in_control ? kParticipantInControl : 0));
		p.name = other.name;
		s.participants.push_back(p);
	}
	std::sort(s.participants.begin(), s.participants.end(),
		[](const Participant &a, const Participant &b) { return a.slot < b.slot; });

	std::vector<uint8_t> payload;
	ByteWriter w(payload);
	WriteSessionState(w, s);
	SendToGuest(g, PacketType::State, payload);
	g.last_state_ms = now_ms;
}

void Host::BroadcastState(uint64_t now_ms)
{
	fast_state_until_ms = now_ms + kFastStateWindowMs;
	for(auto &entry : guests)
	{
		if(entry.second->confirmed)
			SendState(*entry.second, now_ms);
	}
}

void Host::SendVideoConfig(Guest &g)
{
	if(!video_config_id)
		return;
	std::vector<uint8_t> payload;
	ByteWriter w(payload);
	w.U16(video_config_id);
	w.Bytes(video_config.data(), video_config.size());
	SendToGuest(g, PacketType::VideoConfig, payload);
}

void Host::SendFrame(Guest &g, const FrameCache::Entry &entry)
{
	uint32_t count = VideoFragmentCount(entry.data->size());
	std::vector<uint8_t> fragment;
	for(uint32_t i = 0; i < count; i++)
	{
		if(BuildVideoFragment(entry, i, false, fragment))
			SendToGuest(g, PacketType::Video, fragment);
	}
}

void Host::SetStreamInfo(const StreamInfo &info)
{
	std::lock_guard<std::mutex> lock(mutex);
	if(stream == info)
		return;
	stream = info;
	state_seq++;
	BroadcastState(NowMs());
}

void Host::SetAudioHeader(const AudioHeader &header)
{
	std::lock_guard<std::mutex> lock(mutex);
	if(audio == header)
		return;
	audio = header;
	state_seq++;
	BroadcastState(NowMs());
}

void Host::PushVideo(const uint8_t *data, size_t size, int32_t frames_lost, bool recovered)
{
	if(!data || !size || size > kMaxVideoFrameSize || !running)
		return;
	std::lock_guard<std::mutex> lock(mutex);
	FrameKind kind = ClassifyFrame(stream.codec, data, size);

	if(kind == FrameKind::Config)
	{
		if(size > kMaxPayload - 2)
			return; // cannot happen with real parameter sets
		if(video_config.size() != size || std::memcmp(video_config.data(), data, size) != 0 || !video_config_id)
		{
			video_config.assign(data, data + size);
			BumpSeq(video_config_id);
			state_seq++;
		}
		for(auto &entry : guests)
		{
			Guest &g = *entry.second;
			if(g.confirmed && (g.want & kWantVideo))
			{
				SendVideoConfig(g);
				g.awaiting_key = true; // nothing decodes across a parameter set change
			}
		}
		return;
	}

	if(kind == FrameKind::Key)
		keyframe_outstanding = false;

	bool any = false;
	for(auto &entry : guests)
	{
		if(entry.second->confirmed && (entry.second->want & kWantVideo))
		{
			any = true;
			break;
		}
	}
	if(!any)
		return;

	FrameCache::Entry e;
	e.frame_seq = next_frame_seq++;
	e.flags = static_cast<uint8_t>((kind == FrameKind::Key ? kVideoKey : 0) | (recovered ? kVideoRecovered : 0));
	e.frames_lost = static_cast<uint8_t>(frames_lost < 0 ? 0 : (frames_lost > 255 ? 255 : frames_lost));
	e.capture_ms = static_cast<uint32_t>(NowMs());
	e.data = std::make_shared<const std::vector<uint8_t>>(data, data + size);
	cache.Put(e);

	for(auto &entry : guests)
	{
		Guest &g = *entry.second;
		if(!g.confirmed || !(g.want & kWantVideo))
			continue;
		if(kind == FrameKind::Key)
		{
			// Costs a few bytes and makes sure the guest can always start decoding here
			SendVideoConfig(g);
			g.awaiting_key = false;
		}
		else if(g.awaiting_key)
			continue; // of no use to this guest yet
		SendFrame(g, e);
	}
}

void Host::PushAudio(const uint8_t *data, size_t size)
{
	if(!data || !size || size > kMaxPayload - 4 || !running)
		return;
	std::lock_guard<std::mutex> lock(mutex);
	if(guests.empty())
		return;
	std::vector<uint8_t> payload;
	payload.reserve(size + 4);
	ByteWriter w(payload);
	w.U32(++audio_seq);
	w.Bytes(data, size);
	for(auto &entry : guests)
	{
		Guest &g = *entry.second;
		if(g.confirmed && (g.want & kWantAudio))
			SendToGuest(g, PacketType::Audio, payload);
	}
}

void Host::PushHaptics(const uint8_t *data, size_t size)
{
	if(!data || !size || size > kMaxPayload - 4 || !running)
		return;
	std::lock_guard<std::mutex> lock(mutex);
	if(guests.empty())
		return;
	std::vector<uint8_t> payload;
	payload.reserve(size + 4);
	ByteWriter w(payload);
	w.U32(++haptics_seq);
	w.Bytes(data, size);
	for(auto &entry : guests)
	{
		Guest &g = *entry.second;
		if(g.confirmed && (g.want & kWantHaptics))
			SendToGuest(g, PacketType::Haptics, payload);
	}
}

void Host::SetRumble(uint8_t left, uint8_t right)
{
	std::lock_guard<std::mutex> lock(mutex);
	feedback.rumble_left = left;
	feedback.rumble_right = right;
	BumpSeq(feedback.rumble_seq);
	state_seq++;
	BroadcastState(NowMs());
}

void Host::SetTriggerEffects(uint8_t type_left, uint8_t type_right, const uint8_t left[10], const uint8_t right[10])
{
	std::lock_guard<std::mutex> lock(mutex);
	feedback.trigger_type_left = type_left;
	feedback.trigger_type_right = type_right;
	std::memcpy(feedback.trigger_left.data(), left, feedback.trigger_left.size());
	std::memcpy(feedback.trigger_right.data(), right, feedback.trigger_right.size());
	BumpSeq(feedback.trigger_seq);
	state_seq++;
	BroadcastState(NowMs());
}

void Host::SetLed(const uint8_t rgb[3])
{
	std::lock_guard<std::mutex> lock(mutex);
	std::memcpy(feedback.led.data(), rgb, feedback.led.size());
	BumpSeq(feedback.led_seq);
	state_seq++;
	BroadcastState(NowMs());
}

void Host::SetPlayerIndex(uint8_t index)
{
	std::lock_guard<std::mutex> lock(mutex);
	feedback.player_index = index;
	BumpSeq(feedback.player_index_seq);
	state_seq++;
	BroadcastState(NowMs());
}

void Host::SetHapticIntensity(uint8_t intensity)
{
	std::lock_guard<std::mutex> lock(mutex);
	feedback.haptic_intensity = intensity;
	BumpSeq(feedback.haptic_intensity_seq);
	state_seq++;
	BroadcastState(NowMs());
}

void Host::SetTriggerIntensity(uint8_t intensity)
{
	std::lock_guard<std::mutex> lock(mutex);
	feedback.trigger_intensity = intensity;
	BumpSeq(feedback.trigger_intensity_seq);
	state_seq++;
	BroadcastState(NowMs());
}

void Host::NotifyMotionReset()
{
	std::lock_guard<std::mutex> lock(mutex);
	BumpSeq(feedback.motion_reset_seq);
	state_seq++;
	BroadcastState(NowMs());
}

void Host::SetLocalInput(const PadState &state)
{
	std::lock_guard<std::mutex> lock(mutex);
	mixer.SetSource(InputMixer::kHostSource, state, NowMs());
}

PadState Host::MixedInput()
{
	std::lock_guard<std::mutex> lock(mutex);
	PadState mix = mixer.Mix(NowMs());
	last_reported_mix = mix;
	return mix;
}

void Host::SetMixMode(MixMode mode)
{
	Deferred d;
	{
		std::lock_guard<std::mutex> lock(mutex);
		if(mixer.Mode() == mode)
			return;
		mixer.SetMode(mode);
		uint64_t now = NowMs();
		state_seq++;
		BroadcastState(now);
		UpdateControl(now, d);
		d.guests_changed = true;
	}
	Fire(d);
}

MixMode Host::GetMixMode()
{
	std::lock_guard<std::mutex> lock(mutex);
	return mixer.Mode();
}

void Host::SetGuestCanControl(uint8_t slot, bool allowed)
{
	Deferred d;
	{
		std::lock_guard<std::mutex> lock(mutex);
		Guest *g = FindBySlot(slot);
		if(!g || g->can_control == allowed)
			return;
		g->can_control = allowed;
		mixer.SetMask(slot, allowed ? InputMask::All() : InputMask::None());
		uint64_t now = NowMs();
		state_seq++;
		BroadcastState(now);
		UpdateControl(now, d);
		d.guests_changed = true;
	}
	Fire(d);
}

void Host::KickGuest(uint8_t slot)
{
	Deferred d;
	{
		std::lock_guard<std::mutex> lock(mutex);
		Guest *g = FindBySlot(slot);
		if(!g)
			return;
		SendBye(*g, ByeReason::Kicked);
		SendBye(*g, ByeReason::Kicked);
		RemoveGuest(g->conn_id, d, "was removed");
	}
	Fire(d);
}

std::vector<GuestInfo> Host::Guests()
{
	std::lock_guard<std::mutex> lock(mutex);
	std::vector<GuestInfo> out;
	for(auto &entry : guests)
	{
		const Guest &g = *entry.second;
		if(!g.confirmed)
			continue;
		GuestInfo info;
		info.slot = g.slot;
		info.name = g.name;
		info.address = g.addr.ToString();
		info.want = g.want;
		info.controller = g.controller;
		info.can_control = g.can_control;
		info.in_control = g.in_control;
		info.rtt_ms = g.rtt_ms;
		out.push_back(info);
	}
	std::sort(out.begin(), out.end(), [](const GuestInfo &a, const GuestInfo &b) { return a.slot < b.slot; });
	return out;
}

bool Host::AnyGuestWantsVideo()
{
	std::lock_guard<std::mutex> lock(mutex);
	for(auto &entry : guests)
	{
		if(entry.second->confirmed && (entry.second->want & kWantVideo))
			return true;
	}
	return false;
}

} // namespace coplay
