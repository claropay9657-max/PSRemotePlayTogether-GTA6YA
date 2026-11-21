// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_COPLAYROOMS_H
#define CHIAKI_COPLAYROOMS_H

#include <QObject>
#include <QString>

#include <functional>
#include <QTimer>
#include <QVariantList>

class QNetworkAccessManager;

/*
 * Public rooms: sessions whose hosts want them to be found. A list on a server
 * (server/rooms/psrpt_rooms.php) says which there are, with a title, a language,
 * the game and how many play; joining one works like an invite. The list only
 * says how to find a room: nothing of a session passes through it, and a listed
 * invite names no address (the host is found through the meeting point).
 */
namespace CoPlayRooms
{

/** The built-in list. PSRPT_ROOMS_URL in the environment replaces it, for trying things out. */
QString DefaultUrl();

/**
 * The list a setting names: empty is the built-in one, "off" is none (an empty
 * result), "example.org/rooms.php" gets https:// in front. Empty as well for what
 * is no address.
 */
QString UrlFromSetting(const QString &setting);

/** The languages a room can say it speaks, as [code, name in that language]. */
QVariantList Languages();

/**
 * A picture of the game with this title id (CUSA12345, PPSA12345), from Sony's
 * public catalog, which needs no login: the square key art on Sony's image
 * server, as an address on https://image.api.playstation.com/, or empty when
 * the catalog has none. Asked once per title id while the app runs; done is
 * called later, and not at all once context is gone.
 */
void FindCover(const QString &title_id, QObject *context, std::function<void(const QString &cover)> done);

/** Whether an address is a picture on Sony's image server that a room may show. */
bool IsCover(const QString &address);

} // namespace CoPlayRooms

/** The list of public rooms, read when somebody looks at it. One for the whole app. */
class CoPlayRoomList : public QObject
{
	Q_OBJECT

	public:
		enum State { Idle = 0, Loading = 1, Loaded = 2, Failed = 3, Off = 4 };

		static CoPlayRoomList *Instance();

		/** Reads the list from the server the setting names. */
		void Refresh(const QString &setting);
		/** Each room: id, title, lang, game, host, players, max, policy, control, web, invite, proto, titleId, cover, since, full, compatible. */
		QVariantList Rooms() const { return rooms; }
		int GetState() const { return state; }
		QString Error() const { return error; }

	signals:
		void Changed();

	private:
		explicit CoPlayRoomList(QObject *parent = nullptr);
		QNetworkAccessManager *network = nullptr;
		QVariantList rooms;
		int state = Idle;
		QString error;
		quint64 generation = 0;
};

/**
 * A host's room on the list: announced now and then while the session runs, and
 * taken off when it ends (or when this object goes).
 */
class CoPlayRoomAnnouncer : public QObject
{
	Q_OBJECT

	public:
		struct Room
		{
			QString title;
			QString lang;
			QString game;
			QString host;
			/** "ask", "watch" or "open". */
			QString policy;
			/** Who controls: "all" at once, "turns" one at a time, or only the "host". */
			QString control = QStringLiteral("all");
			/** The game's title id as the console reports it (CUSA12345, PPSA12345); may be empty. */
			QString title_id;
			/** A picture of the game on Sony's image server; may be empty. */
			QString cover;
			/** psrpt://-/KEY[?meet=...]: no address. */
			QString invite;
			/** One letter for each player, the host first ("?" without a name): only initials, no names. */
			QString initials;
			/** Everybody's Twitch login who streams there and wants it shown, host first, as "a,b", else empty. */
			QString twitch;
			int players = 1;
			int max = 5;
			bool web = false;
			int proto = 0;

			bool operator==(const Room &o) const
			{
				return title == o.title && lang == o.lang && game == o.game && host == o.host && policy == o.policy
					&& control == o.control && title_id == o.title_id && cover == o.cover
					&& invite == o.invite && initials == o.initials && twitch == o.twitch && players == o.players && max == o.max
					&& web == o.web && proto == o.proto;
			}
		};

		CoPlayRoomAnnouncer(const QString &url, QObject *parent = nullptr);
		~CoPlayRoomAnnouncer();

		/** What the list says about the room; announced at once if something changed. */
		void Update(const Room &room);
		/** Off the list (closed to newcomers, for one), or back on. */
		void SetListed(bool listed);
		/** Empty while the list takes the room; otherwise why not. */
		QString Problem() const { return problem; }
		/**
		 * Pictures of the game for the list: when the list takes them, PictureWanted()
		 * asks for one now and then, as often as the list says (every 45 seconds by default).
		 */
		void SetPictures(bool on);
		/** A small JPEG of the game (at most 640x360, the list takes up to 48 KB). */
		void SendPicture(const QByteArray &jpeg);
		/**
		 * The players' pictures, in the order of Room::initials: a small JPEG (64x64) for each,
		 * or an empty one for a player without a picture. Sent when they change.
		 */
		void SetFaces(const QList<QByteArray> &faces);

	signals:
		void Changed();
		void PictureWanted();

	private:
		void Announce();
		void Remove();
		void UpdatePictureTimer();
		void SendFaces();
		QNetworkAccessManager *network = nullptr;
		QString url;
		QString id;
		QString token;
		Room room;
		bool have_room = false;
		bool listed = true;
		bool announced = false;
		QTimer timer;
		QTimer soon;
		QString problem;
		/** How often the list takes a picture, in ms; 0 for never (or a list that does not know pictures). */
		int picture_ms = 0;
		bool pictures = false;
		bool picture_asked = false;
		QTimer picture_timer;
		QList<QByteArray> faces;
		/** The list has the faces as they are; false again after the room left the list. */
		bool faces_sent = true;
		/** Some player had a picture once: from then on, also "none" is worth telling the list. */
		bool faces_ever = false;
		QTimer faces_soon;
};

#endif // CHIAKI_COPLAYROOMS_H
