// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_OVERLAYSERVER_H
#define CHIAKI_OVERLAYSERVER_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QTcpServer>
#include <QTimer>
#include <QVariantMap>

class QTcpSocket;
class Settings;
class StreamSession;
class LiveScenes;

/**
 * Serves the stream overlay: a web page for an OBS browser source that shows
 * the players of a session, who is in control and everybody's controller,
 * their webcams, and plays their conversation.
 *
 * A very small HTTP server on this computer only. The page and the controller
 * skins come out of the application's resources; the live data is pushed to
 * the page as server-sent events, webcams as motion JPEG and the conversation
 * as a stream of raw audio.
 */
class OverlayServer : public QObject
{
	Q_OBJECT

	public:
		explicit OverlayServer(Settings *settings, QObject *parent = nullptr);
		~OverlayServer();

		/** Starts, stops or moves the server to match the settings, and tells open pages about new options. */
		void ApplySettings();
		void SetSession(StreamSession *session);
		/** The stream scenes, for the scene pages (/scene/<id>). */
		void SetScenes(LiveScenes *scenes);

		bool IsRunning() const { return server.isListening(); }
		QString Url() const;
		QString LastError() const { return last_error; }

		/** Names of the controller skins that can be used. */
		static QStringList Skins();

	signals:
		void StateChanged();
		/** The overlay page was arranged in a browser: setting names and their new values. */
		void LayoutEdited(const QVariantMap &values);

	private:
		void OnNewConnection();
		void OnReadyRead(QTcpSocket *socket);
		void Handle(QTcpSocket *socket, const QByteArray &method, const QString &path, const QString &query);
		void Respond(QTcpSocket *socket, int status, const QByteArray &content_type, const QByteArray &body, bool cacheable = false);
		void ServeResource(QTcpSocket *socket, const QString &resource, bool cacheable);
		void ServePad(QTcpSocket *socket, const QString &skin);
		void StartEvents(QTcpSocket *socket, bool scenes);
		void ServeFile(QTcpSocket *socket, const QString &path, const QByteArray &type);
		void SendEvent(QTcpSocket *socket, const QByteArray &event, const QByteArray &json);
		void StartCam(QTcpSocket *socket, int slot);
		void StartVoice(QTcpSocket *socket);
		void UpdateVoiceTap();
		void UpdateTimer();
		void Tick();
		QByteArray ConfigJson() const;
		QByteArray StateJson() const;
		QByteArray ChatJson() const;

		Settings *settings;
		QTcpServer server;
		QPointer<StreamSession> session;
		QHash<QTcpSocket *, QByteArray> pending; // request bytes received so far
		QList<QTcpSocket *> event_clients;
		/** Event clients that are scene pages: they also get the scenes' data. */
		QList<QTcpSocket *> scene_clients;
		QPointer<LiveScenes> scenes;
		QByteArray last_scene;
		int scene_tick = 0;
		struct CamClient
		{
			int slot = 0;
			quint64 serial = 0; // of the picture sent last
		};
		QHash<QTcpSocket *, CamClient> cam_clients;
		QList<QTcpSocket *> voice_clients;
		bool tap_on = false;
		bool tap_own = false;
		QPointer<StreamSession> tap_session;
		QTimer timer;
		QByteArray last_state;
		QByteArray last_chat;
		QByteArray last_config;
		qint64 last_keepalive_ms = 0;
		QString last_error;
};

#endif // CHIAKI_OVERLAYSERVER_H
