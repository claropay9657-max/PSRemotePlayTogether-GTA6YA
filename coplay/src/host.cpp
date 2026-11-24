// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <coplay/host.h>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace coplay {

static const uint32_t kUnconfirmedTimeoutMs = 3000;
/** The first note waits this long for the STUN answer, so that it is complete. */
static const uint32_t kFirstNoteWaitMs = 600;
/** How often the addresses in the note are checked: a laptop changes networks. */
static const uint32_t kOwnAddressCheckMs = 10000;
static const uint32_t kPunchIntervalMs = 200;
static const size_t kMaxPunches = 64;
static const size_t kMaxPunchedGuests = 128;
static const uint32_t kReachCheckMs = 500;
static const uint64_t kFastStateWindowMs = 100;
static const uint32_t kFastStateIntervalMs = 30;
static const size_t kMaxSeenNonces = 256;
static const size_t kMaxRetransmitFragmentsPerNack = 256;
/** Live inputs go out at most this often, and at least every kInputsRefreshMs. */
static const uint32_t kInputsMinIntervalMs = 16;
static const uint32_t kInputsRefreshMs = 500;
static const uint32_t kBlobPollIntervalMs = 50;
/** An unacknowledged chat message goes out again after this long, until this long has passed. */
static const uint32_t kChatResendMs = 400;
static const uint32_t kChatGiveUpMs = 20000;
static const size_t kChatLogSize = 100;
/** Link preview pictures kept for the guests to fetch. */
static const size_t kMaxChatImages = 24;
static const size_t kChatUpSeenSize = 64;
/** Packets for a guest in a browser that have been waiting this long mean its connection cannot keep up. */
static const uint32_t kWebBacklogMs = 250;
/** A browser whose picture had to start over this often within this long cannot carry the stream as it is. */
static const uint32_t kWebBehindCount = 3;
static const uint32_t kWebBehindWindowMs = 20000;
/** A connection to a browser stays open this long after the last packet for it (a Bye) went out. */
static const uint32_t kWebCloseDelayMs = 400;
static const size_t kMaxWebSeen = 256;

static uint64_t WallClockMs()
{
	return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count());
}
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
	own_avatar = blobs.Put(config.avatar);
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
	meet_on = config.meet.Enabled();
	if(meet_on)
	{
		{
			std::lock_guard<std::mutex> lock(mutex);
			RandomBytes(reinterpret_cast<uint8_t *>(&meet_id), sizeof(meet_id));
			meet_started_ms = NowMs();
			have_note = false;
			last_note = MeetNote();
			stun = StunProbe();
			punches.clear();
			punched.clear();
			reach = HostReach();
			reach.meet = MeetState::Trying;
			if(!config.meet.stun_servers.empty())
				stun_resolver.Start(config.meet.stun_servers);
		}
		// Friends in a web browser find the host through the meeting point as well
		if(config.web.enabled)
		{
			WebGateway::Callbacks wc;
			wc.on_message = [this](uint64_t peer, const uint8_t *data, size_t size) { OnWebMessage(peer, data, size); };
			wc.on_closed = [this](uint64_t peer) { OnWebClosed(peer); };
			wc.on_answers_changed = [this]() { OnWebAnswers(); };
			wc.on_log = callbacks.on_log;
			web = config.web_factory ? config.web_factory(config.web, wc) : CreateWebGateway(config.web, wc);
			web_on = web != nullptr;
		}
		// Before the network thread, which hands it the note; it asks nobody until it has one
		meet_worker = std::make_unique<MeetWorker>(config.meet, config.secret, MeetRole::Host,
			[this](const std::vector<MeetNote> &notes) { OnMeetNotes(notes); },
			[this](const std::vector<std::string> &texts) { OnOtherNotes(texts); });
	}
	thread = std::thread(&Host::ThreadMain, this);
	return true;
}

void Host::Stop()
{
	StopWith(ByeReason::HostEnded);
}

void Host::StopForReconnect()
{
	StopWith(ByeReason::Reconnecting);
}

void Host::StopWith(ByeReason reason)
{
	if(!running)
		return;
	// First, and without the lock: what it finds is handed over under the lock
	if(meet_worker)
		meet_worker->Stop();
	bool browsers = false;
	{
		std::lock_guard<std::mutex> lock(mutex);
		for(auto &entry : guests)
		{
			if(entry.second->confirmed)
			{
				SendBye(*entry.second, reason);
				SendBye(*entry.second, reason);
				browsers = browsers || entry.second->web_peer != 0;
			}
		}
	}
	stop_requested = true;
	if(thread.joinable())
		thread.join();
	if(web)
	{
		// The goodbye has to be on its way before the connections are cut
		if(browsers)
			std::this_thread::sleep_for(std::chrono::milliseconds(150));
		// Whoever still pushes picture or sound finds no gateway any more from here on
		std::unique_ptr<WebGateway> gateway;
		{
			std::lock_guard<std::mutex> lock(mutex);
			web_on = false;
			gateway = std::move(web);
		}
		gateway.reset(); // its thread has ended when this returns: nothing calls back any more
	}
	socket.Close();
	meet_worker.reset();
	meet_on = false;
	{
		std::lock_guard<std::mutex> lock(mutex);
		for(auto &entry : guests)
			mixer.RemoveSource(entry.second->slot);
		guests.clear();
		punches.clear();
		web_closing.clear();
		web_seen.clear();
		reach = HostReach();
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
	if(d.reach_changed && callbacks.on_reach_changed)
		callbacks.on_reach_changed();
	if(d.sleep_requested && callbacks.on_sleep_requested)
		callbacks.on_sleep_requested(d.sleep_slot, d.sleep_name);
	if(d.wake_requested && callbacks.on_wake_requested)
		callbacks.on_wake_requested(d.wake_slot, d.wake_name);
	if(d.chat_marks_changed && callbacks.on_chat_marks)
		callbacks.on_chat_marks();
	if(callbacks.on_guest_behind)
	{
		for(const auto &who : d.behind)
			callbacks.on_guest_behind(who.first, who.second);
	}
	if(d.pending_changed && callbacks.on_pending_changed)
		callbacks.on_pending_changed();
	if(callbacks.on_voice)
	{
		for(const auto &v : d.voice)
			callbacks.on_voice(v.first, v.second.data(), v.second.size());
	}
	if(callbacks.on_cam_frame)
	{
		for(const auto &c : d.cams)
			callbacks.on_cam_frame(c.first, c.second);
	}
	if(callbacks.on_chat)
	{
		for(const ChatMessage &m : d.chats)
			callbacks.on_chat(m);
	}
	if(callbacks.on_chat_image)
	{
		for(const BlobRef &ref : d.chat_images)
			callbacks.on_chat_image(ref);
	}
	if(callbacks.on_key_mouse)
	{
		for(const KeyMouseUpdate &u : d.key_mouse)
			callbacks.on_key_mouse(u);
	}
	if(d.keyframe_needed && callbacks.on_keyframe_needed)
		callbacks.on_keyframe_needed();
	if(d.input_changed && callbacks.on_input_changed)
		callbacks.on_input_changed();
}

bool Host::SendToGuest(Guest &g, PacketType type, const std::vector<uint8_t> &payload)
{
	if(g.web_peer)
	{
		// A browser gets the packet without header and seal: its connection provides both
		if(!web || payload.size() > kMaxPayload)
			return false;
		uint8_t frame[1 + kMaxPayload];
		frame[0] = static_cast<uint8_t>(type);
		if(!payload.empty())
			std::memcpy(frame + 1, payload.data(), payload.size());
		return web->Send(g.web_peer, frame, 1 + payload.size());
	}
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
	guest_feedback.erase(g.slot);
	if(g.key_mouse_allowed && g.have_key_mouse)
	{
		// Nothing it held stays held on the host's computer
		KeyMouseUpdate gone;
		gone.slot = g.slot;
		gone.gone = true;
		d.key_mouse.push_back(gone);
	}
	BlobRef avatar = g.avatar;
	const uint8_t slot = g.slot;
	if(g.web_peer)
		web_closing.emplace_back(g.web_peer, NowMs() + kWebCloseDelayMs);
	guests.erase(it);
	// Their read mark and reactions go: the slot may be somebody else's next
	if(was_confirmed)
	{
		std::vector<ChatMark> gone;
		if(chat_marks.read.count(slot))
		{
			ChatMark m;
			m.kind = ChatMarkKind::Read;
			m.slot = slot;
			gone.push_back(m);
		}
		for(const auto &message : chat_marks.reactions)
		{
			for(const auto &kind : message.second)
			{
				if(std::find(kind.second.begin(), kind.second.end(), slot) == kind.second.end())
					continue;
				ChatMark m;
				m.kind = ChatMarkKind::Reaction;
				m.slot = slot;
				m.seq = message.first;
				m.on = false;
				m.emoji = kind.first;
				gone.push_back(m);
			}
		}
		for(const ChatMark &m : gone)
			ChangeMark(m, NowMs(), d);
	}
	if(avatar.Valid() && avatar != own_avatar)
	{
		bool still_used = false;
		for(auto &entry : guests)
			still_used = still_used || entry.second->avatar == avatar;
		if(!still_used)
			blobs.Forget(avatar.hash);
	}
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

	switch(CheckAdmission(hello, false, now_ms, d))
	{
		case Admission::In:
			break;
		case Admission::Wait: {
			// It keeps asking; this tells it that somebody is deciding, so it waits longer
			std::vector<uint8_t> packet;
			if(SealHandshakePacket(welcome_key, PacketType::Waiting, hello.guest_nonce.data(), kHandshakeNonceSize, packet))
				socket.SendTo(packet.data(), packet.size(), from);
			return;
		}
		case Admission::Refused:
			reject(hello.guest_nonce.data(), RejectReason::Refused);
			return;
		case Admission::Locked:
			reject(hello.guest_nonce.data(), RejectReason::Locked);
			return;
	}

	// The same player joining again (its connection broke, or the host came back):
	// it takes its old place over instead of sitting next to its own ghost
	uint8_t previous_slot = 0;
	if(hello.player_id)
	{
		for(auto &entry : guests)
		{
			if(entry.second->player_id == hello.player_id)
			{
				previous_slot = entry.second->slot;
				RemoveGuest(entry.first, d, "is joining again");
				break;
			}
		}
	}

	if(guests.size() >= config.max_guests)
	{
		reject(hello.guest_nonce.data(), RejectReason::Full);
		return;
	}

	auto g = std::make_unique<Guest>();
	g->slot = FreeSlot(previous_slot);
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
	g->battery = hello.battery;
	g->player_id = hello.player_id;
	g->avatar = hello.avatar;
	g->media = hello.media & kMediaOwnMask;
	g->can_control = StartsInControl(hello.player_id);
	g->key_mouse_allowed = key_mouse_for_newcomers;
	if(hello.player_id)
		admitted_players.insert(hello.player_id);

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
	// A STUN server telling this socket's address on the internet
	if(meet_on && stun.Handle(data, size, from, now_ms))
		return;

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
	if(g.web_peer)
		return; // a guest in a browser is not reached over this socket

	std::vector<uint8_t> payload;
	if(!OpenPacket(g.keys.guest_to_host, data, size, payload))
		return;
	if(!g.replay.Accept(header.counter))
		return;

	// Authenticated and fresh: follow the guest if its address changed (NAT rebinding, network switch)
	g.addr = from;
	g.last_rx_ms = now_ms;

	if(!g.confirmed)
		ConfirmGuest(g, now_ms, d);
	HandleGuestPacket(g, header.type, payload.data(), payload.size(), now_ms, d);
}

uint8_t Host::FreeSlot(uint8_t preferred)
{
	if(preferred && preferred <= config.max_guests && !FindBySlot(preferred))
		return preferred;
	for(uint8_t slot = 1; slot <= config.max_guests; slot++)
	{
		if(!FindBySlot(slot))
			return slot;
	}
	return 0;
}

void Host::ConfirmGuest(Guest &g, uint64_t now_ms, Deferred &d)
{
	g.confirmed = true;
	g.awaiting_key = true;
	mixer.SetSource(g.slot, PadState(), now_ms);
	mixer.SetMask(g.slot, g.can_control ? InputMask::All() : InputMask::None());
	d.guests_changed = true;
	if(g.web_peer)
		d.logs.push_back(g.name + " joined from a web browser" + (g.web_address.empty() ? std::string() : " at " + g.web_address));
	else
		d.logs.push_back(g.name + " joined from " + g.addr.ToString());
	blobs.Want(g.avatar);
	if(g.want & kWantVideo)
	{
		SendVideoConfig(g);
		RequestKeyframe(now_ms, d);
	}
	state_seq++;
	BroadcastState(now_ms);
	// What was said lately, so that the newcomer knows what the others are talking about
	size_t skip = chat_log.size() > kChatHistorySize ? chat_log.size() - kChatHistorySize : 0;
	for(size_t i = skip; i < chat_log.size(); i++)
		SendChatTo(g, chat_log[i].seq, now_ms, true);
	// And who read how far, and the reactions to it
	for(const auto &read : chat_marks.read)
	{
		ChatMark m;
		m.kind = ChatMarkKind::Read;
		m.slot = read.first;
		m.seq = read.second;
		SendMarkTo(g, m, now_ms);
	}
	for(const auto &message : chat_marks.reactions)
	{
		for(const auto &kind : message.second)
		{
			for(uint8_t slot : kind.second)
			{
				ChatMark m;
				m.kind = ChatMarkKind::Reaction;
				m.slot = slot;
				m.seq = message.first;
				m.emoji = kind.first;
				SendMarkTo(g, m, now_ms);
			}
		}
	}
}

bool Host::HandleGuestPacket(Guest &g, PacketType type, const uint8_t *payload, size_t size, uint64_t now_ms, Deferred &d)
{
	ByteReader r(payload, size);
	switch(type)
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
			// Each on a controller of their own: a guest's input changes no mix, but its controller
			if(mixer.Mode() == MixMode::EachOwn && state != mixer.Raw(g.slot, now_ms))
				d.input_changed = true;
			mixer.SetSource(g.slot, state, now_ms);
			break;
		}
		case PacketType::WakeUp: {
			// Only while the console sleeps, and only where the host lets guests wake it
			if(config.guests_may_wake && stream.console_asleep && g.confirmed && !d.wake_requested)
			{
				d.wake_requested = true;
				d.wake_slot = g.slot;
				d.wake_name = g.name;
			}
			break;
		}
		case PacketType::KeyMouse: {
			uint32_t seq = r.U32();
			KeyMouseState state;
			if(!r.ok() || !ReadKeyMouseState(r, state))
				break;
			if(g.have_key_mouse && static_cast<int32_t>(seq - g.last_key_mouse_seq) <= 0)
				break; // older than what we already have, or the same again
			KeyMouseUpdate u;
			u.slot = g.slot;
			// The first one only says where its totals start
			if(g.have_key_mouse)
			{
				u.dx = state.move_x - g.key_mouse.move_x;
				u.dy = state.move_y - g.key_mouse.move_y;
				u.wheel_y = state.wheel_y - g.key_mouse.wheel_y;
				u.wheel_x = state.wheel_x - g.key_mouse.wheel_x;
			}
			g.have_key_mouse = true;
			g.last_key_mouse_seq = seq;
			g.key_mouse = state;
			u.state = state;
			if(g.key_mouse_allowed)
				d.key_mouse.push_back(u);
			break;
		}
		case PacketType::Ping: {
			uint32_t ping_id = r.U32();
			uint64_t sent_us = r.U64();
			uint8_t want = r.U8() & kWantMask;
			uint16_t rtt = r.U16();
			if(!r.ok())
				break;
			ControllerKind kind = ControllerKindFromWire(r.U8());
			uint8_t media = r.U8() & kMediaOwnMask;
			uint8_t battery = r.U8();
			if(!r.ok())
				break;
			// Newer guests add their Twitch login; older ones end here
			std::string twitch;
			if(r.remaining() > 0)
			{
				twitch = r.Str8();
				if(!r.ok() || !ValidTwitchLogin(twitch))
					twitch.clear();
			}
			if(twitch != g.twitch)
			{
				g.twitch = twitch;
				d.guests_changed = true;
			}
			g.rtt_ms = rtt;
			if(kind != g.controller || media != g.media || battery != g.battery)
			{
				g.battery = battery;
				if((g.media & kMediaCamera) && !(media & kMediaCamera))
					g.cam.Reset();
				g.controller = kind;
				g.media = media;
				d.guests_changed = true;
				state_seq++;
				BroadcastState(now_ms);
			}
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
		case PacketType::VoiceUp: {
			uint32_t seq = r.U32();
			if(!r.ok() || r.remaining() == 0 || r.remaining() > kMaxVoiceFrameSize)
				break;
			if(g.silenced || !g.voice_seq.Accept(seq, now_ms))
				break;
			// To everybody else as it is, under the speaker's own numbering
			std::vector<uint8_t> out;
			out.reserve(5 + r.remaining());
			ByteWriter w(out);
			w.U8(g.slot);
			w.U32(seq);
			w.Bytes(r.cursor(), r.remaining());
			for(auto &entry : guests)
			{
				Guest &other = *entry.second;
				if(&other != &g && other.confirmed && (other.want & kWantVoice))
					SendToGuest(other, PacketType::Voice, out);
			}
			d.voice.emplace_back(g.slot, std::vector<uint8_t>(r.cursor(), r.cursor() + r.remaining()));
			break;
		}
		case PacketType::CamUp: {
			CamFragmentHeader h;
			if(!ReadCamFragmentHeader(r, h))
				break;
			std::vector<uint8_t> out;
			out.reserve(1 + size);
			out.push_back(g.slot);
			out.insert(out.end(), payload, payload + size);
			for(auto &entry : guests)
			{
				Guest &other = *entry.second;
				if(&other != &g && other.confirmed && (other.want & kWantCams))
					SendToGuest(other, PacketType::Cam, out);
			}
			if(g.cam.OnFragment(h, r.cursor(), r.remaining(), now_ms))
				d.cams.emplace_back(g.slot, g.cam.Frame());
			break;
		}
		case PacketType::Nack:
			HandleNack(g, payload, size);
			break;
		case PacketType::ChatUp:
			HandleChatUp(g, r, now_ms, d);
			break;
		case PacketType::ChatAck: {
			uint32_t seq = r.U32();
			if(r.ok())
				g.chat_pending.erase(seq);
			break;
		}
		case PacketType::ChatSeen: {
			const uint32_t seq = r.U32();
			if(!r.ok() || !g.confirmed || seq == 0 || seq > chat_seq)
				break;
			const auto at = chat_marks.read.find(g.slot);
			if(at != chat_marks.read.end() && at->second >= seq)
				break;
			ChatMark m;
			m.kind = ChatMarkKind::Read;
			m.slot = g.slot;
			m.seq = seq;
			ChangeMark(m, now_ms, d);
			break;
		}
		case PacketType::ChatReactUp: {
			ChatReaction react;
			if(!ReadChatReaction(r, react) || !g.confirmed)
				break;
			// Only messages still in the log
			if(std::none_of(chat_log.begin(), chat_log.end(), [&](const ChatMessage &m) { return m.seq == react.seq; }))
				break;
			ChatMark m;
			m.kind = ChatMarkKind::Reaction;
			m.slot = g.slot;
			m.seq = react.seq;
			m.on = react.on && ReactionAllowed(react.seq, g.slot, react.emoji);
			m.emoji = react.emoji;
			// Nothing changed (it came again, or it is over the limits): this guest is told how it
			// stands, so that it stops sending it
			if(!ChangeMark(m, now_ms, d))
				SendMarkTo(g, m, now_ms);
			break;
		}
		case PacketType::ChatMarkAck: {
			const uint32_t mark = r.U32();
			if(r.ok())
				g.mark_pending.erase(mark);
			break;
		}
		case PacketType::ChatDeleteUp: {
			const uint32_t seq = r.U32();
			if(!r.ok() || seq == 0 || !g.confirmed)
				break;
			DeleteFromLog(seq, g.slot, &g, now_ms, d);
			break;
		}
		case PacketType::KeyframeRequest:
			if(g.want & kWantVideo)
				RequestKeyframe(now_ms, d);
			break;
		case PacketType::BlobRequest: {
			BlobHash hash;
			uint16_t index;
			if(!ReadBlobRequest(r, hash, index))
				break;
			BlobData data = blobs.Get(hash);
			if(!data)
				data = chat_blobs.Get(hash);
			std::vector<uint8_t> chunk;
			if(data && BuildBlobChunk(hash, *data, index, chunk))
				SendToGuest(g, PacketType::BlobChunk, chunk);
			break;
		}
		case PacketType::BlobChunk: {
			BlobChunkHeader chunk;
			if(!ReadBlobChunkHeader(r, chunk))
				break;
			// A guest can only hand in its own picture, or the preview picture of a message it wrote
			if(!g.avatar.Valid() || chunk.hash != g.avatar.hash)
			{
				auto from = chat_blob_from.find(chunk.hash);
				if(from == chat_blob_from.end() || from->second != g.conn_id || !chat_blobs.Wanted(chunk.hash))
					break;
				if(chat_blobs.OnChunk(chunk, r.cursor(), r.remaining()))
				{
					BlobRef ref;
					ref.hash = chunk.hash;
					ref.size = static_cast<uint16_t>(chunk.total_size);
					d.chat_images.push_back(ref);
				}
				break;
			}
			if(blobs.OnChunk(chunk, r.cursor(), r.remaining()))
			{
				// Complete: from now on the others are told about it
				d.guests_changed = true;
				state_seq++;
				BroadcastState(now_ms);
			}
			break;
		}
		case PacketType::Bye:
		{
			// Only what this host allowed and told the guest it may ask for
			bool sleep = config.guests_may_sleep && g.confirmed && size >= 1
				&& payload[0] == static_cast<uint8_t>(ByeReason::Sleep);
			if(sleep && !d.sleep_requested)
			{
				d.sleep_requested = true;
				d.sleep_slot = g.slot;
				d.sleep_name = g.name;
			}
			RemoveGuest(g.conn_id, d, sleep ? "left and asked for the console to sleep" : "left");
			return false; // g is gone
		}
		default:
			break;
	}
	return true;
}

void Host::HandleNack(Guest &g, const uint8_t *payload, size_t size)
{
	if(!(g.want & kWantVideo))
		return;
	std::vector<NackEntry> entries;
	ByteReader r(payload, size);
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
	TickPending(now_ms, d);
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
	SendInputs(now_ms);
	PollBlobs(now_ms);
	ResendChats(now_ms);

	// Right after a change the state goes out a few times in quick succession, so that
	// a lost packet does not leave a controller rumbling for long
	uint32_t interval = now_ms < fast_state_until_ms ? kFastStateIntervalMs : config.state_interval_ms;
	for(auto &entry : guests)
	{
		Guest &g = *entry.second;
		if(g.confirmed && now_ms - g.last_state_ms >= interval)
			SendState(g, now_ms);
	}

	if(meet_on)
		TickMeet(now_ms, d);
	if(web)
		TickWeb(now_ms, d);
}

// ------------------------------------------------------------------ joining without a forwarded port

void Host::TickMeet(uint64_t now_ms, Deferred &d)
{
	std::vector<Address> servers;
	if(stun_resolver.Take(servers))
	{
		if(servers.empty())
			d.logs.push_back("The STUN server could not be found: friends are not told this computer's address on the internet");
		stun.SetServers(servers);
	}
	stun.Tick(socket, now_ms);

	// The note says where friends should send to. It is written again when an address in it changes.
	bool first_due = !have_note && (stun.Public().IsValid() || config.meet.stun_servers.empty()
		|| ElapsedMs(now_ms, meet_started_ms) >= kFirstNoteWaitMs);
	bool check_due = have_note && (stun.Generation() != note_stun_generation
		|| ElapsedMs(now_ms, note_checked_ms) >= kOwnAddressCheckMs);
	if(first_due || check_due)
	{
		note_stun_generation = stun.Generation();
		note_checked_ms = now_ms;
		MeetNote note;
		note.role = MeetRole::Host;
		note.peer_id = meet_id;
		note.candidates = GatherCandidates(stun, socket, config.meet.own_addresses);
		if(!have_note || !note.SameAddresses(last_note))
		{
			note.seq = last_note.seq + 1;
			last_note = note;
			have_note = true;
			meet_worker->SetNote(note);
		}
	}

	// Sending to a guest's addresses makes this side's router let the guest's packets in
	for(auto it = punches.begin(); it != punches.end();)
	{
		if(now_ms >= it->until_ms)
		{
			it = punches.erase(it);
			continue;
		}
		if(now_ms >= it->next_ms)
		{
			it->next_ms = now_ms + kPunchIntervalMs;
			std::vector<uint8_t> body;
			ByteWriter w(body);
			w.U64(it->peer_id);
			std::vector<uint8_t> packet;
			if(SealHandshakePacket(welcome_key, PacketType::Punch, body.data(), body.size(), packet))
				socket.SendTo(packet.data(), packet.size(), it->to);
		}
		++it;
	}

	if(ElapsedMs(now_ms, reach_checked_ms) >= kReachCheckMs)
	{
		reach_checked_ms = now_ms;
		HostReach current = BuildReach();
		if(!(current == reach))
		{
			// Addresses stay out of the log: logs get passed around
			if(current.meet != reach.meet || current.meet_error != reach.meet_error)
			{
				if(current.meet == MeetState::Ok)
					d.logs.push_back("The meeting point answers: friends can find this session without a forwarded port");
				else if(current.meet == MeetState::Failed)
					d.logs.push_back("The meeting point cannot be asked (" + current.meet_error + "): friends need the forwarded port");
			}
			if(current.mapping != reach.mapping && current.mapping == PortMapping::PerDestination)
				d.logs.push_back("This router gives every connection a new port (symmetric NAT): only some friends get in without a forwarded port");
			if(current.mapping != reach.mapping && current.mapping == PortMapping::Stable)
				d.logs.push_back("This router keeps one port for the session: hole punching can work");
			reach = current;
			d.reach_changed = true;
		}
	}
}

HostReach Host::BuildReach()
{
	HostReach r;
	r.meet = meet_worker ? meet_worker->State() : MeetState::Off;
	if(r.meet == MeetState::Failed)
		r.meet_error = meet_worker->Error();
	Address seen = stun.Public();
	if(seen.IsValid())
		r.public_address = seen.ToString();
	r.stun_silent = stun.GaveUp();
	r.mapping = stun.Mapping();
	return r;
}

HostReach Host::Reach()
{
	std::lock_guard<std::mutex> lock(mutex);
	return reach;
}

void Host::OnMeetNotes(const std::vector<MeetNote> &notes)
{
	bool newcomer = false;
	{
		std::lock_guard<std::mutex> lock(mutex);
		uint64_t now_ms = NowMs();
		for(const MeetNote &note : notes)
		{
			if(note.role != MeetRole::Guest)
				continue;
			auto known = punched.find(note.peer_id);
			if(known != punched.end() && known->second == note.seq)
				continue; // this note has been acted on
			if(known == punched.end())
			{
				if(punched.size() >= kMaxPunchedGuests)
					punched.clear();
				newcomer = true;
			}
			punched[note.peer_id] = note.seq;
			for(const MeetCandidate &c : note.candidates)
			{
				bool running_already = false;
				for(Punching &p : punches)
				{
					if(p.peer_id == note.peer_id && p.to.SameEndpoint(c.address))
					{
						p.until_ms = now_ms + config.meet.punch_ms;
						running_already = true;
					}
				}
				if(running_already || punches.size() >= kMaxPunches)
					continue;
				Punching p;
				p.to = c.address;
				p.peer_id = note.peer_id;
				p.until_ms = now_ms + config.meet.punch_ms;
				p.next_ms = now_ms;
				punches.push_back(p);
			}
		}
	}
	if(newcomer && callbacks.on_log)
		callbacks.on_log("A friend is on the way in through the meeting point");
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
	// A controller of its own: its own rumble and player number. The numbers count on from
	// the shared ones, so that a guest notices every change either way.
	auto own = guest_feedback.find(g.slot);
	if(s.mix_mode == MixMode::EachOwn && own != guest_feedback.end())
	{
		if(own->second.rumble)
		{
			s.feedback.rumble_seq = own->second.rumble_seq;
			s.feedback.rumble_left = own->second.rumble_left;
			s.feedback.rumble_right = own->second.rumble_right;
		}
		if(own->second.player_index)
		{
			s.feedback.player_index_seq = own->second.player_index_seq;
			s.feedback.player_index = own->second.index;
		}
	}

	Participant host;
	host.slot = 0;
	host.flags = static_cast<uint8_t>(kParticipantHost | kParticipantCanControl | kParticipantMaySleep
		| (host_in_control ? kParticipantInControl : 0));
	host.name = config.name;
	host.avatar = own_avatar;
	host.controller = config.controller;
	host.battery = config.battery;
	host.media = config.media;
	host.player = s.mix_mode == MixMode::EachOwn ? 1 : 0;
	s.participants.push_back(host);
	for(auto &entry : guests)
	{
		const Guest &other = *entry.second;
		if(!other.confirmed)
			continue;
		Participant p;
		p.slot = other.slot;
		p.flags = static_cast<uint8_t>((other.can_control ? kParticipantCanControl : 0) | (other.in_control ? kParticipantInControl : 0)
			| (config.guests_may_sleep ? kParticipantMaySleep : 0) | (other.key_mouse_allowed ? kParticipantKeyMouse : 0)
			| (config.guests_may_wake ? kParticipantMayWake : 0) | (other.web_peer ? kParticipantWeb : 0));
		p.name = other.name;
		p.controller = other.controller;
		p.battery = other.battery;
		p.media = static_cast<uint8_t>(other.media | (other.silenced ? kMediaSilenced : 0));
		if(s.mix_mode == MixMode::EachOwn)
		{
			auto player = guest_feedback.find(other.slot);
			if(player != guest_feedback.end() && player->second.player_index)
				p.player = static_cast<uint8_t>(player->second.index + 1);
		}
		// Only once it is here completely, or the others would ask for it in vain
		if(blobs.Has(other.avatar))
			p.avatar = other.avatar;
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
	// Also while not running: the parameter sets usually arrive before sharing starts
	if(!data || !size || size > kMaxVideoFrameSize)
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
		if(g.web_peer && WebHoldVideo(g, NowMs()))
			continue; // more would only make the pile in front of its connection higher
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

void Host::PushVoice(const uint8_t *data, size_t size)
{
	if(!data || !size || size > kMaxVoiceFrameSize || !running)
		return;
	std::lock_guard<std::mutex> lock(mutex);
	if(guests.empty())
		return;
	std::vector<uint8_t> payload;
	payload.reserve(size + 5);
	ByteWriter w(payload);
	w.U8(0); // the host's slot
	w.U32(++voice_seq);
	w.Bytes(data, size);
	for(auto &entry : guests)
	{
		Guest &g = *entry.second;
		if(g.confirmed && (g.want & kWantVoice))
			SendToGuest(g, PacketType::Voice, payload);
	}
}

void Host::PushCamFrame(const uint8_t *data, size_t size)
{
	if(!data || !size || size > kMaxCamFrameSize || !running)
		return;
	std::lock_guard<std::mutex> lock(mutex);
	bool any = false;
	for(auto &entry : guests)
		any = any || (entry.second->confirmed && (entry.second->want & kWantCams));
	if(!any)
		return;
	uint32_t seq = ++cam_seq;
	uint32_t count = CamFragmentCount(size);
	std::vector<uint8_t> fragment;
	std::vector<uint8_t> payload;
	for(uint32_t i = 0; i < count; i++)
	{
		if(!BuildCamFragment(seq, data, size, i, fragment))
			return;
		payload.clear();
		payload.push_back(0); // the host's slot
		payload.insert(payload.end(), fragment.begin(), fragment.end());
		for(auto &entry : guests)
		{
			Guest &g = *entry.second;
			if(g.confirmed && (g.want & kWantCams))
				SendToGuest(g, PacketType::Cam, payload);
		}
	}
}

void Host::SetLocalMedia(uint8_t media)
{
	media &= kMediaOwnMask;
	std::lock_guard<std::mutex> lock(mutex);
	if(config.media == media)
		return;
	config.media = media;
	state_seq++;
	BroadcastState(NowMs());
}

uint8_t Host::LocalMedia()
{
	std::lock_guard<std::mutex> lock(mutex);
	return config.media;
}

void Host::SetGuestSilenced(uint8_t slot, bool silenced)
{
	Deferred d;
	{
		std::lock_guard<std::mutex> lock(mutex);
		Guest *g = FindBySlot(slot);
		if(!g || g->silenced == silenced)
			return;
		g->silenced = silenced;
		state_seq++;
		BroadcastState(NowMs());
		d.guests_changed = true;
	}
	Fire(d);
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

void Host::SetGuestRumble(uint8_t slot, uint8_t left, uint8_t right)
{
	std::lock_guard<std::mutex> lock(mutex);
	GuestFeedback &f = guest_feedback[slot];
	if(f.rumble && f.rumble_left == left && f.rumble_right == right)
		return;
	f.rumble = true;
	f.rumble_left = left;
	f.rumble_right = right;
	// One counter for both, so that the guest sees a change whichever it got before
	BumpSeq(feedback.rumble_seq);
	f.rumble_seq = feedback.rumble_seq;
	state_seq++;
	Guest *g = FindBySlot(slot);
	if(g && g->confirmed)
		SendState(*g, NowMs());
}

void Host::SetGuestPlayerIndex(uint8_t slot, uint8_t index)
{
	std::lock_guard<std::mutex> lock(mutex);
	GuestFeedback &f = guest_feedback[slot];
	const bool has = index != kNoPlayer;
	if(f.player_index == has && (!has || f.index == index))
		return;
	f.player_index = has;
	f.index = has ? index : 0;
	BumpSeq(feedback.player_index_seq);
	f.player_index_seq = feedback.player_index_seq;
	state_seq++;
	// Everybody's list of players says who is which
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

PadState Host::GuestsInput()
{
	std::lock_guard<std::mutex> lock(mutex);
	uint64_t now = NowMs();
	mixer.Mix(now); // who is in control right now
	return mixer.MixWithout(InputMixer::kHostSource, now);
}

PadState Host::GuestInput(uint8_t slot)
{
	std::lock_guard<std::mutex> lock(mutex);
	Guest *g = FindBySlot(slot);
	if(!g || !g->confirmed || !g->can_control)
		return PadState();
	return mixer.Player(slot, NowMs());
}

void Host::SetController(ControllerKind kind)
{
	std::lock_guard<std::mutex> lock(mutex);
	if(config.controller == kind)
		return;
	config.controller = kind;
	state_seq++;
	BroadcastState(NowMs());
}

void Host::SetBattery(uint8_t battery)
{
	std::lock_guard<std::mutex> lock(mutex);
	if(config.battery == battery)
		return;
	config.battery = battery;
	state_seq++;
	BroadcastState(NowMs());
}

void Host::SetGuestsMayWake(bool allowed)
{
	std::lock_guard<std::mutex> lock(mutex);
	if(config.guests_may_wake == allowed)
		return;
	config.guests_may_wake = allowed;
	state_seq++;
	BroadcastState(NowMs());
}

void Host::SetGuestsMaySleep(bool allowed)
{
	std::lock_guard<std::mutex> lock(mutex);
	if(config.guests_may_sleep == allowed)
		return;
	config.guests_may_sleep = allowed;
	state_seq++;
	BroadcastState(NowMs());
}

bool Host::GuestsMaySleep()
{
	std::lock_guard<std::mutex> lock(mutex);
	return config.guests_may_sleep;
}

void Host::SetMaxGuests(size_t max_guests)
{
	std::lock_guard<std::mutex> lock(mutex);
	config.max_guests = std::max<size_t>(1, std::min(max_guests, kMaxGuests));
}

size_t Host::MaxGuests()
{
	std::lock_guard<std::mutex> lock(mutex);
	return config.max_guests;
}

void Host::SetSecret(const Secret &secret)
{
	Deferred d;
	{
		std::lock_guard<std::mutex> lock(mutex);
		if(secret == config.secret)
			return;
		config.secret = secret;
		hello_key = DeriveHelloKey(secret);
		welcome_key = DeriveWelcomeKey(secret);
		// Who knocks with the old invite does not get in with it; who is in stays
		if(!knocks.empty())
		{
			knocks.clear();
			d.pending_changed = true;
		}
		// Addresses met through the old room are not sent to any more
		punches.clear();
		d.logs.push_back("New invite: the ones handed out before do not get in any more");
	}
	// Set before the network thread started and kept until it has ended
	if(meet_worker)
		meet_worker->SetSecret(secret);
	Fire(d);
	// Browsers on their way in had their answers sealed with the old key
	OnWebAnswers();
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

void Host::SetGuestKeyMouse(uint8_t slot, bool allowed)
{
	Deferred d;
	{
		std::lock_guard<std::mutex> lock(mutex);
		Guest *g = FindBySlot(slot);
		if(!g || g->key_mouse_allowed == allowed)
			return;
		g->key_mouse_allowed = allowed;
		if(!allowed && g->have_key_mouse)
		{
			KeyMouseUpdate gone;
			gone.slot = slot;
			gone.gone = true;
			d.key_mouse.push_back(gone);
		}
		state_seq++;
		BroadcastState(NowMs());
		d.guests_changed = true;
	}
	Fire(d);
}

void Host::SetKeyMouseForNewcomers(bool allowed)
{
	std::lock_guard<std::mutex> lock(mutex);
	key_mouse_for_newcomers = allowed;
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
		// Holds when the player comes back, whatever the join policy says
		if(g->player_id)
			control_choice[g->player_id] = allowed;
		mixer.SetMask(slot, allowed ? InputMask::All() : InputMask::None());
		uint64_t now = NowMs();
		state_seq++;
		BroadcastState(now);
		UpdateControl(now, d);
		d.guests_changed = true;
	}
	Fire(d);
}

void Host::KickGuest(uint8_t slot, bool block)
{
	Deferred d;
	{
		std::lock_guard<std::mutex> lock(mutex);
		Guest *g = FindBySlot(slot);
		if(!g)
			return;
		if(block && g->player_id)
		{
			refused_players.insert(g->player_id);
			admitted_players.erase(g->player_id);
		}
		SendBye(*g, ByeReason::Kicked);
		SendBye(*g, ByeReason::Kicked);
		RemoveGuest(g->conn_id, d, block ? "was removed and blocked" : "was removed");
	}
	Fire(d);
}

// ------------------------------------------------------------------ who gets in

/** A player that sends no id is told apart by its nonce, which stays the same while it knocks. */
static uint64_t KnockKey(const HelloPayload &hello)
{
	if(hello.player_id)
		return hello.player_id;
	uint64_t key = 0;
	std::memcpy(&key, hello.guest_nonce.data(), sizeof(key));
	return key | 1;
}

Host::Admission Host::CheckAdmission(const HelloPayload &hello, bool web_guest, uint64_t now_ms, Deferred &d)
{
	const uint64_t player = KnockKey(hello);
	if(refused_players.count(player))
		return Admission::Refused;
	// Who is in, or was in this session, comes back without being asked again
	bool known = admitted_players.count(player) > 0;
	for(auto &entry : guests)
		known = known || (hello.player_id && entry.second->player_id == hello.player_id);
	if(known)
		return Admission::In;
	if(locked)
		return Admission::Locked;
	if(config.join_policy != JoinPolicy::Ask)
		return Admission::In;

	for(Knock &k : knocks)
	{
		if(k.player == player)
		{
			k.last_ms = now_ms;
			return Admission::Wait;
		}
	}
	Knock k;
	k.id = next_knock_id++;
	k.player = player;
	k.name = hello.name.empty() ? std::string("Somebody") : CleanName(hello.name, 1);
	k.web = web_guest;
	k.last_ms = now_ms;
	d.logs.push_back(k.name + (web_guest ? " asks to join from a web browser" : " asks to join"));
	knocks.push_back(k);
	d.pending_changed = true;
	return Admission::Wait;
}

bool Host::StartsInControl(uint64_t player_id) const
{
	if(player_id)
	{
		auto choice = control_choice.find(player_id);
		if(choice != control_choice.end())
			return choice->second;
	}
	return config.join_policy != JoinPolicy::WatchFirst;
}

void Host::TickPending(uint64_t now_ms, Deferred &d)
{
	// Who stopped knocking has given up, or its app is gone
	size_t before = knocks.size();
	knocks.erase(std::remove_if(knocks.begin(), knocks.end(), [&](const Knock &k) {
		return now_ms - k.last_ms > config.pending_timeout_ms;
	}), knocks.end());
	if(knocks.size() != before)
		d.pending_changed = true;
}

void Host::SetJoinPolicy(JoinPolicy policy)
{
	Deferred d;
	{
		std::lock_guard<std::mutex> lock(mutex);
		if(config.join_policy == policy)
			return;
		config.join_policy = policy;
		// Nobody is asked any more: who waits gets in at its next Hello
		if(policy != JoinPolicy::Ask && !knocks.empty())
		{
			for(const Knock &k : knocks)
				admitted_players.insert(k.player);
			knocks.clear();
			d.pending_changed = true;
		}
	}
	Fire(d);
}

JoinPolicy Host::GetJoinPolicy()
{
	std::lock_guard<std::mutex> lock(mutex);
	return config.join_policy;
}

std::vector<PendingGuest> Host::Pending()
{
	std::lock_guard<std::mutex> lock(mutex);
	std::vector<PendingGuest> out;
	for(const Knock &k : knocks)
	{
		PendingGuest p;
		p.id = k.id;
		p.name = k.name;
		p.web = k.web;
		out.push_back(p);
	}
	return out;
}

void Host::Admit(uint64_t pending_id, bool allow)
{
	Deferred d;
	{
		std::lock_guard<std::mutex> lock(mutex);
		auto it = std::find_if(knocks.begin(), knocks.end(), [&](const Knock &k) { return k.id == pending_id; });
		if(it == knocks.end())
			return;
		if(allow)
			admitted_players.insert(it->player);
		else
			refused_players.insert(it->player);
		d.logs.push_back(it->name + (allow ? " was let in" : " was not let in"));
		knocks.erase(it);
		d.pending_changed = true;
	}
	Fire(d);
}

void Host::SetLocked(bool closed)
{
	std::lock_guard<std::mutex> lock(mutex);
	locked = closed;
}

bool Host::Locked()
{
	std::lock_guard<std::mutex> lock(mutex);
	return locked;
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
		info.address = g.web_peer ? g.web_address : g.addr.ToString();
		info.web = g.web_peer != 0;
		info.want = g.want;
		info.controller = g.controller;
		info.battery = g.battery;
		info.twitch = g.twitch;
		info.can_control = g.can_control;
		info.in_control = g.in_control;
		info.key_mouse = g.key_mouse_allowed;
		info.rtt_ms = g.rtt_ms;
		info.media = static_cast<uint8_t>(g.media | (g.silenced ? kMediaSilenced : 0));
		if(blobs.Has(g.avatar))
			info.avatar = g.avatar;
		out.push_back(info);
	}
	std::sort(out.begin(), out.end(), [](const GuestInfo &a, const GuestInfo &b) { return a.slot < b.slot; });
	return out;
}

void Host::BuildInputs(uint64_t now_ms, InputsSnapshot &s)
{
	s.players.clear();
	s.players.push_back(ToPlayerInput(0, mixer.Raw(InputMixer::kHostSource, now_ms)));
	for(auto &entry : guests)
	{
		const Guest &g = *entry.second;
		if(g.confirmed)
			s.players.push_back(ToPlayerInput(g.slot, mixer.Raw(g.slot, now_ms)));
	}
	std::sort(s.players.begin(), s.players.end(),
		[](const PlayerInput &a, const PlayerInput &b) { return a.slot < b.slot; });
	s.combined = ToPlayerInput(0, mixer.Mix(now_ms));
}

void Host::SendInputs(uint64_t now_ms)
{
	bool wanted = false;
	for(auto &entry : guests)
	{
		if(entry.second->confirmed && (entry.second->want & kWantInputs))
		{
			wanted = true;
			break;
		}
	}
	if(!wanted || now_ms - last_inputs_ms < kInputsMinIntervalMs)
		return;
	InputsSnapshot s;
	BuildInputs(now_ms, s);
	if(s.SameInputs(last_inputs) && now_ms - last_inputs_ms < kInputsRefreshMs)
		return;
	s.seq = last_inputs.seq + 1;
	last_inputs = s;
	last_inputs_ms = now_ms;
	std::vector<uint8_t> payload;
	ByteWriter w(payload);
	WriteInputs(w, s);
	for(auto &entry : guests)
	{
		Guest &g = *entry.second;
		if(g.confirmed && (g.want & kWantInputs))
			SendToGuest(g, PacketType::Inputs, payload);
	}
}

void Host::PollBlobs(uint64_t now_ms)
{
	if(now_ms - last_blob_poll_ms < kBlobPollIntervalMs)
		return;
	last_blob_poll_ms = now_ms;
	std::vector<BlobStore::Request> requests;
	chat_blobs.Poll(now_ms, requests, 4);
	for(const BlobStore::Request &request : requests)
	{
		// Ask the guest whose message it came with
		auto from = chat_blob_from.find(request.hash);
		auto it = from == chat_blob_from.end() ? guests.end() : guests.find(from->second);
		if(it == guests.end() || !it->second->confirmed)
			continue;
		std::vector<uint8_t> payload;
		ByteWriter w(payload);
		WriteBlobRequest(w, request.hash, request.index);
		SendToGuest(*it->second, PacketType::BlobRequest, payload);
	}
	blobs.Poll(now_ms, requests);
	for(const BlobStore::Request &request : requests)
	{
		// Ask the guest whose picture it is
		for(auto &entry : guests)
		{
			Guest &g = *entry.second;
			if(g.confirmed && g.avatar.Valid() && g.avatar.hash == request.hash)
			{
				std::vector<uint8_t> payload;
				ByteWriter w(payload);
				WriteBlobRequest(w, request.hash, request.index);
				SendToGuest(g, PacketType::BlobRequest, payload);
				break;
			}
		}
	}
}

// ------------------------------------------------------------------ guests in a web browser

Host::Guest *Host::FindByWebPeer(uint64_t peer_id)
{
	for(auto &entry : guests)
	{
		if(entry.second->web_peer == peer_id)
			return entry.second.get();
	}
	return nullptr;
}

void Host::OnOtherNotes(const std::vector<std::string> &texts)
{
	if(!web)
		return;
	for(const std::string &text : texts)
	{
		WebOffer offer;
		Secret secret;
		{
			std::lock_guard<std::mutex> lock(mutex);
			secret = config.secret; // SetSecret() may change it
		}
		if(!OpenWebOffer(secret, text, offer))
			continue;
		{
			// A note stays at the meeting point for half a minute and is seen again and again
			std::lock_guard<std::mutex> lock(mutex);
			if(std::find(web_seen.begin(), web_seen.end(), offer.peer_id) != web_seen.end())
				continue;
			web_seen.push_back(offer.peer_id);
			if(web_seen.size() > kMaxWebSeen)
				web_seen.pop_front();
		}
		if(callbacks.on_log)
			callbacks.on_log("A friend is on the way in from a web browser");
		web->Offer(offer);
	}
}

void Host::OnWebAnswers()
{
	// On the gateway's own thread, which outlives this call: the gateway does too
	WebGateway *gateway;
	{
		std::lock_guard<std::mutex> lock(mutex);
		gateway = web.get();
	}
	if(!gateway || !meet_worker)
		return;
	WebAnswers answers = gateway->Answers();
	std::string text;
	// As many as fit into one note; the others' turn comes when these are in
	Secret secret;
	{
		std::lock_guard<std::mutex> lock(mutex);
		secret = config.secret; // SetSecret() may change it
	}
	while(!answers.answers.empty() && !SealWebAnswers(secret, answers, text))
		answers.answers.pop_back();
	if(answers.answers.empty())
		text.clear();
	meet_worker->SetExtraNote(text);
}

void Host::OnWebMessage(uint64_t peer_id, const uint8_t *data, size_t size)
{
	if(!size)
		return;
	Deferred d;
	{
		std::lock_guard<std::mutex> lock(mutex);
		if(stop_requested || !running || !web)
			return;
		uint64_t now = NowMs();
		PacketType type = static_cast<PacketType>(data[0]);
		if(type == PacketType::Hello)
			HandleWebHello(peer_id, data + 1, size - 1, now, d);
		else if(Guest *g = FindByWebPeer(peer_id))
		{
			g->last_rx_ms = now;
			HandleGuestPacket(*g, type, data + 1, size - 1, now, d);
		}
	}
	Fire(d);
}

void Host::OnWebClosed(uint64_t peer_id)
{
	Deferred d;
	{
		std::lock_guard<std::mutex> lock(mutex);
		if(Guest *g = FindByWebPeer(peer_id))
			RemoveGuest(g->conn_id, d, "left (the browser's connection ended)");
	}
	Fire(d);
}

void Host::HandleWebHello(uint64_t peer_id, const uint8_t *payload, size_t size, uint64_t now_ms, Deferred &d)
{
	auto reject = [&](RejectReason reason) {
		uint8_t frame[2 + kHandshakeNonceSize] = { 0 };
		frame[0] = static_cast<uint8_t>(PacketType::Reject);
		frame[1 + kHandshakeNonceSize] = static_cast<uint8_t>(reason);
		web->Send(peer_id, frame, sizeof(frame));
		web_closing.emplace_back(peer_id, now_ms + kWebCloseDelayMs);
	};

	ByteReader r(payload, size);
	uint8_t version = r.U8();
	if(!r.ok())
		return;
	if(version != kProtocolVersion)
	{
		reject(RejectReason::Version);
		return;
	}
	HelloPayload hello;
	if(!ReadHello(r, hello))
		return;

	// The page repeats its Hello until it hears a Welcome
	if(Guest *known = FindByWebPeer(peer_id))
	{
		web->Send(peer_id, known->welcome_packet.data(), known->welcome_packet.size());
		return;
	}

	switch(CheckAdmission(hello, true, now_ms, d))
	{
		case Admission::In:
			break;
		case Admission::Wait: {
			uint8_t frame[1 + kHandshakeNonceSize] = { 0 };
			frame[0] = static_cast<uint8_t>(PacketType::Waiting);
			web->Send(peer_id, frame, sizeof(frame));
			return;
		}
		case Admission::Refused:
			reject(RejectReason::Refused);
			return;
		case Admission::Locked:
			reject(RejectReason::Locked);
			return;
	}

	// The same player joining again takes its old place over, as in HandleHello()
	uint8_t previous_slot = 0;
	if(hello.player_id)
	{
		for(auto &entry : guests)
		{
			if(entry.second->player_id == hello.player_id)
			{
				previous_slot = entry.second->slot;
				RemoveGuest(entry.first, d, "is joining again");
				break;
			}
		}
	}
	if(guests.size() >= config.max_guests)
	{
		reject(RejectReason::Full);
		return;
	}

	auto g = std::make_unique<Guest>();
	g->slot = FreeSlot(previous_slot);
	if(g->slot == 0)
	{
		reject(RejectReason::Full);
		return;
	}
	do
	{
		RandomBytes(reinterpret_cast<uint8_t *>(&g->conn_id), sizeof(g->conn_id));
	} while(g->conn_id == 0 || guests.count(g->conn_id));
	g->web_peer = peer_id;
	g->web_address = web->PeerAddress(peer_id);
	g->name = CleanName(hello.name, g->slot);
	g->created_ms = now_ms;
	g->last_rx_ms = now_ms;
	g->want = hello.want;
	g->controller = hello.controller;
	g->battery = hello.battery;
	g->player_id = hello.player_id;
	g->avatar = hello.avatar;
	g->media = hello.media & kMediaOwnMask;
	g->can_control = StartsInControl(hello.player_id);
	g->key_mouse_allowed = key_mouse_for_newcomers;
	if(hello.player_id)
		admitted_players.insert(hello.player_id);

	WelcomePayload welcome;
	welcome.conn_id = g->conn_id;
	welcome.slot = g->slot;
	welcome.host_name = config.name;
	welcome.stream = stream;
	welcome.audio = audio;
	g->welcome_packet.push_back(static_cast<uint8_t>(PacketType::Welcome));
	ByteWriter w(g->welcome_packet);
	WriteWelcome(w, welcome);
	web->Send(peer_id, g->welcome_packet.data(), g->welcome_packet.size());

	Guest &added = *g;
	guests[added.conn_id] = std::move(g);
	// Nobody but a holder of the invite gets this far, so the guest counts at once
	ConfirmGuest(added, now_ms, d);
}

bool Host::WebHoldVideo(Guest &g, uint64_t now_ms)
{
	if(!web)
		return true; // sharing is ending
	if(!web->Buffered(g.web_peer))
		g.web_backlog_ms = 0;
	else if(!g.web_backlog_ms)
		g.web_backlog_ms = now_ms;
	if(!g.web_starved && g.web_backlog_ms && ElapsedMs(now_ms, g.web_backlog_ms) > kWebBacklogMs)
	{
		// Nothing more until what waits has gone out; then the stream starts over with a key frame
		g.web_starved = true;
		g.awaiting_key = true;
	}
	return g.web_starved;
}

void Host::TickWeb(uint64_t now_ms, Deferred &d)
{
	for(auto &entry : guests)
	{
		Guest &g = *entry.second;
		if(!g.web_peer || !g.web_starved || web->Buffered(g.web_peer))
			continue;
		g.web_starved = false;
		g.web_backlog_ms = 0;
		// Once is a hiccup. Again and again within seconds, its connection cannot carry the picture
		// as the console sends it, and each new start is a key frame for everybody: the host
		// should know, because only a lower bitrate for the session changes that
		if(!g.web_starved_since_ms || ElapsedMs(now_ms, g.web_starved_since_ms) > kWebBehindWindowMs)
		{
			g.web_starved_since_ms = now_ms;
			g.web_starved_count = 0;
		}
		g.web_starved_count++;
		if(g.web_starved_count >= kWebBehindCount && !g.web_behind_told)
		{
			g.web_behind_told = true;
			d.behind.emplace_back(g.slot, g.name);
			d.logs.push_back(g.name + "'s browser cannot keep up with the picture: it had to start over "
				+ std::to_string(g.web_starved_count) + " times within " + std::to_string(kWebBehindWindowMs / 1000) + " s");
		}
		if(g.want & kWantVideo)
			RequestKeyframe(now_ms, d);
	}
	for(auto it = web_closing.begin(); it != web_closing.end();)
	{
		if(now_ms < it->second)
		{
			++it;
			continue;
		}
		// Not if a player has come back under the same connection in the meantime
		if(!FindByWebPeer(it->first))
			web->Close(it->first);
		it = web_closing.erase(it);
	}
}

// ------------------------------------------------------------------ chat

void Host::HandleChatUp(Guest &g, ByteReader &r, uint64_t now_ms, Deferred &d)
{
	ChatMessage m;
	if(!ReadChatMessage(r, m))
		return;
	// Always acknowledged: the earlier answer may have been lost
	std::vector<uint8_t> ack;
	ByteWriter w(ack);
	w.U32(m.seq);
	SendToGuest(g, PacketType::ChatUpAck, ack);
	if(std::find(g.chat_up_seen.begin(), g.chat_up_seen.end(), m.seq) != g.chat_up_seen.end())
		return;
	g.chat_up_seen.push_back(m.seq);
	if(g.chat_up_seen.size() > kChatUpSeenSize)
		g.chat_up_seen.pop_front();

	m.up_id = m.seq;
	m.slot = g.slot;
	m.name = g.name;
	m.flags = 0;
	// An answer only to a message everybody can still see
	if(!InChatLog(m.reply))
		m.reply = 0;
	if(m.embed.image.Valid())
	{
		// The others will ask us for it, and we ask the writer
		chat_blob_from[m.embed.image.hash] = g.conn_id;
		chat_blobs.Want(m.embed.image);
		TrackChatImage(m.embed.image.hash);
	}
	PostChat(m, now_ms);
	d.chats.push_back(m);
}

void Host::PostChat(ChatMessage &m, uint64_t now_ms)
{
	m.seq = ++chat_seq;
	m.time_ms = WallClockMs();
	chat_log.push_back(m);
	if(chat_log.size() > kChatLogSize)
		chat_log.pop_front();
	while(!chat_marks.reactions.empty() && chat_marks.reactions.begin()->first < chat_log.front().seq)
		chat_marks.reactions.erase(chat_marks.reactions.begin());
	// And what was deleted that long ago
	while(!chat_marks.deleted.empty() && *chat_marks.deleted.begin() < chat_log.front().seq)
		chat_marks.deleted.erase(chat_marks.deleted.begin());
	while(!chat_deleted_by.empty() && chat_deleted_by.begin()->first < chat_log.front().seq)
		chat_deleted_by.erase(chat_deleted_by.begin());
	for(auto &entry : guests)
	{
		Guest &g = *entry.second;
		if(g.confirmed)
			SendChatTo(g, m.seq, now_ms, false);
	}
}

void Host::SendChatTo(Guest &g, uint32_t seq, uint64_t now_ms, bool history)
{
	auto it = std::find_if(chat_log.begin(), chat_log.end(), [seq](const ChatMessage &m) { return m.seq == seq; });
	if(it == chat_log.end())
	{
		g.chat_pending.erase(seq); // too old by now
		return;
	}
	ChatMessage m = *it;
	if(history)
		m.flags |= kChatHistory;
	std::vector<uint8_t> payload;
	ByteWriter w(payload);
	WriteChatMessage(w, m);
	SendToGuest(g, PacketType::Chat, payload);
	Guest::PendingChat &p = g.chat_pending[seq];
	if(!p.first_ms)
		p.first_ms = now_ms;
	p.last_ms = now_ms;
	p.history = history;
}

void Host::ResendChats(uint64_t now_ms)
{
	if(now_ms - last_chat_resend_ms < kChatResendMs / 4)
		return;
	last_chat_resend_ms = now_ms;
	for(auto &entry : guests)
	{
		Guest &g = *entry.second;
		if(!g.confirmed)
			continue;
		std::vector<std::pair<uint32_t, bool>> due;
		for(auto it = g.chat_pending.begin(); it != g.chat_pending.end();)
		{
			// A message the host itself wrote carries the time of the thread that wrote it,
			// which can be newer than now_ms
			if(ElapsedMs(now_ms, it->second.first_ms) > kChatGiveUpMs)
				it = g.chat_pending.erase(it);
			else
			{
				if(ElapsedMs(now_ms, it->second.last_ms) >= kChatResendMs)
					due.emplace_back(it->first, it->second.history);
				++it;
			}
		}
		for(const auto &item : due)
			SendChatTo(g, item.first, now_ms, item.second);
		for(auto it = g.mark_pending.begin(); it != g.mark_pending.end();)
		{
			if(ElapsedMs(now_ms, it->second.first_ms) > kChatGiveUpMs)
			{
				it = g.mark_pending.erase(it);
				continue;
			}
			if(ElapsedMs(now_ms, it->second.last_ms) >= kChatResendMs)
			{
				it->second.last_ms = now_ms;
				SendToGuest(g, PacketType::ChatMark, it->second.payload);
			}
			++it;
		}
	}
}

bool Host::ChangeMark(ChatMark m, uint64_t now_ms, Deferred &d)
{
	if(!ApplyChatMark(chat_marks, m))
		return false;
	for(auto &entry : guests)
	{
		if(entry.second->confirmed)
			SendMarkTo(*entry.second, m, now_ms);
	}
	d.chat_marks_changed = true;
	return true;
}

void Host::SendMarkTo(Guest &g, ChatMark m, uint64_t now_ms)
{
	// Every mark a number of its own: the guest applies them in that order
	m.mark = ++chat_mark_seq;
	std::vector<uint8_t> payload;
	ByteWriter w(payload);
	WriteChatMark(w, m);
	SendToGuest(g, PacketType::ChatMark, payload);
	Guest::PendingMark &p = g.mark_pending[m.mark];
	p.payload = payload;
	p.first_ms = now_ms;
	p.last_ms = now_ms;
}

bool Host::ReactionAllowed(uint32_t seq, uint8_t slot, const std::string &emoji) const
{
	const auto message = chat_marks.reactions.find(seq);
	if(message == chat_marks.reactions.end())
		return true;
	if(message->second.count(emoji))
		return true;
	if(message->second.size() >= kMaxReactionKinds)
		return false;
	size_t own = 0;
	for(const auto &kind : message->second)
		own += std::count(kind.second.begin(), kind.second.end(), slot) > 0 ? 1 : 0;
	return own < kMaxReactionsPerPlayer;
}

void Host::SetChatRead(uint32_t seq)
{
	Deferred d;
	{
		std::lock_guard<std::mutex> lock(mutex);
		const auto at = chat_marks.read.find(0);
		if(!running || seq == 0 || seq > chat_seq || (at != chat_marks.read.end() && at->second >= seq))
			return;
		ChatMark m;
		m.kind = ChatMarkKind::Read;
		m.slot = 0;
		m.seq = seq;
		ChangeMark(m, NowMs(), d);
	}
	Fire(d);
}

void Host::React(uint32_t seq, const std::string &emoji, bool on)
{
	if(!ValidReaction(emoji))
		return;
	Deferred d;
	{
		std::lock_guard<std::mutex> lock(mutex);
		if(!running || std::none_of(chat_log.begin(), chat_log.end(), [seq](const ChatMessage &m) { return m.seq == seq; }))
			return;
		ChatMark m;
		m.kind = ChatMarkKind::Reaction;
		m.slot = 0;
		m.seq = seq;
		m.on = on && ReactionAllowed(seq, 0, emoji);
		m.emoji = emoji;
		ChangeMark(m, NowMs(), d);
	}
	Fire(d);
}

void Host::DeleteChat(uint32_t seq)
{
	Deferred d;
	{
		std::lock_guard<std::mutex> lock(mutex);
		if(!running || seq == 0)
			return;
		DeleteFromLog(seq, 0, nullptr, NowMs(), d);
	}
	Fire(d);
}

bool Host::InChatLog(uint32_t seq) const
{
	return seq != 0 && std::any_of(chat_log.begin(), chat_log.end(), [seq](const ChatMessage &m) { return m.seq == seq; });
}

void Host::DeleteFromLog(uint32_t seq, uint8_t slot, Guest *asking, uint64_t now_ms, Deferred &d)
{
	ChatMark m;
	m.kind = ChatMarkKind::Deleted;
	m.slot = slot;
	m.seq = seq;
	// Deleted already (the asking came again): the writer is told, so that it stops asking
	const auto done = chat_deleted_by.find(seq);
	if(done != chat_deleted_by.end())
	{
		if(asking && done->second == slot)
			SendMarkTo(*asking, m, now_ms);
		return;
	}
	// Only the writer's own, and only while everybody can still see it
	auto it = std::find_if(chat_log.begin(), chat_log.end(), [seq](const ChatMessage &c) { return c.seq == seq; });
	if(it == chat_log.end() || it->slot != slot)
		return;
	chat_log.erase(it);
	chat_deleted_by[seq] = slot;
	// Not sent to anybody any more (a newcomer's history, a guest still waiting for it)
	for(auto &entry : guests)
		entry.second->chat_pending.erase(seq);
	ChangeMark(m, now_ms, d);
}

ChatMarks Host::GetChatMarks()
{
	std::lock_guard<std::mutex> lock(mutex);
	return chat_marks;
}

void Host::TrackChatImage(const BlobHash &hash)
{
	if(std::find(chat_blob_order.begin(), chat_blob_order.end(), hash) != chat_blob_order.end())
		return;
	chat_blob_order.push_back(hash);
	while(chat_blob_order.size() > kMaxChatImages)
	{
		chat_blobs.Forget(chat_blob_order.front());
		chat_blob_from.erase(chat_blob_order.front());
		chat_blob_order.pop_front();
	}
}

ChatMessage Host::SendChat(const std::string &text, const ChatEmbed &embed, const std::vector<uint8_t> &image, uint32_t reply)
{
	ChatMessage m;
	m.text = TruncateUtf8(text, kMaxChatText);
	m.embed = embed;
	if(m.text.empty() && !m.embed.Valid())
		return m;
	m.name = config.name;
	m.slot = 0;
	std::lock_guard<std::mutex> lock(mutex);
	if(!running)
		return m;
	m.reply = InChatLog(reply) ? reply : 0;
	if(m.embed.Valid() && !image.empty() && image.size() <= kMaxChatImageSize)
	{
		m.embed.image = chat_blobs.Put(image);
		TrackChatImage(m.embed.image.hash);
	}
	else
		m.embed.image = BlobRef();
	PostChat(m, NowMs());
	return m;
}

BlobData Host::ChatImage(const BlobHash &hash)
{
	std::lock_guard<std::mutex> lock(mutex);
	return chat_blobs.Get(hash);
}

InputsSnapshot Host::Inputs()
{
	std::lock_guard<std::mutex> lock(mutex);
	InputsSnapshot s;
	BuildInputs(NowMs(), s);
	return s;
}

BlobRef Host::OwnAvatar()
{
	std::lock_guard<std::mutex> lock(mutex);
	return own_avatar;
}

BlobData Host::Avatar(const BlobHash &hash)
{
	std::lock_guard<std::mutex> lock(mutex);
	return blobs.Get(hash);
}

bool Host::HostInControl()
{
	std::lock_guard<std::mutex> lock(mutex);
	return host_in_control;
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
