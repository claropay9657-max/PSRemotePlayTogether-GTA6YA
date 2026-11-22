// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <coplay/client.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

namespace coplay {

static const uint32_t kHelloIntervalMs = 250;
/** The first note waits this long for the STUN answer, so that it is complete. */
static const uint32_t kFirstNoteWaitMs = 500;
/** More addresses than this are not tried for one host. */
static const size_t kMaxTargets = 24;
static const uint32_t kPingIntervalMs = 500;
static const uint32_t kFastPingIntervalMs = 100;
static const uint32_t kFastPings = 5;
/** Refresh of an unchanged controller state; must stay well below the host's stale time. */
static const uint32_t kInputRefreshMs = 40;
/** A changed state is sent this many times in quick succession to survive packet loss. */
static const int kInputBurst = 3;
static const uint32_t kInputBurstSpacingMs = 4;

Client::Client(const ClientConfig &config) : config(config), assembler(config.assembler)
{
	hello_key = DeriveHelloKey(config.invite.secret);
	welcome_key = DeriveWelcomeKey(config.invite.secret);
	want = static_cast<uint8_t>(config.want & kWantMask);
	controller = static_cast<uint8_t>(config.controller);
	battery = config.battery;
	media = static_cast<uint8_t>(config.media & kMediaOwnMask);
	own_avatar = blobs.Put(config.avatar);
}

Client::~Client()
{
	Stop();
}

void Client::SetSimulatedLoss(double probability, uint32_t seed)
{
	loss_probability = probability;
	loss_seed = seed;
	if(socket)
		socket->SetSimulatedLoss(probability, seed);
}

std::string Client::HostAddress() const
{
	return host_addr.ToString();
}

bool Client::Connect(std::string *error)
{
	auto fail = [error](const std::string &msg) {
		if(error)
			*error = msg;
		return false;
	};
	if(connected)
		return true;

	const bool use_meet = config.meet.Enabled();
	const bool named = !config.invite.host.empty();
	if(!named && !use_meet)
		return fail("This invite names no address: the host is found through the meeting point, which is switched off here.");

	// Every address the host may answer under. The one in the invite comes first; the
	// meeting point, if there is one, adds what the host itself says.
	struct Target
	{
		Address address;
		bool from_invite;
	};
	std::vector<Target> targets;
	std::string lookup_error;
	if(named)
	{
		std::vector<Address> addresses;
		if(Resolve(config.invite.host, config.invite.port, addresses, &lookup_error))
		{
			for(const Address &a : addresses)
				targets.push_back({ a, true });
		}
		else if(!use_meet)
			return fail(lookup_error);
	}
	auto add_target = [&targets](const Address &a) {
		if(!a.IsValid() || targets.size() >= kMaxTargets)
			return false;
		for(const Target &t : targets)
		{
			if(t.address.SameEndpoint(a))
				return false;
		}
		targets.push_back({ a, false });
		return true;
	};

	HelloPayload hello;
	if(!RandomBytes(hello.guest_nonce.data(), hello.guest_nonce.size()))
		return fail("No random numbers available");
	hello.want = want;
	hello.controller = ControllerKindFromWire(controller);
	hello.name = config.name;
	hello.avatar = own_avatar;
	hello.media = media;
	hello.battery = battery;
	hello.player_id = config.player_id;
	std::vector<uint8_t> body;
	ByteWriter w(body);
	WriteHello(w, hello);
	std::vector<uint8_t> hello_packet;
	if(!SealHandshakePacket(hello_key, PacketType::Hello, body.data(), body.size(), hello_packet))
		return fail("Cannot build the join request");

	// One socket for IPv4 and IPv6, on a port of its own: the address this socket
	// has on the internet is what the host is told to send to
	auto s = std::make_unique<UdpSocket>();
	if(!s->Bind(0, nullptr))
		return fail("Cannot open a network socket");
	s->SetSimulatedFilter(simulated_filter);

	// What the meeting point's thread finds; it goes away before these do
	std::mutex found_mutex;
	std::vector<MeetNote> found;
	StunResolver resolver;
	StunProbe stun;
	uint64_t my_id = 0;
	MeetNote my_note;
	bool have_note = false;
	uint32_t note_stun_generation = 0;
	bool host_found = false;
	std::unique_ptr<MeetWorker> worker;
	if(use_meet)
	{
		RandomBytes(reinterpret_cast<uint8_t *>(&my_id), sizeof(my_id));
		if(!config.meet.stun_servers.empty())
			resolver.Start(config.meet.stun_servers);
		worker = std::make_unique<MeetWorker>(config.meet, config.invite.secret, MeetRole::Guest,
			[&found_mutex, &found](const std::vector<MeetNote> &notes) {
				std::lock_guard<std::mutex> lock(found_mutex);
				found.insert(found.end(), notes.begin(), notes.end());
			});
	}

	auto gave_up = [&]() {
		if(!use_meet)
			return std::string("No answer from the host. Check the invite, and that the host is sharing and reachable on its port.");
		if(host_found)
		{
			std::string msg = "The host is sharing, but no connection could be made through your two routers. "
				"That happens when a router gives every connection a new port, or blocks this kind of traffic. "
				"It works when the host forwards the port of the invite, or when you both use the same VPN.";
			if(stun.Mapping() == PortMapping::PerDestination)
				msg += " Your own router is one of that kind.";
			return msg;
		}
		std::string msg = targets.empty() && !lookup_error.empty() ? lookup_error + ". " : std::string();
		if(worker->State() == MeetState::Ok)
			return msg + "No answer from the host, and it is not at the meeting point either. Check the invite, and that the host is sharing right now.";
		std::string why = worker->Error();
		return msg + "No answer from the host. Check the invite, and that the host is sharing and reachable on its port. "
			"The meeting point could not be asked either" + (why.empty() ? std::string(".") : " (" + why + ").");
	};

	uint64_t start = NowMs();
	uint64_t last_hello = 0;
	bool hello_now = true;
	// The host has heard us and lets us in, or not, by hand: that may take a while
	uint64_t waiting_since = 0;
	uint8_t buf[2048];
	while(!cancel_connect)
	{
		uint64_t now = NowMs();
		// A host that was found at the meeting point looks for us only every few seconds
		uint64_t allowed = config.connect_timeout_ms;
		if(host_found && config.meet.connect_ms > allowed)
			allowed = config.meet.connect_ms;
		if(waiting_since)
		{
			if(ElapsedMs(now, waiting_since) >= config.admission_timeout_ms)
				return fail("The host did not answer your request to join.");
		}
		else if(ElapsedMs(now, start) >= allowed)
			return fail(gave_up());

		if(use_meet)
		{
			std::vector<Address> servers;
			if(resolver.Take(servers))
				stun.SetServers(servers);
			stun.Tick(*s, now);

			bool first_due = !have_note && (stun.Public().IsValid() || config.meet.stun_servers.empty()
				|| ElapsedMs(now, start) >= kFirstNoteWaitMs);
			if(first_due || (have_note && stun.Generation() != note_stun_generation))
			{
				note_stun_generation = stun.Generation();
				MeetNote note;
				note.role = MeetRole::Guest;
				note.peer_id = my_id;
				note.candidates = GatherCandidates(stun, *s, config.meet.own_addresses);
				if(!have_note || !note.SameAddresses(my_note))
				{
					note.seq = my_note.seq + 1;
					my_note = note;
					have_note = true;
					worker->SetNote(note);
				}
			}

			std::vector<MeetNote> notes;
			{
				std::lock_guard<std::mutex> lock(found_mutex);
				notes.swap(found);
			}
			for(const MeetNote &note : notes)
			{
				host_found = true;
				for(const MeetCandidate &c : note.candidates)
					hello_now = add_target(c.address) || hello_now;
			}
		}

		if(hello_now || now - last_hello >= kHelloIntervalMs)
		{
			hello_now = false;
			last_hello = now;
			for(const Target &t : targets)
				s->SendTo(hello_packet.data(), hello_packet.size(), t.address);
		}

		Address from;
		int n = s->RecvFrom(buf, sizeof(buf), from, 10);
		if(n < 0)
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		if(n <= 0)
			continue;
		if(use_meet && stun.Handle(buf, static_cast<size_t>(n), from, NowMs()))
			continue;
		PacketHeader header;
		if(!ParseHeader(buf, static_cast<size_t>(n), header))
			continue;
		if(header.type == PacketType::Punch)
		{
			std::vector<uint8_t> payload;
			if(!use_meet || !OpenPacket(welcome_key, buf, static_cast<size_t>(n), payload))
				continue;
			ByteReader r(payload.data(), payload.size());
			if(r.U64() != my_id || !r.ok())
				continue;
			// This is where the host's packets come from, whatever its note says: its
			// router may give every connection a new port. Answer right there.
			host_found = true;
			hello_now = add_target(from) || hello_now;
			continue;
		}
		if(header.type != PacketType::Welcome && header.type != PacketType::Reject && header.type != PacketType::Waiting)
			continue;
		std::vector<uint8_t> payload;
		if(!OpenPacket(welcome_key, buf, static_cast<size_t>(n), payload))
			continue;
		if(payload.size() < kHandshakeNonceSize
			|| std::memcmp(payload.data(), hello.guest_nonce.data(), kHandshakeNonceSize) != 0)
			continue; // answer to someone else's or an old request
		if(header.type == PacketType::Waiting)
		{
			if(!waiting_since)
			{
				waiting_since = NowMs();
				if(config.on_waiting)
					config.on_waiting();
			}
			continue;
		}
		if(header.type == PacketType::Reject)
		{
			uint8_t reason = payload.size() > kHandshakeNonceSize ? payload[kHandshakeNonceSize] : 0;
			if(reason == static_cast<uint8_t>(RejectReason::Full))
				return fail("The session is full.");
			if(reason == static_cast<uint8_t>(RejectReason::Version))
				return fail("The host runs a different version of the app. Both sides need the same version.");
			if(reason == static_cast<uint8_t>(RejectReason::Refused))
				return fail("The host did not let you in.");
			if(reason == static_cast<uint8_t>(RejectReason::Locked))
				return fail("The host has closed the session to newcomers.");
			return fail("The host declined the connection.");
		}
		if(header.version != kProtocolVersion)
			return fail("The host runs a different version of the app. Both sides need the same version.");
		ByteReader r(payload.data(), payload.size());
		WelcomePayload p;
		if(!ReadWelcome(r, p) || p.conn_id == 0)
			continue;
		welcome = p;
		keys = DeriveSessionKeys(config.invite.secret, hello.guest_nonce, p.host_nonce);
		host_addr = from;
		via_meeting_point = true;
		for(const Target &t : targets)
		{
			if(t.from_invite && t.address.SameEndpoint(from))
				via_meeting_point = false;
		}
		socket = std::move(s);
		socket->SetSimulatedLoss(loss_probability, loss_seed);
		send_counter = 0;
		connected = true;
		// The host only counts us in once it has seen a packet sealed with the session
		// keys. Send one now, the caller may take a while before it calls Start().
		SendPing(NowMs());
		return true;
	}
	return fail("Cancelled");
}

void Client::Start(const ClientCallbacks &cb)
{
	if(!connected || started)
		return;
	callbacks = cb;
	stop_requested = false;
	started = true;
	thread = std::thread(&Client::ThreadMain, this);
}

void Client::Stop(bool sleep_console)
{
	if(connected && !lost)
	{
		std::vector<uint8_t> bye;
		ByteWriter w(bye);
		w.U8(static_cast<uint8_t>(sleep_console ? ByeReason::Sleep : ByeReason::Quit));
		Send(PacketType::Bye, bye);
		Send(PacketType::Bye, bye);
	}
	stop_requested = true;
	if(thread.joinable())
		thread.join();
	started = false;
	connected = false;
	if(socket)
		socket->Close();
}

bool Client::Send(PacketType type, const std::vector<uint8_t> &payload)
{
	std::lock_guard<std::mutex> lock(send_mutex);
	if(!socket || !socket->IsOpen())
		return false;
	std::vector<uint8_t> packet;
	if(!SealPacket(keys.guest_to_host, type, welcome.conn_id, ++send_counter, payload.data(), payload.size(), packet))
		return false;
	return socket->SendTo(packet.data(), packet.size(), host_addr);
}

void Client::SendInput(uint64_t now_ms)
{
	// input_mutex is held by the caller
	std::vector<uint8_t> payload;
	ByteWriter w(payload);
	w.U32(++input_seq);
	WritePadState(w, input);
	last_input_send_ms = now_ms;
	Send(PacketType::Input, payload);
}

void Client::SetInput(const PadState &state)
{
	if(!connected || lost)
		return;
	std::lock_guard<std::mutex> lock(input_mutex);
	if(state == input && input_seq != 0)
		return;
	input = state;
	input_burst = kInputBurst - 1;
	SendInput(NowMs());
}

void Client::SendKeyMouse(uint64_t now_ms)
{
	// input_mutex is held by the caller
	std::vector<uint8_t> payload;
	ByteWriter w(payload);
	w.U32(++key_mouse_seq);
	WriteKeyMouseState(w, key_mouse);
	last_key_mouse_send_ms = now_ms;
	Send(PacketType::KeyMouse, payload);
}

void Client::SetKeyMouse(const KeyMouseState &state)
{
	if(!connected || lost)
		return;
	std::lock_guard<std::mutex> lock(input_mutex);
	if(key_mouse_used && state.buttons == key_mouse.buttons && state.move_x == key_mouse.move_x
		&& state.move_y == key_mouse.move_y && state.wheel_y == key_mouse.wheel_y && state.wheel_x == key_mouse.wheel_x
		&& state.keys == key_mouse.keys)
		return;
	key_mouse = state;
	key_mouse_used = true;
	// Sent now and twice more a moment later, like the controller: a lost packet costs nothing
	key_mouse_burst = kInputBurst - 1;
	SendKeyMouse(NowMs());
}

void Client::SetWant(uint8_t new_want)
{
	new_want &= kWantMask;
	if(want.exchange(new_want) == new_want)
		return;
	// The next ping carries it; make that one come soon
	ping_now = true;
}

void Client::SetController(ControllerKind kind)
{
	if(controller.exchange(static_cast<uint8_t>(kind)) != static_cast<uint8_t>(kind))
		ping_now = true;
}

void Client::SetBattery(uint8_t level)
{
	if(battery.exchange(level) != level)
		ping_now = true;
}

void Client::SetMedia(uint8_t new_media)
{
	new_media &= kMediaOwnMask;
	if(media.exchange(new_media) != new_media)
		ping_now = true;
}

void Client::SetTwitch(const std::string &login)
{
	const std::string value = ValidTwitchLogin(login) ? login : std::string();
	std::lock_guard<std::mutex> lock(twitch_mutex);
	if(twitch != value)
	{
		twitch = value;
		ping_now = true;
	}
}

void Client::SendVoice(const uint8_t *data, size_t size)
{
	if(!data || !size || size > kMaxVoiceFrameSize || !connected || lost)
		return;
	std::vector<uint8_t> payload;
	payload.reserve(size + 4);
	ByteWriter w(payload);
	w.U32(++voice_seq);
	w.Bytes(data, size);
	Send(PacketType::VoiceUp, payload);
}

// ------------------------------------------------------------------ chat

static const uint32_t kChatResendMs = 400;
static const uint32_t kChatGiveUpMs = 20000;
static const size_t kChatSeenSize = 256;
static const size_t kMaxChatImages = 24;

uint32_t Client::SendChat(const std::string &text, const ChatEmbed &embed, const std::vector<uint8_t> &image)
{
	if(!connected || lost)
		return 0;
	ChatMessage m;
	m.text = TruncateUtf8(text, kMaxChatText);
	m.embed = embed;
	if(m.text.empty() && !m.embed.Valid())
		return 0;
	m.embed.image = BlobRef();
	if(m.embed.Valid() && !image.empty() && image.size() <= kMaxChatImageSize)
	{
		// The host asks us for it, see HandlePacket
		std::lock_guard<std::mutex> lock(blob_mutex);
		m.embed.image = chat_blobs.Put(image);
		if(m.embed.image.Valid())
		{
			chat_blob_order.push_back(m.embed.image.hash);
			while(chat_blob_order.size() > kMaxChatImages)
			{
				chat_blobs.Forget(chat_blob_order.front());
				chat_blob_order.pop_front();
			}
		}
	}
	PendingChat pending;
	uint32_t id;
	{
		std::lock_guard<std::mutex> lock(chat_mutex);
		id = ++chat_up_seq;
		m.seq = id;
		ByteWriter w(pending.payload);
		WriteChatMessage(w, m);
		pending.first_ms = pending.last_ms = NowMs();
		chat_out[id] = pending;
	}
	Send(PacketType::ChatUp, pending.payload);
	return id;
}

BlobData Client::ChatImage(const BlobHash &hash)
{
	std::lock_guard<std::mutex> lock(blob_mutex);
	return chat_blobs.Get(hash);
}

void Client::ResendChats(uint64_t now_ms)
{
	if(now_ms - last_chat_resend_ms < kChatResendMs / 4)
		return;
	last_chat_resend_ms = now_ms;
	std::vector<std::vector<uint8_t>> due;
	{
		std::lock_guard<std::mutex> lock(chat_mutex);
		for(auto it = chat_out.begin(); it != chat_out.end();)
		{
			// first_ms is from the thread that wrote the message and can be newer than now_ms
			if(ElapsedMs(now_ms, it->second.first_ms) > kChatGiveUpMs)
			{
				it = chat_out.erase(it);
				continue;
			}
			if(ElapsedMs(now_ms, it->second.last_ms) >= kChatResendMs)
			{
				it->second.last_ms = now_ms;
				due.push_back(it->second.payload);
			}
			++it;
		}
	}
	for(const auto &payload : due)
		Send(PacketType::ChatUp, payload);
}

void Client::SendCamFrame(const uint8_t *data, size_t size)
{
	if(!data || !size || size > kMaxCamFrameSize || !connected || lost)
		return;
	uint32_t seq = ++cam_seq;
	uint32_t count = CamFragmentCount(size);
	std::vector<uint8_t> fragment;
	for(uint32_t i = 0; i < count; i++)
	{
		if(!BuildCamFragment(seq, data, size, i, fragment))
			return;
		Send(PacketType::CamUp, fragment);
	}
}

void Client::SendPing(uint64_t now_ms)
{
	std::vector<uint8_t> payload;
	ByteWriter w(payload);
	w.U32(++ping_id);
	w.U64(NowUs());
	w.U8(want);
	uint32_t rtt = rtt_ms;
	w.U16(static_cast<uint16_t>(rtt > 0xffff ? 0xffff : rtt));
	w.U8(controller);
	w.U8(media);
	w.U8(battery);
	{
		std::lock_guard<std::mutex> lock(twitch_mutex);
		w.Str8(twitch);
	}
	Send(PacketType::Ping, payload);
	(void)now_ms;
}

BlobData Client::Avatar(const BlobHash &hash)
{
	std::lock_guard<std::mutex> lock(blob_mutex);
	return blobs.Get(hash);
}

ClientStats Client::Stats()
{
	std::lock_guard<std::mutex> lock(stats_mutex);
	ClientStats s = stats;
	s.rtt_ms = rtt_ms;
	return s;
}

void Client::ThreadMain()
{
	uint8_t buf[2048];
	last_rx_ms = NowMs();
	applied_want = want;
	while(!stop_requested && !lost)
	{
		Address from;
		int n = socket->RecvFrom(buf, sizeof(buf), from, 2);
		uint64_t now = NowMs();
		for(int drained = 0; n > 0 && drained < 256 && !lost; drained++)
		{
			HandlePacket(buf, static_cast<size_t>(n), from, now);
			n = socket->RecvFrom(buf, sizeof(buf), from, 0);
		}
		if(lost)
			break;
		Tick(now);
		if(n < 0)
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
}

void Client::HandlePacket(const uint8_t *data, size_t size, const Address &from, uint64_t now_ms)
{
	PacketHeader header;
	if(!ParseHeader(data, size, header))
		return;
	if(header.version != kProtocolVersion || header.conn_id != welcome.conn_id)
		return;
	if(header.type == PacketType::Welcome || header.type == PacketType::Reject)
		return; // late duplicates of the handshake
	std::vector<uint8_t> payload;
	if(!OpenPacket(keys.host_to_guest, data, size, payload))
		return;
	if(!replay.Accept(header.counter))
		return;

	last_rx_ms = now_ms;
	if(from != host_addr)
	{
		// Authenticated, so this is the host on a new address
		std::lock_guard<std::mutex> lock(send_mutex);
		host_addr = from;
	}
	{
		std::lock_guard<std::mutex> lock(stats_mutex);
		stats.packets_received++;
		stats.bytes_received += size;
	}

	ByteReader r(payload.data(), payload.size());
	switch(header.type)
	{
		case PacketType::Pong: {
			r.U32();
			uint64_t sent_us = r.U64();
			if(!r.ok())
				break;
			uint64_t now_us = NowUs();
			if(now_us < sent_us)
				break;
			uint32_t sample = static_cast<uint32_t>((now_us - sent_us + 500) / 1000);
			uint32_t old = rtt_ms;
			uint32_t smoothed = old ? (old * 7 + sample + 4) / 8 : sample;
			rtt_ms = smoothed;
			assembler.SetRttMs(smoothed);
			break;
		}
		case PacketType::State: {
			SessionState s;
			if(!ReadSessionState(r, s))
				break;
			if(have_state && static_cast<int32_t>(s.state_seq - last_state_seq) <= 0)
				break;
			have_state = true;
			last_state_seq = s.state_seq;
			{
				// Fetch the pictures we do not have yet
				std::lock_guard<std::mutex> lock(blob_mutex);
				for(const Participant &p : s.participants)
					blobs.Want(p.avatar);
			}
			if(callbacks.on_state)
				callbacks.on_state(s);
			break;
		}
		case PacketType::Inputs: {
			InputsSnapshot s;
			if(!ReadInputs(r, s))
				break;
			if(have_inputs && static_cast<int32_t>(s.seq - last_inputs_seq) <= 0)
				break;
			have_inputs = true;
			last_inputs_seq = s.seq;
			if(callbacks.on_inputs)
				callbacks.on_inputs(s);
			break;
		}
		case PacketType::BlobRequest: {
			// The host asks for our picture, or for the preview picture of something we wrote
			BlobHash hash;
			uint16_t index;
			if(!ReadBlobRequest(r, hash, index))
				break;
			std::vector<uint8_t> chunk;
			if(own_avatar.Valid() && hash == own_avatar.hash)
			{
				if(BuildBlobChunk(hash, config.avatar, index, chunk))
					Send(PacketType::BlobChunk, chunk);
				break;
			}
			BlobData data;
			{
				std::lock_guard<std::mutex> lock(blob_mutex);
				data = chat_blobs.Get(hash);
			}
			if(data && BuildBlobChunk(hash, *data, index, chunk))
				Send(PacketType::BlobChunk, chunk);
			break;
		}
		case PacketType::BlobChunk: {
			BlobChunkHeader chunk;
			if(!ReadBlobChunkHeader(r, chunk))
				break;
			bool complete;
			bool chat_image;
			BlobRef ref;
			{
				std::lock_guard<std::mutex> lock(blob_mutex);
				chat_image = chat_blobs.Wanted(chunk.hash);
				complete = (chat_image ? chat_blobs : blobs).OnChunk(chunk, r.cursor(), r.remaining());
				if(complete)
				{
					ref.hash = chunk.hash;
					ref.size = static_cast<uint16_t>(chunk.total_size);
				}
			}
			if(complete && chat_image && callbacks.on_chat_image)
				callbacks.on_chat_image(ref);
			else if(complete && !chat_image && callbacks.on_avatar)
				callbacks.on_avatar(ref);
			break;
		}
		case PacketType::Chat: {
			ChatMessage m;
			if(!ReadChatMessage(r, m))
				break;
			// Always acknowledged: the earlier answer may have been lost
			std::vector<uint8_t> ack;
			ByteWriter w(ack);
			w.U32(m.seq);
			Send(PacketType::ChatAck, ack);
			if(std::find(chat_seen.begin(), chat_seen.end(), m.seq) != chat_seen.end())
				break;
			chat_seen.push_back(m.seq);
			if(chat_seen.size() > kChatSeenSize)
				chat_seen.pop_front();
			if(m.embed.image.Valid())
			{
				std::lock_guard<std::mutex> lock(blob_mutex);
				if(!chat_blobs.Has(m.embed.image))
				{
					chat_blobs.Want(m.embed.image);
					chat_blob_order.push_back(m.embed.image.hash);
					while(chat_blob_order.size() > kMaxChatImages)
					{
						chat_blobs.Forget(chat_blob_order.front());
						chat_blob_order.pop_front();
					}
				}
			}
			if(callbacks.on_chat)
				callbacks.on_chat(m);
			break;
		}
		case PacketType::ChatUpAck: {
			uint32_t id = r.U32();
			if(!r.ok())
				break;
			std::lock_guard<std::mutex> lock(chat_mutex);
			chat_out.erase(id);
			break;
		}
		case PacketType::VideoConfig: {
			uint16_t id = r.U16();
			if(!r.ok() || id == 0 || r.remaining() == 0)
				break;
			if(have_video_config && id == video_config_id)
				break;
			have_video_config = true;
			video_config_id = id;
			// Frames encoded against the old parameter sets are of no use any more
			assembler.Reset();
			if(callbacks.on_video_config)
				callbacks.on_video_config(r.cursor(), r.remaining());
			break;
		}
		case PacketType::Video: {
			if(!(want & kWantVideo))
				break;
			VideoFragmentHeader h;
			if(!ReadVideoFragmentHeader(r, h))
				break;
			if(!assembler.OnFragment(h, r.cursor(), r.remaining(), now_ms))
				break;
			AssembledFrame frame;
			while(assembler.PopReady(frame))
			{
				if(!have_video_config)
				{
					// Cannot decode without parameter sets; they come with the next key frame
					assembler.Reset();
					break;
				}
				if(callbacks.on_video_frame)
					callbacks.on_video_frame(frame);
			}
			break;
		}
		case PacketType::Audio: {
			uint32_t seq = r.U32();
			if(!r.ok() || r.remaining() == 0)
				break;
			if(have_audio && static_cast<int32_t>(seq - last_audio_seq) <= 0)
				break; // late or duplicate: playing it now would only add a glitch
			have_audio = true;
			last_audio_seq = seq;
			{
				std::lock_guard<std::mutex> lock(stats_mutex);
				stats.audio_frames++;
			}
			if(callbacks.on_audio && (want & kWantAudio))
				callbacks.on_audio(r.cursor(), r.remaining());
			break;
		}
		case PacketType::Haptics: {
			uint32_t seq = r.U32();
			if(!r.ok() || r.remaining() == 0)
				break;
			if(have_haptics && static_cast<int32_t>(seq - last_haptics_seq) <= 0)
				break;
			have_haptics = true;
			last_haptics_seq = seq;
			{
				std::lock_guard<std::mutex> lock(stats_mutex);
				stats.haptics_frames++;
			}
			if(callbacks.on_haptics && (want & kWantHaptics))
				callbacks.on_haptics(r.cursor(), r.remaining());
			break;
		}
		case PacketType::Voice: {
			uint8_t slot = r.U8();
			uint32_t seq = r.U32();
			if(!r.ok() || slot > kMaxGuests || r.remaining() == 0 || r.remaining() > kMaxVoiceFrameSize)
				break;
			if(!(want & kWantVoice) || !voice_in[slot].Accept(seq, now_ms))
				break;
			{
				std::lock_guard<std::mutex> lock(stats_mutex);
				stats.voice_frames++;
			}
			if(callbacks.on_voice)
				callbacks.on_voice(slot, r.cursor(), r.remaining());
			break;
		}
		case PacketType::Cam: {
			uint8_t slot = r.U8();
			CamFragmentHeader h;
			if(!r.ok() || slot > kMaxGuests || !ReadCamFragmentHeader(r, h))
				break;
			if(!(want & kWantCams) || !cam_in[slot].OnFragment(h, r.cursor(), r.remaining(), now_ms))
				break;
			{
				std::lock_guard<std::mutex> lock(stats_mutex);
				stats.cam_frames++;
			}
			if(callbacks.on_cam_frame)
				callbacks.on_cam_frame(slot, cam_in[slot].Frame());
			break;
		}
		case PacketType::Bye: {
			uint8_t reason = r.U8();
			lost = true;
			if(callbacks.on_disconnected)
			{
				DisconnectReason why = DisconnectReason::HostEnded;
				if(reason == static_cast<uint8_t>(ByeReason::Kicked))
					why = DisconnectReason::Kicked;
				else if(reason == static_cast<uint8_t>(ByeReason::Reconnecting))
					why = DisconnectReason::HostReconnecting;
				callbacks.on_disconnected(why);
			}
			break;
		}
		default:
			break;
	}
}

void Client::Tick(uint64_t now_ms)
{
	if(now_ms - last_rx_ms > config.host_timeout_ms)
	{
		lost = true;
		if(callbacks.on_disconnected)
			callbacks.on_disconnected(DisconnectReason::Timeout);
		return;
	}

	uint8_t current_want = want;
	if((current_want & kWantVideo) && !(applied_want & kWantVideo))
		assembler.Reset(); // video was off: what we knew about the stream is outdated
	applied_want = current_want;

	uint32_t ping_interval = pings_sent < kFastPings ? kFastPingIntervalMs : kPingIntervalMs;
	if(ping_now.exchange(false) || last_ping_ms == 0 || now_ms - last_ping_ms >= ping_interval)
	{
		last_ping_ms = now_ms;
		pings_sent++;
		SendPing(now_ms);
	}

	{
		std::lock_guard<std::mutex> lock(input_mutex);
		if(input_burst > 0)
		{
			if(now_ms - last_input_send_ms >= kInputBurstSpacingMs)
			{
				input_burst--;
				SendInput(now_ms);
			}
		}
		else if(now_ms - last_input_send_ms >= kInputRefreshMs)
			SendInput(now_ms);
		if(key_mouse_used)
		{
			if(key_mouse_burst > 0)
			{
				if(now_ms - last_key_mouse_send_ms >= kInputBurstSpacingMs)
				{
					key_mouse_burst--;
					SendKeyMouse(now_ms);
				}
			}
			else if(now_ms - last_key_mouse_send_ms >= kInputRefreshMs)
				SendKeyMouse(now_ms);
		}
	}

	ResendChats(now_ms);

	if(now_ms - last_blob_poll_ms >= 50)
	{
		last_blob_poll_ms = now_ms;
		std::vector<BlobStore::Request> requests;
		{
			std::lock_guard<std::mutex> lock(blob_mutex);
			blobs.Poll(now_ms, requests);
			std::vector<BlobStore::Request> more;
			chat_blobs.Poll(now_ms, more, 4);
			requests.insert(requests.end(), more.begin(), more.end());
		}
		for(const BlobStore::Request &request : requests)
		{
			std::vector<uint8_t> payload;
			ByteWriter w(payload);
			WriteBlobRequest(w, request.hash, request.index);
			Send(PacketType::BlobRequest, payload);
		}
	}

	if(current_want & kWantVideo)
	{
		std::vector<NackEntry> nacks;
		bool need_keyframe = false;
		assembler.Poll(now_ms, nacks, need_keyframe);
		size_t first = 0;
		while(first < nacks.size())
		{
			std::vector<uint8_t> payload;
			ByteWriter w(payload);
			size_t written = WriteNack(w, nacks, first, kMaxPayload);
			if(!written)
				break;
			first += written;
			Send(PacketType::Nack, payload);
		}
		if(need_keyframe)
			Send(PacketType::KeyframeRequest, std::vector<uint8_t>());
		std::lock_guard<std::mutex> lock(stats_mutex);
		stats.video = assembler.GetStats();
	}
}

} // namespace coplay
