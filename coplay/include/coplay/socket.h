// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef COPLAY_SOCKET_H
#define COPLAY_SOCKET_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <set>
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

	/**
	 * The address without the port: 4 bytes for IPv4, also when it is written as IPv6
	 * ("::ffff:1.2.3.4", the way a socket for both kinds reports IPv4 peers), 16 for IPv6.
	 * @return the number of bytes, 0 if this is no address
	 */
	size_t Bytes(uint8_t out[16]) const;
	/** The same machine and port, however the address is written. */
	bool SameEndpoint(const Address &o) const;
	/** From 4 or 16 bytes; anything else gives an invalid address. */
	static Address FromBytes(const uint8_t *bytes, size_t size, uint16_t port);
};

/** Reads "192.0.2.1" or "2001:db8::1" (no names: nothing is looked up). */
bool ParseAddress(const std::string &literal, uint16_t port, Address &out);

/** Resolves a host name or literal address. IPv6 results come first. */
bool Resolve(const std::string &host, uint16_t port, std::vector<Address> &out, std::string *error);

class UdpSocket
{
	private:
		enum class Kind { Closed, IPv4, IPv6, Both };

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
		/** Whether IPv6 addresses can be sent to from this socket. */
		bool HasIPv6() const { return kind == Kind::Both || kind == Kind::IPv6; }

		/** Safe to call from several threads at once. */
		bool SendTo(const uint8_t *data, size_t size, const Address &to);
		/**
		 * @return number of bytes received, 0 on timeout, -1 on error
		 */
		int RecvFrom(uint8_t *buf, size_t capacity, Address &from, int timeout_ms);

		/** Randomly drops outgoing datagrams. For tests and the simulator. */
		void SetSimulatedLoss(double probability, uint32_t seed = 1);

		/**
		 * Behaves like a socket behind a router without a forwarded port: what arrives
		 * is dropped unless this socket has sent to where it comes from. For tests and
		 * the simulator.
		 */
		enum class Filter
		{
			None,
			/** Lets in whatever comes from a machine it has sent to, from any port. */
			Address,
			/** Lets in only what comes from the very address and port it has sent to. */
			AddressAndPort
		};
		void SetSimulatedFilter(Filter filter);

	private:
#ifdef _WIN32
		using Handle = SOCKET;
#else
		using Handle = int;
#endif
		void Configure();
		std::string FilterKey(const Address &a, Filter f) const;

		Handle handle;
		Kind kind = Kind::Closed;
		std::atomic<uint32_t> loss_threshold{0}; // of 2^32
		std::atomic<uint32_t> loss_state{1};
		std::atomic<int> filter{0};
		std::mutex filter_mutex;
		std::set<std::string> filter_open; // with filter_mutex
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

/**
 * Milliseconds from `then_ms` to `now_ms`, and 0 if `then_ms` is the later of the two.
 *
 * Time stamps are taken by different threads: one that the application just wrote can be
 * a moment newer than the "now" a network thread read at the top of its loop. Subtracting
 * the unsigned values directly turns that moment into half a billion years, and whatever
 * was waiting for a timeout is given up on the spot.
 */
inline uint64_t ElapsedMs(uint64_t now_ms, uint64_t then_ms)
{
	return now_ms > then_ms ? now_ms - then_ms : 0;
}
/** Microseconds on a monotonic clock. */
uint64_t NowUs();

} // namespace coplay

#endif // COPLAY_SOCKET_H
