// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef COPLAY_STUN_H
#define COPLAY_STUN_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "socket.h"

namespace coplay {

// An invite for friends elsewhere has to carry the address this machine is seen
// under from the internet, which the machine itself does not know behind a
// router. A STUN server (RFC 5389) tells: it answers a request with the address
// the request came from. Nothing but that one question is sent to it.

constexpr uint16_t kStunDefaultPort = 3478;

/** Which of the server's addresses to ask, and so which kind of address comes back. */
enum class StunFamily
{
	Any, // IPv4 if the server has it, otherwise IPv6
	IPv4,
	IPv6
};

/**
 * Asks a STUN server for this machine's public address. Blocks until the answer
 * is there or the time is up, so not for a thread that must stay responsive.
 *
 * @param server host name or literal address
 * @param timeout_ms how long to wait for one of the server's addresses; the
 *        request is sent three times in that time, with growing pauses
 * @param public_address for example "203.0.113.7" or "2001:db8::7"
 * @param public_port the port the request left the router with; this is the
 *        port of the socket used for asking, not of a session. May be nullptr.
 */
bool StunQuery(const std::string &server, uint16_t port, uint32_t timeout_ms,
		std::string &public_address, uint16_t *public_port, std::string *error,
		StunFamily family = StunFamily::Any);

struct StunServer
{
	std::string host;
	uint16_t port = kStunDefaultPort;
};

/**
 * Reads "host", "host:port" or "[2001:db8::1]:port" as written in a setting.
 * Without a port it is kStunDefaultPort.
 */
bool ParseStunServer(const std::string &text, StunServer &server);

/** Public servers to ask when the user has not named one, in this order. */
std::vector<StunServer> DefaultStunServers();

/**
 * Asks the servers in turn until one answers; see StunQuery(). The error is
 * that of the last server.
 */
bool StunQueryFirst(const std::vector<StunServer> &servers, uint32_t timeout_ms,
		std::string &public_address, uint16_t *public_port, std::string *error,
		StunFamily family = StunFamily::Any);

// The message format, separate from the network for the tests

constexpr size_t kStunTransactionIdSize = 12;
using StunTransactionId = std::array<uint8_t, kStunTransactionIdSize>;

/** A Binding Request without attributes: 20 bytes. */
std::vector<uint8_t> StunBindingRequest(const StunTransactionId &id);

/**
 * Reads the address out of a Binding Success Response. XOR-MAPPED-ADDRESS is
 * preferred, MAPPED-ADDRESS of servers from before RFC 5389 is taken as well.
 * @return false if this is not a well-formed answer to the request with this
 *         transaction id, or if it holds no address
 */
bool StunParseBindingResponse(const uint8_t *data, size_t size, const StunTransactionId &id,
		std::string &address, uint16_t *port, std::string *error);

// ------------------------------------------------------------------ for a session's own socket
//
// StunQuery() asks from a socket of its own, which is right for "which address
// does this machine have". To be reached without a forwarded port, a session
// needs to know under which address and port its very socket is seen: a router
// makes one mapping per socket. StunProbe asks from that socket, while it is
// in use for everything else.

/** How a router hands out ports, told by asking two servers from one socket. */
enum class PortMapping
{
	Unknown,       // fewer than two servers have answered
	Stable,        // every destination sees the same port: others can be told where to send
	PerDestination // every destination sees another port ("symmetric NAT")
};

/**
 * Looks the servers' names up on a thread of its own, because a lookup can take
 * many seconds when the network is down. Gives one IPv4 address per server.
 */
class StunResolver
{
	public:
		void Start(const std::vector<StunServer> &servers);
		/** @return true once, when the lookup is finished (out may still be empty) */
		bool Take(std::vector<Address> &out);

	private:
		struct State;
		std::shared_ptr<State> state;
};

/**
 * Keeps asking up to two STUN servers from a socket that is busy with other
 * things. Not safe for use from several threads at once.
 */
class StunProbe
{
	public:
		/** How often a server that has answered is asked again; also keeps the router's mapping alive. */
		static constexpr uint32_t kRefreshMs = 15000;

		/** The servers to ask (see StunResolver); only the first two are used. */
		void SetServers(const std::vector<Address> &servers);
		bool HasServers() const { return count > 0; }
		/** Sends the requests that are due. Call it every few milliseconds. */
		void Tick(UdpSocket &socket, uint64_t now_ms);
		/**
		 * Looks at a datagram that arrived on the socket.
		 * @return true if it was the answer to one of our requests; it is used up then
		 */
		bool Handle(const uint8_t *data, size_t size, const Address &from, uint64_t now_ms);

		/** The socket's address and port on the internet; invalid until a server has answered. */
		Address Public() const;
		PortMapping Mapping() const;
		/** Whether every server has been asked several times and none has ever answered. */
		bool GaveUp() const;
		/** Counts up whenever Public() or Mapping() changes. */
		uint32_t Generation() const { return generation; }

	private:
		struct Server
		{
			Address address;
			StunTransactionId id{};
			bool waiting = false;
			int tries = 0;
			uint64_t next_ms = 0;
			Address mapped;
		};
		Server servers[2];
		int count = 0;
		uint32_t generation = 0;
};

} // namespace coplay

#endif // COPLAY_STUN_H
