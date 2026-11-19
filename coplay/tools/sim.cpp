// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// coplay-sim: stands in for the console (and, if wanted, for a guest) so that
// co-play can be developed and tested without a PlayStation.
//
//   coplay-sim host video.h264            play an H.264/H.265 elementary stream as "the console"
//   coplay-sim guest psrpt://...          join a session without a window and print what arrives
//   coplay-sim stun                       print the address this machine has on the internet

#include <coplay/client.h>
#include <coplay/host.h>
#include <coplay/invite.h>
#include <coplay/socket.h>
#include <coplay/stun.h>

#ifdef COPLAY_SIM_OPUS
#include <opus/opus.h>
#endif

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace coplay;

static std::atomic<bool> g_quit{false};
static const double kPi = 3.14159265358979323846;

static void OnSignal(int)
{
	g_quit = true;
}

static void Usage()
{
	std::printf(
		"coplay-sim host <video.h264|video.h265> [options]\n"
		"    Plays an Annex B elementary stream in a loop as if it were the console and\n"
		"    shares it. Prints the combined controller state whenever it changes and makes\n"
		"    the guests' controllers rumble while Cross is held.\n"
		"      --codec h264|h265   (default: from the file extension)\n"
		"      --fps N             (default 60)\n"
		"      --size WxH          (default 1280x720, informational)\n"
		"      --port N            (default %u)\n"
		"      --address HOST      address written into the invite (default 127.0.0.1)\n"
		"      --secret TEXT       reuse a session key instead of generating one\n"
		"      --name NAME\n"
		"      --avatar FILE       picture shown to the other players (image file, at most 48 KB)\n"
		"      --mode together|last-active|host-only\n"
		"      --loss PERCENT      drop this share of outgoing packets\n"
		"      --seconds N         stop after N seconds\n"
		"      --voice             the host talks: a short tone every two seconds (needs Opus)\n"
		"      --cam A.jpg,B.jpg   the host has a webcam: these pictures in turn, 8 per second\n"
		"\n"
		"coplay-sim guest <invite> [options]\n"
		"    Joins a session without showing anything and prints statistics.\n"
		"      --name NAME\n"
		"      --avatar FILE       picture shown to the other players\n"
		"      --no-video          controller only\n"
		"      --dump FILE         write the received video stream to FILE\n"
		"      --press             press and release Cross once per second\n"
		"      --loss PERCENT      drop this share of outgoing packets\n"
		"      --seconds N         leave after N seconds\n"
		"\n"
		"coplay-sim stun [server[:port] ...]\n"
		"    Asks STUN servers under which address this machine is seen from the internet,\n"
		"    the way the app does for the invite for friends elsewhere, and prints what\n"
		"    each of them answers. Without a server, the ones the app asks by default.\n"
		"\n"
		"A suitable test video:\n"
		"  ffmpeg -f lavfi -i testsrc2=size=1280x720:rate=60 -t 20 -c:v libx264 -preset veryfast \\\n"
		"    -tune zerolatency -pix_fmt yuv420p -x264-params keyint=60:min-keyint=60:scenecut=0:bframes=0 \\\n"
		"    -f h264 sample.h264\n",
		kDefaultPort);
}

struct Options
{
	std::vector<std::string> positional;
	std::string codec, size = "1280x720", address = "127.0.0.1", secret, name, mode = "together", dump, avatar;
	int fps = 60;
	int port = kDefaultPort;
	double loss = 0.0;
	int seconds = 0;
	bool no_video = false;
	bool press = false;
	bool voice = false;
	std::string cam;
};

static bool ParseOptions(int argc, char **argv, Options &o)
{
	for(int i = 2; i < argc; i++)
	{
		std::string a = argv[i];
		auto value = [&](std::string &out) {
			if(i + 1 >= argc)
				return false;
			out = argv[++i];
			return true;
		};
		std::string v;
		if(a == "--codec") { if(!value(o.codec)) return false; }
		else if(a == "--size") { if(!value(o.size)) return false; }
		else if(a == "--address") { if(!value(o.address)) return false; }
		else if(a == "--secret") { if(!value(o.secret)) return false; }
		else if(a == "--name") { if(!value(o.name)) return false; }
		else if(a == "--mode") { if(!value(o.mode)) return false; }
		else if(a == "--dump") { if(!value(o.dump)) return false; }
		else if(a == "--avatar") { if(!value(o.avatar)) return false; }
		else if(a == "--fps") { if(!value(v)) return false; o.fps = std::atoi(v.c_str()); }
		else if(a == "--port") { if(!value(v)) return false; o.port = std::atoi(v.c_str()); }
		else if(a == "--loss") { if(!value(v)) return false; o.loss = std::atof(v.c_str()) / 100.0; }
		else if(a == "--seconds") { if(!value(v)) return false; o.seconds = std::atoi(v.c_str()); }
		else if(a == "--cam") { if(!value(o.cam)) return false; }
		else if(a == "--voice") o.voice = true;
		else if(a == "--no-video") o.no_video = true;
		else if(a == "--press") o.press = true;
		else if(!a.empty() && a[0] == '-') return false;
		else o.positional.push_back(a);
	}
	return true;
}

static std::vector<uint8_t> ReadFile(const std::string &path)
{
	std::ifstream f(path, std::ios::binary);
	return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// ---------------------------------------------------------------- elementary stream

struct StreamItem
{
	bool config = false;
	bool key = false;
	std::vector<uint8_t> data;
};

struct Nal
{
	size_t begin = 0; // of the start code
	size_t end = 0;
	uint8_t first = 0;  // first NAL header byte
	uint8_t second = 0; // byte after the NAL header
};

static std::vector<Nal> SplitNals(const std::vector<uint8_t> &d, bool h265)
{
	std::vector<Nal> nals;
	size_t header = h265 ? 2 : 1;
	size_t i = 0;
	while(i + 3 < d.size())
	{
		if(d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1)
		{
			size_t begin = (i > 0 && d[i - 1] == 0) ? i - 1 : i;
			if(!nals.empty())
				nals.back().end = begin;
			Nal n;
			n.begin = begin;
			n.end = d.size();
			n.first = d[i + 3];
			n.second = i + 3 + header < d.size() ? d[i + 3 + header] : 0;
			nals.push_back(n);
			i += 3;
		}
		else
			i++;
	}
	return nals;
}

/**
 * Splits an Annex B stream into what the console's video receiver hands out:
 * the parameter sets on their own, and one buffer per picture without them.
 */
static std::vector<StreamItem> LoadStream(const std::string &path, bool h265, std::string &error)
{
	std::vector<StreamItem> items;
	std::ifstream f(path, std::ios::binary);
	if(!f)
	{
		error = "Cannot open " + path;
		return items;
	}
	std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	std::vector<Nal> nals = SplitNals(d, h265);
	if(nals.empty())
	{
		error = path + " does not look like an Annex B elementary stream";
		return items;
	}

	std::vector<uint8_t> config, last_config, picture;
	bool picture_has_slice = false;
	bool picture_key = false;

	auto flush = [&] {
		if(!picture_has_slice)
		{
			picture.clear();
			return;
		}
		if(!config.empty() && config != last_config)
		{
			StreamItem c;
			c.config = true;
			c.data = config;
			items.push_back(c);
			last_config = config;
		}
		config.clear();
		StreamItem p;
		p.key = picture_key;
		p.data = picture;
		items.push_back(std::move(p));
		picture.clear();
		picture_has_slice = false;
		picture_key = false;
	};

	for(const Nal &n : nals)
	{
		bool vcl, key, param, aud, first_slice;
		if(h265)
		{
			unsigned type = (n.first >> 1) & 0x3f;
			vcl = type <= 31;
			key = type >= 16 && type <= 23;
			param = type >= 32 && type <= 34;
			aud = type == 35;
			first_slice = vcl && (n.second & 0x80);
		}
		else
		{
			unsigned type = n.first & 0x1f;
			vcl = type >= 1 && type <= 5;
			key = type == 5;
			param = type == 7 || type == 8;
			aud = type == 9;
			first_slice = vcl && (n.second & 0x80); // first_mb_in_slice == 0
		}
		if(picture_has_slice && (!vcl || first_slice))
			flush();
		if(aud)
			continue; // the console does not send access unit delimiters
		if(param)
		{
			config.insert(config.end(), d.begin() + n.begin, d.begin() + n.end);
			continue;
		}
		picture.insert(picture.end(), d.begin() + n.begin, d.begin() + n.end);
		if(vcl)
		{
			picture_has_slice = true;
			picture_key = picture_key || key;
		}
	}
	flush();

	bool first_picture_is_key = false;
	for(const StreamItem &it : items)
	{
		if(!it.config)
		{
			first_picture_is_key = it.key;
			break;
		}
	}
	if(!first_picture_is_key)
	{
		error = path + " does not start with a key frame";
		items.clear();
	}
	return items;
}

static std::string DescribePad(const PadState &s)
{
	static const struct { uint32_t bit; const char *name; } names[] = {
		{ kButtonCross, "Cross" }, { kButtonCircle, "Circle" }, { kButtonSquare, "Square" }, { kButtonTriangle, "Triangle" },
		{ kButtonDpadLeft, "Left" }, { kButtonDpadRight, "Right" }, { kButtonDpadUp, "Up" }, { kButtonDpadDown, "Down" },
		{ kButtonL1, "L1" }, { kButtonR1, "R1" }, { kButtonL3, "L3" }, { kButtonR3, "R3" },
		{ kButtonOptions, "Options" }, { kButtonShare, "Create" }, { kButtonTouchpad, "Touchpad" }, { kButtonPs, "PS" },
	};
	std::string out;
	for(auto &n : names)
	{
		if(s.buttons & n.bit)
			out += std::string(out.empty() ? "" : "+") + n.name;
	}
	if(out.empty())
		out = "-";
	char buf[160];
	std::snprintf(buf, sizeof(buf), "  L(%6d,%6d) R(%6d,%6d) L2 %3u R2 %3u%s", s.left_x, s.left_y, s.right_x, s.right_y,
		s.l2, s.r2, s.HasMotion() ? " +motion" : "");
	return out + buf;
}

// ---------------------------------------------------------------- host

static int RunHost(const Options &o)
{
	if(o.positional.empty())
	{
		Usage();
		return 2;
	}
	const std::string &path = o.positional[0];
	bool h265 = o.codec == "h265" || o.codec == "hevc"
		|| (o.codec.empty() && (path.size() > 5 && (path.rfind(".h265") == path.size() - 5 || path.rfind(".hevc") == path.size() - 5)));
	std::string error;
	std::vector<StreamItem> items = LoadStream(path, h265, error);
	if(items.empty())
	{
		std::fprintf(stderr, "%s\n", error.c_str());
		return 1;
	}
	size_t pictures = 0, keys = 0;
	for(auto &it : items)
	{
		if(!it.config)
		{
			pictures++;
			keys += it.key ? 1 : 0;
		}
	}
	std::printf("Loaded %zu pictures (%zu key frames) from %s\n", pictures, keys, path.c_str());

	HostConfig config;
	config.port = static_cast<uint16_t>(o.port);
	config.name = o.name.empty() ? "Simulated console" : o.name;
	config.controller = ControllerKind::DualSense;
	if(!o.avatar.empty())
		config.avatar = ReadFile(o.avatar);
	if(!o.secret.empty())
	{
		if(!SecretFromString(o.secret, config.secret))
		{
			std::fprintf(stderr, "--secret is not a valid session key\n");
			return 2;
		}
	}
	else
		config.secret = GenerateSecret();

	std::atomic<bool> want_key{false};
	std::atomic<bool> input_changed{true};
	std::atomic<bool> guests_changed{false};
	std::mutex print_mutex;

	HostCallbacks cb;
	cb.on_keyframe_needed = [&] { want_key = true; };
	cb.on_input_changed = [&] { input_changed = true; };
	cb.on_guests_changed = [&] { guests_changed = true; };
	// What the guests say and show, counted per player
	std::atomic<uint32_t> voice_frames[kMaxGuests + 1];
	std::atomic<uint32_t> cam_frames[kMaxGuests + 1];
	std::atomic<uint32_t> cam_bytes[kMaxGuests + 1];
	for(size_t i = 0; i <= kMaxGuests; i++)
	{
		voice_frames[i] = 0;
		cam_frames[i] = 0;
		cam_bytes[i] = 0;
	}
	cb.on_voice = [&](uint8_t slot, const uint8_t *, size_t) {
		if(slot <= kMaxGuests)
			voice_frames[slot]++;
	};
	cb.on_cam_frame = [&](uint8_t slot, const std::vector<uint8_t> &jpeg) {
		if(slot <= kMaxGuests)
		{
			cam_frames[slot]++;
			cam_bytes[slot] = static_cast<uint32_t>(jpeg.size());
		}
	};
	cb.on_log = [&](const std::string &line) {
		std::lock_guard<std::mutex> lock(print_mutex);
		std::printf("[host] %s\n", line.c_str());
		std::fflush(stdout);
	};
	Host host(config, cb);
	if(!host.Start(&error))
	{
		std::fprintf(stderr, "%s\n", error.c_str());
		return 1;
	}
	if(o.mode == "last-active")
		host.SetMixMode(MixMode::LastActive);
	else if(o.mode == "host-only")
		host.SetMixMode(MixMode::HostOnly);
	if(o.loss > 0)
		host.SetSimulatedLoss(o.loss, 12345);

	StreamInfo info;
	info.codec = h265 ? VideoCodec::H265 : VideoCodec::H264;
	int w = 1280, h = 720;
	std::sscanf(o.size.c_str(), "%dx%d", &w, &h);
	info.width = static_cast<uint16_t>(w);
	info.height = static_cast<uint16_t>(h);
	info.fps = static_cast<uint8_t>(o.fps);
	info.ps5 = true;
	info.dualsense = true;
	host.SetStreamInfo(info);

	Invite invite;
	invite.host = o.address;
	invite.port = host.Port();
	invite.secret = config.secret;
	std::printf("Sharing on UDP port %u. Invite:\n\n    %s\n\n", host.Port(), EncodeInvite(invite).c_str());
	std::fflush(stdout);

	// The host's own webcam and voice
	std::vector<std::vector<uint8_t>> cam_pictures;
	{
		size_t start = 0;
		while(start < o.cam.size())
		{
			size_t comma = o.cam.find(',', start);
			std::string file = o.cam.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
			start = comma == std::string::npos ? o.cam.size() : comma + 1;
			std::vector<uint8_t> picture = ReadFile(file);
			if(picture.empty() || picture.size() > kMaxCamFrameSize)
			{
				std::fprintf(stderr, "--cam: cannot use %s (missing, empty or larger than %zu bytes)\n", file.c_str(), kMaxCamFrameSize);
				return 2;
			}
			cam_pictures.push_back(std::move(picture));
		}
	}
	uint8_t media = cam_pictures.empty() ? 0 : kMediaCamera;
	size_t cam_index = 0;
	uint64_t voice_sample = 0;
#ifdef COPLAY_SIM_OPUS
	OpusEncoder *voice_opus = nullptr;
	if(o.voice)
	{
		int voice_error = 0;
		voice_opus = opus_encoder_create(48000, 1, OPUS_APPLICATION_VOIP, &voice_error);
		if(voice_opus)
			media |= kMediaVoice;
	}
#else
	if(o.voice)
		std::fprintf(stderr, "--voice: built without Opus, the host stays silent\n");
#endif
	host.SetLocalMedia(media);
	(void)voice_sample;

#ifdef COPLAY_SIM_OPUS
	int opus_error = 0;
	OpusEncoder *opus = opus_encoder_create(48000, 2, OPUS_APPLICATION_AUDIO, &opus_error);
	if(opus)
	{
		AudioHeader audio;
		audio.valid = true;
		audio.channels = 2;
		audio.bits = 16;
		audio.rate = 48000;
		audio.frame_size = 480;
		host.SetAudioHeader(audio);
	}
	uint64_t audio_sample = 0;
#endif

	using clock = std::chrono::steady_clock;
	auto start = clock::now();
	auto frame_interval = std::chrono::microseconds(1000000 / (o.fps > 0 ? o.fps : 60));
	auto next_frame = start;
	auto next_10ms = start;
	size_t pos = 0;
	uint64_t ticks_10ms = 0;
	bool rumbling = false;

	// the console sends the parameter sets once at the start
	for(const StreamItem &it : items)
	{
		if(it.config)
		{
			host.PushVideo(it.data.data(), it.data.size(), 0, false);
			break;
		}
	}

	while(!g_quit)
	{
		auto now = clock::now();
		if(o.seconds > 0 && now - start >= std::chrono::seconds(o.seconds))
			break;

		if(now >= next_frame)
		{
			next_frame += frame_interval;
			if(next_frame < now)
				next_frame = now + frame_interval; // fell behind, do not burst
			if(want_key.exchange(false))
			{
				// Like the console answering an IDR request: continue with the next key frame
				size_t probe = pos;
				for(size_t n = 0; n < items.size(); n++)
				{
					if(!items[probe].config && items[probe].key)
						break;
					probe = (probe + 1) % items.size();
				}
				// the parameter sets belonging to that key frame come first, if they changed there
				if(probe > 0 && items[probe - 1].config)
					probe--;
				pos = probe;
			}
			while(items[pos].config)
			{
				host.PushVideo(items[pos].data.data(), items[pos].data.size(), 0, false);
				pos = (pos + 1) % items.size();
			}
			host.PushVideo(items[pos].data.data(), items[pos].data.size(), 0, false);
			pos = (pos + 1) % items.size();
		}

		if(now >= next_10ms)
		{
			next_10ms += std::chrono::milliseconds(10);
			if(next_10ms < now)
				next_10ms = now + std::chrono::milliseconds(10);
			ticks_10ms++;

			// Haptics: 10 ms of 3 kHz stereo PCM; a short 150 Hz buzz every two seconds
			int16_t pcm[60];
			bool buzz = (ticks_10ms % 200) < 20;
			for(int i = 0; i < 30; i++)
			{
				double t = (ticks_10ms * 30 + i) / 3000.0;
				int16_t v = buzz ? static_cast<int16_t>(12000 * std::sin(2 * kPi * 150 * t)) : 0;
				pcm[i * 2] = v;
				pcm[i * 2 + 1] = v;
			}
			host.PushHaptics(reinterpret_cast<const uint8_t *>(pcm), sizeof(pcm));

			// Webcam: the next picture every 120 ms
			if(!cam_pictures.empty() && ticks_10ms % 12 == 0)
			{
				const std::vector<uint8_t> &picture = cam_pictures[cam_index++ % cam_pictures.size()];
				host.PushCamFrame(picture.data(), picture.size());
			}
#ifdef COPLAY_SIM_OPUS
			// Voice: 20 ms frames; half a second of a tone every two seconds, silence is not sent
			if(voice_opus && ticks_10ms % 2 == 0)
			{
				int16_t samples[960];
				bool talking = std::fmod(voice_sample / 48000.0, 2.0) < 0.5;
				for(int i = 0; i < 960; i++)
				{
					double t = (voice_sample + i) / 48000.0;
					samples[i] = talking ? static_cast<int16_t>(9000 * std::sin(2 * kPi * 520 * t)) : 0;
				}
				voice_sample += 960;
				if(talking)
				{
					uint8_t packet[600];
					int len = opus_encode(voice_opus, samples, 960, packet, sizeof(packet));
					if(len > 2)
						host.PushVoice(packet, static_cast<size_t>(len));
				}
			}
#endif
			// Once per second: what arrived from the guests' microphones and webcams
			if(ticks_10ms % 100 == 0)
			{
				std::string line;
				for(size_t slot = 1; slot <= kMaxGuests; slot++)
				{
					uint32_t v = voice_frames[slot].exchange(0);
					uint32_t c = cam_frames[slot].exchange(0);
					if(v || c)
						line += "  #" + std::to_string(slot) + ": " + std::to_string(v) + " voice frames, " + std::to_string(c)
							+ " webcam pictures (" + std::to_string(cam_bytes[slot].load()) + " bytes)";
				}
				if(!line.empty())
				{
					std::lock_guard<std::mutex> lock(print_mutex);
					std::printf("[host hears/sees]%s\n", line.c_str());
					std::fflush(stdout);
				}
			}

#ifdef COPLAY_SIM_OPUS
			if(opus)
			{
				// A quiet two-note chime once per second
				int16_t samples[480 * 2];
				for(int i = 0; i < 480; i++)
				{
					uint64_t n = audio_sample + i;
					double t = n / 48000.0;
					double in_second = std::fmod(t, 1.0);
					double freq = in_second < 0.12 ? 660.0 : (in_second < 0.24 ? 880.0 : 0.0);
					double env = freq > 0 ? 0.12 : 0.0;
					int16_t v = static_cast<int16_t>(32767 * env * std::sin(2 * kPi * freq * t));
					samples[i * 2] = v;
					samples[i * 2 + 1] = v;
				}
				audio_sample += 480;
				uint8_t packet[1000];
				int len = opus_encode(opus, samples, 480, packet, sizeof(packet));
				if(len > 0)
					host.PushAudio(packet, static_cast<size_t>(len));
			}
#endif
		}

		if(input_changed.exchange(false))
		{
			PadState mix = host.MixedInput();
			{
				std::lock_guard<std::mutex> lock(print_mutex);
				std::printf("[console sees] %s\n", DescribePad(mix).c_str());
				std::fflush(stdout);
			}
			bool should_rumble = (mix.buttons & kButtonCross) != 0;
			if(should_rumble != rumbling)
			{
				rumbling = should_rumble;
				host.SetRumble(rumbling ? 200 : 0, rumbling ? 100 : 0);
			}
		}
		if(guests_changed.exchange(false))
		{
			auto guests = host.Guests();
			std::lock_guard<std::mutex> lock(print_mutex);
			std::printf("[host] %zu guest(s):", guests.size());
			for(auto &g : guests)
				std::printf("  #%u %s (%s%s%s%s%s, %u ms)", g.slot, g.name.c_str(), (g.want & kWantVideo) ? "video" : "controller only",
					g.in_control ? ", in control" : "", (g.media & kMediaVoice) ? ", voice" : "",
					(g.media & kMediaMuted) ? " muted" : "", (g.media & kMediaCamera) ? ", webcam" : "", g.rtt_ms);
			std::printf("\n");
			std::fflush(stdout);
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

#ifdef COPLAY_SIM_OPUS
	if(opus)
		opus_encoder_destroy(opus);
	if(voice_opus)
		opus_encoder_destroy(voice_opus);
#endif
	host.Stop();
	return 0;
}

// ---------------------------------------------------------------- guest

static int RunGuest(const Options &o)
{
	if(o.positional.empty())
	{
		Usage();
		return 2;
	}
	ClientConfig config;
	std::string error;
	if(!DecodeInvite(o.positional[0], config.invite, &error))
	{
		std::fprintf(stderr, "%s\n", error.c_str());
		return 2;
	}
	config.name = o.name.empty() ? "Simulated guest" : o.name;
	config.want = o.no_video ? 0 : kWantAll;
	config.controller = ControllerKind::Xbox;
	if(!o.avatar.empty())
		config.avatar = ReadFile(o.avatar);

	Client client(config);
	if(o.loss > 0)
		client.SetSimulatedLoss(o.loss, 54321);
	if(!client.Connect(&error))
	{
		std::fprintf(stderr, "%s\n", error.c_str());
		return 1;
	}
	const WelcomePayload &welcome = client.Welcome();
	static const char *codecs[] = { "H.264", "H.265", "H.265 HDR" };
	std::printf("Joined \"%s\" at %s as player %u: %s %ux%u @ %u fps\n", welcome.host_name.c_str(),
		client.HostAddress().c_str(), welcome.slot + 1, codecs[static_cast<int>(welcome.stream.codec)],
		welcome.stream.width, welcome.stream.height, welcome.stream.fps);

	std::mutex mutex;
	std::ofstream dump;
	if(!o.dump.empty())
		dump.open(o.dump, std::ios::binary);
	uint64_t frames = 0, key_frames = 0, video_bytes = 0, haptics = 0, audio = 0;
	uint16_t rumble_seq = 0;
	std::atomic<bool> disconnected{false};

	ClientCallbacks cb;
	cb.on_video_config = [&](const uint8_t *d, size_t n) {
		std::lock_guard<std::mutex> lock(mutex);
		if(dump.is_open())
			dump.write(reinterpret_cast<const char *>(d), static_cast<std::streamsize>(n));
	};
	cb.on_video_frame = [&](const AssembledFrame &f) {
		std::lock_guard<std::mutex> lock(mutex);
		frames++;
		key_frames += f.key ? 1 : 0;
		video_bytes += f.data.size();
		if(dump.is_open())
			dump.write(reinterpret_cast<const char *>(f.data.data()), static_cast<std::streamsize>(f.data.size()));
	};
	cb.on_audio = [&](const uint8_t *, size_t) {
		std::lock_guard<std::mutex> lock(mutex);
		audio++;
	};
	cb.on_haptics = [&](const uint8_t *, size_t) {
		std::lock_guard<std::mutex> lock(mutex);
		haptics++;
	};
	cb.on_state = [&](const SessionState &s) {
		std::lock_guard<std::mutex> lock(mutex);
		if(s.feedback.rumble_seq != rumble_seq)
		{
			rumble_seq = s.feedback.rumble_seq;
			std::printf("[guest] rumble %u / %u\n", s.feedback.rumble_left, s.feedback.rumble_right);
			std::fflush(stdout);
		}
	};
	cb.on_disconnected = [&](DisconnectReason r) {
		static const char *reasons[] = { "the host ended the session", "removed by the host", "connection to the host lost" };
		std::printf("[guest] disconnected: %s\n", reasons[static_cast<int>(r)]);
		disconnected = true;
	};
	client.Start(cb);

	auto start = std::chrono::steady_clock::now();
	uint64_t last_frames = 0, last_bytes = 0;
	int second = 0;
	while(!g_quit && !disconnected)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		if(o.press)
		{
			PadState s;
			s.buttons = kButtonCross;
			s.left_x = 20000;
			client.SetInput(s);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		if(o.press)
			client.SetInput(PadState());
		second++;
		ClientStats stats = client.Stats();
		{
			std::lock_guard<std::mutex> lock(mutex);
			std::printf("[guest] %3d s  %3llu fps  %6.2f Mbit/s  rtt %u ms  frames %llu (key %llu, dropped %llu)  re-requested %llu  audio %llu  haptics %llu\n",
				second, static_cast<unsigned long long>(frames - last_frames),
				(video_bytes - last_bytes) * 8 / 1000000.0, stats.rtt_ms,
				static_cast<unsigned long long>(frames), static_cast<unsigned long long>(key_frames),
				static_cast<unsigned long long>(stats.video.frames_dropped),
				static_cast<unsigned long long>(stats.video.nack_fragments),
				static_cast<unsigned long long>(audio), static_cast<unsigned long long>(haptics));
			std::fflush(stdout);
			last_frames = frames;
			last_bytes = video_bytes;
		}
		if(o.seconds > 0 && std::chrono::steady_clock::now() - start >= std::chrono::seconds(o.seconds))
			break;
	}
	client.Stop();
	return disconnected ? 1 : 0;
}

// ---------------------------------------------------------------- public address

static int RunStun(const Options &o)
{
	std::vector<StunServer> servers;
	for(const std::string &text : o.positional)
	{
		StunServer server;
		if(!ParseStunServer(text, server))
		{
			std::fprintf(stderr, "Not a server address: %s\n", text.c_str());
			return 2;
		}
		servers.push_back(server);
	}
	if(servers.empty())
		servers = DefaultStunServers();

	bool found = false;
	for(const StunServer &server : servers)
	{
		std::printf("%s port %u\n", server.host.c_str(), server.port);
		for(StunFamily family : { StunFamily::IPv4, StunFamily::IPv6 })
		{
			const char *kind = family == StunFamily::IPv4 ? "IPv4" : "IPv6";
			std::string address, error;
			uint16_t port = 0;
			if(StunQuery(server.host, server.port, 1500, address, &port, &error, family))
			{
				// The port is the one this question left the router with, not the session's
				std::printf("    %s: %s (asked through port %u)\n", kind, address.c_str(), port);
				found = true;
			}
			else
				std::printf("    %s: %s\n", kind, error.c_str());
		}
	}

	std::string local = GuessLocalAddress();
	std::string global6 = GuessGlobalIPv6Address();
	std::printf("This machine itself\n");
	std::printf("    on the local network: %s\n", local.empty() ? "no address" : local.c_str());
	std::printf("    global IPv6 address: %s\n", global6.empty() ? "none" : global6.c_str());
	return found ? 0 : 1;
}

int main(int argc, char **argv)
{
	if(argc < 2)
	{
		Usage();
		return 2;
	}
	std::signal(SIGINT, OnSignal);
	std::signal(SIGTERM, OnSignal);
	Options o;
	if(!ParseOptions(argc, argv, o))
	{
		Usage();
		return 2;
	}
	std::string command = argv[1];
	if(command == "host")
		return RunHost(o);
	if(command == "guest")
		return RunGuest(o);
	if(command == "stun")
		return RunStun(o);
	Usage();
	return 2;
}
