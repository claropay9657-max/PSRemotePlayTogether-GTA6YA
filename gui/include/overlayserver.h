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

class QTcpSocket;
class Settings;
class StreamSession;

/**
 * Serves the stream overlay: a web page for an OBS browser source that shows
 * the players of a session, who is in control and everybody's controller.
 *
 * A very small HTTP server on this computer only. The page and the controller
 * skins come out of the application's resources; the live data is pushed to
 * the page as server-sent events.
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

		bool IsRunning() const { return server.isListening(); }
		QString Url() const;
		QString LastError() const { return last_error; }

		/** Names of the controller skins that can be used. */
		static QStringList Skins();

	signals:
		void StateChanged();

	private:
		void OnNewConnection();
		void OnReadyRead(QTcpSocket *socket);
		void Handle(QTcpSocket *socket, const QByteArray &method, const QString &path);
		void Respond(QTcpSocket *socket, int status, const QByteArray &content_type, const QByteArray &body, bool cacheable = false);
		void ServeResource(QTcpSocket *socket, const QString &resource, bool cacheable);
		void ServePad(QTcpSocket *socket, const QString &skin);
		void StartEvents(QTcpSocket *socket);
		void SendEvent(QTcpSocket *socket, const QByteArray &event, const QByteArray &json);
		void Tick();
		QByteArray ConfigJson() const;
		QByteArray StateJson() const;

		Settings *settings;
		QTcpServer server;
		QPointer<StreamSession> session;
		QHash<QTcpSocket *, QByteArray> pending; // request bytes received so far
		QList<QTcpSocket *> event_clients;
		QTimer timer;
		QByteArray last_state;
		QByteArray last_config;
		qint64 last_keepalive_ms = 0;
		QString last_error;
};

#endif // CHIAKI_OVERLAYSERVER_H
