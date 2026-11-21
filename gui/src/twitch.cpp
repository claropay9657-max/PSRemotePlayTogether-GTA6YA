// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "twitch.h"
#include "keystore.h"
#include "settings.h"
#include "websocketclient.h"

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

Q_DECLARE_LOGGING_CATEGORY(chiakiGui)

#ifndef PSRPT_TWITCH_CLIENT_ID
#define PSRPT_TWITCH_CLIENT_ID ""
#endif

namespace {

const char kTokensKey[] = "twitch-tokens";

/**
 * What the app asks to see. Read only, except channel points: the app may make its own
 * rewards and mark their redemptions done (viewer actions). user:read:chat reads the
 * channel's chat for the chat element; the app never writes in it.
 */
const char kScopes[] = "moderator:read:followers channel:read:subscriptions bits:read "
	"channel:read:redemptions channel:manage:redemptions channel:read:hype_train channel:read:polls "
	"channel:read:predictions channel:read:goals channel:read:charity moderator:read:shoutouts channel:read:ads "
	"user:read:chat";

struct Subscription
{
	const char *type;
	const char *version;
	/**
	 * Who the condition names: "b" broadcaster, "bm" broadcaster and moderator, "bu" broadcaster
	 * and the user reading (chat), "to" raids to us.
	 */
	const char *condition;
};

const Subscription kSubscriptions[] = {
	{ "channel.follow", "2", "bm" },
	{ "channel.subscribe", "1", "b" },
	{ "channel.subscription.message", "1", "b" },
	{ "channel.subscription.gift", "1", "b" },
	{ "channel.cheer", "1", "b" },
	{ "channel.raid", "1", "to" },
	{ "channel.channel_points_custom_reward_redemption.add", "1", "b" },
	{ "channel.channel_points_automatic_reward_redemption.add", "2", "b" },
	{ "channel.hype_train.begin", "2", "b" },
	{ "channel.hype_train.progress", "2", "b" },
	{ "channel.hype_train.end", "2", "b" },
	{ "channel.poll.begin", "1", "b" },
	{ "channel.poll.progress", "1", "b" },
	{ "channel.poll.end", "1", "b" },
	{ "channel.prediction.begin", "1", "b" },
	{ "channel.prediction.progress", "1", "b" },
	{ "channel.prediction.lock", "1", "b" },
	{ "channel.prediction.end", "1", "b" },
	{ "channel.goal.begin", "1", "b" },
	{ "channel.goal.progress", "1", "b" },
	{ "channel.goal.end", "1", "b" },
	{ "channel.charity_campaign.donate", "1", "b" },
	{ "channel.charity_campaign.start", "1", "b" },
	{ "channel.charity_campaign.progress", "1", "b" },
	{ "channel.charity_campaign.stop", "1", "b" },
	{ "channel.shoutout.receive", "1", "bm" },
	{ "channel.ad_break.begin", "1", "b" },
	{ "stream.online", "1", "b" },
	{ "stream.offline", "1", "b" },
	{ "channel.chat.message", "1", "bu" },
	{ "channel.chat.message_delete", "1", "bu" },
	{ "channel.chat.clear_user_messages", "1", "bu" },
	{ "channel.chat.clear", "1", "bu" },
};

QString EnvOr(const char *name, const QString &fallback)
{
	const QByteArray v = qgetenv(name);
	return v.isEmpty() ? fallback : QString::fromUtf8(v);
}

QByteArray Form(std::initializer_list<std::pair<const char *, QString>> fields)
{
	QUrlQuery q;
	for(const auto &f : fields)
		q.addQueryItem(QLatin1String(f.first), QString::fromUtf8(QUrl::toPercentEncoding(f.second)));
	return q.toString(QUrl::FullyEncoded).toUtf8();
}

} // namespace

bool TwitchClient::showInRooms() const
{
	return settings->GetValue(QStringLiteral("twitch/show_in_rooms"), false).toBool();
}

void TwitchClient::setShowInRooms(bool show)
{
	if(show == showInRooms())
		return;
	settings->SetValue(QStringLiteral("twitch/show_in_rooms"), show);
	emit changed();
}

QString TwitchClient::RoomStream() const
{
	if(!channel_live || user_login.isEmpty() || !showInRooms())
		return QString();
	return user_login.toLower();
}

TwitchClient::TwitchClient(Settings *settings, QObject *parent)
	: QObject(parent), settings(settings), network(new QNetworkAccessManager(this))
{
	client_id = EnvOr("PSRPT_TWITCH_CLIENT_ID",
		settings->GetValue(QStringLiteral("twitch/client_id"), QString::fromLatin1(PSRPT_TWITCH_CLIENT_ID)).toString().trimmed());
	if(client_id.isEmpty())
		client_id = QString::fromLatin1(PSRPT_TWITCH_CLIENT_ID);
	id_url = EnvOr("PSRPT_TWITCH_ID_URL", QStringLiteral("https://id.twitch.tv"));
	api_url = EnvOr("PSRPT_TWITCH_API_URL", QStringLiteral("https://api.twitch.tv/helix"));
	eventsub_url = EnvOr("PSRPT_TWITCH_EVENTSUB_URL", QStringLiteral("wss://eventsub.wss.twitch.tv/ws"));
	user_login = settings->GetValue(QStringLiteral("twitch/login")).toString();
	user_id = settings->GetValue(QStringLiteral("twitch/user_id")).toString();

	poll_timer.setSingleShot(false);
	connect(&poll_timer, &QTimer::timeout, this, &TwitchClient::PollToken);
	keepalive_timer.setSingleShot(true);
	connect(&keepalive_timer, &QTimer::timeout, this, [this] {
		qCWarning(chiakiGui) << "Twitch: no word from EventSub, connecting again";
		ScheduleReconnect();
	});
	reconnect_timer.setSingleShot(true);
	connect(&reconnect_timer, &QTimer::timeout, this, &TwitchClient::Connect);
}

TwitchClient::~TwitchClient() = default;

QString TwitchClient::state() const
{
	if(client_id.isEmpty())
		return QStringLiteral("no_app");
	if(!access_token.isEmpty() || (!user_login.isEmpty() && !user_id.isEmpty() && device_code.isEmpty()))
		return QStringLiteral("logged_in");
	if(!device_code.isEmpty())
		return QStringLiteral("code");
	return QStringLiteral("logged_out");
}

void TwitchClient::Fail(const QString &message)
{
	last_error = message;
	qCWarning(chiakiGui).noquote() << "Twitch:" << message;
	emit changed();
}

void TwitchClient::Post(const QUrl &url, const QByteArray &form, std::function<void(int, const QJsonObject &)> done)
{
	QNetworkRequest request(url);
	request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/x-www-form-urlencoded"));
	request.setTransferTimeout(20000);
	QNetworkReply *reply = network->post(request, form);
	connect(reply, &QNetworkReply::finished, this, [reply, done] {
		reply->deleteLater();
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		done(status, QJsonDocument::fromJson(reply->readAll()).object());
	});
}

// ------------------------------------------------------------------ logging in

void TwitchClient::Start()
{
	const QByteArray test_token = qgetenv("PSRPT_TWITCH_TEST_TOKEN");
	if(!test_token.isEmpty())
	{
		// A test: no Twitch, no password store
		test_login = true;
		access_token = QString::fromUtf8(test_token);
		user_id = EnvOr("PSRPT_TWITCH_TEST_USER", QStringLiteral("12345"));
		user_login = EnvOr("PSRPT_TWITCH_TEST_LOGIN", QStringLiteral("testchannel"));
		emit changed();
		Connect();
		return;
	}
	if(client_id.isEmpty() || user_login.isEmpty())
		return;
	const QJsonObject tokens = QJsonDocument::fromJson(keystore::Load(QString::fromLatin1(kTokensKey)).toUtf8()).object();
	access_token = tokens.value("access").toString();
	refresh_token = tokens.value("refresh").toString();
	if(access_token.isEmpty())
	{
		SetLoggedOut(tr("The Twitch login is gone from the password store. Log in again."));
		return;
	}
	Validate(false);
}

void TwitchClient::logIn()
{
	if(client_id.isEmpty())
	{
		Fail(tr("This build of the app has no Twitch app id."));
		return;
	}
	cancelLogIn();
	last_error.clear();
	Post(QUrl(id_url + QStringLiteral("/oauth2/device")),
		Form({ { "client_id", client_id }, { "scopes", QString::fromLatin1(kScopes) } }),
		[this](int status, const QJsonObject &reply) {
			if(status != 200 || reply.value("device_code").toString().isEmpty())
			{
				Fail(tr("Twitch did not start the login (%1).").arg(reply.value("message").toString(QString::number(status))));
				return;
			}
			device_code = reply.value("device_code").toString();
			user_code = reply.value("user_code").toString();
			verify_url = reply.value("verification_uri").toString();
			code_expires_ms = QDateTime::currentMSecsSinceEpoch() + reply.value("expires_in").toInt(1800) * 1000LL;
			poll_timer.start(std::max(2, reply.value("interval").toInt(5)) * 1000);
			emit changed();
		});
}

void TwitchClient::cancelLogIn()
{
	poll_timer.stop();
	device_code.clear();
	user_code.clear();
	verify_url.clear();
	emit changed();
}

void TwitchClient::openVerify()
{
	if(!verify_url.isEmpty())
		QDesktopServices::openUrl(QUrl(verify_url));
}

void TwitchClient::copyCode()
{
	if(!user_code.isEmpty())
		QGuiApplication::clipboard()->setText(user_code);
}

void TwitchClient::PollToken()
{
	if(device_code.isEmpty())
		return;
	if(QDateTime::currentMSecsSinceEpoch() > code_expires_ms)
	{
		cancelLogIn();
		Fail(tr("The code ran out. Log in again."));
		return;
	}
	const QString code = device_code;
	Post(QUrl(id_url + QStringLiteral("/oauth2/token")),
		Form({ { "client_id", client_id }, { "scopes", QString::fromLatin1(kScopes) }, { "device_code", code },
			{ "grant_type", QStringLiteral("urn:ietf:params:oauth:grant-type:device_code") } }),
		[this, code](int status, const QJsonObject &reply) {
			if(device_code != code)
				return;
			if(status == 200 && !reply.value("access_token").toString().isEmpty())
			{
				poll_timer.stop();
				access_token = reply.value("access_token").toString();
				refresh_token = reply.value("refresh_token").toString();
				device_code.clear();
				user_code.clear();
				verify_url.clear();
				Validate(true);
				return;
			}
			const QString message = reply.value("message").toString();
			if(message == QLatin1String("authorization_pending") || message == QLatin1String("slow_down"))
				return;
			cancelLogIn();
			Fail(tr("Twitch did not log you in (%1).").arg(message.isEmpty() ? QString::number(status) : message));
		});
}

void TwitchClient::Validate(bool fresh)
{
	QNetworkRequest request(QUrl(id_url + QStringLiteral("/oauth2/validate")));
	request.setRawHeader("Authorization", "OAuth " + access_token.toUtf8());
	request.setTransferTimeout(20000);
	QNetworkReply *reply = network->get(request);
	connect(reply, &QNetworkReply::finished, this, [this, reply, fresh] {
		reply->deleteLater();
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
		if(status == 401 && !refresh_token.isEmpty())
		{
			Refresh([this](bool ok) {
				if(ok)
					Validate(true);
				else
					SetLoggedOut(tr("Twitch ended the login. Log in again."));
			});
			return;
		}
		if(status != 200)
		{
			if(status == 0)
			{
				// No internet right now: try again later, the login stays
				Fail(tr("Twitch cannot be reached right now."));
				ScheduleReconnect();
				return;
			}
			SetLoggedOut(tr("Twitch did not take the login (%1). Log in again.").arg(status));
			return;
		}
		user_login = o.value("login").toString();
		user_id = o.value("user_id").toString();
		settings->SetValue(QStringLiteral("twitch/login"), user_login);
		settings->SetValue(QStringLiteral("twitch/user_id"), user_id);
		if(fresh)
			SaveTokens();
		last_error.clear();
		qCInfo(chiakiGui).noquote() << "Twitch: logged in as" << user_login;
		emit changed();
		Connect();
	});
}

void TwitchClient::Refresh(std::function<void(bool)> done)
{
	if(refresh_token.isEmpty() || test_login)
	{
		done(false);
		return;
	}
	Post(QUrl(id_url + QStringLiteral("/oauth2/token")),
		Form({ { "client_id", client_id }, { "grant_type", QStringLiteral("refresh_token") }, { "refresh_token", refresh_token } }),
		[this, done](int status, const QJsonObject &reply) {
			if(status != 200 || reply.value("access_token").toString().isEmpty())
			{
				done(false);
				return;
			}
			access_token = reply.value("access_token").toString();
			// A refresh token works once: the new one replaces it
			refresh_token = reply.value("refresh_token").toString(refresh_token);
			SaveTokens();
			done(true);
		});
}

void TwitchClient::SaveTokens()
{
	if(test_login)
		return;
	QJsonObject o;
	o["access"] = access_token;
	o["refresh"] = refresh_token;
	QString error;
	if(!keystore::Save(QString::fromLatin1(kTokensKey), QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)), &error))
		qCWarning(chiakiGui).noquote() << "Twitch: the login is kept in memory only:" << error;
}

void TwitchClient::logOut()
{
	if(!access_token.isEmpty() && !test_login)
		Post(QUrl(id_url + QStringLiteral("/oauth2/revoke")), Form({ { "client_id", client_id }, { "token", access_token } }),
			[](int, const QJsonObject &) {});
	SetLoggedOut(QString());
}

void TwitchClient::SetLoggedOut(const QString &error)
{
	reconnect_timer.stop();
	keepalive_timer.stop();
	if(socket)
	{
		socket->Close();
		socket->deleteLater();
		socket = nullptr;
	}
	if(!test_login)
		keystore::Remove(QString::fromLatin1(kTokensKey));
	access_token.clear();
	refresh_token.clear();
	user_login.clear();
	user_id.clear();
	settings->SetValue(QStringLiteral("twitch/login"), QString());
	settings->SetValue(QStringLiteral("twitch/user_id"), QString());
	events_state = QStringLiteral("off");
	subs.clear();
	channel_live = false;
	last_error = error;
	if(!error.isEmpty())
		qCWarning(chiakiGui).noquote() << "Twitch:" << error;
	emit changed();
}

// ------------------------------------------------------------------ the API

void TwitchClient::Helix(const QByteArray &method, const QString &path, const QJsonObject &body,
	std::function<void(int, const QJsonObject &)> done)
{
	QNetworkRequest request(QUrl(api_url + path));
	request.setRawHeader("Client-Id", client_id.toUtf8());
	request.setRawHeader("Authorization", "Bearer " + access_token.toUtf8());
	request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
	request.setTransferTimeout(20000);
	const QByteArray data = body.isEmpty() ? QByteArray() : QJsonDocument(body).toJson(QJsonDocument::Compact);
	QNetworkReply *reply = network->sendCustomRequest(request, method, data);
	connect(reply, &QNetworkReply::finished, this, [this, reply, method, path, body, done] {
		reply->deleteLater();
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
		if(status == 401 && !refresh_token.isEmpty())
		{
			// The access token ran out: a new one, and the same call once more
			Refresh([this, method, path, body, done](bool ok) {
				if(!ok)
				{
					done(401, QJsonObject());
					return;
				}
				Helix(method, path, body, [done](int s, const QJsonObject &r) { done(s, r); });
			});
			return;
		}
		done(status, o);
	});
}

// ------------------------------------------------------------------ events

void TwitchClient::Connect()
{
	if(access_token.isEmpty() || user_id.isEmpty())
		return;
	moving = false;
	if(socket)
	{
		socket->disconnect(this);
		socket->Close();
		socket->deleteLater();
	}
	socket = new WebSocketClient(this);
	connect(socket, &WebSocketClient::TextReceived, this, &TwitchClient::OnMessage);
	connect(socket, &WebSocketClient::Disconnected, this, [this](const QString &reason) {
		qCWarning(chiakiGui).noquote() << "Twitch: the event connection is gone:" << reason;
		ScheduleReconnect();
	});
	events_state = QStringLiteral("connecting");
	emit changed();
	socket->Open(QUrl(eventsub_url));
}

void TwitchClient::ScheduleReconnect()
{
	keepalive_timer.stop();
	if(access_token.isEmpty())
		return;
	events_state = QStringLiteral("error");
	emit changed();
	// 2, 4, 8, ... seconds, at most a minute
	const int wait = std::min(60, 2 << std::min(reconnect_tries, 5));
	reconnect_tries++;
	reconnect_timer.start(wait * 1000);
}

void TwitchClient::OnMessage(const QByteArray &utf8)
{
	const QJsonObject msg = QJsonDocument::fromJson(utf8).object();
	const QJsonObject metadata = msg.value("metadata").toObject();
	const QJsonObject payload = msg.value("payload").toObject();
	const QString kind = metadata.value("message_type").toString();
	const QJsonObject session = payload.value("session").toObject();
	const int keepalive = session.value("keepalive_timeout_seconds").toInt(0);
	if(keepalive > 0)
		keepalive_timer.setInterval((keepalive + 10) * 1000);
	if(keepalive_timer.interval() <= 0)
		keepalive_timer.setInterval(20000);
	keepalive_timer.start();

	if(kind == QLatin1String("session_welcome"))
	{
		reconnect_tries = 0;
		session_id = session.value("id").toString();
		events_state = QStringLiteral("on");
		last_error.clear();
		emit changed();
		// A welcome after Twitch's own "reconnect" keeps the subscriptions; any other needs them anew
		if(!moving)
			Subscribe(session_id);
		moving = false;
	}
	else if(kind == QLatin1String("session_reconnect"))
	{
		// Twitch moves us: the new address, the same subscriptions
		const QString url = session.value("reconnect_url").toString();
		if(url.startsWith(QLatin1String("wss://")) || url.startsWith(QLatin1String("ws://")))
		{
			moving = true;
			auto *next = new WebSocketClient(this);
			connect(next, &WebSocketClient::TextReceived, this, &TwitchClient::OnMessage);
			connect(next, &WebSocketClient::Disconnected, this, [this](const QString &reason) {
				qCWarning(chiakiGui).noquote() << "Twitch: the event connection is gone:" << reason;
				ScheduleReconnect();
			});
			if(socket)
			{
				socket->disconnect(this);
				socket->Close();
				socket->deleteLater();
			}
			socket = next;
			next->Open(QUrl(url));
		}
	}
	else if(kind == QLatin1String("notification"))
	{
		const QString type = metadata.value("subscription_type").toString();
		const QJsonObject event = payload.value("event").toObject();
		if(type == QLatin1String("stream.online") || type == QLatin1String("stream.offline"))
		{
			channel_live = type == QLatin1String("stream.online");
			emit changed();
		}
		emit Event(type, event);
	}
	else if(kind == QLatin1String("revocation"))
	{
		const QJsonObject sub = payload.value("subscription").toObject();
		SetSubscription(sub.value("type").toString(), false, sub.value("status").toString());
	}
}

void TwitchClient::Subscribe(const QString &session)
{
	subs.clear();
	for(const Subscription &s : kSubscriptions)
	{
		QJsonObject condition;
		const QString which = QString::fromLatin1(s.condition);
		if(which == QLatin1String("to"))
			condition["to_broadcaster_user_id"] = user_id;
		else
			condition["broadcaster_user_id"] = user_id;
		if(which == QLatin1String("bm"))
			condition["moderator_user_id"] = user_id;
		if(which == QLatin1String("bu"))
			condition["user_id"] = user_id;
		QJsonObject transport;
		transport["method"] = QStringLiteral("websocket");
		transport["session_id"] = session;
		QJsonObject body;
		body["type"] = QString::fromLatin1(s.type);
		body["version"] = QString::fromLatin1(s.version);
		body["condition"] = condition;
		body["transport"] = transport;
		const QString type = QString::fromLatin1(s.type);
		Helix("POST", QStringLiteral("/eventsub/subscriptions"), body, [this, type, session](int status, const QJsonObject &reply) {
			if(session != session_id)
				return;
			if(status == 202 || status == 200)
				SetSubscription(type, true, QString());
			else if(status == 409)
				SetSubscription(type, true, QString()); // already there
			else
				SetSubscription(type, false, reply.value("message").toString(QString::number(status)));
		});
	}
}

void TwitchClient::SetSubscription(const QString &type, bool ok, const QString &problem)
{
	for(QVariant &v : subs)
	{
		QVariantMap m = v.toMap();
		if(m.value("type").toString() == type)
		{
			m["ok"] = ok;
			m["problem"] = problem;
			v = m;
			emit changed();
			return;
		}
	}
	QVariantMap m;
	m["type"] = type;
	m["ok"] = ok;
	m["problem"] = problem;
	subs.append(m);
	if(!ok)
		qCInfo(chiakiGui).noquote() << "Twitch: no" << type << "events:" << problem;
	emit changed();
}
