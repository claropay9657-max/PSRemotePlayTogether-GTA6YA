// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef COPLAY_INVITE_H
#define COPLAY_INVITE_H

#include <cstdint>
#include <string>

#include "crypto.h"

namespace coplay {

/**
 * What a guest needs to join: where the host is and the session secret.
 * Written as "psrpt://host:port/secret", for example
 * "psrpt://203.0.113.7:9410/Zm9vYmFyYmF6cXV4MTIzNA" or "psrpt://[2001:db8::1]:9410/...".
 */
struct Invite
{
	std::string host;
	uint16_t port = kDefaultPort;
	Secret secret{};
};

std::string EncodeInvite(const Invite &invite);
bool DecodeInvite(const std::string &text, Invite &invite, std::string *error);

Secret GenerateSecret();
std::string SecretToString(const Secret &secret);
bool SecretFromString(const std::string &text, Secret &secret);

} // namespace coplay

#endif // COPLAY_INVITE_H
