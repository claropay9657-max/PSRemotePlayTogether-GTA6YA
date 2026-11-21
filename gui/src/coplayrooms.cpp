// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "coplayrooms.h"

#include <coplay/meet.h>
#include <coplay/protocol.h>

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QLocale>
#include <QPointer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <QVariantMap>

#include <memory>
#include <vector>

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

// ------------------------------------------------------------------ covers
//
// Sony's catalog says for a title id which game it belongs to, with its pictures,
// and answers without a login. The host's app asks it about the game on its
// console and puts the address of the picture on the list; whoever looks at the
// list loads the picture from Sony's image server, nothing else.

static const qint64 kMaxCatalogSize = 2 * 1024 * 1024;

bool CoPlayRooms::IsCover(const QString &address)
{
	// What the list itself takes (server/rooms/psrpt_rooms.php)
	static const QRegularExpression pattern(QStringLiteral("^https://image\\.api\\.playstation\\.com/[A-Za-z0-9._~/%-]{1,300}$"));
	return pattern.match(address).hasMatch();
}

/** The square key art ("MASTER") in an answer of the catalog, or empty. */
static QString CoverIn(const QByteArray &body)
{
	const QJsonArray concepts = QJsonDocument::fromJson(body).array();
	for(const QJsonValue &concept : concepts)
	{
		const QJsonArray images = concept.toObject().value(QStringLiteral("media")).toObject().value(QStringLiteral("images")).toArray();
		for(const QJsonValue &image : images)
		{
			const QJsonObject o = image.toObject();
			const QString url = o.value(QStringLiteral("url")).toString();
			if(o.value(QStringLiteral("type")).toString() == QLatin1String("MASTER") && CoPlayRooms::IsCover(url))
				return url;
		}
	}
	return QString();
}

namespace
{
struct CoverLookup
{
	QString title_id;
	/** Stores to ask, in turn: "de-DE", then "en-US". A game sold in one only has pictures there. */
	QStringList markets;
	std::vector<std::pair<QPointer<QObject>, std::function<void(const QString &)>>> waiting;
};
}

/** Title id → picture (empty: the catalog has none). Only answers that arrived. */
static QHash<QString, QString> covers_found;
static QHash<QString, std::shared_ptr<CoverLookup>> covers_asked;

static void FinishCover(const std::shared_ptr<CoverLookup> &lookup, const QString &cover, bool answered)
{
	if(answered)
		covers_found.insert(lookup->title_id, cover);
	covers_asked.remove(lookup->title_id);
	for(auto &w : lookup->waiting)
		if(w.first)
			w.second(cover);
}

static void AskCatalog(const std::shared_ptr<CoverLookup> &lookup)
{
	const QString market = lookup->markets.takeFirst();
	QUrl url(QStringLiteral("https://m.np.playstation.com/api/catalog/v2/titles/%1_00/concepts").arg(lookup->title_id));
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("country"), market.section(QLatin1Char('-'), 1));
	query.addQueryItem(QStringLiteral("language"), market);
	// Without all three the catalog leaves the pictures out
	query.addQueryItem(QStringLiteral("age"), QStringLiteral("99"));
	url.setQuery(query);
	QNetworkReply *reply = Network()->get(Request(url));
	QObject::connect(reply, &QNetworkReply::finished, reply, [reply, lookup]() {
		reply->deleteLater();
		const bool answered = reply->error() == QNetworkReply::NoError;
		QString cover;
		if(answered)
		{
			const QByteArray body = reply->read(kMaxCatalogSize + 1);
			if(body.size() <= kMaxCatalogSize)
				cover = CoverIn(body);
		}
		if(cover.isEmpty() && !lookup->markets.isEmpty())
		{
			AskCatalog(lookup);
			return;
		}
		FinishCover(lookup, cover, answered);
	});
}

void CoPlayRooms::FindCover(const QString &title_id, QObject *context, std::function<void(const QString &)> done)
{
	static const QRegularExpression pattern(QStringLiteral("^[A-Z]{4}[0-9]{5}$"));
	if(!pattern.match(title_id).hasMatch() || covers_found.contains(title_id))
	{
		const QString cover = covers_found.value(title_id);
		QTimer::singleShot(0, context, [done, cover]() { done(cover); });
		return;
	}
	if(auto asked = covers_asked.value(title_id))
	{
		asked->waiting.emplace_back(QPointer<QObject>(context), std::move(done));
		return;
	}
	auto lookup = std::make_shared<CoverLookup>();
	lookup->title_id = title_id;
	// The store of the host's own country first, in its language; then the American one
	const QString own = QLocale::system().name().replace(QLatin1Char('_'), QLatin1Char('-'));
	static const QRegularExpression market(QStringLiteral("^[a-z]{2}-[A-Z]{2}$"));
	if(market.match(own).hasMatch() && own != QLatin1String("en-US"))
		lookup->markets.append(own);
	lookup->markets.append(QStringLiteral("en-US"));
	lookup->waiting.emplace_back(QPointer<QObject>(context), std::move(done));
	covers_asked.insert(title_id, lookup);
	AskCatalog(lookup);
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
	connect(reply, &QNetworkReply::finished, this, [this, reply, mine, address]() {
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
			m[QStringLiteral("cover")] = CoPlayRooms::IsCover(cover) ? cover : QString();
			// A picture of the game as it is now, which the host sent to the list: only from the
			// list itself, its address names the room and when the picture was taken
			const QString id = m.value(QStringLiteral("id")).toString();
			const qint64 taken = static_cast<qint64>(r.value(QStringLiteral("preview")).toDouble(0));
			static const QRegularExpression room_id(QStringLiteral("^[0-9a-f]{16}$"));
			QString picture;
			if(taken > 0 && room_id.match(id).hasMatch())
			{
				QUrl picture_url(address);
				QUrlQuery picture_query(picture_url);
				picture_query.addQueryItem(QStringLiteral("v"), QStringLiteral("1"));
				picture_query.addQueryItem(QStringLiteral("preview"), id);
				picture_query.addQueryItem(QStringLiteral("t"), QString::number(taken));
				picture_url.setQuery(picture_query);
				picture = picture_url.toString(QUrl::FullyEncoded);
			}
			m[QStringLiteral("picture")] = picture;
			// Who plays, as initials only: one letter (or "?") for each player, the host first
			QStringList initials;
			for(uint code : r.value(QStringLiteral("initials")).toString().toUcs4())
			{
				if(initials.size() >= 9)
					break;
				const char32_t c = static_cast<char32_t>(code);
				initials.append(QChar::isLetterOrNumber(c) ? QString::fromUcs4(&c, 1) : QStringLiteral("?"));
			}
			m[QStringLiteral("initials")] = initials;
			// The players' pictures where they have one, served by the list (h= names the picture)
			QStringList faces;
			static const QRegularExpression face_name(QStringLiteral("^[0-9a-f]{16}$"));
			for(const QJsonValue &face : r.value(QStringLiteral("faces")).toArray())
			{
				if(faces.size() >= initials.size())
					break;
				const QString name = face.toString();
				if(!face_name.match(name).hasMatch() || !room_id.match(id).hasMatch())
				{
					faces.append(QString());
					continue;
				}
				QUrl face_url(address);
				QUrlQuery face_query(face_url);
				face_query.addQueryItem(QStringLiteral("v"), QStringLiteral("1"));
				face_query.addQueryItem(QStringLiteral("face"), id);
				face_query.addQueryItem(QStringLiteral("h"), name);
				face_url.setQuery(face_query);
				faces.append(face_url.toString(QUrl::FullyEncoded));
			}
			while(faces.size() < initials.size())
				faces.append(QString());
			m[QStringLiteral("faces")] = faces;
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
	connect(&picture_timer, &QTimer::timeout, this, &CoPlayRoomAnnouncer::PictureWanted);
	// After the room's announcement, and several changes in a row as one
	faces_soon.setSingleShot(true);
	faces_soon.setInterval(1000);
	connect(&faces_soon, &QTimer::timeout, this, &CoPlayRoomAnnouncer::SendFaces);
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
	UpdatePictureTimer();
}

void CoPlayRoomAnnouncer::SetPictures(bool on)
{
	pictures = on;
	UpdatePictureTimer();
}

void CoPlayRoomAnnouncer::UpdatePictureTimer()
{
	const bool wanted = pictures && listed && announced && picture_ms > 0;
	if(!wanted)
	{
		picture_timer.stop();
		picture_asked = false;
		return;
	}
	const int interval = qBound(15000, picture_ms, 600000);
	if(picture_timer.isActive() && picture_timer.interval() == interval)
		return;
	picture_timer.start(interval);
	// The first one right away, so that the room does not wait in the list without one
	if(!picture_asked)
	{
		picture_asked = true;
		QTimer::singleShot(1500, this, [this]() {
			if(picture_timer.isActive())
				emit PictureWanted();
		});
	}
}

void CoPlayRoomAnnouncer::SetFaces(const QList<QByteArray> &list)
{
	if(list == faces && faces_sent)
		return;
	faces = list;
	faces_sent = false;
	if(listed && announced)
		faces_soon.start();
}

void CoPlayRoomAnnouncer::SendFaces()
{
	if(!listed || !announced || url.isEmpty() || faces_sent)
		return;
	QStringList parts;
	for(const QByteArray &face : faces)
		parts.append(face.isEmpty() || face.size() > 8 * 1024 ? QString()
			: QString::fromLatin1(face.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
	// Nobody with a picture: nothing to send, the list shows initials
	bool any = false;
	for(const QString &part : parts)
		any = any || !part.isEmpty();
	faces_sent = true;
	if(!any && parts.size() > 0 && !faces_ever)
		return;
	faces_ever = faces_ever || any;
	QUrlQuery form;
	form.addQueryItem(QStringLiteral("v"), QStringLiteral("1"));
	form.addQueryItem(QStringLiteral("action"), QStringLiteral("faces"));
	form.addQueryItem(QStringLiteral("id"), id);
	form.addQueryItem(QStringLiteral("token"), token);
	form.addQueryItem(QStringLiteral("faces"), parts.join(QLatin1Char(',')));
	QNetworkRequest request = Request(QUrl(url));
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
	QNetworkReply *reply = Network()->post(request, form.query(QUrl::FullyEncoded).toUtf8());
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		reply->deleteLater();
		const QJsonObject answer = QJsonDocument::fromJson(reply->read(64 * 1024)).object();
		// Not taken (the room was not there yet, for one): again with the next announcement
		if(reply->error() != QNetworkReply::NoError || !answer.value(QStringLiteral("ok")).toBool())
			faces_sent = false;
	});
}

void CoPlayRoomAnnouncer::SendPicture(const QByteArray &jpeg)
{
	if(!pictures || !listed || !announced || picture_ms <= 0 || url.isEmpty() || jpeg.isEmpty() || jpeg.size() > 48 * 1024)
		return;
	QUrlQuery form;
	form.addQueryItem(QStringLiteral("v"), QStringLiteral("1"));
	form.addQueryItem(QStringLiteral("action"), QStringLiteral("preview"));
	form.addQueryItem(QStringLiteral("id"), id);
	form.addQueryItem(QStringLiteral("token"), token);
	form.addQueryItem(QStringLiteral("image"), QString::fromLatin1(jpeg.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
	QNetworkRequest request = Request(QUrl(url));
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
	QNetworkReply *reply = Network()->post(request, form.query(QUrl::FullyEncoded).toUtf8());
	// A picture the list did not take is simply missing until the next one
	connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
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
	form.addQueryItem(QStringLiteral("initials"), room.initials);
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
			// A list that does not say anything about pictures does not know them
			picture_ms = answer.value(QStringLiteral("preview")).toInt(0);
			UpdatePictureTimer();
			if(!faces_sent && answer.contains(QStringLiteral("preview")) && !faces_soon.isActive())
				faces_soon.start();
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
	picture_timer.stop();
	picture_asked = false;
	// The list forgets the room's pictures with the room
	faces_soon.stop();
	faces_sent = false;
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
