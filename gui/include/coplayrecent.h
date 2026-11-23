// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_COPLAYRECENT_H
#define CHIAKI_COPLAYRECENT_H

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class Settings;

/*
 * Sessions joined before, newest first, so that a friend's session can be joined
 * again with a click: who hosted it, on what, and when. A host keeps its invite
 * from one session to the next, so the same invite works as long as the host
 * does not make a new one.
 *
 * Whether a host is sharing right now is asked at the meeting point, only while
 * somebody looks at the list (the Join page), as a guest that leaves no note
 * (coplay::LookForHost): the host learns nothing of it. The invites stay on this
 * computer, in the settings, like the last invite always did.
 *
 * The page for web browsers keeps the same list in the same way (web/js/recent.js).
 */
class CoPlayRecent : public QObject
{
	Q_OBJECT

	public:
		/** At most this many are kept; the oldest go. */
		static constexpr int kMax = 8;

		enum Status
		{
			Unknown = 0, // not asked (no meeting point), or no answer
			Looking = 1, // asked, no answer yet
			Open = 2,    // the host shares the session now
			Closed = 3   // the meeting point has no note of the host
		};

		CoPlayRecent(Settings *settings, QObject *parent = nullptr);

		/**
		 * Each: key (the invite's key, which tells them apart), invite, host, console
		 * ("ps5", "ps4", "pc", "xbox" or empty), title, game and cover (of a public
		 * room, else empty), joined (milliseconds since 1970), status (Status).
		 */
		QVariantList List() const;

		/**
		 * Joined: first in the list from now on. What a public room said about itself
		 * is taken from the room list when the invite is one of its rooms.
		 */
		void Joined(const QString &invite, const QString &host, const QString &console);
		void Forget(const QString &key);
		/** Asks for every host whether it shares now, unless that is under way already. */
		void Look(const QString &meet_setting);
		/** The list of the setting set that is used now. */
		void Reload();

		/** "ps5", "ps4", "pc" or "xbox" for what a host's welcome says about its game. */
		static QString ConsoleOf(bool ps5, int game_source);

	signals:
		void Changed();

	private:
		void Save();

		Settings *settings;
		QVariantList entries;
		QHash<QString, int> status;
		/** Keys whose host is being looked for. */
		QHash<QString, bool> looking;
		quint64 generation = 0;
};

#endif // CHIAKI_COPLAYRECENT_H
