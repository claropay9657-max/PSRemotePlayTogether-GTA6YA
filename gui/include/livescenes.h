// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_LIVESCENES_H
#define CHIAKI_LIVESCENES_H

#include <QAtomicInteger>
#include <QByteArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QQmlPropertyMap>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

class Settings;
class StreamSession;

/**
 * Stream scenes: full pictures for a live stream that are not the game. "Starting
 * soon" with a countdown, a lobby of webcams, "Be right back", "Play with us" with a
 * QR code to the room, "Technical difficulties" and "Thanks for watching".
 *
 * Each scene exists twice, after the same design: in QML (qml/scenes/), drawn into the
 * app's own live stream in place of the game picture (StreamView's live overlay), and
 * as a page of the overlay server for an OBS browser source (/scene/<id>). Both are
 * filled from here: the players with their pictures and who talks, the room and the
 * game, the countdown, the session's time and the state of the connection.
 *
 * Whoever asked to be left out of streams (MediaFlags::kMediaNoStreams) appears as
 * "Player N", without picture or webcam. Scenes never show addresses or invites:
 * how to get in is the public page of public rooms, or "ask for an invite".
 *
 * What the player fills in once (channel, next stream, minutes, texts) is kept in
 * "scenes/<key>" of the configuration file, offered to QML as a property map like
 * Chiaki.hud: read with `values.channel`, change with `set()`.
 */
class LiveScenes : public QObject
{
	Q_OBJECT
	/** The scene chosen for the stream: "game" (none), "starting", "lobby", "brb", "play", "difficulties", "ending". */
	Q_PROPERTY(QString current READ current NOTIFY sceneChanged)
	/** What the stream shows right now: `current`, or "difficulties" by itself while the connection is lost. */
	Q_PROPERTY(QString shown READ shown NOTIFY sceneChanged)
	/** Every scene: {id, name}, "game" first. */
	Q_PROPERTY(QVariantList scenes READ scenes CONSTANT)
	Q_PROPERTY(QObject *values READ valuesObject CONSTANT)
	/** Everything the scenes show; see BuildData(). */
	Q_PROPERTY(QVariantMap data READ data NOTIFY dataChanged)

	public:
		LiveScenes(Settings *settings, QObject *parent = nullptr);

		QString current() const { return scene; }
		QString shown() const;
		QVariantList scenes() const;
		QObject *valuesObject() { return &map; }
		QVariantMap data() const { return qml_data; }

		/** Shows a scene in the stream ("game" for the game). Starting soon and Be right back start their clock. */
		Q_INVOKABLE void show(const QString &id);
		Q_INVOKABLE void set(const QString &key, const QVariant &value);
		/** The countdown of Starting soon, from the minutes set. */
		Q_INVOKABLE void restartCountdown();
		/** Scene texts with {game}, {host}, {me}, {players}, {channel}; empty when a placeholder has no value. */
		Q_INVOKABLE QString fill(const QString &text) const;
		/** Addresses of the scenes for OBS, by id, or empty while the overlay server is off. */
		Q_INVOKABLE QVariantMap obsUrls() const;
		/** Puts an address on the clipboard (for OBS). */
		Q_INVOKABLE void copy(const QString &text) const;

		void SetSession(StreamSession *session);
		/** The game on the console, as the console's own list says (empty: unknown). */
		void SetGame(const QString &name);
		/** The console connection is being made again (Chiaki.reconnecting). */
		void SetReconnecting(bool reconnecting);
		/** Where the overlay server is, for obsUrls(); empty while it is off. */
		void SetOverlayUrl(const QString &url);
		/** How many scene pages are open in OBS: they need the data even without a stream. */
		void SetPageClients(int count);

		/** A picture of the game (JPEG, 640x360) is wanted: the decoder thread asks, at most every few seconds. */
		bool TakeGamePictureRequest() { return picture_wanted.fetchAndStoreRelaxed(0) != 0; }
		void SetGamePicture(const QByteArray &jpeg);
		QByteArray GamePicture() const { return game_picture; }

		/** The data for the scene pages: pictures as addresses of the overlay server. */
		QByteArray PageJson() const { return page_json; }
		/** The QR code to the room's page as SVG; empty when there is none to show. */
		QByteArray QrSvg() const { return qr_svg; }
		/** The QR code to the public page, for the scene pages' demo (?demo=1). */
		static QByteArray DemoQrSvg();

	signals:
		void sceneChanged();
		void dataChanged();
		void valuesChanged();

	private:
		void Store(const QString &key, const QVariant &value);
		void Refresh();
		void UpdateTimer();
		void BuildQr(const QString &url);

		Settings *settings;
		QQmlPropertyMap map;
		QPointer<StreamSession> session;
		QString scene = QStringLiteral("game");
		bool reconnecting = false;
		QString game;
		QString overlay_url;
		int page_clients = 0;
		QTimer timer;
		QAtomicInteger<int> picture_wanted = 0;
		qint64 picture_asked_ms = 0;
		QByteArray game_picture;
		int picture_serial = 0;
		qint64 countdown_end_ms = 0;
		qint64 break_end_ms = 0;
		qint64 session_start_ms = 0;
		qint64 session_end_ms = 0;
		/** Everybody who was in the session (as streams may show them), for Thanks for watching. */
		QStringList seen_names;
		QSet<int> seen_slots;
		QString qr_url;
		QByteArray qr_svg;
		QVariantList qr_rows;
		QVariantMap qml_data;
		QByteArray page_json;
};

#endif // CHIAKI_LIVESCENES_H
