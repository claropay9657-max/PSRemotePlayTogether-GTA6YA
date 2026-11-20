// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <coplay/invite.h>

#include <coplay/meet.h>

#include <cstdlib>
#include <cstring>

namespace coplay {

static const char kScheme[] = "psrpt://";

Secret GenerateSecret()
{
	Secret s{};
	RandomBytes(s.data(), s.size());
	return s;
}

std::string SecretToString(const Secret &secret)
{
	return Base64UrlEncode(secret.data(), secret.size());
}

bool SecretFromString(const std::string &text, Secret &secret)
{
	std::vector<uint8_t> raw;
	if(!Base64UrlDecode(text, raw) || raw.size() != kSecretSize)
		return false;
	std::memcpy(secret.data(), raw.data(), kSecretSize);
	return true;
}

std::string EncodeInvite(const Invite &invite)
{
	std::string text = kScheme;
	if(invite.host.empty())
		text += "-";
	else
	{
		std::string host = invite.host;
		if(host.find(':') != std::string::npos && host.front() != '[')
			host = "[" + host + "]";
		text += host + ":" + std::to_string(invite.port);
	}
	text += "/" + SecretToString(invite.secret);
	if(!invite.meet.empty())
		text += "?meet=" + invite.meet;
	return text;
}

static std::string Trim(const std::string &s)
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

bool DecodeInvite(const std::string &text, Invite &invite, std::string *error)
{
	auto fail = [error](const char *msg) {
		if(error)
			*error = msg;
		return false;
	};

	std::string s = Trim(text);
	const size_t scheme_len = std::strlen(kScheme);
	if(s.size() < scheme_len || s.compare(0, scheme_len, kScheme) != 0)
		return fail("An invite starts with psrpt://");
	s = s.substr(scheme_len);

	// What follows a "?" are additions, "name=value" joined by "&". Names that this
	// version does not know are skipped, so that a later one can add more.
	std::string meet;
	size_t question = s.find('?');
	if(question != std::string::npos)
	{
		std::string extras = s.substr(question + 1);
		s = s.substr(0, question);
		size_t pos = 0;
		while(pos <= extras.size())
		{
			size_t end = extras.find('&', pos);
			if(end == std::string::npos)
				end = extras.size();
			std::string pair = extras.substr(pos, end - pos);
			pos = end + 1;
			if(pair.compare(0, 5, "meet=") == 0)
			{
				meet = pair.substr(5);
				if(!IsInviteMeetAddress(meet))
					return fail("The meeting point in the invite is malformed");
			}
		}
	}

	size_t slash = s.rfind('/');
	if(slash == std::string::npos)
		return fail("The invite is incomplete");
	std::string authority = s.substr(0, slash);
	std::string secret_text = s.substr(slash + 1);

	std::string host;
	std::string port_text;
	if(!authority.empty() && authority.front() == '[')
	{
		size_t close = authority.find(']');
		if(close == std::string::npos)
			return fail("The address in the invite is malformed");
		host = authority.substr(1, close - 1);
		if(close + 1 < authority.size())
		{
			if(authority[close + 1] != ':')
				return fail("The address in the invite is malformed");
			port_text = authority.substr(close + 2);
		}
	}
	else
	{
		size_t colon = authority.rfind(':');
		if(colon == std::string::npos)
			host = authority;
		else
		{
			host = authority.substr(0, colon);
			port_text = authority.substr(colon + 1);
		}
	}
	if(host.empty())
		return fail("The invite has no address");
	// "-" stands for "no address": the host is found through the meeting point
	if(host == "-")
	{
		if(!port_text.empty())
			return fail("The address in the invite is malformed");
		host.clear();
	}

	uint16_t port = kDefaultPort;
	if(!port_text.empty())
	{
		if(port_text.size() > 5 || port_text.find_first_not_of("0123456789") != std::string::npos)
			return fail("The port in the invite is not a number");
		long v = std::strtol(port_text.c_str(), nullptr, 10);
		if(v < 1 || v > 65535)
			return fail("The port in the invite is out of range");
		port = static_cast<uint16_t>(v);
	}

	Secret secret{};
	if(!SecretFromString(secret_text, secret))
		return fail("The invite's key is malformed");

	invite.host = host;
	invite.port = port;
	invite.secret = secret;
	invite.meet = meet;
	return true;
}

} // namespace coplay
