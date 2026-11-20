// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <coplay/meet.h>

#include <coplay/bytes.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace coplay {

const char *const kDefaultMeetUrl = "https://gamernova.net/api/psrpt_meet.php";

static const uint8_t kNoteVersion = 1;
static const size_t kMaxNotesPerReply = 32;
static const size_t kMaxUrlLength = 300;

/** The slowest pace a meeting point can ask a host for. */
static const uint32_t kSlowestPollMs = 60000;
static const uint32_t kGuestSlowestPollMs = 2500;

bool MeetNote::SameAddresses(const MeetNote &o) const
{
	if(candidates.size() != o.candidates.size())
		return false;
	for(size_t i = 0; i < candidates.size(); i++)
	{
		if(candidates[i].kind != o.candidates[i].kind || !candidates[i].address.SameEndpoint(o.candidates[i].address))
			return false;
	}
	return true;
}

std::string MeetRoomId(const Secret &secret)
{
	auto room = DeriveMeetRoom(secret);
	return Base64UrlEncode(room.data(), room.size());
}

bool SealMeetNote(const Secret &secret, const MeetNote &note, std::string &text)
{
	std::vector<uint8_t> entries;
	ByteWriter e(entries);
	uint8_t count = 0;
	for(const MeetCandidate &c : note.candidates)
	{
		if(count == kMeetMaxCandidates)
			break;
		uint8_t bytes[16];
		size_t n = c.address.Bytes(bytes);
		if(!n || !c.address.Port())
			continue;
		e.U8(static_cast<uint8_t>(c.kind));
		e.U8(static_cast<uint8_t>(n));
		e.Bytes(bytes, n);
		e.U16(c.address.Port());
		count++;
	}

	std::vector<uint8_t> body;
	ByteWriter w(body);
	w.U8(kNoteVersion);
	w.U8(static_cast<uint8_t>(note.role));
	w.U64(note.peer_id);
	w.U32(note.seq);
	w.U8(count);
	w.Bytes(entries.data(), entries.size());

	std::vector<uint8_t> sealed;
	if(!SealHandshakePacket(DeriveMeetKey(secret), PacketType::MeetNote, body.data(), body.size(), sealed))
		return false;
	text = Base64UrlEncode(sealed.data(), sealed.size());
	return text.size() <= kMeetMaxNoteText;
}

bool OpenMeetNote(const Secret &secret, const std::string &text, MeetNote &note)
{
	if(text.empty() || text.size() > kMeetMaxNoteText)
		return false;
	std::vector<uint8_t> raw;
	if(!Base64UrlDecode(text, raw))
		return false;
	PacketHeader header;
	if(!ParseHeader(raw.data(), raw.size(), header) || header.type != PacketType::MeetNote)
		return false;
	std::vector<uint8_t> body;
	if(!OpenPacket(DeriveMeetKey(secret), raw.data(), raw.size(), body))
		return false;

	ByteReader r(body.data(), body.size());
	if(r.U8() != kNoteVersion)
		return false;
	uint8_t role = r.U8();
	if(role != static_cast<uint8_t>(MeetRole::Host) && role != static_cast<uint8_t>(MeetRole::Guest))
		return false;
	MeetNote out;
	out.role = static_cast<MeetRole>(role);
	out.peer_id = r.U64();
	out.seq = r.U32();
	uint8_t count = r.U8();
	if(!r.ok() || count > kMeetMaxCandidates)
		return false;
	for(uint8_t i = 0; i < count; i++)
	{
		uint8_t kind = r.U8();
		uint8_t size = r.U8();
		if(!r.ok() || (size != 4 && size != 16))
			return false;
		uint8_t bytes[16];
		if(!r.Bytes(bytes, size))
			return false;
		uint16_t port = r.U16();
		if(!r.ok())
			return false;
		// A kind this version does not know is left out, the rest of the note still counts
		if(kind < static_cast<uint8_t>(CandidateKind::Public) || kind > static_cast<uint8_t>(CandidateKind::IPv6) || !port)
			continue;
		MeetCandidate c;
		c.kind = static_cast<CandidateKind>(kind);
		c.address = Address::FromBytes(bytes, size, port);
		out.candidates.push_back(c);
	}
	note = std::move(out);
	return true;
}

std::string MeetRequestBody(const std::string &room, uint64_t sender_id, MeetRole role, const std::string &note_text)
{
	char id[17];
	std::snprintf(id, sizeof(id), "%016llx", static_cast<unsigned long long>(sender_id));
	// Every value consists of letters, digits, "-" and "_": nothing needs escaping
	return "v=1&room=" + room + "&id=" + id + "&role=" + (role == MeetRole::Host ? "h" : "g") + "&note=" + note_text;
}

bool ParseMeetReply(const std::string &body, MeetReply &reply, std::string *error)
{
	auto fail = [error](const std::string &why) {
		if(error)
			*error = why;
		return false;
	};
	MeetReply out;
	bool first = true;
	size_t pos = 0;
	while(pos <= body.size())
	{
		size_t end = body.find('\n', pos);
		if(end == std::string::npos)
			end = body.size();
		std::string line = body.substr(pos, end - pos);
		pos = end + 1;
		while(!line.empty() && (line.back() == '\r' || line.back() == ' '))
			line.pop_back();
		if(line.empty())
			continue;
		if(first)
		{
			// A web page instead of an answer: a wrong address, or a server's error page
			if(line.compare(0, 11, "psrpt-meet ") != 0)
				return fail("the answer does not come from a meeting point");
			first = false;
			continue;
		}
		size_t space = line.find(' ');
		std::string word = line.substr(0, space);
		std::string value = space == std::string::npos ? std::string() : line.substr(space + 1);
		if(word == "error")
			return fail("the meeting point says: " + value.substr(0, 120));
		if(word == "poll")
		{
			if(!value.empty() && value.size() <= 7 && value.find_first_not_of("0123456789") == std::string::npos)
				out.poll_ms = static_cast<uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
		}
		else if(word == "note")
		{
			if(!value.empty() && value.size() <= kMeetMaxNoteText && out.notes.size() < kMaxNotesPerReply)
				out.notes.push_back(value);
		}
		// anything else is from a newer meeting point and not for us
	}
	if(first)
		return fail("the meeting point sent an empty answer");
	reply = std::move(out);
	return true;
}

// ------------------------------------------------------------------ the meeting point's address

static std::string TrimText(const std::string &s)
{
	size_t a = 0;
	size_t b = s.size();
	auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
	while(a < b && is_space(s[a]))
		a++;
	while(b > a && is_space(s[b - 1]))
		b--;
	return s.substr(a, b - a);
}

static bool StartsWith(const std::string &s, const char *prefix)
{
	size_t n = std::strlen(prefix);
	return s.size() >= n && s.compare(0, n, prefix) == 0;
}

bool MeetIsOff(const std::string &text)
{
	std::string s = TrimText(text);
	return s.size() == 3 && (s[0] == 'o' || s[0] == 'O') && (s[1] == 'f' || s[1] == 'F') && (s[2] == 'f' || s[2] == 'F');
}

bool IsInviteMeetAddress(const std::string &text)
{
	if(text.empty() || text.size() > 200)
		return false;
	for(char c : text)
	{
		bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
			|| c == '.' || c == '-' || c == '_' || c == '~' || c == '/' || c == ':' || c == '%' || c == '[' || c == ']';
		if(!ok)
			return false;
	}
	// A name or address has to come first, and no second "//" may start another one
	return text.front() != '/' && text.front() != ':' && text.find("//") == std::string::npos;
}

std::string MeetUrlFromText(const std::string &text, const std::string &default_url)
{
	std::string s = TrimText(text);
	if(s.empty())
		return default_url;
	if(MeetIsOff(s) || s.size() > kMaxUrlLength)
		return std::string();
	for(char c : s)
	{
		if(static_cast<unsigned char>(c) <= ' ' || c == '"' || c == '<' || c == '>' || c == '\\')
			return std::string();
	}
	if(StartsWith(s, "https://") || StartsWith(s, "http://"))
		return s;
	if(s.find("://") != std::string::npos)
		return std::string(); // some other kind of address
	return "https://" + s;
}

std::string MeetUrlToInvite(const std::string &url, const std::string &default_url)
{
	if(url.empty() || url == default_url || !StartsWith(url, "https://"))
		return std::string();
	std::string rest = url.substr(8);
	return IsInviteMeetAddress(rest) ? rest : std::string();
}

std::string MeetUrlFromInvite(const std::string &invite_meet, const std::string &default_url)
{
	if(invite_meet.empty())
		return default_url;
	return IsInviteMeetAddress(invite_meet) ? "https://" + invite_meet : std::string();
}

// ------------------------------------------------------------------ the session's addresses

std::vector<MeetCandidate> GatherCandidates(const StunProbe &stun, const UdpSocket &socket, bool own_addresses)
{
	std::vector<MeetCandidate> out;
	Address seen = stun.Public();
	if(seen.IsValid())
		out.push_back({ CandidateKind::Public, seen });
	uint16_t port = socket.LocalPort();
	if(!own_addresses || !port)
		return out;
	Address a;
	std::string local = GuessLocalAddress();
	// Without a router in between the address in the own network is the public one
	if(!local.empty() && ParseAddress(local, port, a) && !(seen.IsValid() && a.SameEndpoint(seen)))
		out.push_back({ CandidateKind::Local, a });
	if(socket.HasIPv6())
	{
		std::string global = GuessGlobalIPv6Address();
		if(!global.empty() && ParseAddress(global, port, a))
			out.push_back({ CandidateKind::IPv6, a });
	}
	return out;
}

// ------------------------------------------------------------------ MeetWorker

MeetWorker::MeetWorker(const MeetConfig &config, const Secret &secret, MeetRole role, OnNotes on_notes)
	: config(config), secret(secret), role(role), on_notes(std::move(on_notes)), room(MeetRoomId(secret))
{
	RandomBytes(reinterpret_cast<uint8_t *>(&sender_id), sizeof(sender_id));
	thread = std::thread(&MeetWorker::Run, this);
}

MeetWorker::~MeetWorker()
{
	Stop();
}

void MeetWorker::Stop()
{
	stop = true;
	{
		std::lock_guard<std::mutex> lock(mutex);
		changed.notify_all();
	}
	if(thread.joinable())
		thread.join();
}

void MeetWorker::SetNote(const MeetNote &note)
{
	std::string text;
	if(!SealMeetNote(secret, note, text))
		return;
	std::lock_guard<std::mutex> lock(mutex);
	note_text = std::move(text);
	have_note = true;
	note_new = true;
	changed.notify_all();
}

MeetState MeetWorker::State()
{
	std::lock_guard<std::mutex> lock(mutex);
	return state;
}

std::string MeetWorker::Error()
{
	std::lock_guard<std::mutex> lock(mutex);
	return error;
}

void MeetWorker::Run()
{
	const bool host = role == MeetRole::Host;
	const uint32_t own_pace = std::max<uint32_t>(host ? config.host_poll_ms : config.guest_poll_ms, 10);
	uint32_t pace = own_pace;
	uint32_t failures = 0;
	uint64_t next_ms = 0;

	std::unique_lock<std::mutex> lock(mutex);
	while(!stop)
	{
		uint64_t now = NowMs();
		// A changed note goes out at once, but not while the meeting point is failing:
		// then only the growing pauses decide
		bool due = have_note && (now >= next_ms || (note_new && failures == 0));
		if(!due)
		{
			uint64_t wait = have_note && next_ms > now ? next_ms - now : 1000;
			changed.wait_for(lock, std::chrono::milliseconds(std::min<uint64_t>(wait, 1000)));
			continue;
		}
		note_new = false;
		HttpRequest request;
		request.url = config.url;
		request.body = MeetRequestBody(room, sender_id, role, note_text);
		request.cancel = &stop;
		lock.unlock();

		HttpResponse response;
		std::string problem;
		requests++;
		bool ok = config.post(request, response, &problem);
		if(ok && response.status != 200)
		{
			ok = false;
			problem = "the meeting point answered with HTTP " + std::to_string(response.status);
		}
		MeetReply reply;
		if(ok && !ParseMeetReply(response.body, reply, &problem))
			ok = false;
		std::vector<MeetNote> notes;
		if(ok)
		{
			for(const std::string &text : reply.notes)
			{
				MeetNote note;
				if(OpenMeetNote(secret, text, note) && note.role != role)
					notes.push_back(std::move(note));
			}
		}
		if(ok && !stop && on_notes && !notes.empty())
			on_notes(notes);

		lock.lock();
		now = NowMs();
		if(ok)
		{
			failures = 0;
			state = MeetState::Ok;
			error.clear();
			if(host)
				pace = std::min(std::max(own_pace, reply.poll_ms), std::max(own_pace, kSlowestPollMs));
			else
				pace = std::min(std::max(own_pace, reply.poll_ms / 8), std::max(own_pace, kGuestSlowestPollMs));
			next_ms = now + pace;
		}
		else
		{
			failures++;
			state = MeetState::Failed;
			error = problem.empty() ? "no answer from the meeting point" : problem;
			// The next request waits, twice as long after each failure
			uint64_t pause = std::max<uint32_t>(host ? config.host_retry_ms : config.guest_retry_ms, 10);
			const uint64_t longest = pause * (host ? 20 : 4);
			for(uint32_t i = 1; i < failures && pause < longest; i++)
				pause *= 2;
			next_ms = now + std::min(pause, longest);
		}
	}
}

} // namespace coplay
