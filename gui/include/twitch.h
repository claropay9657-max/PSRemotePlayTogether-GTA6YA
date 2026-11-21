// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_TWITCH_H
#define CHIAKI_TWITCH_H

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class Settings;
class WebSocketClient;

/**
 * The player's Twitch account (Chiaki.twitch): logging in with Twitch's device code
 * (the app shows a code, the player confirms it on twitch.tv/activate; the app never
 * sees a password), and what happens on the channel, as Twitch's EventSub sends it over
 * a WebSocket: follows, subs, bits, raids, channel points, hype trains, polls and more.
 * Alerts (livealerts.h) and, later, viewer actions are built on it.
 *
 * Nothing of it passes through a server of ours: the app talks to Twitch directly.
 * The tokens are kept in the system's password store (keystore.h); the settings only
 * note who is logged in. The app is a public client: its id is built in
 * (PSRPT_TWITCH_CLIENT_ID), there is no secret.
 *
 * For tests, PSRPT_TWITCH_ID_URL, PSRPT_TWITCH_API_URL and PSRPT_TWITCH_EVENTSUB_URL
 * point it elsewhere, and PSRPT_TWITCH_TEST_TOKEN / PSRPT_TWITCH_TEST_USER log in
 * without Twitch.
 */
class TwitchClient : public QObject
{
	Q_OBJECT
	/** "no_app" (no client id in this build), "logged_out", "code" (waiting for the player), "logged_in". */
	Q_PROPERTY(QString state READ state NOTIFY changed)
	Q_PROPERTY(QString login READ login NOTIFY changed)
	Q_PROPERTY(QString userCode READ userCode NOTIFY changed)
	Q_PROPERTY(QString verifyUrl READ verifyUrl NOTIFY changed)
	/** "off", "connecting", "on" or "error": the event connection. */
	Q_PROPERTY(QString events READ events NOTIFY changed)
	Q_PROPERTY(QString error READ error NOTIFY changed)
	/** Each kind of event: {type, ok, problem}. */
	Q_PROPERTY(QVariantList subscriptions READ subscriptions NOTIFY changed)
	/** The channel is live on Twitch (stream.online / stream.offline). */
	Q_PROPERTY(bool channelLive READ channelLive NOTIFY changed)

	public:
		TwitchClient(Settings *settings, QObject *parent = nullptr);
		~TwitchClient() override;

		QString state() const;
		QString login() const { return user_login; }
		QString userCode() const { return user_code; }
		QString verifyUrl() const { return verify_url; }
		QString events() const { return events_state; }
		QString error() const { return last_error; }
		QVariantList subscriptions() const { return subs; }
		bool channelLive() const { return channel_live; }
		QString UserId() const { return user_id; }
		QString ClientId() const { return client_id; }

		/** Starts logging in: a code appears (userCode) for the player to confirm on Twitch. */
		Q_INVOKABLE void logIn();
		Q_INVOKABLE void cancelLogIn();
		Q_INVOKABLE void logOut();
		/** Opens twitch.tv/activate with the code filled in. */
		Q_INVOKABLE void openVerify();
		Q_INVOKABLE void copyCode();

		/** Connects to Twitch with the saved login, if there is one (reads the password store). */
		void Start();

		/** A call to Twitch's API (Helix): path like "/channel_points/custom_rewards". */
		void Helix(const QByteArray &method, const QString &path, const QJsonObject &body,
			std::function<void(int status, const QJsonObject &reply)> done);

	signals:
		void changed();
		/** An EventSub notification: its type ("channel.follow") and the event. */
		void Event(const QString &type, const QJsonObject &event);

	private:
		void Post(const QUrl &url, const QByteArray &form, std::function<void(int, const QJsonObject &)> done);
		void PollToken();
		void Validate(bool fresh);
		void Refresh(std::function<void(bool)> done);
		void SaveTokens();
		void SetLoggedOut(const QString &error);
		void Connect();
		void ScheduleReconnect();
		void OnMessage(const QByteArray &utf8);
		void Subscribe(const QString &session);
		void SetSubscription(const QString &type, bool ok, const QString &problem);
		void Fail(const QString &message);

		Settings *settings;
		QNetworkAccessManager *network;
		QString client_id;
		QString id_url;
		QString api_url;
		QString eventsub_url;
		bool test_login = false;

		QString access_token;
		QString refresh_token;
		QString user_login;
		QString user_id;

		// logging in
		QString device_code;
		QString user_code;
		QString verify_url;
		QTimer poll_timer;
		qint64 code_expires_ms = 0;

		// events
		QPointer<WebSocketClient> socket;
		QString events_state = QStringLiteral("off");
		QString session_id;
		/** Twitch asked us to move to another address: its welcome keeps our subscriptions. */
		bool moving = false;
		QTimer keepalive_timer;
		QTimer reconnect_timer;
		int reconnect_tries = 0;
		QVariantList subs;
		bool channel_live = false;
		QString last_error;
};

#endif // CHIAKI_TWITCH_H
