// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <coplay/stun.h>

#include <coplay/bytes.h>
#include <coplay/crypto.h>
#include <coplay/socket.h>

#include <cstdlib>
#include <cstring>

namespace coplay {

static const uint32_t kStunMagicCookie = 0x2112a442;
static const size_t kStunHeaderSize = 20;

static const uint16_t kBindingRequest = 0x0001;
static const uint16_t kBindingSuccess = 0x0101;
static const uint16_t kBindingError = 0x0111;

static const uint16_t kAttrMappedAddress = 0x0001;
static const uint16_t kAttrErrorCode = 0x0009;
static const uint16_t kAttrXorMappedAddress = 0x0020;
// The number XOR-MAPPED-ADDRESS had in the drafts; some servers still send it
static const uint16_t kAttrXorMappedAddressDraft = 0x8020;

static const uint8_t kFamilyIPv4 = 0x01;
static const uint8_t kFamilyIPv6 = 0x02;

std::vector<uint8_t> StunBindingRequest(const StunTransactionId &id)
{
	std::vector<uint8_t> out;
	out.reserve(kStunHeaderSize);
	ByteWriter w(out);
	w.U16(kBindingRequest);
	w.U16(0); // no attributes
	w.U32(kStunMagicCookie);
	w.Bytes(id.data(), id.size());
	return out;
}

enum class Answer
{
	Address,  // what we asked for
	Refused,  // the server answered our request, but not with an address
	NotOurs   // something else that arrived on the socket
};

/** One MAPPED-ADDRESS or XOR-MAPPED-ADDRESS value. */
static bool ReadAddress(const uint8_t *value, size_t size, bool xored, const StunTransactionId &id,
		std::string &address, uint16_t &port)
{
	if(size < 4)
		return false;
	uint8_t family = value[1];
	size_t address_size = family == kFamilyIPv4 ? 4 : family == kFamilyIPv6 ? 16 : 0;
	if(!address_size || size < 4 + address_size)
		return false;

	// What the values are combined with: the magic cookie, and for the longer
	// IPv6 address the transaction id after it
	uint8_t pad[16];
	pad[0] = static_cast<uint8_t>(kStunMagicCookie >> 24);
	pad[1] = static_cast<uint8_t>(kStunMagicCookie >> 16);
	pad[2] = static_cast<uint8_t>(kStunMagicCookie >> 8);
	pad[3] = static_cast<uint8_t>(kStunMagicCookie);
	std::memcpy(pad + 4, id.data(), id.size());

	uint8_t raw[16];
	std::memcpy(raw, value + 4, address_size);
	uint16_t p = static_cast<uint16_t>((value[2] << 8) | value[3]);
	if(xored)
	{
		p = static_cast<uint16_t>(p ^ (kStunMagicCookie >> 16));
		for(size_t i = 0; i < address_size; i++)
			raw[i] ^= pad[i];
	}

	bool any = false;
	for(size_t i = 0; i < address_size; i++)
		any = any || raw[i] != 0;
	if(!any)
		return false; // 0.0.0.0 or :: is no address to reach anyone under
	std::string text = AddressBytesToString(raw, address_size);
	if(text.empty())
		return false;
	address = text;
	port = p;
	return true;
}

static Answer ParseResponse(const uint8_t *data, size_t size, const StunTransactionId &id,
		std::string &address, uint16_t *port, std::string &problem)
{
	if(!data || size < kStunHeaderSize)
	{
		problem = "The answer is too short to be a STUN message";
		return Answer::NotOurs;
	}
	ByteReader r(data, size);
	uint16_t type = r.U16();
	uint16_t length = r.U16();
	uint32_t cookie = r.U32();
	StunTransactionId answered{};
	r.Bytes(answered.data(), answered.size());
	// The two highest bits of a STUN message are zero, its length counts whole
	// 32 bit words after the header, and over UDP it fills the datagram
	if((type & 0xc000) != 0 || cookie != kStunMagicCookie || (length & 3) != 0 || kStunHeaderSize + length != size)
	{
		problem = "The answer is not a STUN message";
		return Answer::NotOurs;
	}
	if(answered != id)
	{
		problem = "The answer belongs to a different request";
		return Answer::NotOurs;
	}
	if(type != kBindingSuccess && type != kBindingError)
	{
		problem = "The answer is not an answer to an address request";
		return Answer::NotOurs;
	}

	bool have_xored = false;
	bool have_plain = false;
	std::string xored_address, plain_address;
	uint16_t xored_port = 0, plain_port = 0;
	int error_code = 0;
	while(r.remaining() >= 4)
	{
		uint16_t attr = r.U16();
		size_t attr_size = r.U16();
		size_t padded = (attr_size + 3) & ~static_cast<size_t>(3);
		if(padded > r.remaining())
		{
			problem = "The answer is cut off";
			return Answer::NotOurs;
		}
		const uint8_t *value = r.cursor();
		r.Skip(padded);
		// Of each kind, the first one that can be read counts
		if((attr == kAttrXorMappedAddress || attr == kAttrXorMappedAddressDraft) && !have_xored)
			have_xored = ReadAddress(value, attr_size, true, id, xored_address, xored_port);
		else if(attr == kAttrMappedAddress && !have_plain)
			have_plain = ReadAddress(value, attr_size, false, id, plain_address, plain_port);
		else if(attr == kAttrErrorCode && attr_size >= 4 && !error_code)
			error_code = (value[2] & 0x07) * 100 + value[3];
	}

	if(type == kBindingError)
	{
		problem = error_code ? "The server refused with error " + std::to_string(error_code) : "The server refused";
		return Answer::Refused;
	}
	if(!have_xored && !have_plain)
	{
		problem = "The server's answer holds no address";
		return Answer::Refused;
	}
	// Routers that rewrite addresses inside packets cannot touch the scrambled one
	address = have_xored ? xored_address : plain_address;
	if(port)
		*port = have_xored ? xored_port : plain_port;
	return Answer::Address;
}

bool StunParseBindingResponse(const uint8_t *data, size_t size, const StunTransactionId &id,
		std::string &address, uint16_t *port, std::string *error)
{
	std::string problem;
	if(ParseResponse(data, size, id, address, port, problem) == Answer::Address)
		return true;
	if(error)
		*error = problem;
	return false;
}

std::vector<StunServer> DefaultStunServers()
{
	// Two big, free services run by different companies, so that one of them
	// being down or blocked does not matter
	return { { "stun.l.google.com", 19302 }, { "stun.cloudflare.com", 3478 } };
}

bool ParseStunServer(const std::string &text, StunServer &server)
{
	size_t a = 0;
	size_t b = text.size();
	auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
	while(a < b && is_space(text[a]))
		a++;
	while(b > a && is_space(text[b - 1]))
		b--;
	std::string s = text.substr(a, b - a);

	std::string h;
	std::string port_text;
	if(!s.empty() && s.front() == '[')
	{
		size_t close = s.find(']');
		if(close == std::string::npos)
			return false;
		h = s.substr(1, close - 1);
		if(close + 1 < s.size())
		{
			if(s[close + 1] != ':')
				return false;
			port_text = s.substr(close + 2);
			if(port_text.empty())
				return false;
		}
	}
	else
	{
		size_t colon = s.find(':');
		// More than one colon without brackets: an IPv6 address on its own
		if(colon != std::string::npos && s.find(':', colon + 1) == std::string::npos)
		{
			h = s.substr(0, colon);
			port_text = s.substr(colon + 1);
			if(port_text.empty())
				return false;
		}
		else
			h = s;
	}
	if(h.empty() || h.find_first_of(" \t\r\n/") != std::string::npos)
		return false;

	uint16_t p = kStunDefaultPort;
	if(!port_text.empty())
	{
		if(port_text.size() > 5 || port_text.find_first_not_of("0123456789") != std::string::npos)
			return false;
		long v = std::strtol(port_text.c_str(), nullptr, 10);
		if(v < 1 || v > 65535)
			return false;
		p = static_cast<uint16_t>(v);
	}
	server.host = h;
	server.port = p;
	return true;
}

/** Asks one address of a server. */
static bool QueryAddress(const Address &to, uint32_t timeout_ms, std::string &public_address,
		uint16_t *public_port, std::string &problem)
{
	UdpSocket socket;
	if(!socket.OpenFor(to, &problem))
		return false;
	StunTransactionId id{};
	if(!RandomBytes(id.data(), id.size()))
	{
		problem = "No random numbers available";
		return false;
	}
	const std::vector<uint8_t> request = StunBindingRequest(id);

	// UDP may lose the request or the answer, so it is repeated. Every wait is
	// twice as long as the one before, as in RFC 5389 (section 7.2.1), only with
	// fewer rounds: 1/7 + 2/7 + 4/7 of the time allowed.
	const int kTransmissions = 3;
	if(timeout_ms < 7)
		timeout_ms = 7;
	const uint64_t end = NowMs() + timeout_ms;
	uint64_t wait = timeout_ms / 7;
	std::string last_problem;
	for(int i = 0; i < kTransmissions; i++, wait *= 2)
	{
		if(!socket.SendTo(request.data(), request.size(), to))
		{
			problem = "Cannot send to " + to.ToString();
			return false;
		}
		uint64_t until = i == kTransmissions - 1 ? end : NowMs() + wait;
		// When the time is up, what has arrived by then is still looked at: on a busy
		// machine this thread may wake up long after the answer came in. A limit keeps
		// somebody who floods the socket from holding us here.
		const int kLateLooks = 16;
		int late = 0;
		for(;;)
		{
			uint64_t now = NowMs();
			bool over = now >= until;
			if(over && late++ >= kLateLooks)
				break;
			uint8_t buf[1500];
			Address from;
			int r = socket.RecvFrom(buf, sizeof(buf), from, over ? 0 : static_cast<int>(until - now));
			if(r < 0)
			{
				problem = "Cannot receive from " + to.ToString();
				return false;
			}
			if(r == 0)
			{
				if(over)
					break;
				continue;
			}
			// Anybody can send to this socket; only the server asked is listened to
			if(from != to)
				continue;
			switch(ParseResponse(buf, static_cast<size_t>(r), id, public_address, public_port, last_problem))
			{
				case Answer::Address:
					return true;
				case Answer::Refused:
					problem = last_problem;
					return false;
				case Answer::NotOurs:
					break; // keep waiting for the real one
			}
		}
	}
	problem = "No answer from " + to.ToString();
	if(!last_problem.empty())
		problem += " (" + last_problem + ")";
	return false;
}

bool StunQuery(const std::string &server, uint16_t port, uint32_t timeout_ms,
		std::string &public_address, uint16_t *public_port, std::string *error, StunFamily family)
{
	std::vector<Address> resolved;
	if(!Resolve(server, port, resolved, error))
		return false;

	// One address of each kind is enough. IPv4 first: that is the one a router
	// hides, while a machine knows its own IPv6 address.
	std::vector<Address> candidates;
	if(family != StunFamily::IPv6)
	{
		for(const Address &a : resolved)
		{
			if(!a.IsIPv6())
			{
				candidates.push_back(a);
				break;
			}
		}
	}
	if(family != StunFamily::IPv4)
	{
		for(const Address &a : resolved)
		{
			if(a.IsIPv6())
			{
				candidates.push_back(a);
				break;
			}
		}
	}
	if(candidates.empty())
	{
		if(error)
			*error = std::string("No ") + (family == StunFamily::IPv4 ? "IPv4" : "IPv6") + " address found for \"" + server + "\"";
		return false;
	}

	std::string problem;
	for(const Address &to : candidates)
	{
		if(QueryAddress(to, timeout_ms, public_address, public_port, problem))
			return true;
	}
	if(error)
		*error = problem;
	return false;
}

bool StunQueryFirst(const std::vector<StunServer> &servers, uint32_t timeout_ms,
		std::string &public_address, uint16_t *public_port, std::string *error, StunFamily family)
{
	for(const StunServer &server : servers)
	{
		if(StunQuery(server.host, server.port, timeout_ms, public_address, public_port, error, family))
			return true;
	}
	if(servers.empty() && error)
		*error = "No STUN server to ask";
	return false;
}

} // namespace coplay
