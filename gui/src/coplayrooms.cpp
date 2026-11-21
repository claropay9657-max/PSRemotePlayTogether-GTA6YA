// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "coplayrooms.h"

#include <coplay/meet.h>
#include <coplay/protocol.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QUrl>
#include <QUrlQuery>
#include <QVariantMap>

// The list is small; whatever is much longer is something else
static const qint64 kMaxListSize = 1024 * 1024;
static const int kDefaultPollMs = 15000;
static const int kRequestTimeoutMs = 10000;

/**
 * One for the whole app, owned by the application: a request to take a room off the
 * list still goes out when the session that made it is gone.
 */
static QNetworkAccessManager *Network()
{
	static QNetworkAccessManager *network = nullptr;
	if(!network)
		network = new QNetworkAccessManager(QCoreApplication::instance());
	return network;
}

static QNetworkRequest Request(const QUrl &url)
{
	QNetworkRequest request(url);
	request.setHeader(QNetworkRequest::UserAgentHeader,
		QStringLiteral("PSRemotePlayTogether/%1").arg(QCoreApplication::applicationVersion()));
	request.setTransferTimeout(kRequestTimeoutMs);
	return request;
}

QString CoPlayRooms::DefaultUrl()
{
	const QString replaced = qEnvironmentVariable("PSRPT_ROOMS_URL").trimmed();
	if(!replaced.isEmpty())
	{
		// For trying things out on one machine: plain HTTP is fine here
		QUrl url(replaced);
		if(url.isValid() && (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http")))
			return replaced;
	}
	// Next to the meeting point, on the same server
	QString meet = QString::fromLatin1(coplay::kDefaultMeetUrl);
	return meet.replace(QStringLiteral("psrpt_meet.php"), QStringLiteral("psrpt_rooms.php"));
}

QString CoPlayRooms::UrlFromSetting(const QString &setting)
{
	QString text = setting.trimmed();
	if(text.isEmpty())
		return DefaultUrl();
	if(text.compare(QLatin1String("off"), Qt::CaseInsensitive) == 0)
		return QString();
	if(!text.contains(QLatin1String("://")))
		text = QStringLiteral("https://") + text;
	QUrl url(text);
	if(!url.isValid() || url.host().isEmpty())
		return QString();
	const QString host = url.host();
	const bool local = host == QLatin1String("127.0.0.1") || host == QLatin1String("localhost") || host == QLatin1String("::1");
	// A list that anybody on the way could change is worse than none, except on this computer
	if(url.scheme() == QLatin1String("https") || (url.scheme() == QLatin1String("http") && local))
		return url.toString();
	return QString();
}

QVariantList CoPlayRooms::Languages()
{
	// The name each language has for itself, so that it is found by those who speak it
	static const char *const list[][2] = {
		{ "en", "English" }, { "de", "Deutsch" }, { "fr", "Français" }, { "es", "Español" },
		{ "it", "Italiano" }, { "pt", "Português" }, { "nl", "Nederlands" }, { "pl", "Polski" },
		{ "tr", "Türkçe" }, { "ru", "Русский" }, { "uk", "Українська" }, { "cs", "Čeština" },
		{ "sv", "Svenska" }, { "da", "Dansk" }, { "no", "Norsk" }, { "fi", "Suomi" },
		{ "hu", "Magyar" }, { "ro", "Română" }, { "el", "Ελληνικά" }, { "ar", "العربية" },
		{ "hi", "हिन्दी" }, { "ja", "日本語" }, { "ko", "한국어" }, { "zh", "中文" },
	};
	QVariantList out;
	for(const auto &entry : list)
		out.append(QVariant(QVariantList{ QString::fromLatin1(entry[0]), QString::fromUtf8(entry[1]) }));
	return out;
}

// ------------------------------------------------------------------ the list

CoPlayRoomList *CoPlayRoomList::Instance()
{
	static CoPlayRoomList *instance = nullptr;
	if(!instance)
		instance = new CoPlayRoomList(QCoreApplication::instance());
	return instance;
}

CoPlayRoomList::CoPlayRoomList(QObject *parent)
	: QObject(parent)
{
}

void CoPlayRoomList::Refresh(const QString &setting)
{
	const QString address = CoPlayRooms::UrlFromSetting(setting);
	const quint64 mine = ++generation;
	if(address.isEmpty())
	{
		rooms.clear();
		state = setting.trimmed().compare(QLatin1String("off"), Qt::CaseInsensitive) == 0 ? Off : Failed;
		error = state == Off ? QString() : tr("The address of the public rooms is not valid.");
		emit Changed();
		return;
	}
	QUrl url(address);
	QUrlQuery query(url);
	query.addQueryItem(QStringLiteral("v"), QStringLiteral("1"));
	url.setQuery(query);
	state = Loading;
	emit Changed();

	QNetworkReply *reply = Network()->get(Request(url));
	connect(reply, &QNetworkReply::finished, this, [this, reply, mine]() {
		reply->deleteLater();
		if(mine != generation)
			return; // a newer request is on its way
		const QByteArray body = reply->read(kMaxListSize);
		const QJsonObject answer = QJsonDocument::fromJson(body).object();
		if(reply->error() != QNetworkReply::NoError || !answer.value(QStringLiteral("ok")).toBool())
		{
			state = Failed;
			const QString said = answer.value(QStringLiteral("error")).toString();
			error = !said.isEmpty() ? said
				: reply->error() != QNetworkReply::NoError ? reply->errorString()
				: tr("The list of public rooms gave an answer the app does not understand.");
			emit Changed();
			return;
		}
		QVariantList list;
		for(const QJsonValue &value : answer.value(QStringLiteral("rooms")).toArray())
		{
			const QJsonObject r = value.toObject();
			const QString invite = r.value(QStringLiteral("invite")).toString();
			if(!invite.startsWith(QLatin1String("psrpt://")))
				continue;
			QVariantMap m;
			m[QStringLiteral("id")] = r.value(QStringLiteral("id")).toString();
			m[QStringLiteral("title")] = r.value(QStringLiteral("title")).toString().left(60);
			m[QStringLiteral("lang")] = r.value(QStringLiteral("lang")).toString().left(2);
			m[QStringLiteral("game")] = r.value(QStringLiteral("game")).toString().left(80);
			m[QStringLiteral("host")] = r.value(QStringLiteral("host")).toString().left(32);
			const int players = r.value(QStringLiteral("players")).toInt();
			const int max = r.value(QStringLiteral("max")).toInt();
			m[QStringLiteral("players")] = players;
			m[QStringLiteral("max")] = max;
			m[QStringLiteral("full")] = players >= max;
			m[QStringLiteral("policy")] = r.value(QStringLiteral("policy")).toString();
			m[QStringLiteral("control")] = r.value(QStringLiteral("control")).toString(QStringLiteral("all"));
			m[QStringLiteral("web")] = r.value(QStringLiteral("web")).toBool();
			m[QStringLiteral("invite")] = invite;
			const int proto = r.value(QStringLiteral("proto")).toInt();
			m[QStringLiteral("proto")] = proto;
			m[QStringLiteral("compatible")] = proto == coplay::kProtocolVersion;
			m[QStringLiteral("since")] = r.value(QStringLiteral("since")).toInt();
			m[QStringLiteral("titleId")] = r.value(QStringLiteral("title_id")).toString().left(9);
			// Only a picture on Sony's image server, as the list itself demands
			const QString cover = r.value(QStringLiteral("cover")).toString();
			m[QStringLiteral("cover")] = cover.startsWith(QLatin1String("https://image.api.playstation.com/")) ? cover : QString();
			list.append(m);
		}
		rooms = list;
		state = Loaded;
		error.clear();
		emit Changed();
	});
}

// ------------------------------------------------------------------ a host's room

static QString RandomHex(int bytes)
{
	QByteArray raw(bytes, 0);
	for(int i = 0; i < bytes; i++)
		raw[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
	return QString::fromLatin1(raw.toHex());
}

CoPlayRoomAnnouncer::CoPlayRoomAnnouncer(const QString &url, QObject *parent)
	: QObject(parent),
	url(url),
	id(RandomHex(8)),
	token(RandomHex(16))
{
	timer.setInterval(kDefaultPollMs);
	connect(&timer, &QTimer::timeout, this, &CoPlayRoomAnnouncer::Announce);
	// Several changes in a row (a few players joining) make one announcement
	soon.setSingleShot(true);
	soon.setInterval(400);
	connect(&soon, &QTimer::timeout, this, &CoPlayRoomAnnouncer::Announce);
}

CoPlayRoomAnnouncer::~CoPlayRoomAnnouncer()
{
	Remove();
}

void CoPlayRoomAnnouncer::Update(const Room &r)
{
	if(have_room && r == room)
		return;
	room = r;
	have_room = true;
	if(listed)
		soon.start();
}

void CoPlayRoomAnnouncer::SetListed(bool on)
{
	if(listed == on)
		return;
	listed = on;
	if(on)
		soon.start();
	else
	{
		soon.stop();
		Remove();
	}
}

void CoPlayRoomAnnouncer::Announce()
{
	if(!have_room || !listed || url.isEmpty())
		return;
	QUrlQuery form;
	form.addQueryItem(QStringLiteral("v"), QStringLiteral("1"));
	form.addQueryItem(QStringLiteral("action"), QStringLiteral("announce"));
	form.addQueryItem(QStringLiteral("id"), id);
	form.addQueryItem(QStringLiteral("token"), token);
	form.addQueryItem(QStringLiteral("title"), room.title);
	form.addQueryItem(QStringLiteral("lang"), room.lang);
	form.addQueryItem(QStringLiteral("game"), room.game);
	form.addQueryItem(QStringLiteral("host"), room.host);
	form.addQueryItem(QStringLiteral("players"), QString::number(room.players));
	form.addQueryItem(QStringLiteral("max"), QString::number(room.max));
	form.addQueryItem(QStringLiteral("policy"), room.policy);
	form.addQueryItem(QStringLiteral("control"), room.control);
	form.addQueryItem(QStringLiteral("web"), room.web ? QStringLiteral("1") : QStringLiteral("0"));
	form.addQueryItem(QStringLiteral("invite"), room.invite);
	form.addQueryItem(QStringLiteral("proto"), QString::number(room.proto));
	form.addQueryItem(QStringLiteral("title_id"), room.title_id);
	form.addQueryItem(QStringLiteral("cover"), room.cover);
	QNetworkRequest request = Request(QUrl(url));
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
	// Characters such as "+" and "&" in a title must arrive as they are
	QByteArray body = form.query(QUrl::FullyEncoded).toUtf8().replace("+", "%2B");
	QNetworkReply *reply = Network()->post(request, body);
	announced = true;
	if(!timer.isActive())
		timer.start();
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		reply->deleteLater();
		const QJsonObject answer = QJsonDocument::fromJson(reply->read(64 * 1024)).object();
		QString now;
		if(reply->error() != QNetworkReply::NoError && answer.isEmpty())
			now = reply->errorString();
		else if(!answer.value(QStringLiteral("ok")).toBool())
			now = answer.value(QStringLiteral("error")).toString(tr("The list of public rooms did not take the room."));
		else
		{
			const int poll = answer.value(QStringLiteral("poll")).toInt(kDefaultPollMs);
			timer.setInterval(qBound(5000, poll, 60000));
		}
		if(now != problem)
		{
			problem = now;
			emit Changed();
		}
	});
}

void CoPlayRoomAnnouncer::Remove()
{
	timer.stop();
	if(!announced || url.isEmpty())
		return;
	announced = false;
	QUrlQuery form;
	form.addQueryItem(QStringLiteral("v"), QStringLiteral("1"));
	form.addQueryItem(QStringLiteral("action"), QStringLiteral("remove"));
	form.addQueryItem(QStringLiteral("id"), id);
	form.addQueryItem(QStringLiteral("token"), token);
	QNetworkRequest request = Request(QUrl(url));
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
	QNetworkReply *reply = Network()->post(request, form.query(QUrl::FullyEncoded).toUtf8());
	// Nobody waits for the answer; the network object outlives this one
	connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
}
