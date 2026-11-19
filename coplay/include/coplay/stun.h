// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef COPLAY_STUN_H
#define COPLAY_STUN_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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

} // namespace coplay

#endif // COPLAY_STUN_H
