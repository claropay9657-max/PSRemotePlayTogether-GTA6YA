// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef COPLAY_MEET_H
#define COPLAY_MEET_H

/*
 * Joining without a forwarded port.
 *
 * Host and guest each leave a short note at a meeting point (one HTTPS endpoint)
 * that says under which addresses their session socket can be reached, read the
 * other side's note, and send to each other at the same time. A router lets the
 * answer to what it has just seen going out come back in, so the two meet ("UDP
 * hole punching"). The meeting point only passes the notes on; a session's
 * traffic never goes through it. docs/nat-traversal.md has the whole picture.
 *
 * A note is sealed with a key derived from the invite's key: the meeting point
 * can neither read nor write one, and the name of a session there (the "room")
 * cannot be turned back into the key.
 *
 * What is sent to the meeting point, as an HTML form (POST):
 *
 *     v=1  room=<22 characters>  id=<16 hex digits>  role=h|g  note=<sealed note or empty>
 *
 * and what comes back, as plain text:
 *
 *     psrpt-meet 1
 *     poll 3000          how often a host should ask, in milliseconds (optional)
 *     note <sealed note> one line per note of the other role
 *
 * This library does not speak HTTPS itself: whoever uses it passes a function
 * that does (HttpPost).
 */

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "crypto.h"
#include "socket.h"
#include "stun.h"

namespace coplay {

/** The meeting point that is used when nothing else is said. */
extern const char *const kDefaultMeetUrl;

constexpr size_t kMeetRoomIdLength = 22;
constexpr size_t kMeetMaxCandidates = 8;
/** Upper bound for a sealed note in its text form. */
constexpr size_t kMeetMaxNoteText = 1024;

enum class MeetRole : uint8_t
{
	Host = 1,
	Guest = 2
};

enum class CandidateKind : uint8_t
{
	Public = 1, // IPv4 address and port on the internet, as a STUN server saw the session's socket
	Local = 2,  // address in the own network, for a friend behind the same router
	IPv6 = 3    // global IPv6 address
};

struct MeetCandidate
{
	CandidateKind kind = CandidateKind::Public;
	Address address;
};

struct MeetNote
{
	MeetRole role = MeetRole::Guest;
	/** Random per Host or per attempt to join; tells the notes in a room apart. */
	uint64_t peer_id = 0;
	/** Counts up when the addresses change. */
	uint32_t seq = 0;
	std::vector<MeetCandidate> candidates;

	bool SameAddresses(const MeetNote &o) const;
};

/** The name of the session at the meeting point: kMeetRoomIdLength characters. */
std::string MeetRoomId(const Secret &secret);
/** @param text the sealed note in the form it is sent in (base64url) */
bool SealMeetNote(const Secret &secret, const MeetNote &note, std::string &text);
/** @return false if the text was not sealed with this secret, was changed, or makes no sense */
bool OpenMeetNote(const Secret &secret, const std::string &text, MeetNote &note);

// ------------------------------------------------------------------ talking to the meeting point

struct HttpRequest
{
	std::string url;
	/** application/x-www-form-urlencoded */
	std::string body;
	uint32_t timeout_ms = 4000;
	/** When this becomes true the request is to be given up at once. May be null. */
	const std::atomic<bool> *cancel = nullptr;
};

struct HttpResponse
{
	int status = 0;
	std::string body;
};

/**
 * Sends one POST request and waits for the answer. Called from a thread of the
 * library, one call at a time per session.
 * @return false if no answer came (then error says why); an answer with any status is true
 */
using HttpPost = std::function<bool(const HttpRequest &, HttpResponse &, std::string *error)>;

std::string MeetRequestBody(const std::string &room, uint64_t sender_id, MeetRole role, const std::string &note_text);

struct MeetReply
{
	/** 0 if the meeting point did not say. */
	uint32_t poll_ms = 0;
	std::vector<std::string> notes;
};

bool ParseMeetReply(const std::string &body, MeetReply &reply, std::string *error);

enum class HostLook
{
	There,    // the host shares the session now
	NotThere, // the meeting point has no note of the host
	Unknown   // no answer, or none that makes sense
};

/**
 * Whether the host of a session is sharing it right now, for a list of sessions
 * joined before. One request as a guest that leaves no note: nothing of ours
 * stays at the meeting point, and the host learns nothing of it. A sharing host
 * keeps its note there (MeetWorker); a host that shares without a meeting point
 * is never found this way. Blocks for as long as post does.
 */
HostLook LookForHost(const std::string &url, const HttpPost &post, const Secret &secret, std::string *error = nullptr);

/**
 * The URL for what a user wrote into the setting: nothing is the default, "off"
 * is none (an empty string comes back), "example.org/meet.php" gets https://
 * in front, and a complete URL is taken as it is.
 */
std::string MeetUrlFromText(const std::string &text, const std::string &default_url = kDefaultMeetUrl);
/** Whether the setting says "off". */
bool MeetIsOff(const std::string &text);
/**
 * What an invite says about the meeting point: nothing for the default one,
 * otherwise the URL without its "https://". Empty as well for a URL an invite
 * cannot carry (anything but HTTPS).
 */
std::string MeetUrlToInvite(const std::string &url, const std::string &default_url = kDefaultMeetUrl);
std::string MeetUrlFromInvite(const std::string &invite_meet, const std::string &default_url = kDefaultMeetUrl);
/** Whether a meeting point may be written into an invite like this. */
bool IsInviteMeetAddress(const std::string &text);

struct MeetConfig
{
	/** Empty: no meeting point is used. */
	std::string url;
	HttpPost post;
	/** Asked from the session's own socket for its address on the internet. Empty: nobody is asked. */
	std::vector<StunServer> stun_servers;
	/** Also name the address in the own network and the global IPv6 address. Tests switch this off. */
	bool own_addresses = true;
	/** How often a sharing host looks for guests. The meeting point can ask for a slower pace. */
	uint32_t host_poll_ms = 3000;
	/** How often a guest looks for the host while it is connecting. */
	uint32_t guest_poll_ms = 700;
	/**
	 * How long to wait after a request that did not work; twice as long after each
	 * further failure, up to 20 times this for a host (5 minutes) and 4 times for a
	 * guest. A sharing host must never become a burden for a meeting point that is
	 * down or does not exist, while a guest has only seconds to get in.
	 */
	uint32_t host_retry_ms = 15000;
	uint32_t guest_retry_ms = 1500;
	/** How long a host keeps sending to the addresses of a guest it has found. */
	uint32_t punch_ms = 6000;
	/** How long a guest keeps trying once it has found the host's note. */
	uint32_t connect_ms = 12000;

	bool Enabled() const { return !url.empty() && static_cast<bool>(post); }
};

enum class MeetState
{
	Off,
	Trying, // no answer yet
	Ok,
	Failed  // the last request did not work; it is tried again, with growing pauses
};

/** The addresses a session's socket can be reached under, for its note. */
std::vector<MeetCandidate> GatherCandidates(const StunProbe &stun, const UdpSocket &socket, bool own_addresses);

/**
 * Keeps one note at the meeting point and collects the notes of the other
 * role, on a thread of its own.
 */
class MeetWorker
{
	public:
		/** Called on the worker's thread with every note that could be opened, each time it is seen. */
		using OnNotes = std::function<void(const std::vector<MeetNote> &)>;
		/**
		 * Called on the worker's thread with the notes that are not of the kind above, as
		 * they stand at the meeting point: what a browser leaves there (web.h).
		 */
		using OnOtherNotes = std::function<void(const std::vector<std::string> &)>;

		MeetWorker(const MeetConfig &config, const Secret &secret, MeetRole role, OnNotes on_notes,
			OnOtherNotes on_other_notes = nullptr);
		/** Stops; returns when the thread has ended. */
		~MeetWorker();
		MeetWorker(const MeetWorker &) = delete;
		MeetWorker &operator=(const MeetWorker &) = delete;

		/** The note to keep there from now on. Nothing is sent before the first one is set. */
		void SetNote(const MeetNote &note);
		/**
		 * A second note to keep there next to the first, already sealed by the caller
		 * (a host's answers to browsers, web.h). Empty takes it away again. It costs one
		 * more request per round, so it is only set while it is needed.
		 */
		void SetExtraNote(const std::string &text);
		/**
		 * Another invite's key: the note moves to that key's room at once, and the one in the
		 * old room is taken away, so that an old invite does not even find the host any more.
		 * The extra note goes (it was sealed with the old key); the caller sets a new one.
		 */
		void SetSecret(const Secret &secret);
		void Stop();

		MeetState State();
		/** Why the last request failed. */
		std::string Error();
		uint32_t Requests() const { return requests; }

	private:
		void Run();

		const MeetConfig config;
		// with mutex: they change with SetSecret()
		Secret secret;
		const MeetRole role;
		const OnNotes on_notes;
		const OnOtherNotes on_other_notes;
		std::string room;
		/** A room to take our note out of, once (SetSecret()); empty when there is none. */
		std::string leave_room;
		/** The note as it is, for sealing it again with a new key. */
		MeetNote plain_note;
		uint64_t sender_id = 0;

		std::thread thread;
		std::mutex mutex;
		std::condition_variable changed;
		std::atomic<bool> stop{false};
		std::atomic<uint32_t> requests{0};
		// with mutex
		bool have_note = false;
		bool note_new = false;
		std::string note_text;
		std::string extra_text;
		/** The extra note differs from what the meeting point has. */
		bool extra_new = false;
		/** The meeting point holds an extra note of ours. */
		bool extra_there = false;
		MeetState state = MeetState::Trying;
		std::string error;
};

} // namespace coplay

#endif // COPLAY_MEET_H
