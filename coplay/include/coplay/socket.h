// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef COPLAY_SOCKET_H
#define COPLAY_SOCKET_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif

namespace coplay {

struct Address
{
	sockaddr_storage storage{};
	socklen_t length = 0;

	bool IsValid() const { return length != 0; }
	bool IsIPv6() const { return storage.ss_family == AF_INET6; }
	uint16_t Port() const;
	/** "1.2.3.4:9410" or "[::1]:9410" */
	std::string ToString() const;
	bool operator==(const Address &o) const;
	bool operator!=(const Address &o) const { return !(*this == o); }
};

/** Resolves a host name or literal address. IPv6 results come first. */
bool Resolve(const std::string &host, uint16_t port, std::vector<Address> &out, std::string *error);

class UdpSocket
{
	public:
		UdpSocket();
		~UdpSocket();
		UdpSocket(const UdpSocket &) = delete;
		UdpSocket &operator=(const UdpSocket &) = delete;

		/**
		 * Binds to all interfaces. Prefers a dual-stack socket that accepts
		 * IPv4 and IPv6, and falls back to IPv4 only.
		 * @param port 0 picks a free port
		 */
		bool Bind(uint16_t port, std::string *error);
		/** Opens an unbound socket of the same family as peer. */
		bool OpenFor(const Address &peer, std::string *error);
		void Close();
		bool IsOpen() const;
		uint16_t LocalPort() const;

		/** Safe to call from several threads at once. */
		bool SendTo(const uint8_t *data, size_t size, const Address &to);
		/**
		 * @return number of bytes received, 0 on timeout, -1 on error
		 */
		int RecvFrom(uint8_t *buf, size_t capacity, Address &from, int timeout_ms);

		/** Randomly drops outgoing datagrams. For tests and the simulator. */
		void SetSimulatedLoss(double probability, uint32_t seed = 1);

	private:
#ifdef _WIN32
		using Handle = SOCKET;
#else
		using Handle = int;
#endif
		void Configure();

		Handle handle;
		std::atomic<uint32_t> loss_threshold{0}; // of 2^32
		std::atomic<uint32_t> loss_state{1};
};

/**
 * Best guess for this machine's address on the local network, for example
 * "192.168.1.20". Empty if there is none. Does not send anything.
 */
std::string GuessLocalAddress();

/**
 * This machine's IPv6 address that the whole internet can reach it under, for
 * example "2001:db8:12:34::5". Empty if it has none; addresses that only work
 * on the own link or the own site do not count. Does not send anything.
 */
std::string GuessGlobalIPv6Address();

/** Whether an IPv6 address in text form is one the whole internet can reach. */
bool IsGlobalIPv6Address(const std::string &address);

/** "192.0.2.1" from 4 bytes, "2001:db8::1" from 16; empty for any other size. */
std::string AddressBytesToString(const uint8_t *bytes, size_t size);

/** Milliseconds on a monotonic clock. */
uint64_t NowMs();
/** Microseconds on a monotonic clock. */
uint64_t NowUs();

} // namespace coplay

#endif // COPLAY_SOCKET_H
