// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_WEBSOCKETCLIENT_H
#define CHIAKI_WEBSOCKETCLIENT_H

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QSslSocket>
#include <QTimer>
#include <QUrl>

/**
 * A small WebSocket client (RFC 6455) on top of Qt's own sockets, for Twitch's
 * EventSub (wss://eventsub.wss.twitch.tv/ws). Text messages in, text messages out,
 * pings answered; ws:// as well, for a test server on this computer.
 *
 * Qt has a WebSocket module, but it is one more library for every build; this is
 * all EventSub needs.
 */
class WebSocketClient : public QObject
{
	Q_OBJECT

	public:
		explicit WebSocketClient(QObject *parent = nullptr);
		~WebSocketClient() override;

		void Open(const QUrl &url);
		void Close();
		bool IsOpen() const { return state == Open_; }
		void SendText(const QByteArray &utf8);

	signals:
		void Connected();
		void TextReceived(const QByteArray &utf8);
		/** The connection is gone (or never came); `reason` says why, for the log. */
		void Disconnected(const QString &reason);

	private:
		enum State { Closed, Connecting, Handshake, Open_ };

		void OnConnected();
		void OnReadyRead();
		void OnGone(const QString &reason);
		bool ParseFrames();
		void SendFrame(int opcode, const QByteArray &payload);

		QPointer<QSslSocket> socket;
		State state = Closed;
		QUrl url;
		QByteArray key;
		QByteArray buffer;
		QByteArray message; // a text message that comes in parts
		int message_opcode = 0;
		QTimer connect_timer;
};

#endif // CHIAKI_WEBSOCKETCLIENT_H
