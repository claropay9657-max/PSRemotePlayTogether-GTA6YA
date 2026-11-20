// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <coplay/socket.h>

#include <chrono>
#include <cstring>

#ifdef _WIN32
#include <mutex>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <unistd.h>
#include <cerrno>
#endif

#if defined(__APPLE__) && !defined(IPV6_PREFER_TEMPADDR)
// The system header only shows this option when __APPLE_USE_RFC_3542 is defined
#define IPV6_PREFER_TEMPADDR 63
#endif

namespace coplay {

#ifdef _WIN32
static const SOCKET kInvalidHandle = INVALID_SOCKET;
static void CloseHandle_(SOCKET s) { closesocket(s); }
static void EnsureSocketsInitialized()
{
	static std::once_flag once;
	std::call_once(once, [] {
		WSADATA data;
		WSAStartup(MAKEWORD(2, 2), &data);
	});
}
static std::string LastSocketError()
{
	return "socket error " + std::to_string(WSAGetLastError());
}
#else
static const int kInvalidHandle = -1;
static void CloseHandle_(int s) { close(s); }
static void EnsureSocketsInitialized() {}
static std::string LastSocketError()
{
	return std::strerror(errno);
}
#endif

uint64_t NowMs()
{
	return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count());
}

uint64_t NowUs()
{
	return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count());
}

/** How an IPv4 address starts when it is written as an IPv6 one (::ffff:a.b.c.d). */
static const uint8_t kMappedPrefix[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff };

uint16_t Address::Port() const
{
	if(storage.ss_family == AF_INET)
		return ntohs(reinterpret_cast<const sockaddr_in *>(&storage)->sin_port);
	if(storage.ss_family == AF_INET6)
		return ntohs(reinterpret_cast<const sockaddr_in6 *>(&storage)->sin6_port);
	return 0;
}

std::string Address::ToString() const
{
	char buf[INET6_ADDRSTRLEN] = { 0 };
	if(storage.ss_family == AF_INET)
	{
		auto *in = reinterpret_cast<const sockaddr_in *>(&storage);
		inet_ntop(AF_INET, const_cast<in_addr *>(&in->sin_addr), buf, sizeof(buf));
		return std::string(buf) + ":" + std::to_string(Port());
	}
	if(storage.ss_family == AF_INET6)
	{
		auto *in6 = reinterpret_cast<const sockaddr_in6 *>(&storage);
		// Show IPv4 peers of a dual-stack socket the way people write them
		const uint8_t *a = in6->sin6_addr.s6_addr;
		if(std::memcmp(a, kMappedPrefix, sizeof(kMappedPrefix)) == 0)
		{
			return std::to_string(a[12]) + "." + std::to_string(a[13]) + "." + std::to_string(a[14]) + "."
				+ std::to_string(a[15]) + ":" + std::to_string(Port());
		}
		inet_ntop(AF_INET6, const_cast<in6_addr *>(&in6->sin6_addr), buf, sizeof(buf));
		return "[" + std::string(buf) + "]:" + std::to_string(Port());
	}
	return "(invalid)";
}

bool Address::operator==(const Address &o) const
{
	if(storage.ss_family != o.storage.ss_family)
		return false;
	if(storage.ss_family == AF_INET)
	{
		auto *a = reinterpret_cast<const sockaddr_in *>(&storage);
		auto *b = reinterpret_cast<const sockaddr_in *>(&o.storage);
		return a->sin_port == b->sin_port && std::memcmp(&a->sin_addr, &b->sin_addr, sizeof(a->sin_addr)) == 0;
	}
	if(storage.ss_family == AF_INET6)
	{
		auto *a = reinterpret_cast<const sockaddr_in6 *>(&storage);
		auto *b = reinterpret_cast<const sockaddr_in6 *>(&o.storage);
		return a->sin6_port == b->sin6_port && a->sin6_scope_id == b->sin6_scope_id
			&& std::memcmp(&a->sin6_addr, &b->sin6_addr, sizeof(a->sin6_addr)) == 0;
	}
	return length == o.length;
}

size_t Address::Bytes(uint8_t out[16]) const
{
	if(!IsValid())
		return 0;
	if(storage.ss_family == AF_INET)
	{
		std::memcpy(out, &reinterpret_cast<const sockaddr_in *>(&storage)->sin_addr, 4);
		return 4;
	}
	if(storage.ss_family == AF_INET6)
	{
		const uint8_t *a = reinterpret_cast<const sockaddr_in6 *>(&storage)->sin6_addr.s6_addr;
		if(std::memcmp(a, kMappedPrefix, sizeof(kMappedPrefix)) == 0)
		{
			std::memcpy(out, a + 12, 4);
			return 4;
		}
		std::memcpy(out, a, 16);
		return 16;
	}
	return 0;
}

bool Address::SameEndpoint(const Address &o) const
{
	uint8_t a[16], b[16];
	size_t na = Bytes(a);
	size_t nb = o.Bytes(b);
	return na && na == nb && Port() == o.Port() && std::memcmp(a, b, na) == 0;
}

Address Address::FromBytes(const uint8_t *bytes, size_t size, uint16_t port)
{
	Address a;
	if(bytes && size == 4)
	{
		auto *in = reinterpret_cast<sockaddr_in *>(&a.storage);
		in->sin_family = AF_INET;
		in->sin_port = htons(port);
		std::memcpy(&in->sin_addr, bytes, 4);
		a.length = sizeof(sockaddr_in);
	}
	else if(bytes && size == 16)
	{
		auto *in6 = reinterpret_cast<sockaddr_in6 *>(&a.storage);
		in6->sin6_family = AF_INET6;
		in6->sin6_port = htons(port);
		std::memcpy(in6->sin6_addr.s6_addr, bytes, 16);
		a.length = sizeof(sockaddr_in6);
	}
	return a;
}

bool ParseAddress(const std::string &literal, uint16_t port, Address &out)
{
	EnsureSocketsInitialized();
	in_addr v4;
	if(inet_pton(AF_INET, literal.c_str(), &v4) == 1)
	{
		out = Address::FromBytes(reinterpret_cast<const uint8_t *>(&v4), 4, port);
		return true;
	}
	in6_addr v6;
	if(inet_pton(AF_INET6, literal.c_str(), &v6) == 1)
	{
		out = Address::FromBytes(v6.s6_addr, 16, port);
		return true;
	}
	return false;
}

bool Resolve(const std::string &host, uint16_t port, std::vector<Address> &out, std::string *error)
{
	EnsureSocketsInitialized();
	out.clear();
	addrinfo hints;
	std::memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	addrinfo *result = nullptr;
	std::string service = std::to_string(port);
	int r = getaddrinfo(host.c_str(), service.c_str(), &hints, &result);
	if(r != 0 || !result)
	{
		if(error)
			*error = "Cannot resolve \"" + host + "\"";
		return false;
	}
	std::vector<Address> v4;
	for(addrinfo *ai = result; ai; ai = ai->ai_next)
	{
		if(ai->ai_family != AF_INET && ai->ai_family != AF_INET6)
			continue;
		if(static_cast<size_t>(ai->ai_addrlen) > sizeof(sockaddr_storage))
			continue;
		Address a;
		std::memcpy(&a.storage, ai->ai_addr, static_cast<size_t>(ai->ai_addrlen));
		a.length = static_cast<socklen_t>(ai->ai_addrlen);
		if(ai->ai_family == AF_INET6)
			out.push_back(a);
		else
			v4.push_back(a);
	}
	freeaddrinfo(result);
	out.insert(out.end(), v4.begin(), v4.end());
	if(out.empty())
	{
		if(error)
			*error = "No usable address for \"" + host + "\"";
		return false;
	}
	return true;
}

std::string GuessLocalAddress()
{
	EnsureSocketsInitialized();
	// "Connecting" a UDP socket only asks the system which interface it would route
	// through; no packet is sent. 192.0.2.1 is a documentation address (RFC 5737).
	auto s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if(s == kInvalidHandle)
		return std::string();
	sockaddr_in remote;
	std::memset(&remote, 0, sizeof(remote));
	remote.sin_family = AF_INET;
	remote.sin_port = htons(9);
	inet_pton(AF_INET, "192.0.2.1", &remote.sin_addr);
	std::string result;
	if(connect(s, reinterpret_cast<const sockaddr *>(&remote), sizeof(remote)) == 0)
	{
		sockaddr_in local;
		socklen_t len = sizeof(local);
		if(getsockname(s, reinterpret_cast<sockaddr *>(&local), &len) == 0 && local.sin_addr.s_addr != 0)
		{
			char buf[INET_ADDRSTRLEN] = { 0 };
			if(inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf)))
				result = buf;
		}
	}
	CloseHandle_(s);
	return result;
}

std::string AddressBytesToString(const uint8_t *bytes, size_t size)
{
	if(bytes && size == 4)
	{
		return std::to_string(bytes[0]) + "." + std::to_string(bytes[1]) + "." + std::to_string(bytes[2]) + "."
			+ std::to_string(bytes[3]);
	}
	if(bytes && size == 16)
	{
		EnsureSocketsInitialized();
		in6_addr addr;
		std::memcpy(&addr, bytes, sizeof(addr));
		char buf[INET6_ADDRSTRLEN] = { 0 };
		if(inet_ntop(AF_INET6, &addr, buf, sizeof(buf)))
			return buf;
	}
	return std::string();
}

static bool IsGlobalIPv6(const uint8_t *a)
{
	// Everything handed out for the internet so far lies in 2000::/3. That leaves
	// out loopback, link-local (fe80::/10), unique-local (fc00::/7), multicast and
	// IPv4 written as IPv6.
	if((a[0] & 0xe0) != 0x20)
		return false;
	// 2001:db8::/32 is for documentation
	if(a[0] == 0x20 && a[1] == 0x01 && a[2] == 0x0d && a[3] == 0xb8)
		return false;
	// Teredo (2001::/32) and 6to4 (2002::/16) are tunnels through IPv4 that others
	// cannot count on reaching
	if(a[0] == 0x20 && a[1] == 0x01 && a[2] == 0x00 && a[3] == 0x00)
		return false;
	if(a[0] == 0x20 && a[1] == 0x02)
		return false;
	return true;
}

bool IsGlobalIPv6Address(const std::string &address)
{
	EnsureSocketsInitialized();
	in6_addr addr;
	if(inet_pton(AF_INET6, address.c_str(), &addr) != 1)
		return false;
	return IsGlobalIPv6(addr.s6_addr);
}

/**
 * A machine often has two global IPv6 addresses: a lasting one and a temporary one
 * that it prefers for outgoing connections and replaces every day. An invite should
 * carry the lasting one, which is also the one a router's firewall rule is made for,
 * and a session's socket has to send from the address the invite (or the note at the
 * meeting point) names, or the other side's firewall does not recognise the answer.
 * Not every system lets us choose; then it is the temporary one, in both places.
 */
template <typename H>
static void PreferLastingIPv6Address(H s)
{
#if defined(IPV6_ADDR_PREFERENCES) && defined(IPV6_PREFER_SRC_PUBLIC)
	int prefer = IPV6_PREFER_SRC_PUBLIC;
	setsockopt(s, IPPROTO_IPV6, IPV6_ADDR_PREFERENCES, reinterpret_cast<const char *>(&prefer), sizeof(prefer));
#elif defined(IPV6_PREFER_TEMPADDR)
	int prefer = 0;
	setsockopt(s, IPPROTO_IPV6, IPV6_PREFER_TEMPADDR, reinterpret_cast<const char *>(&prefer), sizeof(prefer));
#else
	(void)s;
#endif
}

std::string GuessGlobalIPv6Address()
{
	EnsureSocketsInitialized();
	// The same question as in GuessLocalAddress(), asked for a documentation
	// address of IPv6 (RFC 3849). No packet is sent here either.
	auto s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	if(s == kInvalidHandle)
		return std::string();
	PreferLastingIPv6Address(s);
	sockaddr_in6 remote;
	std::memset(&remote, 0, sizeof(remote));
	remote.sin6_family = AF_INET6;
	remote.sin6_port = htons(9);
	inet_pton(AF_INET6, "2001:db8::1", &remote.sin6_addr);
	std::string result;
	if(connect(s, reinterpret_cast<const sockaddr *>(&remote), sizeof(remote)) == 0)
	{
		sockaddr_in6 local;
		socklen_t len = sizeof(local);
		if(getsockname(s, reinterpret_cast<sockaddr *>(&local), &len) == 0 && IsGlobalIPv6(local.sin6_addr.s6_addr))
			result = AddressBytesToString(local.sin6_addr.s6_addr, 16);
	}
	CloseHandle_(s);
	return result;
}

UdpSocket::UdpSocket() : handle(kInvalidHandle)
{
	EnsureSocketsInitialized();
}

UdpSocket::~UdpSocket()
{
	Close();
}

void UdpSocket::Configure()
{
	// Video frames arrive as bursts of datagrams; the default buffers are too small for that
	int size = 4 * 1024 * 1024;
	setsockopt(handle, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char *>(&size), sizeof(size));
	setsockopt(handle, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char *>(&size), sizeof(size));
#ifdef _WIN32
	u_long nonblocking = 1;
	ioctlsocket(handle, FIONBIO, &nonblocking);
	// Without this a single ICMP "port unreachable" makes every later recvfrom fail with WSAECONNRESET
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
	BOOL new_behavior = FALSE;
	DWORD bytes_returned = 0;
	WSAIoctl(handle, SIO_UDP_CONNRESET, &new_behavior, sizeof(new_behavior), nullptr, 0, &bytes_returned, nullptr, nullptr);
#else
	int flags = fcntl(handle, F_GETFL, 0);
	if(flags >= 0)
		fcntl(handle, F_SETFL, flags | O_NONBLOCK);
#endif
}

bool UdpSocket::Bind(uint16_t port, std::string *error)
{
	Close();

	handle = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	if(handle != kInvalidHandle)
	{
		int off = 0;
		bool dual = setsockopt(handle, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char *>(&off), sizeof(off)) == 0;
		sockaddr_in6 addr;
		std::memset(&addr, 0, sizeof(addr));
		addr.sin6_family = AF_INET6;
		addr.sin6_addr = in6addr_any;
		addr.sin6_port = htons(port);
		if(dual && bind(handle, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) == 0)
		{
			PreferLastingIPv6Address(handle);
			kind = Kind::Both;
			Configure();
			return true;
		}
		CloseHandle_(handle);
		handle = kInvalidHandle;
	}

	handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if(handle == kInvalidHandle)
	{
		if(error)
			*error = "Cannot create UDP socket: " + LastSocketError();
		return false;
	}
	sockaddr_in addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port);
	if(bind(handle, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) != 0)
	{
		if(error)
			*error = "Cannot bind UDP port " + std::to_string(port) + ": " + LastSocketError();
		CloseHandle_(handle);
		handle = kInvalidHandle;
		return false;
	}
	kind = Kind::IPv4;
	Configure();
	return true;
}

bool UdpSocket::OpenFor(const Address &peer, std::string *error)
{
	Close();
	handle = socket(peer.storage.ss_family, SOCK_DGRAM, IPPROTO_UDP);
	if(handle == kInvalidHandle)
	{
		if(error)
			*error = "Cannot create UDP socket: " + LastSocketError();
		return false;
	}
	kind = peer.storage.ss_family == AF_INET6 ? Kind::IPv6 : Kind::IPv4;
	Configure();
	return true;
}

void UdpSocket::Close()
{
	if(handle != kInvalidHandle)
	{
		CloseHandle_(handle);
		handle = kInvalidHandle;
	}
	kind = Kind::Closed;
}

bool UdpSocket::IsOpen() const
{
	return handle != kInvalidHandle;
}

uint16_t UdpSocket::LocalPort() const
{
	if(handle == kInvalidHandle)
		return 0;
	Address a;
	socklen_t len = sizeof(a.storage);
	if(getsockname(handle, reinterpret_cast<sockaddr *>(&a.storage), &len) != 0)
		return 0;
	a.length = len;
	return a.Port();
}

void UdpSocket::SetSimulatedLoss(double probability, uint32_t seed)
{
	if(probability <= 0.0)
		loss_threshold = 0;
	else if(probability >= 1.0)
		loss_threshold = 0xffffffffu;
	else
		loss_threshold = static_cast<uint32_t>(probability * 4294967295.0);
	loss_state = seed ? seed : 1;
}

void UdpSocket::SetSimulatedFilter(Filter f)
{
	std::lock_guard<std::mutex> lock(filter_mutex);
	filter_open.clear();
	filter = static_cast<int>(f);
}

std::string UdpSocket::FilterKey(const Address &a, Filter f) const
{
	uint8_t bytes[16];
	size_t n = a.Bytes(bytes);
	std::string key(reinterpret_cast<const char *>(bytes), n);
	if(f == Filter::AddressAndPort)
		key += ":" + std::to_string(a.Port());
	return key;
}

bool UdpSocket::SendTo(const uint8_t *data, size_t size, const Address &to_in)
{
	if(handle == kInvalidHandle || !to_in.IsValid())
		return false;

	// A socket for both kinds of address takes IPv4 ones written as IPv6, and a plain
	// IPv4 socket takes them only as IPv4: callers need not care which one they hold
	Address converted;
	const Address *target = &to_in;
	if(kind == Kind::Both && to_in.storage.ss_family == AF_INET)
	{
		uint8_t mapped[16];
		std::memcpy(mapped, kMappedPrefix, sizeof(kMappedPrefix));
		to_in.Bytes(mapped + 12);
		converted = Address::FromBytes(mapped, 16, to_in.Port());
		target = &converted;
	}
	else if(kind == Kind::IPv4 && to_in.storage.ss_family == AF_INET6)
	{
		uint8_t bytes[16];
		if(to_in.Bytes(bytes) != 4)
			return false; // no way to IPv6 from here
		converted = Address::FromBytes(bytes, 4, to_in.Port());
		target = &converted;
	}
	const Address &to = *target;

	Filter f = static_cast<Filter>(filter.load(std::memory_order_relaxed));
	if(f != Filter::None)
	{
		// What leaves opens the way back in, whether or not it arrives
		std::lock_guard<std::mutex> lock(filter_mutex);
		filter_open.insert(FilterKey(to, f));
	}

	uint32_t threshold = loss_threshold.load(std::memory_order_relaxed);
	if(threshold)
	{
		// xorshift32; races between threads only make the sequence less regular
		uint32_t x = loss_state.load(std::memory_order_relaxed);
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		loss_state.store(x, std::memory_order_relaxed);
		if(x < threshold)
			return true; // pretend it was sent
	}
	for(int attempt = 0; attempt < 3; attempt++)
	{
#ifdef _WIN32
		int r = sendto(handle, reinterpret_cast<const char *>(data), static_cast<int>(size), 0,
				reinterpret_cast<const sockaddr *>(&to.storage), to.length);
		if(r != SOCKET_ERROR)
			return true;
		if(WSAGetLastError() != WSAEWOULDBLOCK)
			return false;
#else
		ssize_t r = sendto(handle, data, size, 0, reinterpret_cast<const sockaddr *>(&to.storage), to.length);
		if(r >= 0)
			return true;
		if(errno != EAGAIN && errno != EWOULDBLOCK && errno != ENOBUFS && errno != EINTR)
			return false;
#endif
		// Send buffer is full: wait briefly for it to drain rather than dropping straight away
		fd_set wfds;
		FD_ZERO(&wfds);
		FD_SET(handle, &wfds);
		timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = 2000;
		select(static_cast<int>(handle) + 1, nullptr, &wfds, nullptr, &tv);
	}
	return false;
}

int UdpSocket::RecvFrom(uint8_t *buf, size_t capacity, Address &from, int timeout_ms)
{
	if(handle == kInvalidHandle)
		return -1;
	fd_set rfds;
	FD_ZERO(&rfds);
	FD_SET(handle, &rfds);
	timeval tv;
	tv.tv_sec = timeout_ms / 1000;
	tv.tv_usec = (timeout_ms % 1000) * 1000;
	int sel = select(static_cast<int>(handle) + 1, &rfds, nullptr, nullptr, &tv);
	if(sel == 0)
		return 0;
	if(sel < 0)
	{
#ifndef _WIN32
		if(errno == EINTR)
			return 0;
#endif
		return -1;
	}
	socklen_t len = sizeof(from.storage);
#ifdef _WIN32
	int r = recvfrom(handle, reinterpret_cast<char *>(buf), static_cast<int>(capacity), 0,
			reinterpret_cast<sockaddr *>(&from.storage), &len);
	if(r == SOCKET_ERROR)
	{
		int e = WSAGetLastError();
		// WSAEMSGSIZE: datagram larger than any valid packet, drop it
		return (e == WSAEWOULDBLOCK || e == WSAECONNRESET || e == WSAEMSGSIZE) ? 0 : -1;
	}
#else
	ssize_t r = recvfrom(handle, buf, capacity, 0, reinterpret_cast<sockaddr *>(&from.storage), &len);
	if(r < 0)
		return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR || errno == ECONNREFUSED) ? 0 : -1;
#endif
	from.length = len;
	Filter f = static_cast<Filter>(filter.load(std::memory_order_relaxed));
	if(f != Filter::None)
	{
		std::lock_guard<std::mutex> lock(filter_mutex);
		if(!filter_open.count(FilterKey(from, f)))
			return 0; // the simulated router has never seen us send there
	}
	return static_cast<int>(r);
}

} // namespace coplay
