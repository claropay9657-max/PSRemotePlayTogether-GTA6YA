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
 *
 * Two additions for joining through a meeting point (meet.h):
 * "psrpt://-/secret" names no address at all, the host is found through the
 * meeting point alone; and "...?meet=example.org/meet.php" names a meeting
 * point other than the default one.
 */
struct Invite
{
	/** Empty: the invite names no address. */
	std::string host;
	uint16_t port = kDefaultPort;
	Secret secret{};
	/** A meeting point other than the default, without its "https://"; empty for the default. */
	std::string meet;
};

std::string EncodeInvite(const Invite &invite);
bool DecodeInvite(const std::string &text, Invite &invite, std::string *error);

Secret GenerateSecret();
std::string SecretToString(const Secret &secret);
bool SecretFromString(const std::string &text, Secret &secret);

} // namespace coplay

#endif // COPLAY_INVITE_H
