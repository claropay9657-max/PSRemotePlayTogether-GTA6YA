// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <coplay/client.h>

#include <chrono>
#include <cstring>

namespace coplay {

static const uint32_t kHelloIntervalMs = 250;
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

	std::vector<Address> addresses;
	if(!Resolve(config.invite.host, config.invite.port, addresses, error))
		return false;

	HelloPayload hello;
	if(!RandomBytes(hello.guest_nonce.data(), hello.guest_nonce.size()))
		return fail("No random numbers available");
	hello.want = want;
	hello.controller = ControllerKindFromWire(controller);
	hello.name = config.name;
	hello.avatar = own_avatar;
	hello.media = media;
	std::vector<uint8_t> body;
	ByteWriter w(body);
	WriteHello(w, hello);
	std::vector<uint8_t> hello_packet;
	if(!SealHandshakePacket(hello_key, PacketType::Hello, body.data(), body.size(), hello_packet))
		return fail("Cannot build the join request");

	// One socket per address family; whichever path answers first is used
	std::unique_ptr<UdpSocket> sockets[2]; // 0 = IPv6, 1 = IPv4
	for(const Address &a : addresses)
	{
		int idx = a.IsIPv6() ? 0 : 1;
		if(!sockets[idx])
		{
			auto s = std::make_unique<UdpSocket>();
			if(s->OpenFor(a, nullptr))
				sockets[idx] = std::move(s);
		}
	}
	if(!sockets[0] && !sockets[1])
		return fail("Cannot open a network socket");

	uint64_t start = NowMs();
	uint64_t last_hello = 0;
	uint8_t buf[2048];
	while(!cancel_connect)
	{
		uint64_t now = NowMs();
		if(now - start >= config.connect_timeout_ms)
			return fail("No answer from the host. Check the invite, and that the host is sharing and reachable on its port.");
		if(last_hello == 0 || now - last_hello >= kHelloIntervalMs)
		{
			last_hello = now;
			for(const Address &a : addresses)
			{
				UdpSocket *s = sockets[a.IsIPv6() ? 0 : 1].get();
				if(s)
					s->SendTo(hello_packet.data(), hello_packet.size(), a);
			}
		}
		for(int idx = 0; idx < 2; idx++)
		{
			if(!sockets[idx])
				continue;
			Address from;
			int n = sockets[idx]->RecvFrom(buf, sizeof(buf), from, 10);
			if(n <= 0)
				continue;
			PacketHeader header;
			if(!ParseHeader(buf, static_cast<size_t>(n), header))
				continue;
			if(header.type != PacketType::Welcome && header.type != PacketType::Reject)
				continue;
			std::vector<uint8_t> payload;
			if(!OpenPacket(welcome_key, buf, static_cast<size_t>(n), payload))
				continue;
			if(payload.size() < kHandshakeNonceSize
				|| std::memcmp(payload.data(), hello.guest_nonce.data(), kHandshakeNonceSize) != 0)
				continue; // answer to someone else's or an old request
			if(header.type == PacketType::Reject)
			{
				uint8_t reason = payload.size() > kHandshakeNonceSize ? payload[kHandshakeNonceSize] : 0;
				if(reason == static_cast<uint8_t>(RejectReason::Full))
					return fail("The session is full.");
				if(reason == static_cast<uint8_t>(RejectReason::Version))
					return fail("The host runs a different version of the app. Both sides need the same version.");
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
			socket = std::move(sockets[idx]);
			socket->SetSimulatedLoss(loss_probability, loss_seed);
			send_counter = 0;
			connected = true;
			// The host only counts us in once it has seen a packet sealed with the session
			// keys. Send one now, the caller may take a while before it calls Start().
			SendPing(NowMs());
			return true;
		}
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

void Client::Stop()
{
	if(connected && !lost)
	{
		std::vector<uint8_t> bye;
		ByteWriter w(bye);
		w.U8(static_cast<uint8_t>(ByeReason::Quit));
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

void Client::SetMedia(uint8_t new_media)
{
	new_media &= kMediaOwnMask;
	if(media.exchange(new_media) != new_media)
		ping_now = true;
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
			// The host asks for our picture
			BlobHash hash;
			uint16_t index;
			if(!ReadBlobRequest(r, hash, index) || !own_avatar.Valid() || hash != own_avatar.hash)
				break;
			std::vector<uint8_t> chunk;
			if(BuildBlobChunk(hash, config.avatar, index, chunk))
				Send(PacketType::BlobChunk, chunk);
			break;
		}
		case PacketType::BlobChunk: {
			BlobChunkHeader chunk;
			if(!ReadBlobChunkHeader(r, chunk))
				break;
			bool complete;
			BlobRef ref;
			{
				std::lock_guard<std::mutex> lock(blob_mutex);
				complete = blobs.OnChunk(chunk, r.cursor(), r.remaining());
				if(complete)
				{
					ref.hash = chunk.hash;
					ref.size = static_cast<uint16_t>(chunk.total_size);
				}
			}
			if(complete && callbacks.on_avatar)
				callbacks.on_avatar(ref);
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
				callbacks.on_disconnected(reason == static_cast<uint8_t>(ByeReason::Kicked)
					? DisconnectReason::Kicked : DisconnectReason::HostEnded);
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
	}

	if(now_ms - last_blob_poll_ms >= 50)
	{
		last_blob_poll_ms = now_ms;
		std::vector<BlobStore::Request> requests;
		{
			std::lock_guard<std::mutex> lock(blob_mutex);
			blobs.Poll(now_ms, requests);
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
