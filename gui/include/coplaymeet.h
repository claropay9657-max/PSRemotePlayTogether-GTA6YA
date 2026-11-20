// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_COPLAYMEET_H
#define CHIAKI_COPLAYMEET_H

#include <coplay/invite.h>
#include <coplay/meet.h>

#include <QString>

#include <string>

/**
 * Play Together without a forwarded port: what the app adds to the library's
 * meeting-point code (coplay/meet.h), which is the settings and the HTTPS
 * requests. docs/nat-traversal.md describes how the whole thing works.
 *
 * The meeting point is a server on the internet, so it is only asked while the
 * user shares a session or joins one, never just because the app runs.
 */
namespace CoPlayMeet
{

/** The built-in meeting point. PSRPT_MEET_URL in the environment replaces it, for trying things out. */
std::string DefaultUrl();

/** Whether the setting allows a meeting point at all ("off" does not). */
bool Allowed(const QString &meet_setting);

/**
 * For somebody who shares: the meeting point from the setting, and the STUN
 * servers that tell the session's own address. Not enabled if the setting says
 * "off" or is no address.
 */
coplay::MeetConfig ForHost(const QString &meet_setting, const QString &stun_setting);

/**
 * For somebody who joins: the meeting point the invite names, the built-in one
 * if it names none. Not enabled if the own setting says "off".
 */
coplay::MeetConfig ForGuest(const QString &meet_setting, const QString &stun_setting, const coplay::Invite &invite);

/** What an invite of this host says about the meeting point: nothing for the built-in one. */
std::string InviteValue(const QString &meet_setting);

/** One POST request, on the calling thread. */
bool Post(const coplay::HttpRequest &request, coplay::HttpResponse &response, std::string *error);

} // namespace CoPlayMeet

#endif // CHIAKI_COPLAYMEET_H
