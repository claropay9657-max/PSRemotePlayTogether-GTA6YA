// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef COPLAY_WEB_H
#define COPLAY_WEB_H

/*
 * Guests that join from a web browser, without the app.
 *
 * A browser cannot send the datagrams of protocol.h. What it can do is WebRTC:
 * a direct, encrypted connection between the browser and the host, set up with
 * the same trick as joining without a forwarded port (both sides send to each
 * other at the same time). Nothing of a session passes through a server here
 * either.
 *
 * How the two find each other: the page leaves a note at the meeting point
 * (meet.h) that says how its side of the connection can be reached and how it
 * proves who it is (the fingerprint of its certificate). The host answers with
 * a note of its own. Both notes are sealed with a key derived from the invite's
 * key, so only somebody with the invite can write one, and the certificates
 * named in them are the only ones the connection accepts.
 *
 *     AES-256-GCM, key = HKDF(invite key, "psrpt coplay web note")
 *     text = base64url(12 random bytes | ciphertext | 16 byte tag)
 *
 * (AES-GCM and not the ChaCha20-Poly1305 of everything else, because that is
 * what a browser brings along.)
 *
 * Once connected, the two talk over one data channel that delivers like UDP
 * (no order, nothing sent again). Each message is one packet of protocol.h
 * without its header and its seal, which the connection itself provides:
 *
 *     0     type      (PacketType)
 *     1..   payload   exactly as in protocol.h
 *
 * The first message is Hello with the protocol version in front of its payload;
 * the answer is Welcome or Reject. From then on a browser guest is a guest like
 * any other.
 */

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "crypto.h"
#include "socket.h"
#include "stun.h"

namespace coplay {

/** The page that browser guests open when nothing else is said. */
extern const char *const kDefaultWebUrl;

constexpr size_t kWebFingerprintSize = 32; // SHA-256
constexpr size_t kWebMaxCandidates = 8;
/** Browsers that can be on their way in at the same time; one more has to wait a few seconds. */
constexpr size_t kWebMaxPending = 4;

enum class WebCandidateType : uint8_t
{
	Host = 1,     // an address of the machine itself
	Reflexive = 2 // its address on the internet, as a STUN server saw it
};

struct WebCandidate
{
	WebCandidateType type = WebCandidateType::Host;
	Address address;

	bool operator==(const WebCandidate &o) const { return type == o.type && address.SameEndpoint(o.address); }
};

/** One side's half of a WebRTC connection. */
struct WebPeerDescription
{
	std::string ufrag;
	std::string pwd;
	std::vector<WebCandidate> candidates;
};

/** What a browser leaves at the meeting point. */
struct WebOffer
{
	/** Random per attempt to join; also the browser's id at the meeting point. */
	uint64_t peer_id = 0;
	/** The version of protocol.h the page speaks. */
	uint8_t protocol = 0;
	std::array<uint8_t, kWebFingerprintSize> fingerprint{};
	/** The name the browser gave the data channel's part of the connection ("0"). */
	std::string mid;
	WebPeerDescription side;
};

/** The host's answer to one browser. */
struct WebAnswer
{
	uint64_t peer_id = 0;
	/** Whether the host opens the encrypted connection (it does, unless the WebRTC library decides otherwise). */
	bool active = true;
	WebPeerDescription side;

	bool operator==(const WebAnswer &o) const
	{
		return peer_id == o.peer_id && active == o.active && side.ufrag == o.side.ufrag && side.pwd == o.side.pwd
			&& side.candidates == o.side.candidates;
	}
};

/** What the host leaves at the meeting point while browsers are on their way in. */
struct WebAnswers
{
	uint8_t protocol = 0;
	std::array<uint8_t, kWebFingerprintSize> fingerprint{};
	std::vector<WebAnswer> answers;
};

Key DeriveWebNoteKey(const Secret &secret);

bool SealWebOffer(const Secret &secret, const WebOffer &offer, std::string &text);
bool OpenWebOffer(const Secret &secret, const std::string &text, WebOffer &offer);
bool SealWebAnswers(const Secret &secret, const WebAnswers &answers, std::string &text);
bool OpenWebAnswers(const Secret &secret, const std::string &text, WebAnswers &answers);

/** "AB:CD:..." as it stands in a session description. */
std::string WebFingerprintToString(const std::array<uint8_t, kWebFingerprintSize> &fingerprint);
bool WebFingerprintFromString(const std::string &text, std::array<uint8_t, kWebFingerprintSize> &fingerprint);

/** The session description a browser would have sent, put together again from its note. */
std::string WebOfferToSdp(const WebOffer &offer);

/**
 * The link a friend opens: the page, and behind a "#" (the part a browser keeps
 * to itself) the invite's key and, if it is not the default one, the meeting point.
 * @param meet as in Invite::meet
 */
std::string WebInviteLink(const std::string &page_url, const Secret &secret, const std::string &meet);

/**
 * The page for what a user wrote into the setting: nothing is the default,
 * "example.org/play/" gets https:// in front. Empty for what is no address.
 */
std::string WebUrlFromText(const std::string &text, const std::string &default_url = kDefaultWebUrl);

struct WebConfig
{
	bool enabled = false;
	/** Asked by the WebRTC library for this computer's address on the internet. */
	std::vector<StunServer> stun_servers;
	/**
	 * 0: every browser guest gets a port of its own, whichever is free. Otherwise all
	 * of them share this UDP port, which a host can forward in the router for friends
	 * whose own router is of the difficult kind.
	 */
	uint16_t port = 0;
	/** A browser that has not connected after this long is given up. */
	uint32_t connect_timeout_ms = 30000;
};

/**
 * The WebRTC side of a host: one connection per browser. All callbacks come
 * from one thread of the gateway, one at a time, and never while the gateway
 * holds a lock of its own or of the WebRTC library.
 */
class WebGateway
{
	public:
		struct Callbacks
		{
			/** The data channel of this browser is open: messages can go both ways. */
			std::function<void(uint64_t peer_id)> on_open;
			std::function<void(uint64_t peer_id, const uint8_t *data, size_t size)> on_message;
			/** The connection is gone, or never came to be. Not called after Close(). */
			std::function<void(uint64_t peer_id)> on_closed;
			/** Answers() says something else now. */
			std::function<void()> on_answers_changed;
			std::function<void(const std::string &)> on_log;
		};

		virtual ~WebGateway() = default;

		/** A browser wants in. The same offer may be handed in any number of times. */
		virtual void Offer(const WebOffer &offer) = 0;
		/** What to tell the browsers that are on their way in right now. */
		virtual WebAnswers Answers() = 0;

		/** @return false if the browser is not connected */
		virtual bool Send(uint64_t peer_id, const uint8_t *data, size_t size) = 0;
		/** Bytes that wait to be sent to this browser; grows when its connection cannot keep up. */
		virtual size_t Buffered(uint64_t peer_id) = 0;
		virtual void Close(uint64_t peer_id) = 0;
		/** "203.0.113.7:54321", or empty if it is not known. */
		virtual std::string PeerAddress(uint64_t peer_id) = 0;
};

using WebGatewayFactory = std::function<std::unique_ptr<WebGateway>(const WebConfig &, const WebGateway::Callbacks &)>;

/** Whether this build can take browser guests at all. */
bool WebGuestsSupported();
/** The gateway of this build, or null if it has none. */
std::unique_ptr<WebGateway> CreateWebGateway(const WebConfig &config, const WebGateway::Callbacks &callbacks);

} // namespace coplay

#endif // COPLAY_WEB_H
