// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Tests for the co-play library. The network tests run a real host and real
// clients against each other over UDP on the loopback interface.

#include <coplay/blob.h>
#include <coplay/client.h>
#include <coplay/host.h>
#include <coplay/invite.h>
#include <coplay/media.h>
#include <coplay/mixer.h>
#include <coplay/socket.h>
#include <coplay/stun.h>
#include <coplay/video.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace coplay;

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond) \
	do \
	{ \
		g_checks++; \
		if(!(cond)) \
		{ \
			g_failed++; \
			std::printf("    FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		} \
	} while(0)

struct TestCase
{
	const char *name;
	void (*fn)();
};

static std::vector<TestCase> &Registry()
{
	static std::vector<TestCase> tests;
	return tests;
}

struct Registrar
{
	Registrar(const char *name, void (*fn)()) { Registry().push_back({ name, fn }); }
};

#define TEST(name) \
	static void name(); \
	static Registrar registrar_##name(#name, name); \
	static void name()

static bool WaitFor(const std::function<bool()> &pred, int timeout_ms)
{
	auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
	while(std::chrono::steady_clock::now() < deadline)
	{
		if(pred())
			return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return pred();
}

static void SleepMs(int ms)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// ---------------------------------------------------------------- bytes / protocol

TEST(bytes_round_trip)
{
	std::vector<uint8_t> buf;
	ByteWriter w(buf);
	w.U8(0xab);
	w.U16(0x1234);
	w.U32(0xdeadbeef);
	w.U64(0x0123456789abcdefull);
	w.I16(-12345);
	w.I8(-1);
	w.F32(3.5f);
	w.Str8("hello");
	CHECK(buf[1] == 0x12 && buf[2] == 0x34); // big endian

	ByteReader r(buf.data(), buf.size());
	CHECK(r.U8() == 0xab);
	CHECK(r.U16() == 0x1234);
	CHECK(r.U32() == 0xdeadbeef);
	CHECK(r.U64() == 0x0123456789abcdefull);
	CHECK(r.I16() == -12345);
	CHECK(r.I8() == -1);
	CHECK(r.F32() == 3.5f);
	CHECK(r.Str8() == "hello");
	CHECK(r.ok());
	CHECK(r.remaining() == 0);
	CHECK(r.U8() == 0);
	CHECK(!r.ok());
}

TEST(bytes_reader_never_reads_past_end)
{
	uint8_t data[3] = { 1, 2, 3 };
	ByteReader r(data, sizeof(data));
	CHECK(r.U32() == 0);
	CHECK(!r.ok());
	CHECK(r.U8() == 0); // stays failed
	// a string whose length byte promises more than there is
	uint8_t text[3] = { 5, 'a', 'b' };
	ByteReader s(text, sizeof(text));
	CHECK(s.Str8().empty());
	CHECK(!s.ok());
}

TEST(pad_state_round_trip)
{
	PadState a;
	a.buttons = kButtonCross | kButtonR1 | kButtonPs;
	a.l2 = 200;
	a.r2 = 3;
	a.left_x = -32768;
	a.left_y = 32767;
	a.right_x = 1234;
	a.right_y = -4321;
	a.touch_id_next = 5;
	a.touches[0] = { 1000, 500, 4 };
	std::vector<uint8_t> buf;
	ByteWriter w(buf);
	WritePadState(w, a);
	size_t without_motion = buf.size();
	PadState b;
	ByteReader r(buf.data(), buf.size());
	CHECK(ReadPadState(r, b));
	CHECK(a == b);
	CHECK(!b.HasMotion());

	a.gyro[0] = 0.25f;
	a.accel[1] = 0.9f;
	a.orient[3] = 0.7f;
	buf.clear();
	ByteWriter w2(buf);
	WritePadState(w2, a);
	CHECK(buf.size() == without_motion + 40);
	ByteReader r2(buf.data(), buf.size());
	CHECK(ReadPadState(r2, b));
	CHECK(a == b);
	CHECK(b.HasMotion());

	// truncated
	ByteReader r3(buf.data(), buf.size() - 1);
	CHECK(!ReadPadState(r3, b));
}

TEST(pad_state_rejects_non_finite_motion)
{
	// A local controller never produces NaN, so craft the bytes a hostile peer would send
	PadState a;
	a.gyro[0] = 1.0f;
	std::vector<uint8_t> buf;
	ByteWriter w(buf);
	WritePadState(w, a);
	PadState b;
	ByteReader ok(buf.data(), buf.size());
	CHECK(ReadPadState(ok, b));
	const size_t gyro1_offset = 1 + 4 + 2 + 8 + 1 + 10 + 4;
	CHECK(buf.size() == gyro1_offset - 4 + 40);
	const uint8_t nan_bits[4] = { 0x7f, 0xc0, 0x00, 0x00 };
	std::memcpy(buf.data() + gyro1_offset, nan_bits, 4);
	ByteReader r(buf.data(), buf.size());
	CHECK(!ReadPadState(r, b));
	const uint8_t inf_bits[4] = { 0x7f, 0x80, 0x00, 0x00 };
	std::memcpy(buf.data() + gyro1_offset, inf_bits, 4);
	ByteReader r2(buf.data(), buf.size());
	CHECK(!ReadPadState(r2, b));
}

TEST(session_state_round_trip)
{
	SessionState s;
	s.state_seq = 77;
	s.stream.codec = VideoCodec::H265Hdr;
	s.stream.width = 1920;
	s.stream.height = 1080;
	s.stream.fps = 60;
	s.stream.dualsense = true;
	s.audio.valid = true;
	s.audio.channels = 2;
	s.audio.bits = 16;
	s.audio.rate = 48000;
	s.audio.frame_size = 480;
	s.video_config_id = 3;
	s.feedback.rumble_seq = 9;
	s.feedback.rumble_left = 255;
	s.feedback.trigger_seq = 2;
	s.feedback.trigger_type_right = 0x21;
	s.feedback.trigger_right[9] = 42;
	s.feedback.led = { 1, 2, 3 };
	s.feedback.motion_reset_seq = 4;
	s.mix_mode = MixMode::LastActive;
	s.your_slot = 2;
	s.participants.push_back({ 0, kParticipantHost | kParticipantCanControl, "Nico" });
	s.participants.push_back({ 2, kParticipantCanControl | kParticipantInControl, "Friend" });
	s.participants[1].media = kMediaVoice | kMediaCamera;

	std::vector<uint8_t> buf;
	ByteWriter w(buf);
	WriteSessionState(w, s);
	CHECK(buf.size() <= kMaxPayload);

	SessionState t;
	ByteReader r(buf.data(), buf.size());
	CHECK(ReadSessionState(r, t));
	CHECK(t.state_seq == 77);
	CHECK(t.stream == s.stream);
	CHECK(t.audio == s.audio);
	CHECK(t.video_config_id == 3);
	CHECK(t.feedback.rumble_seq == 9 && t.feedback.rumble_left == 255);
	CHECK(t.feedback.trigger_type_right == 0x21 && t.feedback.trigger_right[9] == 42);
	CHECK(t.feedback.led[2] == 3 && t.feedback.motion_reset_seq == 4);
	CHECK(t.mix_mode == MixMode::LastActive);
	CHECK(t.your_slot == 2);
	CHECK(t.participants.size() == 2 && t.participants[1].name == "Friend");
	CHECK(t.participants[1].flags == (kParticipantCanControl | kParticipantInControl));
	CHECK(t.participants[0].media == 0 && t.participants[1].media == (kMediaVoice | kMediaCamera));

	for(size_t cut = 0; cut < buf.size(); cut++)
	{
		ByteReader rc(buf.data(), cut);
		CHECK(!ReadSessionState(rc, t));
	}
}

TEST(largest_session_state_fits_one_packet)
{
	SessionState s;
	for(uint8_t i = 0; i <= kMaxGuests; i++)
		s.participants.push_back({ i, 0xff, std::string(kMaxNameLength, 'x') });
	std::vector<uint8_t> buf;
	ByteWriter w(buf);
	WriteSessionState(w, s);
	CHECK(buf.size() <= kMaxPayload);
}

TEST(nack_round_trip_and_split)
{
	std::vector<NackEntry> entries;
	entries.push_back({ 10, { 1, 2, 3 } });
	entries.push_back({ 11, {} });
	NackEntry big;
	big.frame_seq = 12;
	for(uint16_t i = 0; i < 500; i++)
		big.fragments.push_back(i);
	entries.push_back(big);
	entries.push_back(big);

	std::vector<NackEntry> all;
	size_t first = 0;
	int packets = 0;
	while(first < entries.size())
	{
		std::vector<uint8_t> buf;
		ByteWriter w(buf);
		size_t n = WriteNack(w, entries, first, kMaxPayload);
		CHECK(n > 0);
		if(!n)
			break;
		CHECK(buf.size() <= kMaxPayload);
		std::vector<NackEntry> got;
		ByteReader r(buf.data(), buf.size());
		CHECK(ReadNack(r, got));
		all.insert(all.end(), got.begin(), got.end());
		first += n;
		packets++;
	}
	CHECK(packets == 2);
	CHECK(all.size() == 4);
	CHECK(all[0].frame_seq == 10 && all[0].fragments.size() == 3 && all[0].fragments[2] == 3);
	CHECK(all[1].frame_seq == 11 && all[1].fragments.empty());
	CHECK(all[3].fragments.size() == 500 && all[3].fragments[499] == 499);
}

// ---------------------------------------------------------------- crypto

TEST(hkdf_matches_rfc5869_test_vector_1)
{
	uint8_t ikm[22];
	std::memset(ikm, 0x0b, sizeof(ikm));
	uint8_t salt[13];
	for(uint8_t i = 0; i < 13; i++)
		salt[i] = i;
	std::string info;
	for(int i = 0xf0; i <= 0xf9; i++)
		info.push_back(static_cast<char>(i));
	Key okm = HkdfSha256(ikm, sizeof(ikm), salt, sizeof(salt), info);
	static const uint8_t expected[32] = {
		0x3c, 0xb2, 0x5f, 0x25, 0xfa, 0xac, 0xd5, 0x7a, 0x90, 0x43, 0x4f, 0x64, 0xd0, 0x36, 0x2f, 0x2a,
		0x2d, 0x2d, 0x0a, 0x90, 0xcf, 0x1a, 0x5a, 0x4c, 0x5d, 0xb0, 0x2d, 0x56, 0xec, 0xc4, 0xc5, 0xbf
	};
	CHECK(std::memcmp(okm.data(), expected, 32) == 0);
}

TEST(session_keys_differ_per_direction_and_nonce)
{
	Secret secret = GenerateSecret();
	HandshakeNonce a{}, b{};
	a[0] = 1;
	b[0] = 2;
	SessionKeys k1 = DeriveSessionKeys(secret, a, b);
	SessionKeys k2 = DeriveSessionKeys(secret, b, a);
	CHECK(k1.guest_to_host != k1.host_to_guest);
	CHECK(k1.guest_to_host != k2.guest_to_host);
	CHECK(DeriveHelloKey(secret) != DeriveWelcomeKey(secret));
	Secret other = secret;
	other[5] ^= 1;
	CHECK(DeriveSessionKeys(other, a, b).guest_to_host != k1.guest_to_host);
}

TEST(seal_open_round_trip_and_tamper_detection)
{
	Key key = HkdfSha256(reinterpret_cast<const uint8_t *>("k"), 1, nullptr, 0, "test");
	std::vector<uint8_t> payload(300);
	for(size_t i = 0; i < payload.size(); i++)
		payload[i] = static_cast<uint8_t>(i * 7);
	std::vector<uint8_t> packet;
	CHECK(SealPacket(key, PacketType::Input, 0x11223344, 99, payload.data(), payload.size(), packet));
	CHECK(packet.size() == kHeaderSize + payload.size() + kTagSize);
	// the payload is not visible in the packet
	CHECK(std::memcmp(packet.data() + kHeaderSize, payload.data(), 32) != 0);

	PacketHeader h;
	CHECK(ParseHeader(packet.data(), packet.size(), h));
	CHECK(h.type == PacketType::Input && h.version == kProtocolVersion && h.conn_id == 0x11223344 && h.counter == 99);

	std::vector<uint8_t> out;
	CHECK(OpenPacket(key, packet.data(), packet.size(), out));
	CHECK(out == payload);

	// any modified byte, header included, is rejected
	for(size_t i = 0; i < packet.size(); i += 7)
	{
		std::vector<uint8_t> bad = packet;
		bad[i] ^= 0x01;
		CHECK(!OpenPacket(key, bad.data(), bad.size(), out));
	}
	// truncated
	CHECK(!OpenPacket(key, packet.data(), packet.size() - 1, out));

	Key wrong = key;
	wrong[0] ^= 1;
	CHECK(!OpenPacket(wrong, packet.data(), packet.size(), out));

	// empty payload
	CHECK(SealPacket(key, PacketType::KeyframeRequest, 1, 1, nullptr, 0, packet));
	CHECK(OpenPacket(key, packet.data(), packet.size(), out));
	CHECK(out.empty());

	// too large
	std::vector<uint8_t> big(kMaxPayload + 1);
	CHECK(!SealPacket(key, PacketType::Video, 1, 2, big.data(), big.size(), packet));
	big.resize(kMaxPayload);
	CHECK(SealPacket(key, PacketType::Video, 1, 2, big.data(), big.size(), packet));
	CHECK(packet.size() == kMaxDatagram);
}

TEST(replay_window)
{
	ReplayWindow w;
	CHECK(!w.Accept(0));
	CHECK(w.Accept(1));
	CHECK(!w.Accept(1));
	CHECK(w.Accept(2));
	CHECK(w.Accept(5));
	CHECK(w.Accept(3)); // out of order but new
	CHECK(!w.Accept(3));
	CHECK(w.Accept(4));
	CHECK(!w.Accept(5));

	// jump ahead by less than the window: old ones still tracked
	CHECK(w.Accept(1000));
	CHECK(!w.Accept(4));
	CHECK(w.Accept(999));
	CHECK(w.Accept(6));
	CHECK(!w.Accept(6));

	// jump far ahead: everything older than the window is refused
	// (not named "far": the Windows headers define that as a macro)
	uint64_t ahead =1000 + ReplayWindow::kWindow * 3;
	CHECK(w.Accept(ahead));
	CHECK(!w.Accept(7));
	CHECK(!w.Accept(ahead - ReplayWindow::kWindow));
	CHECK(w.Accept(ahead - ReplayWindow::kWindow + 1));
	CHECK(!w.Accept(ahead - ReplayWindow::kWindow + 1));
	CHECK(w.Accept(ahead - 1));

	// a slot reused by a newer counter must not look "seen"
	ReplayWindow v;
	for(uint64_t c = 1; c <= ReplayWindow::kWindow * 2 + 10; c++)
		CHECK(v.Accept(c));
	CHECK(!v.Accept(ReplayWindow::kWindow * 2 + 10));
}

TEST(base64url)
{
	uint8_t data[] = { 0xfb, 0xff, 0x00, 0x10, 0x83 };
	for(size_t n = 0; n <= sizeof(data); n++)
	{
		std::string text = Base64UrlEncode(data, n);
		CHECK(text.find('+') == std::string::npos && text.find('/') == std::string::npos && text.find('=') == std::string::npos);
		std::vector<uint8_t> out;
		CHECK(Base64UrlDecode(text, out));
		CHECK(out.size() == n && std::memcmp(out.data(), data, n) == 0);
	}
	std::vector<uint8_t> out;
	CHECK(!Base64UrlDecode("ab*d", out));
	CHECK(!Base64UrlDecode("a", out));
}

TEST(invite_round_trip)
{
	Invite a;
	a.host = "203.0.113.7";
	a.port = 9410;
	a.secret = GenerateSecret();
	std::string text = EncodeInvite(a);
	CHECK(text.rfind("psrpt://203.0.113.7:9410/", 0) == 0);
	Invite b;
	std::string error;
	CHECK(DecodeInvite("  " + text + "\n", b, &error));
	CHECK(b.host == a.host && b.port == a.port && b.secret == a.secret);

	a.host = "2001:db8::1";
	a.port = 12345;
	text = EncodeInvite(a);
	CHECK(text.rfind("psrpt://[2001:db8::1]:12345/", 0) == 0);
	CHECK(DecodeInvite(text, b, &error));
	CHECK(b.host == "2001:db8::1" && b.port == 12345 && b.secret == a.secret);

	a.host = "my-home.example.org";
	CHECK(DecodeInvite(EncodeInvite(a), b, &error));
	CHECK(b.host == "my-home.example.org");

	// default port
	CHECK(DecodeInvite("psrpt://host.example/" + SecretToString(a.secret), b, &error));
	CHECK(b.port == kDefaultPort);

	CHECK(!DecodeInvite("", b, &error));
	CHECK(!error.empty());
	CHECK(!DecodeInvite("http://host:1/" + SecretToString(a.secret), b, &error));
	CHECK(!DecodeInvite("psrpt://host:1/short", b, &error));
	CHECK(!DecodeInvite("psrpt://host:99999/" + SecretToString(a.secret), b, &error));
	CHECK(!DecodeInvite("psrpt://host:12x/" + SecretToString(a.secret), b, &error));
	CHECK(!DecodeInvite("psrpt://:9410/" + SecretToString(a.secret), b, &error));
	CHECK(!DecodeInvite("psrpt://[::1:9410/" + SecretToString(a.secret), b, &error));

	// two generated secrets differ
	CHECK(GenerateSecret() != GenerateSecret());
}

// ---------------------------------------------------------------- mixer

static PadState Pad(uint32_t buttons, int16_t lx = 0, int16_t ly = 0, uint8_t l2 = 0, uint8_t r2 = 0)
{
	PadState s;
	s.buttons = buttons;
	s.left_x = lx;
	s.left_y = ly;
	s.l2 = l2;
	s.r2 = r2;
	return s;
}

TEST(merge_pad_states)
{
	PadState a = Pad(kButtonCross, 10000, 0, 50, 0);
	PadState b = Pad(kButtonSquare, 0, -30000, 20, 200);
	b.right_x = 500;
	PadState m = MergePadStates(a, b);
	CHECK(m.buttons == (kButtonCross | kButtonSquare));
	CHECK(m.l2 == 50 && m.r2 == 200);
	// the stick that is pushed further wins as a whole, axes are not mixed
	CHECK(m.left_x == 0 && m.left_y == -30000);
	CHECK(m.right_x == 500);

	// touches: first one that has a finger down
	a.touches[0] = { 10, 20, 1 };
	b.touches[0] = { 30, 40, 2 };
	b.touches[1] = { 50, 60, 3 };
	m = MergePadStates(a, b);
	CHECK(m.touches[0].id == 1 && m.touches[1].id == 3);

	// motion is taken from one controller, never blended
	a.gyro[0] = 1.0f;
	b.gyro[0] = 5.0f;
	b.accel[2] = 0.5f;
	m = MergePadStates(a, b);
	CHECK(m.gyro[0] == 1.0f && m.accel[2] == 0.0f);
	PadState no_motion = Pad(0);
	m = MergePadStates(no_motion, b);
	CHECK(m.gyro[0] == 5.0f && m.accel[2] == 0.5f);
}

TEST(mixer_together)
{
	InputMixer mixer;
	mixer.SetSource(0, Pad(kButtonCross), 1000);
	mixer.SetSource(1, Pad(kButtonCircle, 20000, 0), 1000);
	mixer.SetSource(2, Pad(0, 0, 0, 0, 255), 1000);
	PadState m = mixer.Mix(1010);
	CHECK(m.buttons == (kButtonCross | kButtonCircle));
	CHECK(m.left_x == 20000 && m.r2 == 255);
	CHECK(mixer.InControl(0) && mixer.InControl(1) && mixer.InControl(2));
	CHECK(!mixer.InControl(3));

	mixer.RemoveSource(1);
	m = mixer.Mix(1020);
	CHECK(m.buttons == kButtonCross && m.left_x == 0);
}

TEST(mixer_guest_that_went_silent_lets_go)
{
	InputMixer::Config config;
	config.stale_ms = 500;
	InputMixer mixer(config);
	mixer.SetSource(0, Pad(kButtonCross), 1000);
	mixer.SetSource(1, Pad(kButtonR1, 32767, 0, 0, 255), 1000);
	CHECK(mixer.Mix(1400).buttons == (kButtonCross | kButtonR1));
	// the guest's last packet is now older than stale_ms: its buttons are released
	PadState m = mixer.Mix(1501);
	CHECK(m.buttons == kButtonCross);
	CHECK(m.left_x == 0 && m.r2 == 0);
	// the host's own state never goes stale
	CHECK(mixer.Mix(100000).buttons == kButtonCross);
	// and the guest is back as soon as it sends again
	mixer.SetSource(1, Pad(kButtonR1), 2000);
	CHECK(mixer.Mix(2001).buttons == (kButtonCross | kButtonR1));
}

TEST(mixer_masks)
{
	InputMixer mixer;
	PadState full = Pad(kButtonCross | kButtonTouchpad, 30000, 30000, 100, 100);
	full.right_x = 30000;
	full.touches[0] = { 1, 1, 0 };
	full.gyro[0] = 1.0f;
	mixer.SetSource(1, full, 0);

	mixer.SetMask(1, InputMask::None());
	CHECK(mixer.Mix(1) == PadState());

	InputMask driver = InputMask::None();
	driver.left_stick = true;
	driver.l2 = driver.r2 = true;
	mixer.SetMask(1, driver);
	PadState m = mixer.Mix(1);
	CHECK(m.buttons == 0 && m.left_x == 30000 && m.right_x == 0 && m.l2 == 100 && m.r2 == 100);
	CHECK(m.touches[0].id == -1 && !m.HasMotion());

	mixer.SetMask(1, InputMask::All());
	CHECK(mixer.Mix(1) == full);
}

TEST(mixer_host_only)
{
	InputMixer mixer;
	mixer.SetMode(MixMode::HostOnly);
	mixer.SetSource(0, Pad(kButtonCross), 0);
	mixer.SetSource(1, Pad(kButtonCircle), 0);
	CHECK(mixer.Mix(1).buttons == kButtonCross);
	CHECK(mixer.InControl(0) && !mixer.InControl(1));
}

TEST(mixer_last_active_passes_control)
{
	InputMixer::Config config;
	config.handoff_ms = 300;
	config.stale_ms = 100000;
	InputMixer mixer(config);
	mixer.SetMode(MixMode::LastActive);
	mixer.SetSource(0, Pad(0), 0);
	mixer.SetSource(1, Pad(0), 0);
	CHECK(mixer.Mix(10) == PadState());
	CHECK(!mixer.InControl(0) && !mixer.InControl(1));

	// guest starts playing: takes control
	mixer.SetSource(1, Pad(kButtonCross, 20000, 0), 20);
	CHECK(mixer.Mix(20).buttons == kButtonCross);
	CHECK(mixer.InControl(1) && !mixer.InControl(0));

	// host presses while the guest is still playing: ignored
	mixer.SetSource(0, Pad(kButtonCircle), 30);
	PadState m = mixer.Mix(30);
	CHECK(m.buttons == kButtonCross && m.left_x == 20000);

	// guest lets go; host has to wait for the hand-off time
	mixer.SetSource(1, Pad(0), 100);
	CHECK(mixer.Mix(100).buttons == 0);
	CHECK(mixer.Mix(250).buttons == 0);
	CHECK(mixer.InControl(1));
	// last activity of the guest was at t=30, so from t=330 the host can take over
	CHECK(mixer.Mix(331).buttons == kButtonCircle);
	CHECK(mixer.InControl(0) && !mixer.InControl(1));

	// a stick resting slightly off centre is not "playing"
	mixer.SetSource(0, Pad(0, 3000, -2000), 400);
	mixer.SetSource(1, Pad(kButtonSquare), 800);
	CHECK(mixer.Mix(800).buttons == kButtonSquare);
	CHECK(mixer.InControl(1));

	// the player in control leaves: the other one can take over at once
	mixer.SetSource(0, Pad(kButtonTriangle), 810);
	CHECK(mixer.Mix(810).buttons == kButtonSquare);
	mixer.RemoveSource(1);
	CHECK(mixer.Mix(811).buttons == kButtonTriangle);
}

// ---------------------------------------------------------------- video

static std::vector<uint8_t> MakeNal(std::initializer_list<uint8_t> header, size_t payload, uint32_t seed)
{
	std::vector<uint8_t> out = { 0, 0, 0, 1 };
	out.insert(out.end(), header.begin(), header.end());
	std::mt19937 rng(seed);
	for(size_t i = 0; i < payload; i++)
		out.push_back(static_cast<uint8_t>(rng()));
	return out;
}

static std::vector<uint8_t> H264Config()
{
	std::vector<uint8_t> out = MakeNal({ 0x67 }, 20, 1);
	std::vector<uint8_t> pps = MakeNal({ 0x68 }, 6, 2);
	out.insert(out.end(), pps.begin(), pps.end());
	return out;
}

static std::vector<uint8_t> H264Key(size_t size, uint32_t seed)
{
	return MakeNal({ 0x65 }, size, seed);
}

/** Payload size that makes a frame of exactly this many full fragments. */
static size_t FragmentsPayload(size_t fragments)
{
	return kVideoFragmentSize * fragments - 5; // 4 bytes start code + 1 byte NAL header
}

static std::vector<uint8_t> H264Delta(size_t size, uint32_t seed)
{
	return MakeNal({ 0x41 }, size, seed);
}

TEST(classify_frames)
{
	CHECK(ClassifyFrame(VideoCodec::H264, H264Config().data(), H264Config().size()) == FrameKind::Config);
	auto key = H264Key(100, 1);
	CHECK(ClassifyFrame(VideoCodec::H264, key.data(), key.size()) == FrameKind::Key);
	auto delta = H264Delta(100, 1);
	CHECK(ClassifyFrame(VideoCodec::H264, delta.data(), delta.size()) == FrameKind::Delta);

	// parameter sets in front of an IDR slice (as some encoders send them) still make a key frame
	auto both = H264Config();
	both.insert(both.end(), key.begin(), key.end());
	CHECK(ClassifyFrame(VideoCodec::H264, both.data(), both.size()) == FrameKind::Key);

	// 3 byte start codes
	std::vector<uint8_t> short_sc = { 0, 0, 1, 0x65, 0x88 };
	CHECK(ClassifyFrame(VideoCodec::H264, short_sc.data(), short_sc.size()) == FrameKind::Key);

	// HEVC: VPS(32) SPS(33) PPS(34) -> config, IDR_N_LP(20) -> key, TRAIL_R(1) -> delta
	auto vps = MakeNal({ 32 << 1, 1 }, 10, 3);
	auto sps = MakeNal({ 33 << 1, 1 }, 10, 4);
	vps.insert(vps.end(), sps.begin(), sps.end());
	CHECK(ClassifyFrame(VideoCodec::H265, vps.data(), vps.size()) == FrameKind::Config);
	auto idr = MakeNal({ 20 << 1, 1 }, 50, 5);
	CHECK(ClassifyFrame(VideoCodec::H265Hdr, idr.data(), idr.size()) == FrameKind::Key);
	auto trail = MakeNal({ 1 << 1, 1 }, 50, 6);
	CHECK(ClassifyFrame(VideoCodec::H265, trail.data(), trail.size()) == FrameKind::Delta);

	// garbage is passed through as a delta frame, never mistaken for config
	std::vector<uint8_t> junk(64, 0xaa);
	CHECK(ClassifyFrame(VideoCodec::H264, junk.data(), junk.size()) == FrameKind::Delta);
	CHECK(ClassifyFrame(VideoCodec::H264, nullptr, 0) == FrameKind::Delta);
}

TEST(frame_cache)
{
	FrameCache cache(4, 1000);
	for(uint32_t seq = 1; seq <= 6; seq++)
	{
		FrameCache::Entry e;
		e.frame_seq = seq;
		e.data = std::make_shared<const std::vector<uint8_t>>(100, static_cast<uint8_t>(seq));
		cache.Put(e);
	}
	CHECK(!cache.Find(1) && !cache.Find(2));
	CHECK(cache.Find(3) && cache.Find(6));
	CHECK(cache.Find(5)->data->at(0) == 5);
	CHECK(!cache.Find(7));

	// byte limit
	FrameCache::Entry big;
	big.frame_seq = 7;
	big.data = std::make_shared<const std::vector<uint8_t>>(950, 7);
	cache.Put(big);
	CHECK(cache.Find(7));
	CHECK(!cache.Find(6)); // evicted to stay under 1000 bytes

	// a gap in the numbering starts over
	FrameCache::Entry jump;
	jump.frame_seq = 20;
	jump.data = std::make_shared<const std::vector<uint8_t>>(10, 1);
	cache.Put(jump);
	CHECK(cache.Find(20) && !cache.Find(7));
}

struct Fragmented
{
	std::vector<VideoFragmentHeader> headers;
	std::vector<std::vector<uint8_t>> data;
};

static Fragmented Fragment(uint32_t seq, const std::vector<uint8_t> &frame, bool key, uint32_t capture_ms = 0)
{
	FrameCache::Entry e;
	e.frame_seq = seq;
	e.flags = key ? kVideoKey : 0;
	e.capture_ms = capture_ms;
	e.data = std::make_shared<const std::vector<uint8_t>>(frame);
	Fragmented out;
	uint32_t count = VideoFragmentCount(frame.size());
	for(uint32_t i = 0; i < count; i++)
	{
		std::vector<uint8_t> payload;
		BuildVideoFragment(e, i, false, payload);
		ByteReader r(payload.data(), payload.size());
		VideoFragmentHeader h;
		ReadVideoFragmentHeader(r, h);
		out.headers.push_back(h);
		out.data.emplace_back(r.cursor(), r.cursor() + r.remaining());
	}
	return out;
}

static void Feed(FrameAssembler &a, const Fragmented &f, size_t index, uint64_t now)
{
	CHECK(a.OnFragment(f.headers[index], f.data[index].data(), f.data[index].size(), now));
}

TEST(fragment_sizes)
{
	CHECK(VideoFragmentCount(0) == 0);
	CHECK(VideoFragmentCount(1) == 1);
	CHECK(VideoFragmentCount(kVideoFragmentSize) == 1);
	CHECK(VideoFragmentCount(kVideoFragmentSize + 1) == 2);
	CHECK(kVideoFragmentHeaderSize + kVideoFragmentSize <= kMaxPayload);
	CHECK(VideoFragmentCount(kMaxVideoFrameSize) <= 0xffff);

	auto frame = H264Key(kVideoFragmentSize * 2 + 10, 9);
	Fragmented f = Fragment(1, frame, true);
	CHECK(f.headers.size() == 3);
	CHECK(f.data[0].size() == kVideoFragmentSize && f.data[2].size() == frame.size() - 2 * kVideoFragmentSize);
	CHECK(f.headers[2].frag_index == 2 && f.headers[2].frag_count == 3 && f.headers[2].total_size == frame.size());
}

TEST(assembler_in_order_and_reordered)
{
	FrameAssembler a;
	auto k = H264Key(5000, 1);
	auto d1 = H264Delta(3000, 2);
	auto d2 = H264Delta(100, 3);
	Fragmented fk = Fragment(10, k, true), f1 = Fragment(11, d1, false), f2 = Fragment(12, d2, false);

	AssembledFrame out;
	CHECK(a.WaitingForKey());
	// a delta before any key frame is held back
	Feed(a, f2, 0, 0);
	CHECK(!a.PopReady(out));

	// fragments of the key frame out of order
	for(size_t i = fk.headers.size(); i-- > 0;)
		Feed(a, fk, i, 1);
	CHECK(a.PopReady(out));
	CHECK(out.frame_seq == 10 && out.key && out.data == k && out.frames_lost == 0 && !out.recovered);
	CHECK(!a.WaitingForKey());
	// frame 12 is complete but 11 is not: nothing yet
	CHECK(!a.PopReady(out));

	for(size_t i = 0; i < f1.headers.size(); i++)
		Feed(a, f1, i, 2);
	CHECK(a.PopReady(out));
	CHECK(out.frame_seq == 11 && !out.key && out.data == d1);
	CHECK(a.PopReady(out));
	CHECK(out.frame_seq == 12 && out.data == d2);
	CHECK(!a.PopReady(out));

	// duplicates and late fragments are ignored
	Feed(a, f1, 0, 3);
	CHECK(!a.PopReady(out));
	CHECK(a.GetStats().frames_delivered == 3);
	CHECK(a.GetStats().duplicate_fragments == 1);
}

TEST(assembler_rejects_malformed_fragments)
{
	FrameAssembler a;
	auto k = H264Key(3000, 1);
	Fragmented f = Fragment(1, k, true);
	VideoFragmentHeader h = f.headers[0];

	VideoFragmentHeader bad = h;
	bad.frag_index = bad.frag_count;
	CHECK(!a.OnFragment(bad, f.data[0].data(), f.data[0].size(), 0));
	bad = h;
	bad.frag_count++;
	CHECK(!a.OnFragment(bad, f.data[0].data(), f.data[0].size(), 0));
	bad = h;
	bad.total_size = static_cast<uint32_t>(kMaxVideoFrameSize + 1);
	bad.frag_count = static_cast<uint16_t>(VideoFragmentCount(bad.total_size));
	CHECK(!a.OnFragment(bad, f.data[0].data(), f.data[0].size(), 0));
	bad = h;
	bad.total_size = 0;
	bad.frag_count = 0;
	CHECK(!a.OnFragment(bad, f.data[0].data(), 0, 0));
	// wrong fragment length
	CHECK(!a.OnFragment(h, f.data[0].data(), f.data[0].size() - 1, 0));
	// a second fragment that disagrees about the frame's size
	CHECK(a.OnFragment(h, f.data[0].data(), f.data[0].size(), 0));
	auto other = H264Key(3000 + kVideoFragmentSize, 1);
	Fragmented g = Fragment(1, other, true);
	CHECK(!a.OnFragment(g.headers[1], g.data[1].data(), g.data[1].size(), 0));
}

TEST(assembler_asks_for_lost_fragments)
{
	FrameAssembler::Config config;
	config.nack_delay_ms = 5;
	config.nack_interval_ms = 40;
	config.max_wait_ms = 150;
	FrameAssembler a(config);
	auto k = H264Key(FragmentsPayload(4), 1);
	auto d1 = H264Delta(FragmentsPayload(3), 2);
	auto d2 = H264Delta(500, 3);
	Fragmented fk = Fragment(1, k, true), f1 = Fragment(2, d1, false), f2 = Fragment(3, d2, false);
	for(size_t i = 0; i < fk.headers.size(); i++)
		Feed(a, fk, i, 100);
	AssembledFrame out;
	CHECK(a.PopReady(out));

	// fragment 1 of frame 2 is lost, frame 3 arrives completely
	Feed(a, f1, 0, 110);
	Feed(a, f1, 2, 110);
	Feed(a, f2, 0, 112);
	CHECK(!a.PopReady(out));

	std::vector<NackEntry> nacks;
	bool key = false;
	a.Poll(112, nacks, key); // too early: the fragment may just be late
	CHECK(nacks.empty() && !key);
	a.Poll(116, nacks, key);
	CHECK(nacks.size() == 1 && !key);
	CHECK(nacks[0].frame_seq == 2 && nacks[0].fragments.size() == 1 && nacks[0].fragments[0] == 1);
	a.Poll(120, nacks, key); // not again right away
	CHECK(nacks.empty());
	a.Poll(160, nacks, key); // but again after the interval
	CHECK(nacks.size() == 1);

	// the retransmission arrives: both frames come out, in order
	Feed(a, f1, 1, 170);
	CHECK(a.PopReady(out));
	CHECK(out.frame_seq == 2 && out.data == d1);
	CHECK(a.PopReady(out));
	CHECK(out.frame_seq == 3 && out.data == d2);
	a.Poll(175, nacks, key);
	CHECK(nacks.empty() && !key);
}

TEST(assembler_asks_for_frames_that_vanished_completely)
{
	FrameAssembler a;
	auto k = H264Key(300, 1);
	auto d2 = H264Delta(300, 3);
	Fragmented fk = Fragment(1, k, true), f2 = Fragment(3, d2, false);
	Feed(a, fk, 0, 100);
	AssembledFrame out;
	CHECK(a.PopReady(out));
	// frame 2 never shows up, frame 3 does
	Feed(a, f2, 0, 110);
	std::vector<NackEntry> nacks;
	bool key = false;
	a.Poll(120, nacks, key);
	CHECK(nacks.size() == 1 && nacks[0].frame_seq == 2 && nacks[0].fragments.empty());
	CHECK(!key);

	auto d1 = H264Delta(700, 2);
	Fragmented f1 = Fragment(2, d1, false);
	Feed(a, f1, 0, 130);
	CHECK(a.PopReady(out) && out.frame_seq == 2);
	CHECK(a.PopReady(out) && out.frame_seq == 3);
}

TEST(assembler_tail_loss_is_noticed_without_a_later_frame)
{
	FrameAssembler a;
	auto k = H264Key(FragmentsPayload(3), 1);
	Fragmented fk = Fragment(1, k, true);
	CHECK(fk.headers.size() == 3);
	Feed(a, fk, 0, 100);
	// the last two fragments are lost and nothing else follows
	std::vector<NackEntry> nacks;
	bool key = false;
	a.Poll(101, nacks, key);
	CHECK(key); // nothing decodable yet: ask for a key frame once
	a.Poll(110, nacks, key);
	CHECK(!key);
	CHECK(nacks.size() == 1 && nacks[0].fragments.size() == 2 && nacks[0].fragments[0] == 1 && nacks[0].fragments[1] == 2);
}

TEST(assembler_gives_up_and_waits_for_key_frame)
{
	FrameAssembler::Config config;
	config.max_wait_ms = 150;
	config.keyframe_interval_ms = 500;
	FrameAssembler a(config);
	auto k = H264Key(300, 1);
	Fragmented fk = Fragment(1, k, true);
	Feed(a, fk, 0, 0);
	AssembledFrame out;
	CHECK(a.PopReady(out));

	std::vector<NackEntry> nacks;
	bool key = false;
	// frame 2 is lost for good; 3, 4, 5 arrive
	for(uint32_t seq = 3; seq <= 5; seq++)
	{
		Fragmented f = Fragment(seq, H264Delta(200, seq), false);
		Feed(a, f, 0, 100 + seq);
	}
	a.Poll(120, nacks, key);
	CHECK(!key && !nacks.empty());
	a.Poll(260, nacks, key);
	CHECK(key);
	CHECK(a.WaitingForKey());
	CHECK(!a.PopReady(out)); // 3..5 depend on 2 and must never be shown

	a.Poll(300, nacks, key);
	CHECK(!key); // not again before the interval
	a.Poll(770, nacks, key);
	CHECK(key);

	// more deltas: still nothing
	Fragmented f6 = Fragment(6, H264Delta(200, 6), false);
	Feed(a, f6, 0, 800);
	CHECK(!a.PopReady(out));

	// the new key frame and what follows it are delivered
	auto k7 = H264Key(400, 7);
	Fragmented f7 = Fragment(7, k7, true), f8 = Fragment(8, H264Delta(200, 8), false);
	Feed(a, f8, 0, 810);
	Feed(a, f7, 0, 811);
	CHECK(a.PopReady(out));
	CHECK(out.frame_seq == 7 && out.key && out.data == k7 && out.recovered);
	CHECK(out.frames_lost > 0);
	CHECK(a.PopReady(out));
	CHECK(out.frame_seq == 8 && !out.recovered && out.frames_lost == 0);
	CHECK(!a.WaitingForKey());
}

TEST(assembler_skips_ahead_to_a_complete_key_frame)
{
	FrameAssembler a;
	Fragmented fk = Fragment(1, H264Key(300, 1), true);
	Feed(a, fk, 0, 0);
	AssembledFrame out;
	CHECK(a.PopReady(out));
	// 2 and 3 are missing, 4 is a key frame: no reason to wait for 2 and 3
	auto k4 = H264Key(300, 4);
	Fragmented f4 = Fragment(4, k4, true);
	Feed(a, f4, 0, 10);
	CHECK(a.PopReady(out));
	CHECK(out.frame_seq == 4 && out.frames_lost == 2 && out.recovered);
	std::vector<NackEntry> nacks;
	bool key = false;
	a.Poll(100, nacks, key);
	CHECK(nacks.empty() && !key);
}

TEST(assembler_does_not_ask_for_everything_after_a_stall)
{
	FrameAssembler a;
	Fragmented fk = Fragment(1, H264Key(300, 1), true, 1000);
	Feed(a, fk, 0, 0);
	AssembledFrame out;
	CHECK(a.PopReady(out));
	CHECK(out.capture_ms == 1000);

	// The connection stalls for a second. Frames 2..60 are gone; 61 and 62 arrive.
	Fragmented f61 = Fragment(61, H264Delta(300, 61), false, 2000);
	Fragmented f62 = Fragment(62, H264Delta(300, 62), false, 2016);
	Feed(a, f61, 0, 1000);
	Feed(a, f62, 0, 1016);
	std::vector<NackEntry> nacks;
	bool key = false;
	a.Poll(1020, nacks, key);
	// asking for 59 frames again would flood the connection for nothing: straight to a key frame
	CHECK(nacks.empty());
	CHECK(key);
	CHECK(a.WaitingForKey());

	// A single lost frame right before, by contrast, is asked for
	FrameAssembler b;
	Fragmented gk = Fragment(1, H264Key(300, 1), true, 1000);
	Feed(b, gk, 0, 0);
	CHECK(b.PopReady(out));
	Fragmented g3 = Fragment(3, H264Delta(300, 3), false, 1033);
	Feed(b, g3, 0, 33);
	b.Poll(40, nacks, key);
	CHECK(nacks.size() == 1 && nacks[0].frame_seq == 2);
	CHECK(!key);
}

// ---------------------------------------------------------------- network (loopback)

struct TestHost
{
	HostConfig config;
	std::unique_ptr<Host> host;
	std::atomic<int> input_changes{0};
	std::atomic<int> keyframe_requests{0};
	std::atomic<int> guest_changes{0};
	std::mutex mutex;
	std::vector<std::pair<uint8_t, std::vector<uint8_t>>> voice; // slot, frame
	std::vector<std::pair<uint8_t, std::vector<uint8_t>>> cams;  // slot, picture
	std::vector<ChatMessage> chats;
	std::vector<BlobRef> chat_images;

	~TestHost() { host->Stop(); } // before the counters the callbacks touch go away

	explicit TestHost(size_t max_guests = 4, const std::vector<uint8_t> &avatar = {})
	{
		config.port = 0; // any free port
		config.secret = GenerateSecret();
		config.name = "Host";
		config.avatar = avatar;
		config.max_guests = max_guests;
		HostCallbacks cb;
		cb.on_input_changed = [this] { input_changes++; };
		cb.on_keyframe_needed = [this] { keyframe_requests++; };
		cb.on_guests_changed = [this] { guest_changes++; };
		cb.on_voice = [this](uint8_t slot, const uint8_t *d, size_t n) {
			std::lock_guard<std::mutex> lock(mutex);
			voice.emplace_back(slot, std::vector<uint8_t>(d, d + n));
		};
		cb.on_cam_frame = [this](uint8_t slot, const std::vector<uint8_t> &jpeg) {
			std::lock_guard<std::mutex> lock(mutex);
			cams.emplace_back(slot, jpeg);
		};
		cb.on_chat = [this](const ChatMessage &m) {
			std::lock_guard<std::mutex> lock(mutex);
			chats.push_back(m);
		};
		cb.on_chat_image = [this](const BlobRef &ref) {
			std::lock_guard<std::mutex> lock(mutex);
			chat_images.push_back(ref);
		};
		host = std::make_unique<Host>(config, cb);
		std::string error;
		bool ok = host->Start(&error);
		CHECK(ok);
		if(!ok)
			std::printf("    %s\n", error.c_str());
		StreamInfo info;
		info.codec = VideoCodec::H264;
		info.width = 1280;
		info.height = 720;
		info.fps = 60;
		host->SetStreamInfo(info);
	}

	Invite MakeInvite() const
	{
		Invite invite;
		invite.host = "127.0.0.1";
		invite.port = host->Port();
		invite.secret = config.secret;
		return invite;
	}
};

struct TestGuest
{
	std::unique_ptr<Client> client;
	std::mutex mutex;
	std::vector<SessionState> states;
	std::vector<std::vector<uint8_t>> configs;
	std::vector<AssembledFrame> frames;
	std::vector<std::vector<uint8_t>> audio;
	std::vector<std::vector<uint8_t>> haptics;
	std::atomic<int> disconnects{0};
	DisconnectReason reason = DisconnectReason::Timeout;
	std::vector<InputsSnapshot> inputs;
	std::vector<BlobRef> avatars;
	std::vector<std::pair<uint8_t, std::vector<uint8_t>>> voice; // slot, frame
	std::vector<std::pair<uint8_t, std::vector<uint8_t>>> cams;  // slot, picture
	std::vector<ChatMessage> chats;
	std::vector<BlobRef> chat_images;

	~TestGuest() { client->Stop(); } // callbacks use the members below

	TestGuest(const Invite &invite, const std::string &name, uint8_t want = kWantAll, uint32_t connect_timeout_ms = 3000,
		const std::vector<uint8_t> &avatar = {}, uint64_t player_id = 0, uint32_t host_timeout_ms = 1500)
	{
		ClientConfig config;
		config.invite = invite;
		config.name = name;
		config.avatar = avatar;
		config.player_id = player_id;
		config.want = want;
		config.controller = ControllerKind::Xbox;
		config.connect_timeout_ms = connect_timeout_ms;
		config.host_timeout_ms = host_timeout_ms;
		client = std::make_unique<Client>(config);
	}

	bool Connect(std::string *error = nullptr)
	{
		std::string e;
		bool ok = client->Connect(&e);
		if(error)
			*error = e;
		if(ok)
		{
			ClientCallbacks cb;
			cb.on_state = [this](const SessionState &s) {
				std::lock_guard<std::mutex> lock(mutex);
				states.push_back(s);
			};
			cb.on_video_config = [this](const uint8_t *d, size_t n) {
				std::lock_guard<std::mutex> lock(mutex);
				configs.emplace_back(d, d + n);
			};
			cb.on_video_frame = [this](const AssembledFrame &f) {
				std::lock_guard<std::mutex> lock(mutex);
				frames.push_back(f);
			};
			cb.on_audio = [this](const uint8_t *d, size_t n) {
				std::lock_guard<std::mutex> lock(mutex);
				audio.emplace_back(d, d + n);
			};
			cb.on_haptics = [this](const uint8_t *d, size_t n) {
				std::lock_guard<std::mutex> lock(mutex);
				haptics.emplace_back(d, d + n);
			};
			cb.on_disconnected = [this](DisconnectReason r) {
				reason = r;
				disconnects++;
			};
			cb.on_inputs = [this](const InputsSnapshot &snapshot) {
				std::lock_guard<std::mutex> lock(mutex);
				inputs.push_back(snapshot);
			};
			cb.on_avatar = [this](const BlobRef &ref) {
				std::lock_guard<std::mutex> lock(mutex);
				avatars.push_back(ref);
			};
			cb.on_voice = [this](uint8_t slot, const uint8_t *d, size_t n) {
				std::lock_guard<std::mutex> lock(mutex);
				voice.emplace_back(slot, std::vector<uint8_t>(d, d + n));
			};
			cb.on_cam_frame = [this](uint8_t slot, const std::vector<uint8_t> &jpeg) {
				std::lock_guard<std::mutex> lock(mutex);
				cams.emplace_back(slot, jpeg);
			};
			cb.on_chat = [this](const ChatMessage &m) {
				std::lock_guard<std::mutex> lock(mutex);
				chats.push_back(m);
			};
			cb.on_chat_image = [this](const BlobRef &ref) {
				std::lock_guard<std::mutex> lock(mutex);
				chat_images.push_back(ref);
			};
			client->Start(cb);
		}
		return ok;
	}

	SessionState LastState()
	{
		std::lock_guard<std::mutex> lock(mutex);
		return states.empty() ? SessionState() : states.back();
	}

	size_t FrameCount()
	{
		std::lock_guard<std::mutex> lock(mutex);
		return frames.size();
	}

	size_t VoiceCount(int slot = -1)
	{
		std::lock_guard<std::mutex> lock(mutex);
		size_t n = 0;
		for(const auto &v : voice)
			n += slot < 0 || v.first == slot;
		return n;
	}

	std::vector<ChatMessage> Chats()
	{
		std::lock_guard<std::mutex> lock(mutex);
		return chats;
	}

	size_t CamCount(int slot = -1)
	{
		std::lock_guard<std::mutex> lock(mutex);
		size_t n = 0;
		for(const auto &c : cams)
			n += slot < 0 || c.first == slot;
		return n;
	}
};

TEST(net_handshake_and_roster)
{
	TestHost h;
	h.host->SetAudioHeader({ true, 2, 16, 48000, 480, 0 });
	TestGuest g(h.MakeInvite(), "Friend");
	std::string error;
	CHECK(g.Connect(&error));
	if(!error.empty())
		std::printf("    %s\n", error.c_str());
	CHECK(g.client->Welcome().slot == 1);
	CHECK(g.client->Welcome().host_name == "Host");
	CHECK(g.client->Welcome().stream.width == 1280 && g.client->Welcome().stream.fps == 60);
	CHECK(g.client->Welcome().audio.valid && g.client->Welcome().audio.rate == 48000);

	CHECK(WaitFor([&] { return h.host->Guests().size() == 1; }, 2000));
	auto guests = h.host->Guests();
	CHECK(guests.size() == 1);
	if(guests.size() == 1)
	{
		CHECK(guests[0].name == "Friend" && guests[0].slot == 1 && guests[0].can_control);
		CHECK(guests[0].controller == ControllerKind::Xbox);
		CHECK(guests[0].want == kWantAll);
	}
	CHECK(h.guest_changes > 0);
	// a guest that wants video needs a key frame to start
	CHECK(WaitFor([&] { return h.keyframe_requests > 0; }, 2000));

	CHECK(WaitFor([&] { return g.LastState().participants.size() == 2; }, 2000));
	SessionState s = g.LastState();
	CHECK(s.your_slot == 1);
	CHECK(s.mix_mode == MixMode::Together);
	if(s.participants.size() == 2)
	{
		CHECK(s.participants[0].slot == 0 && (s.participants[0].flags & kParticipantHost) && s.participants[0].name == "Host");
		CHECK(s.participants[1].slot == 1 && s.participants[1].name == "Friend");
		CHECK(s.participants[1].flags & kParticipantCanControl);
	}

	// second guest gets the next slot and both see three participants
	TestGuest g2(h.MakeInvite(), "Other");
	CHECK(g2.Connect());
	CHECK(g2.client->Welcome().slot == 2);
	CHECK(WaitFor([&] { return g.LastState().participants.size() == 3 && g2.LastState().participants.size() == 3; }, 2000));

	// leaving is noticed right away
	g2.client->Stop();
	CHECK(WaitFor([&] { return h.host->Guests().size() == 1; }, 1000));
	CHECK(WaitFor([&] { return g.LastState().participants.size() == 2; }, 1000));

	// RTT gets measured on loopback
	CHECK(WaitFor([&] { return g.client->Stats().packets_received > 3; }, 2000));
	CHECK(g.client->Stats().rtt_ms < 50);
}

TEST(net_wrong_secret_gets_no_answer)
{
	TestHost h;
	Invite invite = h.MakeInvite();
	invite.secret[0] ^= 0x55;
	TestGuest g(invite, "Intruder", kWantAll, 600);
	std::string error;
	CHECK(!g.Connect(&error));
	CHECK(!error.empty());
	CHECK(h.host->Guests().empty());
}

TEST(net_garbage_packets_are_ignored)
{
	TestHost h;
	UdpSocket s;
	std::vector<Address> addrs;
	CHECK(Resolve("127.0.0.1", h.host->Port(), addrs, nullptr));
	CHECK(s.OpenFor(addrs[0], nullptr));
	std::mt19937 rng(42);
	for(int i = 0; i < 300; i++)
	{
		std::vector<uint8_t> junk(1 + rng() % 1400);
		for(auto &b : junk)
			b = static_cast<uint8_t>(rng());
		if(i % 3 == 0 && junk.size() > 2)
		{
			junk[0] = static_cast<uint8_t>(PacketType::Hello);
			junk[1] = kProtocolVersion;
		}
		s.SendTo(junk.data(), junk.size(), addrs[0]);
	}
	// nothing comes back, nobody joined, and a real guest can still connect
	uint8_t buf[2048];
	Address from;
	CHECK(s.RecvFrom(buf, sizeof(buf), from, 200) == 0);
	CHECK(h.host->Guests().empty());
	TestGuest g(h.MakeInvite(), "Friend");
	CHECK(g.Connect());
}

TEST(net_session_full)
{
	TestHost h(1);
	TestGuest g1(h.MakeInvite(), "One");
	CHECK(g1.Connect());
	TestGuest g2(h.MakeInvite(), "Two", kWantAll, 1500);
	std::string error;
	CHECK(!g2.Connect(&error));
	CHECK(error.find("full") != std::string::npos);
	CHECK(WaitFor([&] { return h.host->Guests().size() == 1; }, 1000));
}

TEST(net_input_reaches_host_and_is_merged)
{
	TestHost h;
	TestGuest g(h.MakeInvite(), "Friend", 0);
	CHECK(g.Connect());
	CHECK(WaitFor([&] { return h.host->Guests().size() == 1; }, 2000));

	h.host->SetLocalInput(Pad(kButtonCross));
	CHECK(h.host->MixedInput().buttons == kButtonCross);

	int before = h.input_changes;
	PadState guest_state = Pad(kButtonCircle, -20000, 15000, 0, 180);
	guest_state.gyro[2] = 0.5f;
	g.client->SetInput(guest_state);
	CHECK(WaitFor([&] { return h.host->MixedInput().buttons == (kButtonCross | kButtonCircle); }, 1000));
	CHECK(h.input_changes > before); // the host was told to fetch the new state
	PadState m = h.host->MixedInput();
	CHECK(m.left_x == -20000 && m.left_y == 15000 && m.r2 == 180);
	CHECK(m.gyro[2] == 0.5f);

	// release
	g.client->SetInput(Pad(0));
	CHECK(WaitFor([&] { return h.host->MixedInput().buttons == kButtonCross; }, 1000));

	// host takes the guest's control away
	g.client->SetInput(Pad(kButtonTriangle));
	CHECK(WaitFor([&] { return h.host->MixedInput().buttons == (kButtonCross | kButtonTriangle); }, 1000));
	h.host->SetGuestCanControl(1, false);
	CHECK(h.host->MixedInput().buttons == kButtonCross);
	CHECK(WaitFor([&] {
		SessionState s = g.LastState();
		return s.participants.size() == 2 && !(s.participants[1].flags & kParticipantCanControl);
	}, 1000));
	h.host->SetGuestCanControl(1, true);
	CHECK(WaitFor([&] { return h.host->MixedInput().buttons == (kButtonCross | kButtonTriangle); }, 1000));

	// watch-only mode for everybody
	h.host->SetMixMode(MixMode::HostOnly);
	CHECK(h.host->MixedInput().buttons == kButtonCross);
	CHECK(WaitFor([&] { return g.LastState().mix_mode == MixMode::HostOnly; }, 1000));
	h.host->SetMixMode(MixMode::Together);
}

TEST(net_held_button_is_released_when_guest_vanishes)
{
	HostConfig probe;
	TestHost h;
	TestGuest g(h.MakeInvite(), "Friend", 0);
	CHECK(g.Connect());
	g.client->SetInput(Pad(kButtonR1, 32767, 0, 0, 255));
	CHECK(WaitFor([&] { return h.host->MixedInput().buttons == kButtonR1; }, 1000));
	// The host's own notice about the press can come after the line above has already seen
	// it (on a slow machine by quite a bit). Wait until none is on its way any more, or it
	// would be taken for the release below.
	for(int seen = -1; seen != h.input_changes;)
	{
		seen = h.input_changes;
		SleepMs(120);
	}

	// the guest's process dies without saying goodbye: nothing it sends arrives any more
	g.client->SetSimulatedLoss(1.0);
	int before = h.input_changes;
	uint64_t t0 = NowMs();
	// The host is told on its own that the combined state changed (it does not have to ask),
	// so that it actually sends the released state to the console
	CHECK(WaitFor([&] { return h.input_changes > before; }, 2000));
	uint64_t took = NowMs() - t0;
	CHECK(h.host->MixedInput() == PadState());
	// well before the guest is dropped altogether, but not on a single late packet
	CHECK(took < probe.guest_timeout_ms / 2);
	CHECK(took >= 300);
	CHECK(h.host->Guests().size() == 1);
}

TEST(net_feedback_reaches_guest)
{
	TestHost h;
	TestGuest g(h.MakeInvite(), "Friend", 0);
	CHECK(g.Connect());
	CHECK(WaitFor([&] { return !g.LastState().participants.empty(); }, 2000));
	CHECK(g.LastState().feedback.rumble_seq == 0);

	h.host->SetRumble(200, 30);
	CHECK(WaitFor([&] { return g.LastState().feedback.rumble_left == 200; }, 1000));
	uint16_t seq1 = g.LastState().feedback.rumble_seq;
	CHECK(seq1 != 0 && g.LastState().feedback.rumble_right == 30);

	h.host->SetRumble(0, 0);
	CHECK(WaitFor([&] { return g.LastState().feedback.rumble_seq != seq1; }, 1000));
	CHECK(g.LastState().feedback.rumble_left == 0);

	uint8_t left[10], right[10];
	for(uint8_t i = 0; i < 10; i++)
	{
		left[i] = i;
		right[i] = static_cast<uint8_t>(100 + i);
	}
	h.host->SetTriggerEffects(0x21, 0x26, left, right);
	uint8_t led[3] = { 10, 20, 30 };
	h.host->SetLed(led);
	h.host->SetPlayerIndex(3);
	h.host->SetHapticIntensity(2);
	h.host->SetTriggerIntensity(1);
	h.host->NotifyMotionReset();
	CHECK(WaitFor([&] { return g.LastState().feedback.motion_reset_seq != 0; }, 1000));
	Feedback f = g.LastState().feedback;
	CHECK(f.trigger_seq != 0 && f.trigger_type_left == 0x21 && f.trigger_type_right == 0x26);
	CHECK(f.trigger_left[9] == 9 && f.trigger_right[0] == 100);
	CHECK(f.led_seq != 0 && f.led[0] == 10 && f.led[2] == 30);
	CHECK(f.player_index_seq != 0 && f.player_index == 3);
	CHECK(f.haptic_intensity_seq != 0 && f.haptic_intensity == 2);
	CHECK(f.trigger_intensity_seq != 0 && f.trigger_intensity == 1);
}

TEST(net_feedback_survives_packet_loss)
{
	TestHost h;
	TestGuest g(h.MakeInvite(), "Friend", 0);
	CHECK(g.Connect());
	CHECK(WaitFor([&] { return !g.LastState().participants.empty(); }, 2000));
	h.host->SetSimulatedLoss(0.6, 7);
	h.host->SetRumble(123, 45);
	// state is repeated, so it gets through although most packets are dropped
	CHECK(WaitFor([&] { return g.LastState().feedback.rumble_left == 123; }, 3000));
	h.host->SetRumble(0, 0);
	CHECK(WaitFor([&] { return g.LastState().feedback.rumble_left == 0; }, 3000));
}

TEST(net_audio_and_haptics)
{
	TestHost h;
	TestGuest all(h.MakeInvite(), "All");
	TestGuest pad_only(h.MakeInvite(), "PadOnly", 0);
	CHECK(all.Connect());
	CHECK(pad_only.Connect());
	CHECK(WaitFor([&] { return h.host->Guests().size() == 2; }, 2000));

	for(uint8_t i = 0; i < 50; i++)
	{
		std::vector<uint8_t> opus(120, i);
		h.host->PushAudio(opus.data(), opus.size());
		std::vector<uint8_t> pcm(120, static_cast<uint8_t>(200 - i));
		h.host->PushHaptics(pcm.data(), pcm.size());
		SleepMs(1);
	}
	CHECK(WaitFor([&] {
		std::lock_guard<std::mutex> lock(all.mutex);
		return all.audio.size() == 50 && all.haptics.size() == 50;
	}, 2000));
	{
		std::lock_guard<std::mutex> lock(all.mutex);
		bool in_order = all.audio.size() == 50;
		for(size_t i = 0; in_order && i < all.audio.size(); i++)
			in_order = all.audio[i].size() == 120 && all.audio[i][0] == i;
		CHECK(in_order);
		CHECK(all.haptics.size() == 50 && all.haptics[49][0] == 151);
	}
	// controller-only guests are not sent media they did not ask for
	SleepMs(100);
	{
		std::lock_guard<std::mutex> lock(pad_only.mutex);
		CHECK(pad_only.audio.empty() && pad_only.haptics.empty() && pad_only.frames.empty());
	}
	CHECK(pad_only.client->Stats().audio_frames == 0);
}

/** Plays the console: produces frames and answers key frame requests. */
struct FakeConsole
{
	TestHost &h;
	std::map<uint32_t, std::vector<uint8_t>> sent; // by order of sending, starting at 1
	uint32_t count = 0;
	int handled_keyframe_requests = 0;
	std::mt19937 rng{ 123 };

	explicit FakeConsole(TestHost &h) : h(h) {}

	void PushConfig()
	{
		auto config = H264Config();
		h.host->PushVideo(config.data(), config.size(), 0, false);
	}

	void PushFrame(bool force_key = false)
	{
		bool key = force_key || h.keyframe_requests > handled_keyframe_requests;
		handled_keyframe_requests = h.keyframe_requests;
		size_t size = key ? 20000 + rng() % 40000 : 1500 + rng() % 12000;
		auto frame = key ? H264Key(size, rng()) : H264Delta(size, rng());
		sent[++count] = frame;
		h.host->PushVideo(frame.data(), frame.size(), 0, false);
	}
};

/** Every delivered frame is intact, and no frame is delivered whose predecessor is missing. */
static void CheckDecodable(TestGuest &g, FakeConsole &console, size_t *delivered_out = nullptr)
{
	std::lock_guard<std::mutex> lock(g.mutex);
	bool intact = true;
	bool chain_ok = true;
	uint32_t prev = 0;
	// The host numbers only the frames it relays, so match by content
	std::map<std::vector<uint8_t>, uint32_t> index;
	for(auto &e : console.sent)
		index[e.second] = e.first;
	for(const AssembledFrame &f : g.frames)
	{
		auto it = index.find(f.data);
		if(it == index.end())
		{
			intact = false;
			continue;
		}
		uint32_t n = it->second;
		if(prev == 0)
		{
			if(!f.key)
				chain_ok = false; // must start with a key frame
		}
		else if(n != prev + 1 && !f.key)
			chain_ok = false; // a gap may only be followed by a key frame
		else if(n <= prev)
			chain_ok = false;
		prev = n;
	}
	CHECK(intact);
	CHECK(chain_ok);
	if(delivered_out)
		*delivered_out = g.frames.size();
}

TEST(net_video_without_loss)
{
	TestHost h;
	FakeConsole console(h);
	console.PushConfig();
	// frames before anyone joined are not relayed
	console.PushFrame(true);
	console.PushFrame();

	TestGuest g(h.MakeInvite(), "Friend");
	CHECK(g.Connect());
	CHECK(WaitFor([&] { return h.keyframe_requests > 0; }, 2000));
	for(int i = 0; i < 200; i++)
	{
		console.PushFrame();
		SleepMs(1);
	}
	CHECK(WaitFor([&] { return g.FrameCount() == 200; }, 3000));
	CHECK(g.FrameCount() == 200);
	{
		std::lock_guard<std::mutex> lock(g.mutex);
		CHECK(!g.configs.empty() && g.configs[0] == H264Config());
		CHECK(!g.frames.empty() && g.frames[0].key);
	}
	CheckDecodable(g, console);
	ClientStats stats = g.client->Stats();
	CHECK(stats.video.frames_dropped == 0);
}

TEST(net_video_recovers_from_packet_loss)
{
	TestHost h;
	FakeConsole console(h);
	console.PushConfig();
	TestGuest g(h.MakeInvite(), "Friend");
	CHECK(g.Connect());
	CHECK(WaitFor([&] { return h.keyframe_requests > 0; }, 2000));

	// 5% of all datagrams from the host are lost
	h.host->SetSimulatedLoss(0.05, 99);
	const int total = 300;
	for(int i = 0; i < total; i++)
	{
		console.PushFrame();
		SleepMs(5);
	}
	h.host->SetSimulatedLoss(0.0);
	// let retransmissions finish, then end on a clean frame
	SleepMs(300);
	console.PushFrame();
	CHECK(WaitFor([&] {
		std::lock_guard<std::mutex> lock(g.mutex);
		return !g.frames.empty() && g.frames.back().data == console.sent[console.count];
	}, 3000));

	size_t delivered = 0;
	CheckDecodable(g, console, &delivered);
	ClientStats stats = g.client->Stats();
	std::printf("    5%% loss: %zu of %d frames delivered, %llu fragments re-requested, %llu key frame requests\n",
		delivered, total + 1, static_cast<unsigned long long>(stats.video.nack_fragments),
		static_cast<unsigned long long>(stats.video.keyframe_requests));
	// With frames of ~6 fragments about a quarter of all frames lose a fragment.
	// Retransmission has to save nearly all of them.
	CHECK(delivered >= static_cast<size_t>(total) * 95 / 100);
	CHECK(stats.video.nack_fragments > 0);
}

TEST(net_video_recovers_from_a_blackout)
{
	TestHost h;
	FakeConsole console(h);
	console.PushConfig();
	TestGuest g(h.MakeInvite(), "Friend");
	CHECK(g.Connect());
	CHECK(WaitFor([&] { return h.keyframe_requests > 0; }, 2000));
	for(int i = 0; i < 30; i++)
	{
		console.PushFrame();
		SleepMs(2);
	}
	CHECK(WaitFor([&] { return g.FrameCount() == 30; }, 2000));

	// Frames two milliseconds apart for a stretch of time. By the clock, not by a number of
	// frames: where a 2 ms sleep takes several times as long (a busy or virtual machine), a
	// count would stretch the blackout below past the 1.5 s after which this guest gives the
	// host up, and the test would be about a lost host instead of a recovery.
	auto push_frames_for = [&](int ms) {
		auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
		while(std::chrono::steady_clock::now() < end)
		{
			console.PushFrame();
			SleepMs(2);
		}
	};

	// half a second during which nothing arrives: far more than can be retransmitted
	int requests_before = h.keyframe_requests;
	h.host->SetSimulatedLoss(1.0);
	push_frames_for(500);
	h.host->SetSimulatedLoss(0.0);
	push_frames_for(400);
	// the guest asked for a key frame, got one and is showing the latest frames again
	CHECK(h.keyframe_requests > requests_before);
	CHECK(WaitFor([&] {
		std::lock_guard<std::mutex> lock(g.mutex);
		return !g.frames.empty() && g.frames.back().data == console.sent[console.count];
	}, 3000));
	CheckDecodable(g, console);
	CHECK(g.client->Stats().video.frames_dropped > 0);
	CHECK(g.disconnects == 0);
}

TEST(net_switching_video_off_and_on)
{
	TestHost h;
	FakeConsole console(h);
	console.PushConfig();
	TestGuest g(h.MakeInvite(), "Friend");
	CHECK(g.Connect());
	CHECK(WaitFor([&] { return h.keyframe_requests > 0; }, 2000));
	for(int i = 0; i < 20; i++)
	{
		console.PushFrame();
		SleepMs(2);
	}
	CHECK(WaitFor([&] { return g.FrameCount() == 20; }, 2000));

	// controller only
	g.client->SetWant(0);
	CHECK(WaitFor([&] { return !h.host->AnyGuestWantsVideo(); }, 1000));
	for(int i = 0; i < 20; i++)
	{
		console.PushFrame();
		SleepMs(2);
	}
	SleepMs(100);
	CHECK(g.FrameCount() == 20);

	// video back on: starts again with a key frame
	int requests_before = h.keyframe_requests;
	g.client->SetWant(kWantAll);
	CHECK(WaitFor([&] { return h.keyframe_requests > requests_before; }, 1000));
	for(int i = 0; i < 20; i++)
	{
		console.PushFrame();
		SleepMs(2);
	}
	CHECK(WaitFor([&] { return g.FrameCount() == 40; }, 2000));
	{
		std::lock_guard<std::mutex> lock(g.mutex);
		CHECK(g.frames.size() > 20 && g.frames[20].key);
	}
	CheckDecodable(g, console);
}

TEST(net_kick_host_end_and_timeout)
{
	TestHost h;
	TestGuest kicked(h.MakeInvite(), "Kicked", 0);
	TestGuest stays(h.MakeInvite(), "Stays", 0);
	CHECK(kicked.Connect());
	CHECK(stays.Connect());
	CHECK(WaitFor([&] { return h.host->Guests().size() == 2; }, 2000));

	h.host->KickGuest(1);
	CHECK(WaitFor([&] { return kicked.disconnects == 1; }, 1000));
	CHECK(kicked.reason == DisconnectReason::Kicked);
	CHECK(h.host->Guests().size() == 1);
	// a kicked guest's packets no longer do anything
	kicked.client->SetInput(Pad(kButtonCross));
	SleepMs(100);
	CHECK(h.host->MixedInput().buttons == 0);

	h.host->Stop();
	CHECK(WaitFor([&] { return stays.disconnects == 1; }, 1000));
	CHECK(stays.reason == DisconnectReason::HostEnded);

	// a host that disappears without a word is noticed through the timeout (1.5 s in tests)
	TestHost h2;
	TestGuest g(h2.MakeInvite(), "Friend", 0);
	CHECK(g.Connect());
	CHECK(WaitFor([&] { return h2.host->Guests().size() == 1; }, 2000));
	h2.host->SetSimulatedLoss(1.0);
	CHECK(WaitFor([&] { return g.disconnects == 1; }, 4000));
	CHECK(g.reason == DisconnectReason::Timeout);
}

static std::vector<uint8_t> FakeImage(size_t size, uint32_t seed)
{
	std::vector<uint8_t> out(size);
	std::mt19937 rng(seed);
	for(auto &b : out)
		b = static_cast<uint8_t>(rng());
	return out;
}

TEST(blob_store)
{
	auto image = FakeImage(kBlobChunkSize * 3 + 100, 1);
	BlobRef ref = MakeBlobRef(image);
	CHECK(ref.Valid() && ref.size == image.size());
	CHECK(MakeBlobRef(FakeImage(100, 2)) != ref);
	CHECK(!MakeBlobRef({}).Valid());
	CHECK(!MakeBlobRef(FakeImage(kMaxAvatarSize + 1, 3)).Valid());
	CHECK(MakeBlobRef(FakeImage(kMaxAvatarSize, 3)).Valid());
	CHECK(BlobChunkCount(image.size()) == 4);

	BlobStore sender, receiver;
	CHECK(sender.Put(image) == ref);
	CHECK(sender.Has(ref) && !receiver.Has(ref));

	receiver.Want(ref);
	std::vector<BlobStore::Request> requests;
	receiver.Poll(1000, requests, 2);
	CHECK(requests.size() == 2 && requests[0].index == 0 && requests[1].index == 1);
	receiver.Poll(1001, requests, 8);
	CHECK(requests.size() == 2 && requests[0].index == 2 && requests[1].index == 3); // 0 and 1 were just asked for
	receiver.Poll(1100, requests, 8);
	CHECK(requests.empty());
	receiver.Poll(1300, requests, 8);
	CHECK(requests.size() == 4); // no answers: ask again

	auto deliver = [&](uint16_t index, BlobStore &to) {
		std::vector<uint8_t> payload;
		CHECK(BuildBlobChunk(ref.hash, image, index, payload));
		CHECK(payload.size() <= kMaxPayload);
		ByteReader r(payload.data(), payload.size());
		BlobChunkHeader h;
		CHECK(ReadBlobChunkHeader(r, h));
		return to.OnChunk(h, r.cursor(), r.remaining());
	};
	CHECK(!deliver(3, receiver));
	CHECK(!deliver(3, receiver)); // duplicate
	CHECK(!deliver(0, receiver));
	CHECK(!deliver(2, receiver));
	CHECK(!receiver.Has(ref));
	CHECK(deliver(1, receiver));
	CHECK(receiver.Has(ref));
	CHECK(*receiver.Get(ref.hash) == image);
	receiver.Poll(5000, requests, 8);
	CHECK(requests.empty());
	std::vector<uint8_t> payload;
	CHECK(!BuildBlobChunk(ref.hash, image, 4, payload));

	// Chunks nobody asked for are not stored
	BlobStore stranger;
	CHECK(!deliver(0, stranger));
	CHECK(!stranger.Wanted(ref.hash));

	// Data that does not match the announced hash is thrown away
	BlobStore fooled;
	auto other = FakeImage(image.size(), 99);
	fooled.Want(ref);
	for(uint16_t i = 0; i < 4; i++)
	{
		std::vector<uint8_t> p;
		BuildBlobChunk(ref.hash, other, i, p);
		ByteReader r(p.data(), p.size());
		BlobChunkHeader h;
		ReadBlobChunkHeader(r, h);
		CHECK(!fooled.OnChunk(h, r.cursor(), r.remaining()));
	}
	CHECK(!fooled.Has(ref));
	CHECK(!fooled.Get(ref.hash));
}

TEST(inputs_snapshot_round_trip)
{
	InputsSnapshot s;
	s.seq = 9;
	for(uint8_t i = 0; i <= kMaxGuests; i++)
	{
		PadState pad = Pad(kButtonCross << i, static_cast<int16_t>(-1000 * i), 77, i, static_cast<uint8_t>(255 - i));
		pad.right_y = -32768;
		pad.gyro[0] = 3.0f; // not part of a snapshot
		s.players.push_back(ToPlayerInput(i, pad));
	}
	s.combined = ToPlayerInput(0, Pad(kButtonPs, 5, 6, 7, 8));
	std::vector<uint8_t> buf;
	ByteWriter w(buf);
	WriteInputs(w, s);
	CHECK(buf.size() <= kMaxPayload);
	InputsSnapshot t;
	ByteReader r(buf.data(), buf.size());
	CHECK(ReadInputs(r, t));
	CHECK(t.seq == 9 && t.SameInputs(s));
	CHECK(t.players.size() == kMaxGuests + 1 && t.players[3].slot == 3 && t.players[3].right_y == -32768);
	CHECK(t.combined.buttons == kButtonPs && t.combined.r2 == 8);
	ByteReader cut(buf.data(), buf.size() - 1);
	CHECK(!ReadInputs(cut, t));
}

TEST(net_everyone_sees_what_everyone_presses)
{
	TestHost h;
	TestGuest watcher(h.MakeInvite(), "Watcher", kWantInputs);
	TestGuest player(h.MakeInvite(), "Player", 0);
	CHECK(watcher.Connect());
	CHECK(player.Connect());
	CHECK(WaitFor([&] { return h.host->Guests().size() == 2; }, 2000));

	h.host->SetLocalInput(Pad(kButtonSquare));
	player.client->SetInput(Pad(kButtonCross, 12000, -3000, 0, 99));

	auto last = [&](InputsSnapshot &out) {
		std::lock_guard<std::mutex> lock(watcher.mutex);
		if(watcher.inputs.empty())
			return false;
		out = watcher.inputs.back();
		return true;
	};
	CHECK(WaitFor([&] {
		InputsSnapshot s;
		return last(s) && s.players.size() == 3 && s.players[2].buttons == kButtonCross && s.players[0].buttons == kButtonSquare;
	}, 2000));
	InputsSnapshot s;
	CHECK(last(s));
	if(s.players.size() == 3)
	{
		CHECK(s.players[0].slot == 0 && s.players[1].slot == 1 && s.players[2].slot == 2);
		CHECK(s.players[1].buttons == 0); // the watcher itself presses nothing
		CHECK(s.players[2].left_x == 12000 && s.players[2].left_y == -3000 && s.players[2].r2 == 99);
		CHECK(s.combined.buttons == (kButtonSquare | kButtonCross) && s.combined.left_x == 12000);
	}

	// A player who may not control still shows what they press; the console just does not get it
	h.host->SetGuestCanControl(2, false);
	CHECK(WaitFor([&] {
		InputsSnapshot t;
		return last(t) && t.combined.buttons == kButtonSquare && t.players.size() == 3 && t.players[2].buttons == kButtonCross;
	}, 2000));

	// The host has the same view without asking anybody
	InputsSnapshot own = h.host->Inputs();
	CHECK(own.players.size() == 3 && own.players[2].buttons == kButtonCross && own.combined.buttons == kButtonSquare);

	// Guests that did not ask for it are not sent any
	{
		std::lock_guard<std::mutex> lock(player.mutex);
		CHECK(player.inputs.empty());
	}
}

TEST(net_pictures_reach_everyone)
{
	auto host_image = FakeImage(9000, 1);
	auto alice_image = FakeImage(20000, 2);
	auto bob_image = FakeImage(kMaxAvatarSize, 3);
	TestHost h(4, host_image);
	h.host->SetSimulatedLoss(0.1, 5); // chunks get lost and are asked for again

	TestGuest alice(h.MakeInvite(), "Alice", 0, 3000, alice_image);
	alice.client->SetSimulatedLoss(0.1, 6);
	CHECK(alice.Connect());

	BlobRef host_ref = MakeBlobRef(host_image), alice_ref = MakeBlobRef(alice_image), bob_ref = MakeBlobRef(bob_image);
	CHECK(h.host->OwnAvatar() == host_ref);
	CHECK(alice.client->OwnAvatar() == alice_ref);

	// Alice gets the host's picture, the host gets Alice's
	CHECK(WaitFor([&] { return alice.client->Avatar(host_ref.hash) != nullptr; }, 5000));
	CHECK(WaitFor([&] { return h.host->Avatar(alice_ref.hash) != nullptr; }, 5000));
	if(alice.client->Avatar(host_ref.hash))
		CHECK(*alice.client->Avatar(host_ref.hash) == host_image);
	if(h.host->Avatar(alice_ref.hash))
		CHECK(*h.host->Avatar(alice_ref.hash) == alice_image);
	CHECK(WaitFor([&] {
		auto guests = h.host->Guests();
		return guests.size() == 1 && guests[0].avatar == alice_ref;
	}, 2000));

	// Bob joins later and gets both, Alice gets Bob's through the host
	TestGuest bob(h.MakeInvite(), "Bob", 0, 3000, bob_image);
	CHECK(bob.Connect());
	CHECK(WaitFor([&] {
		return bob.client->Avatar(host_ref.hash) && bob.client->Avatar(alice_ref.hash) && alice.client->Avatar(bob_ref.hash);
	}, 8000));
	if(alice.client->Avatar(bob_ref.hash))
		CHECK(*alice.client->Avatar(bob_ref.hash) == bob_image);
	if(bob.client->Avatar(alice_ref.hash))
		CHECK(*bob.client->Avatar(alice_ref.hash) == alice_image);
	{
		std::lock_guard<std::mutex> lock(alice.mutex);
		CHECK(alice.avatars.size() == 2); // told once per picture
	}

	// The roster says whose picture is whose
	SessionState state = bob.LastState();
	CHECK(state.participants.size() == 3);
	if(state.participants.size() == 3)
	{
		CHECK(state.participants[0].avatar == host_ref);
		CHECK(state.participants[1].avatar == alice_ref);
		CHECK(state.participants[2].avatar == bob_ref);
	}

	// A player without a picture simply has none
	TestGuest carl(h.MakeInvite(), "Carl", 0);
	CHECK(carl.Connect());
	CHECK(WaitFor([&] { return carl.LastState().participants.size() == 4; }, 2000));
	CHECK(!carl.LastState().participants[3].avatar.Valid());
	CHECK(!carl.client->OwnAvatar().Valid());
}

TEST(net_battery_levels_reach_everyone)
{
	TestHost h;
	TestGuest a(h.MakeInvite(), "Alice", 0);
	TestGuest b(h.MakeInvite(), "Bob", 0);
	CHECK(a.Connect());
	CHECK(b.Connect());
	CHECK(WaitFor([&] { return b.LastState().participants.size() == 3; }, 2000));
	// nobody said anything yet
	for(const Participant &p : b.LastState().participants)
		CHECK(p.battery == kBatteryUnknown);

	h.host->SetBattery(kBatteryWired);
	a.client->SetBattery(37);
	CHECK(WaitFor([&] {
		SessionState s = b.LastState();
		return s.participants.size() == 3 && s.participants[0].battery == kBatteryWired && s.participants[1].battery == 37;
	}, 2000));
	CHECK(b.LastState().participants[2].battery == kBatteryUnknown);
	auto guests = h.host->Guests();
	CHECK(guests.size() == 2 && guests[0].battery == 37);

	// it drains
	a.client->SetBattery(12);
	CHECK(WaitFor([&] { return b.LastState().participants[1].battery == 12; }, 2000));
}

TEST(net_player_who_joins_again_gets_the_same_place)
{
	TestHost h;
	const uint64_t alice_id = 0x1122334455667788ull;
	auto alice = std::make_unique<TestGuest>(h.MakeInvite(), "Alice", 0, 3000, std::vector<uint8_t>(), alice_id);
	TestGuest bob(h.MakeInvite(), "Bob", 0, 3000, std::vector<uint8_t>(), 42);
	CHECK(alice->Connect());
	CHECK(bob.Connect());
	CHECK(alice->client->Welcome().slot == 1 && bob.client->Welcome().slot == 2);
	CHECK(WaitFor([&] { return h.host->Guests().size() == 2; }, 2000));

	// Alice's connection breaks: nothing she sends arrives, and she does not say goodbye
	alice->client->SetInput(Pad(kButtonCross));
	CHECK(WaitFor([&] { return h.host->MixedInput().buttons == kButtonCross; }, 1000));
	alice->client->SetSimulatedLoss(1.0);

	// She joins again right away. The host still has her old connection, which
	// would only time out after seconds; she must not show up twice.
	TestGuest again(h.MakeInvite(), "Alice", 0, 3000, std::vector<uint8_t>(), alice_id);
	CHECK(again.Connect());
	CHECK(again.client->Welcome().slot == 1);
	CHECK(WaitFor([&] { return h.host->Guests().size() == 2; }, 1000));
	auto guests = h.host->Guests();
	CHECK(guests.size() == 2);
	if(guests.size() == 2)
		CHECK(guests[0].slot == 1 && guests[0].name == "Alice" && guests[1].slot == 2);
	// and the button the broken connection was holding is released
	CHECK(WaitFor([&] { return h.host->MixedInput().buttons == 0; }, 1000));
	CHECK(WaitFor([&] { return bob.LastState().participants.size() == 3; }, 1000));

	// Somebody else (another id, or none) gets a place of their own
	TestGuest carl(h.MakeInvite(), "Carl", 0);
	CHECK(carl.Connect());
	CHECK(carl.client->Welcome().slot == 3);
	alice.reset();
}

TEST(net_host_says_when_it_is_coming_back)
{
	TestHost h;
	TestGuest g(h.MakeInvite(), "Friend", 0);
	CHECK(g.Connect());
	CHECK(WaitFor([&] { return h.host->Guests().size() == 1; }, 2000));
	uint16_t port = h.host->Port();

	// The host lost its console and will share again once it has it back
	h.host->StopForReconnect();
	CHECK(WaitFor([&] { return g.disconnects == 1; }, 1000));
	CHECK(g.reason == DisconnectReason::HostReconnecting);

	// It comes back on the same port with the same invite, and the guest can join again
	HostConfig config = h.config;
	config.port = port;
	Host back(config, HostCallbacks());
	std::string error;
	CHECK(back.Start(&error));
	Invite invite;
	invite.host = "127.0.0.1";
	invite.port = port;
	invite.secret = config.secret;
	TestGuest again(invite, "Friend", 0);
	CHECK(again.Connect());
	CHECK(WaitFor([&] { return back.Guests().size() == 1; }, 2000));
}

TEST(net_replayed_packets_are_ignored)
{
	// A recorded Input packet sent again later must not move the controller
	TestHost h;
	Invite invite = h.MakeInvite();

	// hand-rolled guest so that the raw packets are available
	Secret secret = invite.secret;
	UdpSocket s;
	std::vector<Address> addrs;
	CHECK(Resolve("127.0.0.1", invite.port, addrs, nullptr));
	CHECK(s.OpenFor(addrs[0], nullptr));
	HelloPayload hello;
	RandomBytes(hello.guest_nonce.data(), hello.guest_nonce.size());
	hello.want = 0;
	hello.name = "Raw";
	std::vector<uint8_t> body, packet;
	ByteWriter w(body);
	WriteHello(w, hello);
	CHECK(SealHandshakePacket(DeriveHelloKey(secret), PacketType::Hello, body.data(), body.size(), packet));
	s.SendTo(packet.data(), packet.size(), addrs[0]);
	uint8_t buf[2048];
	Address from;
	int n = s.RecvFrom(buf, sizeof(buf), from, 1000);
	CHECK(n > 0);
	std::vector<uint8_t> payload;
	CHECK(n > 0 && OpenPacket(DeriveWelcomeKey(secret), buf, static_cast<size_t>(n), payload));
	WelcomePayload welcome;
	ByteReader r(payload.data(), payload.size());
	CHECK(ReadWelcome(r, welcome));
	SessionKeys keys = DeriveSessionKeys(secret, hello.guest_nonce, welcome.host_nonce);

	auto input_packet = [&](uint64_t counter, uint32_t seq, uint32_t buttons) {
		std::vector<uint8_t> p, out;
		ByteWriter pw(p);
		pw.U32(seq);
		WritePadState(pw, Pad(buttons));
		SealPacket(keys.guest_to_host, PacketType::Input, welcome.conn_id, counter, p.data(), p.size(), out);
		return out;
	};
	auto press = input_packet(1, 1, kButtonCross);
	auto release = input_packet(2, 2, 0);
	s.SendTo(press.data(), press.size(), addrs[0]);
	CHECK(WaitFor([&] { return h.host->MixedInput().buttons == kButtonCross; }, 1000));
	s.SendTo(release.data(), release.size(), addrs[0]);
	CHECK(WaitFor([&] { return h.host->MixedInput().buttons == 0; }, 1000));

	// the attacker replays the recorded "press"
	for(int i = 0; i < 5; i++)
		s.SendTo(press.data(), press.size(), addrs[0]);
	SleepMs(150);
	CHECK(h.host->MixedInput().buttons == 0);

	// packets sealed for the other direction are not accepted either
	std::vector<uint8_t> p, wrong_dir;
	ByteWriter pw(p);
	pw.U32(3);
	WritePadState(pw, Pad(kButtonCircle));
	SealPacket(keys.host_to_guest, PacketType::Input, welcome.conn_id, 3, p.data(), p.size(), wrong_dir);
	s.SendTo(wrong_dir.data(), wrong_dir.size(), addrs[0]);
	SleepMs(100);
	CHECK(h.host->MixedInput().buttons == 0);

	// and a fresh, valid packet still works
	auto press2 = input_packet(3, 3, kButtonSquare);
	s.SendTo(press2.data(), press2.size(), addrs[0]);
	CHECK(WaitFor([&] { return h.host->MixedInput().buttons == kButtonSquare; }, 1000));
}

// ---------------------------------------------------------------- voice and webcams

static std::vector<uint8_t> FakePicture(size_t size, uint32_t seed)
{
	std::vector<uint8_t> d(size);
	std::mt19937 rng(seed);
	for(uint8_t &b : d)
		b = static_cast<uint8_t>(rng());
	if(size >= 3)
	{
		d[0] = 0xff; // looks like a JPEG from the outside
		d[1] = 0xd8;
		d[2] = 0xff;
	}
	return d;
}

struct CamPieces
{
	std::vector<CamFragmentHeader> headers;
	std::vector<std::vector<uint8_t>> bodies;
};

static CamPieces CutPicture(uint32_t seq, const std::vector<uint8_t> &picture)
{
	CamPieces out;
	std::vector<uint8_t> payload;
	for(uint32_t i = 0; BuildCamFragment(seq, picture.data(), picture.size(), i, payload); i++)
	{
		ByteReader r(payload.data(), payload.size());
		CamFragmentHeader h;
		bool ok = ReadCamFragmentHeader(r, h);
		CHECK(ok);
		out.headers.push_back(h);
		out.bodies.emplace_back(r.cursor(), r.cursor() + r.remaining());
	}
	return out;
}

TEST(cam_fragments)
{
	std::vector<uint8_t> picture = FakePicture(3 * kCamFragmentSize + 17, 1);
	CamPieces p = CutPicture(5, picture);
	CHECK(p.headers.size() == 4);
	CHECK(p.bodies[0].size() == kCamFragmentSize && p.bodies[3].size() == 17);
	CHECK(p.headers[2].frag_index == 2 && p.headers[2].frag_count == 4 && p.headers[2].total_size == picture.size());

	// one fragment exactly, and one byte more
	CHECK(CutPicture(1, FakePicture(kCamFragmentSize, 2)).headers.size() == 1);
	CHECK(CutPicture(1, FakePicture(kCamFragmentSize + 1, 2)).headers.size() == 2);

	// every packet fits a datagram even with the sender's slot in front
	CHECK(1 + kCamFragmentHeaderSize + kCamFragmentSize <= kMaxPayload);
	CHECK(5 + kMaxVoiceFrameSize <= kMaxPayload);

	std::vector<uint8_t> payload;
	CHECK(!BuildCamFragment(1, picture.data(), picture.size(), 4, payload));
	CHECK(!BuildCamFragment(1, picture.data(), 0, 0, payload));
	std::vector<uint8_t> huge(kMaxCamFrameSize + 1);
	CHECK(!BuildCamFragment(1, huge.data(), huge.size(), 0, payload));

	// headers that do not add up are turned down
	CHECK(BuildCamFragment(1, picture.data(), picture.size(), 1, payload));
	for(size_t cut = 0; cut < payload.size(); cut++)
	{
		ByteReader r(payload.data(), cut);
		CamFragmentHeader h;
		CHECK(!ReadCamFragmentHeader(r, h));
	}
	std::vector<uint8_t> bad = payload;
	bad[7] = 9; // frag_count
	{
		ByteReader r(bad.data(), bad.size());
		CamFragmentHeader h;
		CHECK(!ReadCamFragmentHeader(r, h));
	}
}

TEST(cam_assembler)
{
	CamAssembler a;
	std::vector<uint8_t> one = FakePicture(5000, 1);
	std::vector<uint8_t> two = FakePicture(4000, 2);
	std::vector<uint8_t> three = FakePicture(6000, 3);
	CamPieces p1 = CutPicture(1, one);
	CamPieces p2 = CutPicture(2, two);
	CamPieces p3 = CutPicture(3, three);
	uint64_t now = 1000;

	// in any order, complete exactly once
	std::vector<size_t> order = { 3, 0, 4, 1 };
	for(size_t i : order)
		CHECK(!a.OnFragment(p1.headers[i], p1.bodies[i].data(), p1.bodies[i].size(), now));
	CHECK(a.OnFragment(p1.headers[2], p1.bodies[2].data(), p1.bodies[2].size(), now));
	CHECK(a.Frame() == one);
	// a duplicate afterwards does not deliver it again
	CHECK(!a.OnFragment(p1.headers[2], p1.bodies[2].data(), p1.bodies[2].size(), now));

	// picture 2 loses a fragment: picture 3 simply takes over
	CHECK(!a.OnFragment(p2.headers[0], p2.bodies[0].data(), p2.bodies[0].size(), now));
	CHECK(!a.OnFragment(p2.headers[1], p2.bodies[1].data(), p2.bodies[1].size(), now));
	bool done = false;
	for(size_t i = 0; i < p3.headers.size(); i++)
		done = a.OnFragment(p3.headers[i], p3.bodies[i].data(), p3.bodies[i].size(), now);
	CHECK(done);
	CHECK(a.Frame() == three);
	// the rest of picture 2 arriving late changes nothing
	for(size_t i = 2; i < p2.headers.size(); i++)
		CHECK(!a.OnFragment(p2.headers[i], p2.bodies[i].data(), p2.bodies[i].size(), now));
	CHECK(a.Frame() == three);

	// a sender that starts over after a pause is accepted again
	now += 5000;
	done = false;
	for(size_t i = 0; i < p1.headers.size(); i++)
		done = a.OnFragment(p1.headers[i], p1.bodies[i].data(), p1.bodies[i].size(), now);
	CHECK(done);
	CHECK(a.Frame() == one);

	// fragments whose numbers do not fit the picture
	CamFragmentHeader h = p2.headers[0];
	h.frag_index = 9;
	CHECK(!a.OnFragment(h, p2.bodies[0].data(), p2.bodies[0].size(), now));
	h = p2.headers[0];
	h.total_size = 10; // does not hold a full fragment
	CHECK(!a.OnFragment(h, p2.bodies[0].data(), p2.bodies[0].size(), now));
}

TEST(voice_sequence)
{
	VoiceSequence v;
	CHECK(v.Accept(10, 100));
	CHECK(!v.Accept(10, 110)); // duplicate
	CHECK(!v.Accept(9, 120));  // late
	CHECK(v.Accept(12, 130));  // 11 was lost
	CHECK(!v.Accept(11, 140));
	// numbering wraps around
	VoiceSequence w;
	CHECK(w.Accept(0xfffffffful, 100));
	CHECK(w.Accept(0, 120));
	CHECK(!w.Accept(0xfffffffful, 130));
	// after a pause the speaker may start over
	CHECK(v.Accept(1, 5000));
	CHECK(v.Accept(2, 5020));
}

TEST(net_media_flags_reach_everyone)
{
	TestHost h;
	TestGuest a(h.MakeInvite(), "A", kWantAll | kWantVoice | kWantCams);
	TestGuest b(h.MakeInvite(), "B", kWantAll | kWantVoice | kWantCams);
	CHECK(a.Connect());
	CHECK(b.Connect());
	CHECK(WaitFor([&] { return h.host->Guests().size() == 2; }, 2000));
	uint8_t slot_a = a.client->Welcome().slot;

	auto media_of = [](TestGuest &g, uint8_t slot) -> int {
		SessionState s = g.LastState();
		for(const Participant &p : s.participants)
		{
			if(p.slot == slot)
				return p.media;
		}
		return -1;
	};

	CHECK(WaitFor([&] { return media_of(b, slot_a) == 0 && media_of(b, 0) == 0; }, 2000));

	a.client->SetMedia(kMediaVoice | kMediaCamera);
	CHECK(WaitFor([&] { return media_of(b, slot_a) == (kMediaVoice | kMediaCamera); }, 2000));
	CHECK(WaitFor([&] { return media_of(a, slot_a) == (kMediaVoice | kMediaCamera); }, 2000));
	{
		std::vector<GuestInfo> guests = h.host->Guests();
		bool found = false;
		for(const GuestInfo &g : guests)
			found = found || (g.slot == slot_a && g.media == (kMediaVoice | kMediaCamera));
		CHECK(found);
	}

	a.client->SetMedia(kMediaVoice | kMediaMuted);
	CHECK(WaitFor([&] { return media_of(b, slot_a) == (kMediaVoice | kMediaMuted); }, 2000));

	// a guest cannot claim what only the host decides
	a.client->SetMedia(0xff);
	CHECK(WaitFor([&] { return media_of(b, slot_a) == kMediaOwnMask; }, 2000));

	h.host->SetLocalMedia(kMediaVoice);
	CHECK(h.host->LocalMedia() == kMediaVoice);
	CHECK(WaitFor([&] { return media_of(a, 0) == kMediaVoice && media_of(b, 0) == kMediaVoice; }, 2000));

	h.host->SetGuestSilenced(slot_a, true);
	CHECK(WaitFor([&] { return media_of(b, slot_a) == (kMediaOwnMask | kMediaSilenced); }, 2000));
	CHECK(WaitFor([&] { return media_of(a, slot_a) == (kMediaOwnMask | kMediaSilenced); }, 2000));
	h.host->SetGuestSilenced(slot_a, false);
	CHECK(WaitFor([&] { return media_of(b, slot_a) == kMediaOwnMask; }, 2000));
}

TEST(net_voice_reaches_everyone_but_the_speaker)
{
	TestHost h;
	TestGuest a(h.MakeInvite(), "A", kWantAll | kWantVoice);
	TestGuest b(h.MakeInvite(), "B", kWantAll | kWantVoice);
	TestGuest deaf(h.MakeInvite(), "Deaf"); // did not ask for voices
	CHECK(a.Connect());
	CHECK(b.Connect());
	CHECK(deaf.Connect());
	CHECK(WaitFor([&] { return h.host->Guests().size() == 3; }, 2000));
	uint8_t slot_a = a.client->Welcome().slot;
	uint8_t slot_b = b.client->Welcome().slot;

	for(uint8_t i = 0; i < 40; i++)
	{
		std::vector<uint8_t> frame(60 + i, i);
		a.client->SendVoice(frame.data(), frame.size());
		SleepMs(1);
	}
	CHECK(WaitFor([&] { return b.VoiceCount(slot_a) == 40; }, 2000));
	CHECK(WaitFor([&] {
		std::lock_guard<std::mutex> lock(h.mutex);
		return h.voice.size() == 40;
	}, 2000));
	{
		std::lock_guard<std::mutex> lock(b.mutex);
		bool intact = b.voice.size() == 40;
		for(size_t i = 0; intact && i < b.voice.size(); i++)
			intact = b.voice[i].first == slot_a && b.voice[i].second.size() == 60 + i && b.voice[i].second[0] == i;
		CHECK(intact);
	}
	{
		std::lock_guard<std::mutex> lock(h.mutex);
		bool intact = h.voice.size() == 40;
		for(size_t i = 0; intact && i < h.voice.size(); i++)
			intact = h.voice[i].first == slot_a && h.voice[i].second.size() == 60 + i;
		CHECK(intact);
	}

	// the host talks: both hear it under slot 0
	for(uint8_t i = 0; i < 25; i++)
	{
		std::vector<uint8_t> frame(80, static_cast<uint8_t>(100 + i));
		h.host->PushVoice(frame.data(), frame.size());
		SleepMs(1);
	}
	CHECK(WaitFor([&] { return a.VoiceCount(0) == 25 && b.VoiceCount(0) == 25; }, 2000));

	// B answers
	std::vector<uint8_t> reply(50, 7);
	b.client->SendVoice(reply.data(), reply.size());
	CHECK(WaitFor([&] { return a.VoiceCount(slot_b) == 1; }, 2000));

	SleepMs(100);
	CHECK(a.VoiceCount(slot_a) == 0); // nobody hears their own voice back
	CHECK(b.VoiceCount(slot_b) == 0);
	CHECK(deaf.VoiceCount() == 0);
	CHECK(deaf.client->Stats().voice_frames == 0);
	CHECK(b.client->Stats().voice_frames == 65);

	// oversized and empty frames never leave
	std::vector<uint8_t> big(kMaxVoiceFrameSize + 1, 1);
	a.client->SendVoice(big.data(), big.size());
	a.client->SendVoice(big.data(), 0);
	SleepMs(100);
	CHECK(b.VoiceCount(slot_a) == 40);
}

TEST(net_silenced_guest_is_not_heard)
{
	TestHost h;
	TestGuest a(h.MakeInvite(), "A", kWantAll | kWantVoice);
	TestGuest b(h.MakeInvite(), "B", kWantAll | kWantVoice);
	CHECK(a.Connect());
	CHECK(b.Connect());
	CHECK(WaitFor([&] { return h.host->Guests().size() == 2; }, 2000));
	uint8_t slot_a = a.client->Welcome().slot;

	std::vector<uint8_t> frame(70, 1);
	a.client->SendVoice(frame.data(), frame.size());
	CHECK(WaitFor([&] { return b.VoiceCount(slot_a) == 1; }, 2000));

	h.host->SetGuestSilenced(slot_a, true);
	for(int i = 0; i < 10; i++)
	{
		a.client->SendVoice(frame.data(), frame.size());
		SleepMs(2);
	}
	SleepMs(150);
	CHECK(b.VoiceCount(slot_a) == 1);
	{
		std::lock_guard<std::mutex> lock(h.mutex);
		CHECK(h.voice.size() == 1);
	}
	// A still hears the others
	h.host->PushVoice(frame.data(), frame.size());
	CHECK(WaitFor([&] { return a.VoiceCount(0) == 1; }, 2000));

	h.host->SetGuestSilenced(slot_a, false);
	a.client->SendVoice(frame.data(), frame.size());
	CHECK(WaitFor([&] { return b.VoiceCount(slot_a) == 2; }, 2000));
}

TEST(net_webcam_pictures_reach_everyone)
{
	TestHost h;
	TestGuest a(h.MakeInvite(), "A", kWantAll | kWantCams);
	TestGuest b(h.MakeInvite(), "B", kWantAll | kWantCams);
	TestGuest blind(h.MakeInvite(), "Blind"); // did not ask for webcams
	CHECK(a.Connect());
	CHECK(b.Connect());
	CHECK(blind.Connect());
	CHECK(WaitFor([&] { return h.host->Guests().size() == 3; }, 2000));
	uint8_t slot_a = a.client->Welcome().slot;

	std::vector<std::vector<uint8_t>> pictures;
	for(uint32_t i = 0; i < 12; i++)
		pictures.push_back(FakePicture(9000 + i * 700, 100 + i));
	for(const auto &picture : pictures)
	{
		a.client->SendCamFrame(picture.data(), picture.size());
		SleepMs(5);
	}
	CHECK(WaitFor([&] { return b.CamCount(slot_a) == pictures.size(); }, 3000));
	CHECK(WaitFor([&] {
		std::lock_guard<std::mutex> lock(h.mutex);
		return h.cams.size() == pictures.size();
	}, 3000));
	{
		std::lock_guard<std::mutex> lock(b.mutex);
		bool intact = b.cams.size() == pictures.size();
		for(size_t i = 0; intact && i < b.cams.size(); i++)
			intact = b.cams[i].first == slot_a && b.cams[i].second == pictures[i];
		CHECK(intact);
	}
	{
		std::lock_guard<std::mutex> lock(h.mutex);
		bool intact = h.cams.size() == pictures.size();
		for(size_t i = 0; intact && i < h.cams.size(); i++)
			intact = h.cams[i].first == slot_a && h.cams[i].second == pictures[i];
		CHECK(intact);
	}

	// the host's own webcam
	std::vector<uint8_t> host_picture = FakePicture(30000, 7);
	h.host->PushCamFrame(host_picture.data(), host_picture.size());
	CHECK(WaitFor([&] { return a.CamCount(0) == 1 && b.CamCount(0) == 1; }, 3000));
	{
		std::lock_guard<std::mutex> lock(a.mutex);
		CHECK(a.cams.back().second == host_picture);
	}

	SleepMs(100);
	CHECK(a.CamCount(slot_a) == 0); // not back to where it came from
	CHECK(blind.CamCount() == 0);
	CHECK(blind.client->Stats().cam_frames == 0);

	// too large for a picture: nothing is sent
	std::vector<uint8_t> huge(kMaxCamFrameSize + 1, 1);
	a.client->SendCamFrame(huge.data(), huge.size());
	SleepMs(100);
	CHECK(b.CamCount(slot_a) == pictures.size());
}

TEST(net_webcam_pictures_arrive_whole_or_not_at_all)
{
	TestHost h;
	TestGuest a(h.MakeInvite(), "A", kWantAll | kWantCams);
	TestGuest b(h.MakeInvite(), "B", kWantAll | kWantCams);
	CHECK(a.Connect());
	CHECK(b.Connect());
	CHECK(WaitFor([&] { return h.host->Guests().size() == 2; }, 2000));
	uint8_t slot_a = a.client->Welcome().slot;

	a.client->SetSimulatedLoss(0.03, 5);
	h.host->SetSimulatedLoss(0.03, 6);

	std::map<uint32_t, std::vector<uint8_t>> sent; // by seed, which is in the picture
	for(uint32_t i = 0; i < 150; i++)
	{
		std::vector<uint8_t> picture = FakePicture(8000, 1000 + i);
		picture[3] = static_cast<uint8_t>(i);
		sent[i] = picture;
		a.client->SendCamFrame(picture.data(), picture.size());
		SleepMs(3);
	}
	SleepMs(300);
	std::lock_guard<std::mutex> lock(b.mutex);
	size_t got = 0;
	bool intact = true;
	int last = -1;
	for(const auto &c : b.cams)
	{
		if(c.first != slot_a)
			continue;
		got++;
		uint32_t index = c.second.size() > 3 ? c.second[3] : 999;
		intact = intact && sent.count(index) && sent[index] == c.second;
		intact = intact && static_cast<int>(index) > last; // never an older picture after a newer one
		last = static_cast<int>(index);
	}
	CHECK(intact);
	// 7 fragments each way at 3 % loss: about two thirds get through
	CHECK(got > 60 && got < 150);
}

// ---------------------------------------------------------------- public address (STUN)

// The STUN messages for these tests are put together by hand, so that they do
// not share a mistake with the code they test.

static const uint8_t kStunCookie[4] = { 0x21, 0x12, 0xa4, 0x42 };
static const uint16_t kStunSuccess = 0x0101;
static const uint16_t kStunError = 0x0111;
static const uint16_t kStunMapped = 0x0001;
static const uint16_t kStunXorMapped = 0x0020;

using Datagrams = std::vector<std::vector<uint8_t>>;

static void PutU16(std::vector<uint8_t> &out, uint16_t v)
{
	out.push_back(static_cast<uint8_t>(v >> 8));
	out.push_back(static_cast<uint8_t>(v));
}

static std::vector<uint8_t> StunAttr(uint16_t type, const std::vector<uint8_t> &value)
{
	std::vector<uint8_t> out;
	PutU16(out, type);
	PutU16(out, static_cast<uint16_t>(value.size()));
	out.insert(out.end(), value.begin(), value.end());
	while(out.size() % 4)
		out.push_back(0);
	return out;
}

/** Value of XOR-MAPPED-ADDRESS (or, not xored, of MAPPED-ADDRESS) for 4 or 16 address bytes. */
static std::vector<uint8_t> StunAddress(bool xored, const uint8_t *id, std::vector<uint8_t> address, uint16_t port)
{
	if(xored)
	{
		port ^= 0x2112;
		for(size_t i = 0; i < address.size(); i++)
			address[i] ^= i < 4 ? kStunCookie[i] : id[i - 4];
	}
	std::vector<uint8_t> out = { 0, static_cast<uint8_t>(address.size() == 4 ? 1 : 2) };
	PutU16(out, port);
	out.insert(out.end(), address.begin(), address.end());
	return out;
}

static std::vector<uint8_t> StunMessage(uint16_t type, const uint8_t *id, const std::vector<std::vector<uint8_t>> &attrs)
{
	std::vector<uint8_t> body;
	for(const auto &a : attrs)
		body.insert(body.end(), a.begin(), a.end());
	std::vector<uint8_t> out;
	PutU16(out, type);
	PutU16(out, static_cast<uint16_t>(body.size()));
	out.insert(out.end(), kStunCookie, kStunCookie + 4);
	out.insert(out.end(), id, id + 12);
	out.insert(out.end(), body.begin(), body.end());
	return out;
}

/** The usual answer: here is your address. */
static std::vector<uint8_t> StunAnswer(const uint8_t *id, const std::vector<uint8_t> &address, uint16_t port)
{
	return StunMessage(kStunSuccess, id, { StunAttr(kStunXorMapped, StunAddress(true, id, address, port)) });
}

// The transaction id and the answers of the test vectors in RFC 5769 (sections 2.2 and 2.3)
static const StunTransactionId kRfcStunId = { 0xb7, 0xe7, 0xa7, 0x01, 0xbc, 0x34, 0xd6, 0x86, 0xfa, 0x87, 0xdf, 0xae };
static const uint8_t kRfcStunAnswerIPv4[] = {
	0x01, 0x01, 0x00, 0x3c, 0x21, 0x12, 0xa4, 0x42, 0xb7, 0xe7, 0xa7, 0x01, 0xbc, 0x34, 0xd6, 0x86,
	0xfa, 0x87, 0xdf, 0xae,
	// SOFTWARE
	0x80, 0x22, 0x00, 0x0b, 0x74, 0x65, 0x73, 0x74, 0x20, 0x76, 0x65, 0x63, 0x74, 0x6f, 0x72, 0x20,
	// XOR-MAPPED-ADDRESS
	0x00, 0x20, 0x00, 0x08, 0x00, 0x01, 0xa1, 0x47, 0xe1, 0x12, 0xa6, 0x43,
	// MESSAGE-INTEGRITY
	0x00, 0x08, 0x00, 0x14, 0x2b, 0x91, 0xf5, 0x99, 0xfd, 0x9e, 0x90, 0xc3, 0x8c, 0x74, 0x89, 0xf9,
	0x2f, 0x2a, 0xf9, 0xba, 0x53, 0xf0, 0x6b, 0xe7,
	// FINGERPRINT
	0x80, 0x28, 0x00, 0x04, 0xc0, 0x7d, 0x4c, 0x96
};
static const uint8_t kRfcStunAnswerIPv6[] = {
	0x01, 0x01, 0x00, 0x48, 0x21, 0x12, 0xa4, 0x42, 0xb7, 0xe7, 0xa7, 0x01, 0xbc, 0x34, 0xd6, 0x86,
	0xfa, 0x87, 0xdf, 0xae,
	0x80, 0x22, 0x00, 0x0b, 0x74, 0x65, 0x73, 0x74, 0x20, 0x76, 0x65, 0x63, 0x74, 0x6f, 0x72, 0x20,
	0x00, 0x20, 0x00, 0x14, 0x00, 0x02, 0xa1, 0x47, 0x01, 0x13, 0xa9, 0xfa, 0xa5, 0xd3, 0xf1, 0x79,
	0xbc, 0x25, 0xf4, 0xb5, 0xbe, 0xd2, 0xb9, 0xd9,
	0x00, 0x08, 0x00, 0x14, 0xa3, 0x82, 0x95, 0x4e, 0x4b, 0xe6, 0x7b, 0xf1, 0x17, 0x84, 0xc9, 0x7c,
	0x82, 0x92, 0xc2, 0x75, 0xbf, 0xe3, 0xed, 0x41,
	0x80, 0x28, 0x00, 0x04, 0xc8, 0xfb, 0x0b, 0x4c
};

static const std::vector<uint8_t> kStunTestIPv4 = { 198, 51, 100, 23 };
static const std::vector<uint8_t> kStunTestIPv6 = { 0x20, 0x01, 0x0d, 0xb8, 0x85, 0xa3, 0, 0, 0, 0, 0x8a, 0x2e, 0x03, 0x70, 0x73, 0x34 };

TEST(stun_request_and_rfc5769_answers)
{
	std::vector<uint8_t> request = StunBindingRequest(kRfcStunId);
	static const uint8_t header[8] = { 0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xa4, 0x42 };
	CHECK(request.size() == 20);
	CHECK(request.size() == 20 && std::memcmp(request.data(), header, 8) == 0);
	CHECK(request.size() == 20 && std::memcmp(request.data() + 8, kRfcStunId.data(), 12) == 0);

	std::string address, error;
	uint16_t port = 0;
	CHECK(StunParseBindingResponse(kRfcStunAnswerIPv4, sizeof(kRfcStunAnswerIPv4), kRfcStunId, address, &port, &error));
	CHECK(address == "192.0.2.1" && port == 32853);
	port = 0;
	CHECK(StunParseBindingResponse(kRfcStunAnswerIPv6, sizeof(kRfcStunAnswerIPv6), kRfcStunId, address, &port, &error));
	CHECK(address == "2001:db8:1234:5678:11:2233:4455:6677" && port == 32853);
	// the port and the reason are optional
	address.clear();
	CHECK(StunParseBindingResponse(kRfcStunAnswerIPv4, sizeof(kRfcStunAnswerIPv4), kRfcStunId, address, nullptr, nullptr));
	CHECK(address == "192.0.2.1");
}

TEST(stun_takes_the_right_address_attribute)
{
	const uint8_t *id = kRfcStunId.data();
	const std::vector<uint8_t> b = { 203, 0, 113, 7 };
	std::string address, error;
	uint16_t port = 0;

	// both kinds: the scrambled one counts, in whichever order they come
	std::vector<uint8_t> m = StunMessage(kStunSuccess, id,
		{ StunAttr(kStunMapped, StunAddress(false, id, b, 1111)), StunAttr(kStunXorMapped, StunAddress(true, id, kStunTestIPv4, 40001)) });
	CHECK(StunParseBindingResponse(m.data(), m.size(), kRfcStunId, address, &port, &error));
	CHECK(address == "198.51.100.23" && port == 40001);
	m = StunMessage(kStunSuccess, id,
		{ StunAttr(kStunXorMapped, StunAddress(true, id, kStunTestIPv4, 40001)), StunAttr(kStunMapped, StunAddress(false, id, b, 1111)) });
	CHECK(StunParseBindingResponse(m.data(), m.size(), kRfcStunId, address, &port, &error));
	CHECK(address == "198.51.100.23" && port == 40001);

	// a server from before RFC 5389 only sends the plain one
	m = StunMessage(kStunSuccess, id, { StunAttr(kStunMapped, StunAddress(false, id, b, 1111)) });
	CHECK(StunParseBindingResponse(m.data(), m.size(), kRfcStunId, address, &port, &error));
	CHECK(address == "203.0.113.7" && port == 1111);

	// the number the scrambled one had in the drafts
	m = StunMessage(kStunSuccess, id, { StunAttr(0x8020, StunAddress(true, id, kStunTestIPv4, 40002)) });
	CHECK(StunParseBindingResponse(m.data(), m.size(), kRfcStunId, address, &port, &error));
	CHECK(address == "198.51.100.23" && port == 40002);

	// a scrambled one that cannot be read does not hide a plain one that can
	m = StunMessage(kStunSuccess, id,
		{ StunAttr(kStunXorMapped, { 0, 9, 1, 2, 3, 4, 5, 6 }), StunAttr(kStunMapped, StunAddress(false, id, b, 1111)) });
	CHECK(StunParseBindingResponse(m.data(), m.size(), kRfcStunId, address, &port, &error));
	CHECK(address == "203.0.113.7" && port == 1111);

	// attributes we do not know are skipped, padding included
	m = StunMessage(kStunSuccess, id,
		{ StunAttr(0x8022, { 'a', 'b', 'c', 'd', 'e' }), StunAttr(0x7777, {}),
			StunAttr(kStunXorMapped, StunAddress(true, id, kStunTestIPv4, 40001)), StunAttr(0x8028, { 1, 2, 3, 4 }) });
	CHECK(StunParseBindingResponse(m.data(), m.size(), kRfcStunId, address, &port, &error));
	CHECK(address == "198.51.100.23" && port == 40001);

	// IPv6, both ways of writing it
	m = StunMessage(kStunSuccess, id, { StunAttr(kStunXorMapped, StunAddress(true, id, kStunTestIPv6, 9410)) });
	CHECK(StunParseBindingResponse(m.data(), m.size(), kRfcStunId, address, &port, &error));
	CHECK(address == "2001:db8:85a3::8a2e:370:7334" && port == 9410);
	m = StunMessage(kStunSuccess, id, { StunAttr(kStunMapped, StunAddress(false, id, kStunTestIPv6, 9411)) });
	CHECK(StunParseBindingResponse(m.data(), m.size(), kRfcStunId, address, &port, &error));
	CHECK(address == "2001:db8:85a3::8a2e:370:7334" && port == 9411);
}

TEST(stun_rejects_what_is_not_an_answer)
{
	const uint8_t *id = kRfcStunId.data();
	const std::vector<uint8_t> good = StunAnswer(id, kStunTestIPv4, 40001);
	std::string address, error;
	uint16_t port = 0;
	CHECK(StunParseBindingResponse(good.data(), good.size(), kRfcStunId, address, &port, &error));
	// turned away with a reason, and without touching what the caller has
	auto rejected = [&](const std::vector<uint8_t> &m) {
		std::string addr = "untouched", err;
		uint16_t p = 7;
		bool ok = StunParseBindingResponse(m.data(), m.size(), kRfcStunId, addr, &p, &err);
		return !ok && !err.empty() && addr == "untouched" && p == 7;
	};

	// cut off anywhere, or with something behind it
	for(size_t n = 0; n < good.size(); n++)
		CHECK(rejected(std::vector<uint8_t>(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(n))));
	std::vector<uint8_t> m = good;
	m.push_back(0);
	CHECK(rejected(m));
	m.insert(m.end(), 3, 0);
	CHECK(rejected(m));
	CHECK(!StunParseBindingResponse(nullptr, 0, kRfcStunId, address, &port, &error));

	// an answer to somebody else's request
	StunTransactionId other = kRfcStunId;
	other[11] ^= 1;
	error.clear();
	CHECK(!StunParseBindingResponse(good.data(), good.size(), other, address, &port, &error));
	CHECK(!error.empty());

	// not the magic cookie
	m = good;
	m[5] ^= 0x10;
	CHECK(rejected(m));

	// a request, an indication, and no STUN at all
	m = good;
	m[0] = 0x00;
	m[1] = 0x01;
	CHECK(rejected(m));
	m[1] = 0x11;
	CHECK(rejected(m));
	m[0] = 0x81;
	m[1] = 0x01;
	CHECK(rejected(m));

	// the server says no
	m = StunMessage(kStunError, id, { StunAttr(0x0009, { 0, 0, 4, 20, 'n', 'o' }) });
	CHECK(rejected(m));
	error.clear();
	CHECK(!StunParseBindingResponse(m.data(), m.size(), kRfcStunId, address, &port, &error));
	CHECK(error.find("420") != std::string::npos);
	// ... even if it adds an address
	CHECK(rejected(StunMessage(kStunError, id, { StunAttr(kStunXorMapped, StunAddress(true, id, kStunTestIPv4, 40001)) })));

	// success without an address
	CHECK(rejected(StunMessage(kStunSuccess, id, {})));
	CHECK(rejected(StunMessage(kStunSuccess, id, { StunAttr(0x8022, { 'x' }) })));

	// an attribute that claims to be longer than the message
	m = good;
	m[23] = 200;
	CHECK(rejected(m));

	// address attributes that make no sense: unknown family, too short for the family, no address
	CHECK(rejected(StunMessage(kStunSuccess, id, { StunAttr(kStunXorMapped, { 0, 3, 0x12, 0x34, 1, 2, 3, 4 }) })));
	CHECK(rejected(StunMessage(kStunSuccess, id, { StunAttr(kStunXorMapped, { 0, 1, 0x12, 0x34, 1, 2, 3 }) })));
	CHECK(rejected(StunMessage(kStunSuccess, id, { StunAttr(kStunXorMapped, { 0, 2, 0x12, 0x34, 1, 2, 3, 4, 5, 6, 7, 8 }) })));
	CHECK(rejected(StunMessage(kStunSuccess, id, { StunAttr(kStunMapped, { 0, 1 }) })));
	CHECK(rejected(StunMessage(kStunSuccess, id, { StunAttr(kStunMapped, StunAddress(false, id, { 0, 0, 0, 0 }, 1234)) })));
	CHECK(rejected(StunMessage(kStunSuccess, id, { StunAttr(kStunXorMapped, StunAddress(true, id, std::vector<uint8_t>(16, 0), 1234)) })));

	// whatever arrives, the parser neither crashes nor reads past the end
	std::mt19937 rng(7);
	for(int i = 0; i < 4000; i++)
	{
		std::vector<uint8_t> junk;
		if(i % 2)
		{
			junk.resize(rng() % 200);
			for(auto &v : junk)
				v = static_cast<uint8_t>(rng());
		}
		else
		{
			// a real answer with a few bytes changed gets much further into the parser
			junk.assign(kRfcStunAnswerIPv6, kRfcStunAnswerIPv6 + sizeof(kRfcStunAnswerIPv6));
			int changes = 1 + static_cast<int>(rng() % 3);
			for(int k = 0; k < changes; k++)
				junk[rng() % junk.size()] = static_cast<uint8_t>(rng());
			if(rng() % 4 == 0)
				junk.resize(rng() % junk.size());
		}
		// in a buffer of exactly this size, so that a memory checker sees a read past the end
		std::unique_ptr<uint8_t[]> exact(new uint8_t[junk.size() ? junk.size() : 1]);
		if(!junk.empty())
			std::memcpy(exact.get(), junk.data(), junk.size());
		std::string addr;
		if(StunParseBindingResponse(exact.get(), junk.size(), kRfcStunId, addr, &port, &error))
			CHECK(!addr.empty());
	}
}

TEST(stun_server_text)
{
	StunServer s;
	CHECK(ParseStunServer("stun.example.org", s) && s.host == "stun.example.org" && s.port == 3478);
	CHECK(ParseStunServer("  stun.example.org:19302\n", s) && s.host == "stun.example.org" && s.port == 19302);
	CHECK(ParseStunServer("192.0.2.1:3479", s) && s.host == "192.0.2.1" && s.port == 3479);
	CHECK(ParseStunServer("2001:db8::1", s) && s.host == "2001:db8::1" && s.port == 3478);
	CHECK(ParseStunServer("[2001:db8::1]", s) && s.host == "2001:db8::1" && s.port == 3478);
	CHECK(ParseStunServer("[2001:db8::1]:5000", s) && s.host == "2001:db8::1" && s.port == 5000);

	s.host = "untouched";
	s.port = 7;
	CHECK(!ParseStunServer("", s));
	CHECK(!ParseStunServer("   ", s));
	CHECK(!ParseStunServer(":3478", s));
	CHECK(!ParseStunServer("host:", s));
	CHECK(!ParseStunServer("host:0", s));
	CHECK(!ParseStunServer("host:65536", s));
	CHECK(!ParseStunServer("host:12x", s));
	CHECK(!ParseStunServer("[2001:db8::1", s));
	CHECK(!ParseStunServer("[2001:db8::1]5000", s));
	CHECK(!ParseStunServer("[2001:db8::1]:", s));
	CHECK(!ParseStunServer("[]:5000", s));
	CHECK(!ParseStunServer("stun://host", s));
	CHECK(!ParseStunServer("two words", s));
	CHECK(s.host == "untouched" && s.port == 7);

	// the servers asked without a setting can be read the same way
	std::vector<StunServer> defaults = DefaultStunServers();
	CHECK(defaults.size() >= 2);
	for(const StunServer &d : defaults)
	{
		StunServer again;
		CHECK(ParseStunServer(d.host + ":" + std::to_string(d.port), again) && again.host == d.host && again.port == d.port);
	}
}

TEST(global_ipv6_addresses)
{
	CHECK(IsGlobalIPv6Address("2a02:8109:9a40:1b3c:1c2d:3e4f:5a6b:7c8d"));
	CHECK(IsGlobalIPv6Address("2003:e2:af12:3400::17"));
	CHECK(IsGlobalIPv6Address("2600::1"));
	CHECK(!IsGlobalIPv6Address("fe80::1c2d:3e4f:5a6b:7c8d")); // only on the own link
	CHECK(!IsGlobalIPv6Address("fd12:3456:789a::1"));         // only on the own site
	CHECK(!IsGlobalIPv6Address("fc00::1"));
	CHECK(!IsGlobalIPv6Address("::1"));
	CHECK(!IsGlobalIPv6Address("::"));
	CHECK(!IsGlobalIPv6Address("ff02::1"));
	CHECK(!IsGlobalIPv6Address("::ffff:192.0.2.1"));
	CHECK(!IsGlobalIPv6Address("2001:db8::1"));                      // documentation
	CHECK(!IsGlobalIPv6Address("2001:0:53aa:64c:0:fbff:ad4d:21a9")); // Teredo
	CHECK(!IsGlobalIPv6Address("2002:c000:201::1"));                 // 6to4
	CHECK(!IsGlobalIPv6Address("192.0.2.1"));
	CHECK(!IsGlobalIPv6Address("not an address"));
	CHECK(!IsGlobalIPv6Address(""));

	const uint8_t v4[4] = { 192, 0, 2, 1 };
	const uint8_t v6[16] = { 0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 };
	CHECK(AddressBytesToString(v4, 4) == "192.0.2.1");
	CHECK(AddressBytesToString(v6, 16) == "2001:db8::1");
	CHECK(AddressBytesToString(v6, 5).empty());
	CHECK(AddressBytesToString(nullptr, 4).empty());

	// Whether this machine has one depends on where the test runs; what it reports must be one
	std::string mine = GuessGlobalIPv6Address();
	CHECK(mine.empty() || IsGlobalIPv6Address(mine));
	std::printf("    this machine has %s global IPv6 address\n", mine.empty() ? "no" : "a");
}

/**
 * A STUN server on the loopback interface that answers whatever the test tells it to.
 *
 * About the numbers of requests checked below: a client that gets no answer in
 * time rightly asks again, and on a machine busy with other things "in time"
 * can be missed by a thread that simply was not woken up. So the tests that
 * want an exact number give the client far more time than it will need, and
 * the others accept one request more.
 */
struct FakeStunServer
{
	// The datagrams to send back for the n-th request (counting from 1) with this transaction id
	using Responder = std::function<Datagrams(int n, const uint8_t *id)>;
	// For what the server itself cannot do, like an answer from another address
	using Observer = std::function<void(const uint8_t *id, const Address &from)>;

	UdpSocket socket;
	std::thread thread;
	std::atomic<bool> quit{false};
	std::atomic<int> requests{0};
	std::atomic<int> malformed{0};
	uint16_t port = 0;

	explicit FakeStunServer(Responder responder, Observer observer = nullptr)
	{
		std::string error;
		if(!socket.Bind(0, &error))
			std::printf("    fake STUN server: %s\n", error.c_str());
		port = socket.LocalPort();
		thread = std::thread([this, responder, observer] {
			uint8_t buf[1500];
			while(!quit)
			{
				Address from;
				int r = socket.RecvFrom(buf, sizeof(buf), from, 10);
				if(r <= 0)
					continue;
				// A Binding Request without attributes, as RFC 5389 wants it
				static const uint8_t header[8] = { 0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xa4, 0x42 };
				if(r != 20 || std::memcmp(buf, header, 8) != 0)
				{
					malformed++;
					continue;
				}
				int n = ++requests;
				if(observer)
					observer(buf + 8, from);
				for(const auto &datagram : responder(n, buf + 8))
				{
					// An empty vector stands for an empty datagram, which still needs a buffer to point at
					static const uint8_t nothing = 0;
					socket.SendTo(datagram.empty() ? &nothing : datagram.data(), datagram.size(), from);
				}
			}
		});
	}

	~FakeStunServer()
	{
		quit = true;
		thread.join();
	}

	/** For requests that may still be on their way when the client has given up. */
	bool WaitForRequests(int count)
	{
		return WaitFor([&] { return requests >= count; }, 2000) && requests == count;
	}
};

// Long enough that the client never asks twice when the first answer comes
static const uint32_t kStunPlenty = 30000;

TEST(net_stun_finds_the_public_address)
{
	{
		FakeStunServer server([](int, const uint8_t *id) {
			return Datagrams{ StunMessage(kStunSuccess, id,
				{ StunAttr(0x8022, { 't', 'e', 's', 't' }), StunAttr(kStunXorMapped, StunAddress(true, id, kStunTestIPv4, 40001)) }) };
		});
		std::string address, error;
		uint16_t port = 0;
		CHECK(StunQuery("127.0.0.1", server.port, kStunPlenty, address, &port, &error));
		CHECK(address == "198.51.100.23" && port == 40001);
		CHECK(server.requests == 1 && server.malformed == 0);
		// asked for by kind of address, and without wanting the port or a reason
		address.clear();
		CHECK(StunQuery("127.0.0.1", server.port, kStunPlenty, address, nullptr, nullptr, StunFamily::IPv4));
		CHECK(address == "198.51.100.23");
		CHECK(server.requests == 2 && server.malformed == 0);
		// 127.0.0.1 has no IPv6 address to ask
		address = "untouched";
		CHECK(!StunQuery("127.0.0.1", server.port, kStunPlenty, address, &port, &error, StunFamily::IPv6));
		CHECK(address == "untouched" && !error.empty());
		CHECK(server.requests == 2);
	}
	{
		// an old server: only the plain attribute
		FakeStunServer server([](int, const uint8_t *id) {
			return Datagrams{ StunMessage(kStunSuccess, id, { StunAttr(kStunMapped, StunAddress(false, id, { 203, 0, 113, 7 }, 9410)) }) };
		});
		std::string address, error;
		uint16_t port = 0;
		CHECK(StunQuery("127.0.0.1", server.port, kStunPlenty, address, &port, &error));
		CHECK(address == "203.0.113.7" && port == 9410);
	}
	{
		// an IPv6 address in the answer
		FakeStunServer server([](int, const uint8_t *id) { return Datagrams{ StunAnswer(id, kStunTestIPv6, 50000) }; });
		std::string address, error;
		uint16_t port = 0;
		CHECK(StunQuery("127.0.0.1", server.port, kStunPlenty, address, &port, &error));
		CHECK(address == "2001:db8:85a3::8a2e:370:7334" && port == 50000);
	}
}

TEST(net_stun_over_ipv6)
{
	FakeStunServer server([](int, const uint8_t *id) { return Datagrams{ StunAnswer(id, kStunTestIPv6, 50000) }; });
	// Not every machine that runs the tests has IPv6, not even on the loopback interface
	std::vector<Address> loopback;
	UdpSocket probe;
	uint8_t byte = 0;
	bool have_ipv6 = Resolve("::1", server.port, loopback, nullptr) && probe.OpenFor(loopback[0], nullptr)
		&& probe.SendTo(&byte, 1, loopback[0]) && WaitFor([&] { return server.malformed == 1; }, 300);
	if(!have_ipv6)
	{
		std::printf("    skipped: no IPv6 on the loopback interface\n");
		return;
	}
	std::string address, error;
	uint16_t port = 0;
	CHECK(StunQuery("::1", server.port, kStunPlenty, address, &port, &error, StunFamily::IPv6));
	CHECK(address == "2001:db8:85a3::8a2e:370:7334" && port == 50000);
	CHECK(server.requests == 1);
	address.clear();
	CHECK(StunQuery("::1", server.port, kStunPlenty, address, &port, &error));
	CHECK(address == "2001:db8:85a3::8a2e:370:7334");
	// ::1 has no IPv4 address to ask
	address = "untouched";
	CHECK(!StunQuery("::1", server.port, kStunPlenty, address, &port, &error, StunFamily::IPv4));
	CHECK(address == "untouched" && !error.empty());
	CHECK(server.requests == 2);
}

TEST(net_stun_repeats_a_lost_request)
{
	// the first request "gets lost"
	FakeStunServer server([](int n, const uint8_t *id) {
		return n >= 2 ? Datagrams{ StunAnswer(id, kStunTestIPv4, 40001) } : Datagrams{};
	});
	std::string address, error;
	uint16_t port = 0;
	uint64_t start = NowMs();
	CHECK(StunQuery("127.0.0.1", server.port, 1400, address, &port, &error));
	uint64_t took = NowMs() - start;
	CHECK(address == "198.51.100.23" && port == 40001);
	CHECK(server.requests == 2 || server.requests == 3);
	// the second request goes out after a seventh of the time
	CHECK(took >= 190);
}

TEST(net_stun_ignores_answers_that_are_not_ours)
{
	const std::vector<uint8_t> wrong = { 203, 0, 113, 66 };
	// an answer to a request we never made, noise, half an answer and nothing at all
	auto not_ours = [wrong](const uint8_t *id) {
		uint8_t other[12];
		std::memcpy(other, id, 12);
		other[0] ^= 0x80;
		std::vector<uint8_t> half = StunAnswer(id, wrong, 2);
		half.resize(half.size() - 3);
		return Datagrams{ StunAnswer(other, wrong, 1), { 0xde, 0xad, 0xbe, 0xef, 0x00 }, half, {} };
	};
	{
		// only such answers: the wrong address is never taken, the request is repeated, and then it is over
		FakeStunServer server([&](int, const uint8_t *id) { return not_ours(id); });
		std::string address = "untouched", error;
		uint16_t port = 7;
		uint64_t start = NowMs();
		CHECK(!StunQuery("127.0.0.1", server.port, 350, address, &port, &error));
		uint64_t took = NowMs() - start;
		CHECK(address == "untouched" && port == 7);
		CHECK(!error.empty());
		CHECK(took >= 330 && took < 5000);
		CHECK(server.WaitForRequests(3));
	}
	{
		// the real answer after them is found
		FakeStunServer server([&](int, const uint8_t *id) {
			Datagrams out = not_ours(id);
			out.push_back(StunAnswer(id, kStunTestIPv4, 40001));
			return out;
		});
		std::string address, error;
		uint16_t port = 0;
		CHECK(StunQuery("127.0.0.1", server.port, kStunPlenty, address, &port, &error));
		CHECK(address == "198.51.100.23" && port == 40001);
		CHECK(server.requests == 1);
	}
	{
		// Somebody else who has seen the request answers it, with the right transaction id but
		// from another address than the one asked. That does not count; the server's own
		// answer to the repeated request does.
		UdpSocket stranger;
		CHECK(stranger.Bind(0, nullptr));
		std::atomic<int> forged{0};
		FakeStunServer server(
			[](int n, const uint8_t *id) { return n >= 2 ? Datagrams{ StunAnswer(id, kStunTestIPv4, 40001) } : Datagrams{}; },
			[&](const uint8_t *id, const Address &from) {
				std::vector<uint8_t> lie = StunAnswer(id, wrong, 3);
				if(stranger.SendTo(lie.data(), lie.size(), from))
					forged++;
			});
		std::string address, error;
		uint16_t port = 0;
		CHECK(StunQuery("127.0.0.1", server.port, 1400, address, &port, &error));
		CHECK(address == "198.51.100.23" && port == 40001);
		CHECK(forged >= 2);
	}
}

TEST(net_stun_gives_up_after_the_timeout)
{
	{
		// a server that never answers
		FakeStunServer server([](int, const uint8_t *) { return Datagrams{}; });
		std::string address = "untouched", error;
		uint16_t port = 7;
		uint64_t start = NowMs();
		CHECK(!StunQuery("127.0.0.1", server.port, 420, address, &port, &error));
		uint64_t took = NowMs() - start;
		CHECK(address == "untouched" && port == 7);
		CHECK(error.find("No answer") != std::string::npos);
		CHECK(took >= 400 && took < 5000);
		// sent three times: at once, after 1/7 and after 3/7 of the time
		CHECK(server.WaitForRequests(3));
		CHECK(server.malformed == 0);
	}
	{
		// nobody there at all
		uint16_t closed_port = 0;
		{
			UdpSocket s;
			CHECK(s.Bind(0, nullptr));
			closed_port = s.LocalPort();
		}
		std::string address = "untouched", error;
		uint64_t start = NowMs();
		CHECK(!StunQuery("127.0.0.1", closed_port, 200, address, nullptr, &error));
		CHECK(address == "untouched" && !error.empty());
		CHECK(NowMs() - start < 5000);
	}
}

TEST(net_stun_server_that_refuses)
{
	FakeStunServer server([](int, const uint8_t *id) {
		return Datagrams{ StunMessage(kStunError, id, { StunAttr(0x0009, { 0, 0, 5, 0, 'b', 'u', 's', 'y' }) }) };
	});
	std::string address = "untouched", error;
	uint64_t start = NowMs();
	CHECK(!StunQuery("127.0.0.1", server.port, kStunPlenty, address, nullptr, &error));
	// a clear no is not waited out
	CHECK(NowMs() - start < 10000);
	CHECK(address == "untouched");
	CHECK(error.find("500") != std::string::npos);
	CHECK(server.requests == 1);
}

TEST(net_stun_asks_the_next_server_when_one_is_silent)
{
	FakeStunServer silent([](int, const uint8_t *) { return Datagrams{}; });
	FakeStunServer refusing([](int, const uint8_t *id) { return Datagrams{ StunMessage(kStunError, id, {}) }; });
	FakeStunServer working([](int, const uint8_t *id) { return Datagrams{ StunAnswer(id, kStunTestIPv4, 40001) }; });
	FakeStunServer spare([](int, const uint8_t *id) { return Datagrams{ StunAnswer(id, { 203, 0, 113, 66 }, 1) }; });
	std::vector<StunServer> servers = {
		{ "127.0.0.1", silent.port }, { "127.0.0.1", refusing.port }, { "127.0.0.1", working.port }, { "127.0.0.1", spare.port } };
	std::string address, error;
	uint16_t port = 0;
	CHECK(StunQueryFirst(servers, 280, address, &port, &error, StunFamily::IPv4));
	CHECK(address == "198.51.100.23" && port == 40001);
	// every server before the one that answers was asked, none after it
	CHECK(silent.WaitForRequests(3));
	CHECK(refusing.requests >= 1 && working.requests >= 1 && spare.requests == 0);

	// nobody answers: the reason is the last server's
	servers = { { "127.0.0.1", refusing.port }, { "127.0.0.1", silent.port } };
	address = "untouched";
	CHECK(!StunQueryFirst(servers, 280, address, &port, &error));
	CHECK(address == "untouched" && error.find("No answer") != std::string::npos);
	error.clear();
	CHECK(!StunQueryFirst({}, 280, address, &port, &error));
	CHECK(address == "untouched" && !error.empty());
}

// ---------------------------------------------------------------- chat

TEST(chat_message_round_trip)
{
	ChatMessage m;
	m.seq = 7;
	m.up_id = 3;
	m.slot = 2;
	m.flags = kChatHistory;
	m.time_ms = 1795000000123ull;
	m.name = "Nova";
	m.text = "gg \xF0\x9F\x8E\xAE https://example.com/a";
	m.embed.url = "https://example.com/a";
	m.embed.site = "Example";
	m.embed.title = "A page";
	m.embed.description = "Something to read";
	m.embed.image.hash[0] = 9;
	m.embed.image.size = 1234;
	std::vector<uint8_t> buf;
	ByteWriter w(buf);
	WriteChatMessage(w, m);
	CHECK(buf.size() <= kMaxPayload);
	ChatMessage back;
	ByteReader r(buf.data(), buf.size());
	CHECK(ReadChatMessage(r, back));
	CHECK(back.seq == 7 && back.up_id == 3 && back.slot == 2 && back.flags == kChatHistory);
	CHECK(back.time_ms == m.time_ms && back.name == "Nova" && back.text == m.text);
	CHECK(back.embed.url == m.embed.url && back.embed.site == "Example" && back.embed.title == "A page");
	CHECK(back.embed.description == m.embed.description && back.embed.image == m.embed.image);

	// the largest message still fits one packet
	ChatMessage big;
	big.name = std::string(100, 'n');
	big.text = std::string(5000, 'x');
	big.embed.url = std::string(1000, 'u');
	big.embed.site = std::string(1000, 's');
	big.embed.title = std::string(1000, 't');
	big.embed.description = std::string(1000, 'd');
	big.embed.image.size = 100;
	buf.clear();
	ByteWriter w2(buf);
	WriteChatMessage(w2, big);
	CHECK(buf.size() <= kMaxPayload);
	ByteReader r2(buf.data(), buf.size());
	CHECK(ReadChatMessage(r2, back));
	CHECK(back.text.size() == kMaxChatText && back.embed.url.size() == kMaxChatUrl);

	// nothing to say is not a message
	ChatMessage empty;
	buf.clear();
	ByteWriter w3(buf);
	WriteChatMessage(w3, empty);
	ByteReader r3(buf.data(), buf.size());
	CHECK(!ReadChatMessage(r3, back));
}

TEST(chat_text_is_cut_between_characters)
{
	std::string s = "ab\xC3\xA4\xF0\x9F\x98\x80"; // a b ä 😀
	CHECK(TruncateUtf8(s, 100) == s);
	CHECK(TruncateUtf8(s, 3) == "ab");
	CHECK(TruncateUtf8(s, 4) == "ab\xC3\xA4");
	CHECK(TruncateUtf8(s, 7) == "ab\xC3\xA4");
	CHECK(TruncateUtf8(s, 8) == s);
	CHECK(TruncateUtf8(s, 0).empty());
}

TEST(net_chat_reaches_everyone_and_newcomers_get_what_was_said)
{
	TestHost h;
	TestGuest a(h.MakeInvite(), "A");
	TestGuest b(h.MakeInvite(), "B");
	CHECK(a.Connect());
	CHECK(b.Connect());
	CHECK(WaitFor([&] { return h.host->Guests().size() == 2; }, 2000));
	uint8_t slot_a = a.client->Welcome().slot;

	uint32_t id = a.client->SendChat("hello from A", ChatEmbed(), {});
	CHECK(id != 0);
	CHECK(WaitFor([&] { return b.Chats().size() == 1 && a.Chats().size() == 1; }, 2000));
	CHECK(WaitFor([&] {
		std::lock_guard<std::mutex> lock(h.mutex);
		return h.chats.size() == 1;
	}, 2000));
	ChatMessage got = b.Chats()[0];
	CHECK(got.text == "hello from A" && got.slot == slot_a && got.name == "A" && got.time_ms > 0 && !(got.flags & kChatHistory));
	// the writer recognises its own message
	CHECK(a.Chats()[0].up_id == id && a.Chats()[0].slot == slot_a);

	ChatMessage mine = h.host->SendChat("hello from the host", ChatEmbed(), {});
	CHECK(mine.seq != 0 && mine.slot == 0 && mine.name == "Host");
	CHECK(WaitFor([&] { return a.Chats().size() == 2 && b.Chats().size() == 2; }, 2000));
	CHECK(b.Chats()[1].text == "hello from the host" && b.Chats()[1].slot == 0 && b.Chats()[1].seq > got.seq);

	// a newcomer gets both, marked as said before it came
	TestGuest c(h.MakeInvite(), "C");
	CHECK(c.Connect());
	CHECK(WaitFor([&] { return c.Chats().size() == 2; }, 2000));
	std::vector<ChatMessage> history = c.Chats();
	CHECK(history[0].text == "hello from A" && (history[0].flags & kChatHistory));
	CHECK(history[1].text == "hello from the host" && (history[1].flags & kChatHistory));
	SleepMs(600);
	CHECK(c.Chats().size() == 2); // acknowledged, so nothing comes twice
}

TEST(net_chat_gets_through_heavy_loss_exactly_once)
{
	TestHost h;
	// The loss hits everything the host sends, the session state too: the guests are
	// given the app's own patience, so that a slow machine (the Windows build runs
	// these under wine on a busy runner) does not count as a lost host
	TestGuest a(h.MakeInvite(), "A", kWantAll, 3000, {}, 0, 6000);
	TestGuest b(h.MakeInvite(), "B", kWantAll, 3000, {}, 0, 6000);
	CHECK(a.Connect());
	CHECK(b.Connect());
	CHECK(WaitFor([&] { return h.host->Guests().size() == 2; }, 2000));
	h.host->SetSimulatedLoss(0.4, 11);
	a.client->SetSimulatedLoss(0.4, 12);
	for(int i = 0; i < 10; i++)
		a.client->SendChat("message " + std::to_string(i), ChatEmbed(), {});
	CHECK(WaitFor([&] { return b.Chats().size() >= 10; }, 18000));
	CHECK(a.disconnects == 0 && b.disconnects == 0);
	SleepMs(1500);
	std::vector<ChatMessage> got = b.Chats();
	CHECK(got.size() == 10);
	std::vector<std::string> texts;
	for(const ChatMessage &m : got)
		texts.push_back(m.text);
	std::sort(texts.begin(), texts.end());
	bool all = texts.size() == 10;
	for(int i = 0; all && i < 10; i++)
		all = texts[static_cast<size_t>(i)] == "message " + std::to_string(i);
	CHECK(all);
	h.host->SetSimulatedLoss(0.0);
	a.client->SetSimulatedLoss(0.0);
}

TEST(net_chat_link_preview_picture_travels)
{
	TestHost h;
	TestGuest a(h.MakeInvite(), "A");
	TestGuest b(h.MakeInvite(), "B");
	CHECK(a.Connect());
	CHECK(b.Connect());
	CHECK(WaitFor([&] { return h.host->Guests().size() == 2; }, 2000));

	std::vector<uint8_t> picture(5000);
	for(size_t i = 0; i < picture.size(); i++)
		picture[i] = static_cast<uint8_t>(i * 7);
	ChatEmbed embed;
	embed.url = "https://example.com/clip";
	embed.title = "A clip";
	a.client->SendChat("look https://example.com/clip", embed, picture);
	CHECK(WaitFor([&] { return b.Chats().size() == 1; }, 2000));
	ChatMessage m = b.Chats()[0];
	CHECK(m.embed.Valid() && m.embed.title == "A clip" && m.embed.image.Valid());
	// it went from A to the host, and from the host to B
	CHECK(WaitFor([&] {
		std::lock_guard<std::mutex> lock(b.mutex);
		return b.chat_images.size() == 1;
	}, 3000));
	BlobData at_b = b.client->ChatImage(m.embed.image.hash);
	CHECK(at_b && *at_b == picture);
	BlobData at_host = h.host->ChatImage(m.embed.image.hash);
	CHECK(at_host && *at_host == picture);

	// the host's own preview goes straight out
	std::vector<uint8_t> other(300, 0x42);
	ChatEmbed host_embed;
	host_embed.url = "https://example.com/b";
	ChatMessage sent = h.host->SendChat("", host_embed, other);
	CHECK(sent.seq != 0 && sent.embed.image.Valid());
	CHECK(WaitFor([&] { return a.Chats().size() == 2; }, 2000));
	CHECK(WaitFor([&] {
		BlobData d = a.client->ChatImage(sent.embed.image.hash);
		return d && *d == other;
	}, 3000));
}

int main(int argc, char **argv)
{
	std::string filter = argc > 1 ? argv[1] : "";
	int ran = 0;
	int failed_tests = 0;
	for(const TestCase &t : Registry())
	{
		if(!filter.empty() && std::string(t.name).find(filter) == std::string::npos)
			continue;
		int before = g_failed;
		std::printf("%s\n", t.name);
		std::fflush(stdout);
		t.fn();
		ran++;
		if(g_failed != before)
			failed_tests++;
	}
	std::printf("\n%d tests, %d checks, %d failed checks in %d tests\n", ran, g_checks, g_failed, failed_tests);
	return g_failed ? 1 : 0;
}
