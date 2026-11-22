// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// coplay-sim: stands in for the console (and, if wanted, for a guest) so that
// co-play can be developed and tested without a PlayStation.
//
//   coplay-sim host video.h264            play an H.264/H.265 elementary stream as "the console"
//   coplay-sim guest psrpt://...          join a session without a window and print what arrives
//   coplay-sim stun                       print the address this machine has on the internet

#include <coplay/client.h>
#include <coplay/crypto.h>
#include <coplay/host.h>
#include <coplay/invite.h>
#include <coplay/meet.h>
#include <coplay/socket.h>
#include <coplay/stun.h>
#include <coplay/web.h>

#ifdef COPLAY_SIM_OPUS
#include <opus/opus.h>
#endif

#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
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

#ifdef SIGUSR1
static std::atomic<bool> g_coming_back{false};

static void OnComingBackSignal(int)
{
	g_coming_back = true;
	g_quit = true;
}
#endif

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
		"      --chat              the host writes in the chat now and then; prints what others write\n"
		"      --guests-may-sleep  a guest that leaves may ask for the console to be put to sleep;\n"
		"                          the host then stops, as a real one would with its console asleep\n"
		"      --cam A.jpg,B.jpg   the host has a webcam: these pictures in turn, 8 per second\n"
		"      --no-streams        the host asks to be left out of the others' live streams\n"
		"                          (voice, webcam and name)\n"
		"      --meet URL|default  let friends in without a forwarded port: leave a note at this\n"
		"                          meeting point (see server/meeting-point). Needs curl.\n"
		"      --stun HOST[:PORT]  ask this STUN server for the session's address on the internet\n"
		"                          (with --meet; default: the servers the app asks; \"off\": nobody)\n"
		"      --web PAGE|default  let friends in who join from a web browser, and print the link\n"
		"                          for them (with --meet). PAGE is where the page of web/ is served.\n"
		"      --web-port N        all browsers on this UDP port instead of one of their own each\n"
		"      --closed            behave as if behind a router without a forwarded port: what\n"
		"                          comes from an address that was not sent to first is dropped\n"
		"      --no-address        print an invite without an address (psrpt://-/...)\n"
		"      --public TITLE      a public room: put it on the list of --rooms (with --meet)\n"
		"      --rooms URL         the list of public rooms (server/rooms/psrpt_rooms.php)\n"
		"      --lang XX           the room's language (default en)\n"
		"      --game NAME         the game the room says it plays (default \"Simulator\")\n"
		"      --title-id ID       the game's title id (PPSA12345), as a console reports it\n"
		"      --cover URL         a picture of the game on image.api.playstation.com (the app\n"
		"                          finds it in Sony's catalog by the title id)\n"
		"      --preview A.jpg     a picture of the game for the list (a JPEG of at most 640x360),\n"
		"                          sent as often as the list asks, as the app sends a frame\n"
		"      --face A.jpg        the host's picture for the list (a JPEG of at most 128x128); the\n"
		"                          guests' own pictures go along where they are such JPEGs too,\n"
		"                          the others show their initial there\n"
		"      --policy ask|watch|open  who gets in (default open); \"ask\" lets each one in\n"
		"                          after --admit-after seconds, or refuses with --refuse\n"
		"      --admit-after N     seconds before a guest that knocks is let in (default 3)\n"
		"      --refuse            refuse every guest that knocks\n"
		"      --max N             guests at most (default 4)\n"
		"    Ends on SIGINT or SIGTERM like a host that stops sharing. On SIGUSR1 it ends like\n"
		"    one that lost its console and is getting it back, so that the guests wait for it;\n"
		"    killed (SIGKILL) it just goes silent.\n"
		"\n"
		"coplay-sim guest <invite> [options]\n"
		"    Joins a session without showing anything and prints statistics.\n"
		"      --name NAME\n"
		"      --avatar FILE       picture shown to the other players\n"
		"      --no-video          controller only\n"
		"      --dump FILE         write the received video stream to FILE\n"
		"      --press             press and release Cross once per second\n"
		"      --meet URL|default  find the host through this meeting point as well (needs curl)\n"
		"      --stun HOST[:PORT]  as for the host\n"
		"      --closed            as for the host\n"
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
	bool chat = false;
	bool guests_may_sleep = false;
	bool no_streams = false;
	std::string room_title, rooms, lang = "en", game = "Simulator", policy = "open", title_id, cover, preview, face;
	int admit_after = 3;
	bool refuse = false;
	int max_guests = 4;
	std::string cam;
	std::string meet, stun;
	/** The page for guests in a web browser; empty: no such guests. */
	std::string web;
	int web_port = 0;
	bool closed = false;
	bool no_address = false;
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
		else if(a == "--meet") { if(!value(o.meet)) return false; }
		else if(a == "--stun") { if(!value(o.stun)) return false; }
		else if(a == "--web") { if(!value(o.web)) return false; }
		else if(a == "--web-port") { if(!value(v)) return false; o.web_port = std::atoi(v.c_str()); }
		else if(a == "--closed") o.closed = true;
		else if(a == "--no-address") o.no_address = true;
		else if(a == "--voice") o.voice = true;
		else if(a == "--chat") o.chat = true;
		else if(a == "--guests-may-sleep") o.guests_may_sleep = true;
		else if(a == "--no-streams") o.no_streams = true;
		else if(a == "--public") { if(!value(o.room_title)) return false; }
		else if(a == "--rooms") { if(!value(o.rooms)) return false; }
		else if(a == "--lang") { if(!value(o.lang)) return false; }
		else if(a == "--game") { if(!value(o.game)) return false; }
		else if(a == "--title-id") { if(!value(o.title_id)) return false; }
		else if(a == "--cover") { if(!value(o.cover)) return false; }
		else if(a == "--preview") { if(!value(o.preview)) return false; }
		else if(a == "--face") { if(!value(o.face)) return false; }
		else if(a == "--policy") { if(!value(o.policy)) return false; }
		else if(a == "--admit-after") { if(!value(v)) return false; o.admit_after = std::atoi(v.c_str()); }
		else if(a == "--refuse") o.refuse = true;
		else if(a == "--max") { if(!value(v)) return false; o.max_guests = std::atoi(v.c_str()); }
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

// ------------------------------------------------------------------ meeting point

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

/** One POST request with the curl program. Good enough for a test tool; the app uses Qt. */
static bool CurlPost(const HttpRequest &request, HttpResponse &response, std::string *error)
{
	auto fail = [error](const std::string &why) {
		if(error)
			*error = why;
		return false;
	};
	// Both go into a command line: nothing but what a URL and our form fields are made of
	static const char kAllowed[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_~:/[]@=&%";
	if(request.url.find_first_not_of(kAllowed) != std::string::npos || request.body.find_first_not_of(kAllowed) != std::string::npos)
		return fail("the meeting point's address has characters this tool does not pass on");
	uint32_t seconds = (request.timeout_ms + 999) / 1000;
	std::string command = "curl -s -m " + std::to_string(seconds ? seconds : 1) + " -w \"\\n%{http_code}\" --data \""
		+ request.body + "\" \"" + request.url + "\"";
#ifdef _WIN32
	command += " 2>NUL";
#else
	command += " 2>/dev/null";
#endif
	FILE *pipe = popen(command.c_str(), "r");
	if(!pipe)
		return fail("cannot run curl");
	std::string out;
	char buf[4096];
	size_t n;
	while((n = std::fread(buf, 1, sizeof(buf), pipe)) > 0 && out.size() < 256 * 1024)
		out.append(buf, n);
	int result = pclose(pipe);
	size_t line = out.rfind('\n');
	if(result != 0 || line == std::string::npos)
		return fail("no answer (curl gave up)");
	response.status = std::atoi(out.c_str() + line + 1);
	response.body = out.substr(0, line);
	return response.status != 0 || fail("no answer");
}

/** Form fields, each value percent-encoded, for a POST that CurlPost passes on as it is. */
static std::string FormBody(const std::vector<std::pair<std::string, std::string>> &fields)
{
	static const char kHex[] = "0123456789ABCDEF";
	std::string body;
	for(const auto &field : fields)
	{
		if(!body.empty())
			body += '&';
		body += field.first + '=';
		for(unsigned char c : field.second)
		{
			if(std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~')
				body += static_cast<char>(c);
			else
			{
				body += '%';
				body += kHex[c >> 4];
				body += kHex[c & 15];
			}
		}
	}
	return body;
}

/** What --meet, --stun say. Without --meet nothing is asked. */
static bool MeetOptions(const Options &o, MeetConfig &meet)
{
	if(o.meet.empty())
		return true;
	meet.url = MeetUrlFromText(o.meet == "default" ? std::string() : o.meet);
	if(meet.url.empty())
	{
		std::fprintf(stderr, "--meet: \"%s\" is no address of a meeting point\n", o.meet.c_str());
		return false;
	}
	meet.post = CurlPost;
	if(o.stun.empty())
		meet.stun_servers = DefaultStunServers();
	else if(o.stun != "off")
	{
		StunServer server;
		if(!ParseStunServer(o.stun, server))
		{
			std::fprintf(stderr, "--stun: \"%s\" is no server address\n", o.stun.c_str());
			return false;
		}
		meet.stun_servers.push_back(server);
	}
	return true;
}

static const char *MappingText(PortMapping mapping)
{
	switch(mapping)
	{
		case PortMapping::Stable: return "one port for every destination (hole punching can work)";
		case PortMapping::PerDestination: return "a new port for every destination (symmetric NAT)";
		default: return "unknown kind of router";
	}
}

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
	config.guests_may_sleep = o.guests_may_sleep;
	config.max_guests = static_cast<size_t>(std::max(1, std::min(o.max_guests, static_cast<int>(kMaxGuests))));
	if(o.policy == "ask")
		config.join_policy = JoinPolicy::Ask;
	else if(o.policy == "watch")
		config.join_policy = JoinPolicy::WatchFirst;
	else if(o.policy != "open")
	{
		std::fprintf(stderr, "--policy: ask, watch or open\n");
		return 2;
	}
	std::atomic<bool> knocked{false};
	cb.on_pending_changed = [&] { knocked = true; };
	cb.on_sleep_requested = [&](uint8_t, const std::string &name) {
		{
			std::lock_guard<std::mutex> lock(print_mutex);
			std::printf("[host] %s asked for the console to sleep: stopping, as with a console that went to sleep\n", name.c_str());
			std::fflush(stdout);
		}
		g_quit = true;
	};
	cb.on_chat = [&](const ChatMessage &m) {
		std::lock_guard<std::mutex> lock(print_mutex);
		std::printf("[chat] %s: %s%s%s\n", m.name.c_str(), m.text.c_str(), m.embed.Valid() ? "  [preview: " : "",
			m.embed.Valid() ? (m.embed.title + (m.embed.image.Valid() ? ", with picture]" : "]")).c_str() : "");
		std::fflush(stdout);
	};
	if(!MeetOptions(o, config.meet))
		return 2;
	std::string web_page;
	if(!o.web.empty())
	{
		web_page = WebUrlFromText(o.web == "default" ? std::string() : o.web);
		if(web_page.empty() || !config.meet.Enabled())
		{
			std::fprintf(stderr, "--web needs the address of the page (or \"default\") and a meeting point (--meet)\n");
			return 2;
		}
		if(!WebGuestsSupported())
		{
			std::fprintf(stderr, "--web: this build cannot take guests from a web browser (built without COPLAY_ENABLE_WEB)\n");
			return 2;
		}
		config.web.enabled = true;
		config.web.stun_servers = config.meet.stun_servers;
		config.web.port = static_cast<uint16_t>(o.web_port);
	}
	Host *host_for_reach = nullptr;
	cb.on_reach_changed = [&] {
		HostReach r = host_for_reach->Reach();
		std::lock_guard<std::mutex> lock(print_mutex);
		std::printf("[host] meeting point: %s%s%s; address on the internet: %s; router: %s\n",
			r.meet == MeetState::Ok ? "answers" : r.meet == MeetState::Failed ? "cannot be asked (" : "not asked yet",
			r.meet == MeetState::Failed ? r.meet_error.c_str() : "", r.meet == MeetState::Failed ? ")" : "",
			r.public_address.empty() ? (r.stun_silent ? "no STUN server answers" : "not known") : r.public_address.c_str(),
			MappingText(r.mapping));
		std::fflush(stdout);
	};
	Host host(config, cb);
	host_for_reach = &host;
	if(!host.Start(&error))
	{
		std::fprintf(stderr, "%s\n", error.c_str());
		return 1;
	}
	if(o.closed)
		host.SetSimulatedFilter(UdpSocket::Filter::AddressAndPort);
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
	if(!o.no_address)
		invite.host = o.address;
	invite.port = host.Port();
	invite.secret = config.secret;
	invite.meet = MeetUrlToInvite(config.meet.url);
	std::printf("Sharing on UDP port %u. Invite:\n\n    %s\n\n", host.Port(), EncodeInvite(invite).c_str());
	if(host.WebGuestsOn())
	{
		// A meeting point that is not on HTTPS (a test on this machine) is named in full
		std::string meet = invite.meet;
		if(meet.empty() && config.meet.url != kDefaultMeetUrl)
			meet = config.meet.url;
		// "--stun off" also keeps the browser from asking anybody
		std::printf("For a web browser:\n\n    %s\n\n",
			WebInviteLink(web_page, config.secret, meet, WebStunForLink(o.stun)).c_str());
	}
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
	if(o.no_streams)
		media |= kMediaNoStreams;
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

	// A public room on the list, announced now and then, and taken off at the end
	const bool listed = !o.room_title.empty();
	std::string room_id, room_token;
	if(listed)
	{
		if(o.rooms.empty() || !config.meet.Enabled())
		{
			std::fprintf(stderr, "--public needs --rooms and a meeting point (--meet)\n");
			return 2;
		}
		uint8_t raw[24];
		RandomBytes(raw, sizeof(raw));
		static const char kHex[] = "0123456789abcdef";
		for(size_t i = 0; i < sizeof(raw); i++)
			(i < 8 ? room_id : room_token) += std::string{ kHex[raw[i] >> 4], kHex[raw[i] & 15] };
	}
	auto room = [&](bool remove) {
		Invite listed_invite; // no address: found through the meeting point
		listed_invite.port = host.Port();
		listed_invite.secret = config.secret;
		listed_invite.meet = MeetUrlToInvite(config.meet.url);
		std::vector<std::pair<std::string, std::string>> fields = {
			{ "v", "1" }, { "action", remove ? "remove" : "announce" }, { "id", room_id }, { "token", room_token } };
		if(!remove)
		{
			// One letter for each player, the host first (ASCII names here; "?" otherwise)
			auto initial = [](const std::string &name) {
				for(char c : name)
					if(std::isalnum(static_cast<unsigned char>(c)))
						return std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
				return std::string("?");
			};
			std::string initials = initial(config.name);
			for(const GuestInfo &g : host.Guests())
				initials += initial(g.name);
			fields.push_back({ "initials", initials });
			MixMode mode = host.GetMixMode();
			fields.insert(fields.end(), {
				{ "title", o.room_title }, { "lang", o.lang }, { "game", o.game }, { "host", config.name },
				{ "players", std::to_string(1 + host.Guests().size()) }, { "max", std::to_string(1 + config.max_guests) },
				{ "policy", o.policy },
				{ "control", mode == MixMode::LastActive ? "turns" : mode == MixMode::HostOnly ? "host" : "all" },
				{ "web", host.WebGuestsOn() ? "1" : "0" }, { "invite", EncodeInvite(listed_invite) },
				{ "proto", std::to_string(kProtocolVersion) },
				{ "title_id", o.title_id }, { "cover", o.cover } });
		}
		HttpRequest request;
		request.url = o.rooms;
		request.body = FormBody(fields);
		HttpResponse response;
		std::string why;
		bool ok = CurlPost(request, response, &why) && response.status == 200 && response.body.find("\"ok\":true") != std::string::npos;
		std::lock_guard<std::mutex> lock(print_mutex);
		if(!ok)
			std::printf("[room] the list did not take it: %s\n", why.empty() ? response.body.c_str() : why.c_str());
		std::fflush(stdout);
		return ok;
	};
	if(listed && room(false))
		std::printf("[room] \"%s\" is on the list of public rooms (%s)\n", o.room_title.c_str(), o.policy.c_str());
	auto next_room = clock::now() + std::chrono::seconds(15);
	// The same picture again and again, as often as the list takes one (the app sends a frame)
	auto url64 = [](const std::vector<uint8_t> &data) {
		static const char kUrl64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
		std::string out;
		for(size_t i = 0; i < data.size(); i += 3)
		{
			uint32_t n = static_cast<uint32_t>(data[i]) << 16;
			if(i + 1 < data.size()) n |= static_cast<uint32_t>(data[i + 1]) << 8;
			if(i + 2 < data.size()) n |= data[i + 2];
			out += kUrl64[(n >> 18) & 63];
			out += kUrl64[(n >> 12) & 63];
			if(i + 1 < data.size()) out += kUrl64[(n >> 6) & 63];
			if(i + 2 < data.size()) out += kUrl64[n & 63];
		}
		return out;
	};
	std::string preview_encoded, face_encoded;
	if(listed && !o.preview.empty())
	{
		preview_encoded = url64(ReadFile(o.preview));
		if(preview_encoded.empty())
			std::fprintf(stderr, "--preview: cannot read %s\n", o.preview.c_str());
	}
	if(listed && !o.face.empty())
	{
		face_encoded = url64(ReadFile(o.face));
		if(face_encoded.empty())
			std::fprintf(stderr, "--face: cannot read %s\n", o.face.c_str());
	}
	// The host's picture first, then each guest's where it is a small JPEG (the list takes
	// nothing else; the app makes one of any picture): sent when the players or their pictures change
	auto guest_faces = [&]() {
		std::vector<std::string> faces;
		for(const GuestInfo &g : host.Guests())
		{
			BlobData data = g.avatar.Valid() ? host.Avatar(g.avatar.hash) : nullptr;
			bool jpeg = data && data->size() >= 3 && data->size() <= 8 * 1024
				&& (*data)[0] == 0xff && (*data)[1] == 0xd8 && (*data)[2] == 0xff;
			faces.push_back(jpeg ? url64(*data) : std::string());
		}
		return faces;
	};
	std::string faces_sent;
	auto send_faces = [&](size_t) {
		std::string list = face_encoded;
		for(const std::string &face : guest_faces())
			list += "," + face;
		if(list == faces_sent)
			return;
		faces_sent = list;
		HttpRequest request;
		request.url = o.rooms;
		request.body = FormBody({ { "v", "1" }, { "action", "faces" }, { "id", room_id }, { "token", room_token },
			{ "faces", list } });
		HttpResponse response;
		std::string why;
		bool ok = CurlPost(request, response, &why) && response.status == 200 && response.body.find("\"ok\":true") != std::string::npos;
		std::lock_guard<std::mutex> lock(print_mutex);
		std::printf("[room] players' pictures %s\n", ok ? "sent" : (why.empty() ? response.body.c_str() : why.c_str()));
		std::fflush(stdout);
	};
	if(listed && !face_encoded.empty())
		send_faces(1);
	auto send_preview = [&]() {
		HttpRequest request;
		request.url = o.rooms;
		request.body = FormBody({ { "v", "1" }, { "action", "preview" }, { "id", room_id }, { "token", room_token },
			{ "image", preview_encoded } });
		HttpResponse response;
		std::string why;
		bool ok = CurlPost(request, response, &why) && response.status == 200 && response.body.find("\"ok\":true") != std::string::npos;
		std::lock_guard<std::mutex> lock(print_mutex);
		std::printf("[room] picture %s\n", ok ? "sent" : (why.empty() ? response.body.c_str() : why.c_str()));
		std::fflush(stdout);
	};
	auto next_preview = clock::now() + std::chrono::seconds(2);
	size_t room_players = 1;
	// Who knocks: since when, so that they are let in after a moment
	std::map<uint64_t, clock::time_point> knocks;

	auto next_chat = clock::now() + std::chrono::seconds(4);
	int chat_count = 0;
	while(!g_quit)
	{
		auto now = clock::now();
		if(listed)
		{
			size_t players = 1 + host.Guests().size();
			if(now >= next_room || players != room_players)
			{
				const bool changed = players != room_players;
				room_players = players;
				next_room = now + std::chrono::seconds(15);
				room(false);
				if(changed && !face_encoded.empty())
					send_faces(players);
			}
			// A guest's picture arrives a moment after the guest
			if(!face_encoded.empty() && players > 1)
				send_faces(players);
			if(!preview_encoded.empty() && now >= next_preview)
			{
				next_preview = now + std::chrono::seconds(45);
				send_preview();
			}
		}
		if(knocked.exchange(false) || !knocks.empty())
		{
			std::vector<PendingGuest> pending = host.Pending();
			for(const PendingGuest &p : pending)
			{
				if(knocks.emplace(p.id, now).second)
				{
					std::lock_guard<std::mutex> lock(print_mutex);
					std::printf("[host] %s knocks%s\n", p.name.c_str(), p.web ? " from a web browser" : "");
					std::fflush(stdout);
				}
				if(o.refuse || now - knocks[p.id] >= std::chrono::seconds(o.admit_after))
				{
					host.Admit(p.id, !o.refuse);
					std::lock_guard<std::mutex> lock(print_mutex);
					std::printf("[host] %s %s\n", p.name.c_str(), o.refuse ? "is not let in" : "is let in");
					std::fflush(stdout);
				}
			}
			for(auto it = knocks.begin(); it != knocks.end();)
			{
				bool still = false;
				for(const PendingGuest &p : pending)
					still = still || p.id == it->first;
				it = still ? std::next(it) : knocks.erase(it);
			}
		}
		if(o.seconds > 0 && now - start >= std::chrono::seconds(o.seconds))
			break;

		if(o.chat && now >= next_chat)
		{
			next_chat = now + std::chrono::seconds(12);
			static const char *lines[] = {
				"Hi everybody, welcome to the session \xF0\x9F\x91\x8B",
				"Who takes the next mission? \xF0\x9F\x8E\xAE",
				"gg, that was close \xF0\x9F\x94\xA5",
				"Look: https://gamernova.net",
			};
			const char *line = lines[chat_count++ % 4];
			ChatEmbed embed;
			if(std::strstr(line, "https://"))
			{
				embed.url = "https://gamernova.net";
				embed.site = "GamerNova.net";
				embed.title = "GamerNova - tools for players";
				embed.description = "A made-up preview from the simulator";
			}
			host.SendChat(line, embed, {});
		}

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
				std::printf("  #%u %s (%s%s%s%s%s%s%s, %u ms)", g.slot, g.name.c_str(), (g.want & kWantVideo) ? "video" : "controller only",
					g.in_control ? ", in control" : "", (g.media & kMediaVoice) ? ", voice" : "",
					(g.media & kMediaMuted) ? " muted" : "", (g.media & kMediaCamera) ? ", webcam" : "",
					(g.media & kMediaLive) ? ", LIVE" : "", (g.media & kMediaNoStreams) ? ", not in streams" : "", g.rtt_ms);
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
	if(listed)
		room(true);
#ifdef SIGUSR1
	if(g_coming_back)
	{
		std::printf("Stopping like a host that is getting its console back\n");
		host.StopForReconnect();
		return 0;
	}
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

	if(!MeetOptions(o, config.meet))
		return 2;
	// An invite that names a meeting point of its own wins over "default"
	if(!config.meet.url.empty() && !config.invite.meet.empty())
		config.meet.url = MeetUrlFromInvite(config.invite.meet);

	Client client(config);
	if(o.loss > 0)
		client.SetSimulatedLoss(o.loss, 54321);
	if(o.closed)
		client.SetSimulatedFilter(UdpSocket::Filter::AddressAndPort);
	uint64_t connect_started = NowMs();
	if(!client.Connect(&error))
	{
		std::fprintf(stderr, "%s\n", error.c_str());
		return 1;
	}
	std::printf("Connected after %llu ms, %s\n", static_cast<unsigned long long>(NowMs() - connect_started),
		client.ViaMeetingPoint() ? "under an address from the meeting point" : "under the address in the invite");
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
	std::string players_seen;
	cb.on_state = [&](const SessionState &s) {
		std::lock_guard<std::mutex> lock(mutex);
		// Who is in, and as which player (Each their own controller), when it changes
		std::string players;
		for(const Participant &p : s.participants)
		{
			players += (players.empty() ? "" : ", ") + p.name;
			if(p.player)
				players += " P" + std::to_string(p.player);
			if(p.flags & kParticipantKeyMouse)
				players += " M+K";
			if(p.slot == s.your_slot)
				players += " (me)";
		}
		if(players != players_seen)
		{
			players_seen = players;
			std::printf("[guest] players: %s\n", players.c_str());
			std::fflush(stdout);
		}
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
#ifdef SIGUSR1
	std::signal(SIGUSR1, OnComingBackSignal);
#endif
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
