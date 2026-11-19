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
		static const uint8_t mapped_prefix[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff };
		if(std::memcmp(a, mapped_prefix, sizeof(mapped_prefix)) == 0)
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

std::string GuessGlobalIPv6Address()
{
	EnsureSocketsInitialized();
	// The same question as in GuessLocalAddress(), asked for a documentation
	// address of IPv6 (RFC 3849). No packet is sent here either.
	auto s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	if(s == kInvalidHandle)
		return std::string();
	// A machine often has two global addresses: a lasting one and a temporary one
	// that it prefers for outgoing connections and replaces every day. An invite
	// should carry the lasting one, which is also the one a router's firewall rule
	// is made for. Not every system lets us choose; then it is the temporary one.
#if defined(IPV6_ADDR_PREFERENCES) && defined(IPV6_PREFER_SRC_PUBLIC)
	{
		int prefer = IPV6_PREFER_SRC_PUBLIC;
		setsockopt(s, IPPROTO_IPV6, IPV6_ADDR_PREFERENCES, reinterpret_cast<const char *>(&prefer), sizeof(prefer));
	}
#elif defined(IPV6_PREFER_TEMPADDR)
	{
		int prefer = 0;
		setsockopt(s, IPPROTO_IPV6, IPV6_PREFER_TEMPADDR, reinterpret_cast<const char *>(&prefer), sizeof(prefer));
	}
#endif
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

bool UdpSocket::SendTo(const uint8_t *data, size_t size, const Address &to)
{
	if(handle == kInvalidHandle || !to.IsValid())
		return false;
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
	return static_cast<int>(r);
}

} // namespace coplay
