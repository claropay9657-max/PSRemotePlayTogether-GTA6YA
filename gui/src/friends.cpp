// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "friends.h"

#include "chattext.h"
#include "coplaylinkpreview.h"
#include "keystore.h"

#include <coplay/crypto.h>
#include <coplay/protocol.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <cstring>
#include <memory>

namespace fc = coplay::friends;

// What goes to and comes from the server, and what became of each note; never what is in them.
// QT_LOGGING_RULES="psrpt.friends.debug=true" shows it.
Q_LOGGING_CATEGORY(lcFriends, "psrpt.friends", QtInfoMsg)

// ------------------------------------------------------------------ small helpers

namespace {

const char kFileHead[] = "PSRPT-FRIENDS 1\n";
const char kDefaultServer[] = "https://gamernova.net/api/psrpt_friends.php";
const char kDefaultPage[] = "https://crypto90.github.io/PSRemotePlayTogether/";
/** One-time prekeys made at a time, and how few may be left on the server before more go up. */
const int kPrekeyBatch = 50;
const int kPrekeyLow = 20;
/** A status is fresh this long; the app renews its own every minute. */
const qint64 kStatusFreshMs = 3 * 60 * 1000;
const qint64 kStatusPutMs = 60 * 1000;
const qint64 kDayMs = 24ll * 3600 * 1000;
/** Signed prekeys are renewed after a week; the one before stays usable for 30 days. */
const qint64 kSpkRenewMs = 7 * kDayMs;
const int kMessagesKept = 500;

qint64 NowMs()
{
	return QDateTime::currentMSecsSinceEpoch();
}

QString Hex(const uint8_t *p, size_t n)
{
	return QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(p), static_cast<int>(n)).toHex());
}

template<size_t N> QString Hex(const std::array<uint8_t, N> &a)
{
	return Hex(a.data(), N);
}

QString Hex(const fc::Bytes &b)
{
	return Hex(b.data(), b.size());
}

template<size_t N> bool Unhex(const QString &text, std::array<uint8_t, N> &out)
{
	const QByteArray raw = QByteArray::fromHex(text.toLatin1());
	if(raw.size() != static_cast<int>(N))
		return false;
	std::memcpy(out.data(), raw.constData(), N);
	return true;
}

QString B64(const uint8_t *p, size_t n)
{
	return QString::fromStdString(coplay::Base64UrlEncode(p, n));
}

QString B64(const fc::Bytes &b)
{
	return B64(b.data(), b.size());
}

template<size_t N> QString B64(const std::array<uint8_t, N> &a)
{
	return B64(a.data(), N);
}

bool UnB64(const QString &text, fc::Bytes &out)
{
	return coplay::Base64UrlDecode(text.toStdString(), out);
}

template<size_t N> bool UnB64(const QString &text, std::array<uint8_t, N> &out)
{
	fc::Bytes b;
	if(!UnB64(text, b) || b.size() != N)
		return false;
	std::memcpy(out.data(), b.data(), N);
	return true;
}

fc::Bytes ToBytes(const QByteArray &a)
{
	return fc::Bytes(a.begin(), a.end());
}

QByteArray ToQ(const fc::Bytes &b)
{
	return QByteArray(reinterpret_cast<const char *>(b.data()), static_cast<int>(b.size()));
}

QString RandomId()
{
	std::array<uint8_t, 16> id{};
	coplay::RandomBytes(id.data(), id.size());
	return B64(id);
}

/** A key pair from the stored private scalar. */
bool PairFrom(const QString &hex_d, fc::KeyPair &out)
{
	fc::Key32 d{};
	return Unhex(hex_d, d) && fc::KeyPairFromPrivate(d, out);
}

QString SessionKey(const QString &fp, const fc::DeviceId &device)
{
	return fp + QLatin1Char(':') + Hex(device);
}

/** The conversations with one device, the newest first. */
QJsonArray SessionsOf(const QJsonObject &state, const QString &key)
{
	return state.value(QStringLiteral("sessions")).toObject().value(key).toObject().value(QStringLiteral("s")).toArray();
}

void StoreSessions(QJsonObject &state, const QString &key, QJsonArray list)
{
	// Older ones stay a while: both sides may have started one at the same moment
	while(list.size() > 4)
		list.removeLast();
	QJsonObject sessions = state.value(QStringLiteral("sessions")).toObject();
	if(list.isEmpty())
		sessions.remove(key);
	else
	{
		QJsonObject entry = sessions.value(key).toObject();
		entry[QStringLiteral("s")] = list;
		sessions[key] = entry;
	}
	state[QStringLiteral("sessions")] = sessions;
}

/** The ephemeral keys a device started conversations with: a first message comes only once. */
bool SeenEk(const QJsonObject &state, const QString &key, const QString &ek)
{
	return state.value(QStringLiteral("sessions")).toObject().value(key).toObject().value(QStringLiteral("ek")).toArray().contains(ek);
}

void RememberEk(QJsonObject &state, const QString &key, const QString &ek)
{
	QJsonObject sessions = state.value(QStringLiteral("sessions")).toObject();
	QJsonObject entry = sessions.value(key).toObject();
	QJsonArray eks = entry.value(QStringLiteral("ek")).toArray();
	if(eks.contains(ek))
		return;
	eks.append(ek);
	while(eks.size() > 64)
		eks.removeFirst();
	entry[QStringLiteral("ek")] = eks;
	sessions[key] = entry;
	state[QStringLiteral("sessions")] = sessions;
}

bool OwnDevice(const QJsonObject &state, fc::DeviceId &out)
{
	return Unhex(state.value(QStringLiteral("device")).toObject().value(QStringLiteral("id")).toString(), out);
}

QString OwnFp(const QJsonObject &state)
{
	return state.value(QStringLiteral("fp")).toString();
}

/** Keeps a card fetched for fp if it is valid; an older one than the card kept does not win. */
bool KeepCard(QJsonObject &state, const QString &fp, const QString &b64, fc::Card &card)
{
	fc::Bytes bytes;
	if(!UnB64(b64, bytes) || !fc::DecodeCard(bytes, card) || B64(card.fp) != fp)
		return false;
	QJsonObject cards = state.value(QStringLiteral("cards")).toObject();
	fc::Bytes old_bytes;
	fc::Card old;
	if(UnB64(cards.value(fp).toString(), old_bytes) && fc::DecodeCard(old_bytes, old) && old.seq > card.seq)
	{
		card = old;
		return true;
	}
	cards[fp] = b64;
	state[QStringLiteral("cards")] = cards;
	return true;
}

/** A friend's name and picture from their card, opened with their link key. */
void RefreshProfile(QJsonObject &state, const QString &fp, const fc::Card &card)
{
	QJsonObject friends = state.value(QStringLiteral("friends")).toObject();
	QJsonObject f = friends.value(fp).toObject();
	fc::Id16 lk{};
	std::string json;
	if(f.isEmpty() || !Unhex(f.value(QStringLiteral("lk")).toString(), lk) || !fc::OpenProfile(lk, card.fp, card.profile, json))
		return;
	const QJsonObject profile = QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();
	f[QStringLiteral("name")] = profile.value(QStringLiteral("name")).toString().left(32);
	const QString avatar = profile.value(QStringLiteral("avatar")).toString();
	f[QStringLiteral("avatar")] = avatar.size() <= 16000 ? avatar : QString();
	friends[fp] = f;
	state[QStringLiteral("friends")] = friends;
}

/** A message with its link preview stays under this much JSON: sealed, it fits a note (8 KB). */
const int kMaxMessageJson = 7000;
/** The preview's picture: smaller than this it is not worth it, never bigger than that. */
const int kMinPreviewImage = 1200;
const int kMaxPreviewImage = 5600;
const int kMaxPreviewSide = 800;

/** Text cut to at most this many bytes of UTF-8, on one line. */
QString ClipUtf8(const QString &text, size_t max)
{
	return QString::fromStdString(coplay::TruncateUtf8(text.simplified().toStdString(), max));
}

/** A quote of a message: the start of its text (a card's or plan's text too), on one line. */
const size_t kMaxQuoteBytes = 160;
const int kMaxIdLength = 64;
/** Deletes for messages not here yet, kept per chat. */
const int kGoneKept = 100;
/** Muted "until turned back on": far ahead. */
const double kMutedForGood = 9.0e15;

/** What a reply carries (reply, quote) as a friend sent it, checked, into m; nothing if it is not one. */
void TakeReply(const QJsonObject &p, QJsonObject &m)
{
	m.remove(QStringLiteral("reply"));
	m.remove(QStringLiteral("quote"));
	const QJsonValue reply = p.value(QStringLiteral("reply"));
	if(!reply.isString() || reply.toString().isEmpty() || reply.toString().size() > kMaxIdLength)
		return;
	m[QStringLiteral("reply")] = reply.toString();
	const QJsonObject quote = p.value(QStringLiteral("quote")).toObject();
	const QJsonValue from = quote.value(QStringLiteral("from"));
	const QJsonValue text = quote.value(QStringLiteral("text"));
	if(from.isString() && !from.toString().isEmpty() && from.toString().size() <= kMaxIdLength && text.isString())
		m[QStringLiteral("quote")] = QJsonObject{ { QStringLiteral("from"), from.toString() },
			{ QStringLiteral("text"), ClipUtf8(text.toString(), kMaxQuoteBytes) } };
}

/**
 * A link preview as a friend sent it (made by the writer's app, as in a session's chat): only
 * for a link that is in the message itself, short texts, and a small JPEG whose size is read
 * from its header before it is decoded. Empty if there is nothing to show but the link.
 */
QJsonObject CleanEmbed(const QJsonObject &e, const QString &text)
{
	QJsonObject out;
	const QString url = e.value(QStringLiteral("url")).toString();
	const QUrl parsed(url, QUrl::StrictMode);
	if(url.isEmpty() || static_cast<size_t>(url.toUtf8().size()) > coplay::kMaxChatUrl || !parsed.isValid() || parsed.host().isEmpty()
		|| (parsed.scheme() != QLatin1String("https") && parsed.scheme() != QLatin1String("http")))
		return out;
	// The link the message shows, as typed or as the writer's app wrote it (percent-encoded):
	// a preview never leads anywhere else
	const QUrl first = CoPlayLinkPreview::FirstLink(text);
	if(!text.contains(url) && !(first.isValid() && first.toString(QUrl::FullyEncoded) == url))
		return out;
	out[QStringLiteral("url")] = url;
	const std::pair<const char *, size_t> fields[] = { { "site", coplay::kMaxChatSite }, { "title", coplay::kMaxChatTitle },
		{ "description", coplay::kMaxChatDescription } };
	for(const auto &field : fields)
	{
		const QString value = ClipUtf8(e.value(QLatin1String(field.first)).toString(), field.second);
		if(!value.isEmpty())
			out[QLatin1String(field.first)] = value;
	}
	const QString image = e.value(QStringLiteral("image")).toString();
	if(!image.isEmpty() && image.size() <= (kMaxPreviewImage + 2) / 3 * 4)
	{
		QByteArray jpeg = QByteArray::fromBase64(image.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
		if(jpeg.size() > 3 && jpeg.size() <= kMaxPreviewImage && jpeg.startsWith("\xff\xd8\xff"))
		{
			QBuffer buffer(&jpeg);
			buffer.open(QIODevice::ReadOnly);
			QImageReader reader(&buffer, "JPEG");
			const QSize size = reader.size();
			if(size.isValid() && size.width() <= kMaxPreviewSide && size.height() <= kMaxPreviewSide && !reader.read().isNull())
				out[QStringLiteral("image")] = QString::fromLatin1(jpeg.toBase64());
		}
	}
	if(!out.contains(QStringLiteral("title")) && !out.contains(QStringLiteral("description")) && !out.contains(QStringLiteral("image")))
		return QJsonObject();
	return out;
}

/** The preview for a message of one's own: what fits a note beside the message, the picture as big as there is room for. */
QJsonObject MakeEmbed(const coplay::ChatEmbed &embed, const QByteArray &image, const QJsonObject &payload)
{
	if(!embed.Valid())
		return QJsonObject();
	QJsonObject e;
	e[QStringLiteral("url")] = QString::fromStdString(embed.url);
	e[QStringLiteral("site")] = QString::fromStdString(embed.site);
	e[QStringLiteral("title")] = QString::fromStdString(embed.title);
	e[QStringLiteral("description")] = QString::fromStdString(embed.description);
	const QString text = payload.value(QStringLiteral("text")).toString();
	// The picture counts as there: a preview of a picture alone has no texts
	e[QStringLiteral("image")] = QString();
	QJsonObject texts = CleanEmbed(e, text);
	if(texts.isEmpty() && !image.isEmpty())
	{
		// Only a picture: the link and the picture, if it fits
		texts = QJsonObject{ { QStringLiteral("url"), e.value(QStringLiteral("url")) } };
	}
	if(texts.isEmpty())
		return QJsonObject();
	QJsonObject with = payload;
	auto size = [&with](const QJsonObject &candidate) {
		with[QStringLiteral("embed")] = candidate;
		return static_cast<int>(QJsonDocument(with).toJson(QJsonDocument::Compact).size());
	};
	// A long message leaves no room for the texts of a preview either
	if(size(texts) > kMaxMessageJson)
		return QJsonObject();
	// base64 takes 4 bytes for 3, and the field's name and quotes a few more
	const int room = std::min(kMaxPreviewImage, (kMaxMessageJson - size(texts) - 16) * 3 / 4);
	QJsonObject out = texts;
	if(!image.isEmpty() && room >= kMinPreviewImage)
	{
		const QByteArray small = CoPlayLinkPreview::Thumbnail(image, static_cast<size_t>(room));
		if(!small.isEmpty())
		{
			QJsonObject pictured = texts;
			pictured[QStringLiteral("image")] = QString::fromLatin1(small.toBase64());
			pictured = CleanEmbed(pictured, text);
			if(pictured.contains(QStringLiteral("image")) && size(pictured) <= kMaxMessageJson)
				out = pictured;
		}
	}
	// Nothing but the link (no texts, the picture did not fit): no preview
	if(!out.contains(QStringLiteral("title")) && !out.contains(QStringLiteral("description")) && !out.contains(QStringLiteral("image")))
		return QJsonObject();
	return out;
}

/**
 * A session as a friend shows it (a join card in a chat, the room in a status): only what the
 * app knows to show, each checked. The picture only from Sony's image server, as in the public
 * rooms: any other address would tell whoever runs it that this computer looked.
 */
QJsonObject CleanRoom(const QJsonObject &r)
{
	static const QRegularExpression cover(QStringLiteral("^https://image\\.api\\.playstation\\.com/[A-Za-z0-9._~/%-]{1,300}$"));
	QJsonObject out;
	const QString invite = r.value(QStringLiteral("invite")).toString();
	if(invite.size() > 2000 || !(invite.startsWith(QStringLiteral("psrpt://")) || invite.startsWith(QStringLiteral("https://"))))
		return out;
	out[QStringLiteral("invite")] = invite;
	const QString web = r.value(QStringLiteral("web")).toString();
	if(web.size() <= 2000 && web.startsWith(QStringLiteral("https://")))
		out[QStringLiteral("web")] = web;
	for(const char *name : { "title", "host", "game" })
	{
		const QString text = r.value(QLatin1String(name)).toString().simplified().left(80);
		if(!text.isEmpty())
			out[QLatin1String(name)] = text;
	}
	if(cover.match(r.value(QStringLiteral("cover")).toString()).hasMatch())
		out[QStringLiteral("cover")] = r.value(QStringLiteral("cover")).toString();
	const QString console = r.value(QStringLiteral("console")).toString();
	if(console == QLatin1String("ps5") || console == QLatin1String("ps4") || console == QLatin1String("pc") || console == QLatin1String("xbox"))
		out[QStringLiteral("console")] = console;
	const QString policy = r.value(QStringLiteral("policy")).toString();
	if(policy == QLatin1String("ask") || policy == QLatin1String("watch") || policy == QLatin1String("open"))
		out[QStringLiteral("policy")] = policy;
	const QString control = r.value(QStringLiteral("control")).toString();
	if(control == QLatin1String("all") || control == QLatin1String("turns") || control == QLatin1String("host") || control == QLatin1String("each"))
		out[QStringLiteral("control")] = control;
	const int max = r.value(QStringLiteral("max")).toInt();
	const int players = r.value(QStringLiteral("players")).toInt();
	if(max >= 2 && max <= 16 && players >= 1 && players <= max)
	{
		out[QStringLiteral("players")] = players;
		out[QStringLiteral("max")] = max;
	}
	if(r.value(QStringLiteral("talk")).toBool())
		out[QStringLiteral("talk")] = true;
	return out;
}

/** A plan's answers as sent: one per time, 0 no, 1 maybe, 2 yes. */
QJsonArray CleanAnswers(const QJsonValue &v, int times)
{
	QJsonArray out;
	const QJsonArray in = v.toArray();
	for(int i = 0; i < times; i++)
		out.append(std::clamp(i < in.size() ? in[i].toInt() : 0, 0, 2));
	return out;
}

/** A plan as sent: a title and 1 to 8 times. */
QJsonObject CleanPlan(const QJsonObject &p)
{
	QJsonObject out;
	QJsonArray times;
	for(const QJsonValue &t : p.value(QStringLiteral("times")).toArray())
	{
		if(t.isDouble() && t.toDouble() > 0 && times.size() < 8)
			times.append(t.toDouble());
	}
	if(times.isEmpty())
		return out;
	out[QStringLiteral("title")] = p.value(QStringLiteral("title")).toString().simplified().left(80);
	out[QStringLiteral("times")] = times;
	return out;
}

QJsonObject GroupOf(const QJsonObject &state, const QString &gid)
{
	return state.value(QStringLiteral("groups")).toObject().value(gid).toObject();
}

void PutGroup(QJsonObject &state, const QString &gid, const QJsonObject &g)
{
	QJsonObject groups = state.value(QStringLiteral("groups")).toObject();
	groups[gid] = g;
	state[QStringLiteral("groups")] = groups;
}

/** Fingerprints from a list: each once, each a real one. */
QStringList Fps(const QJsonValue &v)
{
	QStringList out;
	for(const QJsonValue &x : v.toArray())
	{
		fc::Id16 fp{};
		if(x.isString() && UnB64(x.toString(), fp) && !out.contains(x.toString()))
			out.append(x.toString());
	}
	return out;
}

QJsonArray ToArray(const QStringList &list)
{
	QJsonArray out;
	for(const QString &s : list)
		out.append(s);
	return out;
}

QStringList Sorted(QStringList list)
{
	list.sort();
	return list;
}

/** Waits for several sends and answers once: true if every one went out. */
struct Fanout
{
	int pending = 1;
	bool ok = true;
	std::function<void(bool)> done;
	void Finish(bool result)
	{
		ok = ok && result;
		if(--pending == 0 && done)
			done(ok);
	}
};

} // namespace

// ------------------------------------------------------------------ setup

FriendsClient::FriendsClient(QObject *parent) : QObject(parent), network(new QNetworkAccessManager(this))
{
	server = QString::fromLatin1(kDefaultServer);
	poll_timer.setInterval(30000);
	connect(&poll_timer, &QTimer::timeout, this, &FriendsClient::Poll);
	status_timer.setInterval(static_cast<int>(kStatusPutMs));
	connect(&status_timer, &QTimer::timeout, this, &FriendsClient::PutStatus);
	link_timer.setInterval(1500);
	connect(&link_timer, &QTimer::timeout, this, &FriendsClient::PollLink);
}

FriendsClient::~FriendsClient() = default;

void FriendsClient::SetStorage(const QString &folder_path, const QString &name)
{
	folder = folder_path;
	secret_name = name;
	Load();
	if(enabled)
	{
		poll_timer.start();
		status_timer.start();
		QTimer::singleShot(0, this, &FriendsClient::Poll);
		QTimer::singleShot(0, this, &FriendsClient::PutStatus);
	}
}

void FriendsClient::SetServer(const QString &url)
{
	const QString trimmed = url.trimmed();
	const QString next = trimmed.isEmpty() ? QString::fromLatin1(kDefaultServer) : trimmed;
	if(next == server)
		return;
	server = next;
	if(enabled)
	{
		// Everything starts again on the other server: the card, the prekeys, the boxes
		state[QStringLiteral("server")] = ServerAddress();
		QJsonObject dev = state.value(QStringLiteral("device")).toObject();
		dev.remove(QStringLiteral("next_opk"));
		state[QStringLiteral("device")] = dev;
		Save();
		card_fetched.clear();
		statuses.clear();
		PublishCard();
	}
	emit changed();
}

QString FriendsClient::ServerAddress() const
{
	return server == QLatin1String(kDefaultServer) ? QString() : server;
}

QString FriendsClient::DefaultServer()
{
	return QString::fromLatin1(kDefaultServer);
}

void FriendsClient::SetProfile(const QString &name, const QByteArray &avatar_jpeg)
{
	if(name == profile_name && avatar_jpeg == profile_avatar)
		return;
	profile_name = name.left(32);
	// The card carries a small picture only (a whole request has to fit in 8 KB)
	profile_avatar = avatar_jpeg.size() <= 4 * 1024 ? avatar_jpeg : QByteArray();
	if(enabled)
		PublishCard();
}

void FriendsClient::SetActivity(const QString &text, const QVariantMap &r)
{
	const QVariantMap clean = r.isEmpty() ? QVariantMap() : CleanRoom(QJsonObject::fromVariantMap(r)).toVariantMap();
	if(text == activity && clean == room)
		return;
	activity = text;
	room = clean;
	if(enabled)
		PutStatus();
}

void FriendsClient::SetBackground(bool in_tray)
{
	background = in_tray;
	poll_timer.setInterval(in_tray ? 120000 : 30000);
}

// ------------------------------------------------------------------ the sealed state file

void FriendsClient::Load()
{
	state = QJsonObject();
	enabled = false;
	QFile file(QDir(folder).filePath(QStringLiteral("friends.dat")));
	if(!file.open(QIODevice::ReadOnly))
		return;
	const QByteArray all = file.readAll();
	const QByteArray head(kFileHead);
	fc::Bytes sealed;
	fc::Key32 key{};
	// Tests give no name: they never touch the password store
	QString key_hex = secret_name.isEmpty() ? QString() : keystore::Load(secret_name);
	if(key_hex.isEmpty())
	{
		// No password store on this computer: the key sits next to the file
		QFile key_file(QDir(folder).filePath(QStringLiteral("friends.key")));
		if(key_file.open(QIODevice::ReadOnly))
			key_hex = QString::fromLatin1(key_file.readAll().trimmed());
	}
	if(!all.startsWith(head) || !Unhex(key_hex, key) || !UnB64(QString::fromLatin1(all.mid(head.size()).trimmed()), sealed) || sealed.size() < 12 + 16)
	{
		problem = tr("The friends of this computer cannot be read: their key is not in the password store any more. A backup brings them back.");
		return;
	}
	fc::Bytes plain;
	if(!fc::Open(key.data(), sealed.data(), fc::Label("psrpt friends store"), fc::Bytes(sealed.begin() + 12, sealed.end()), plain))
	{
		problem = tr("The friends of this computer cannot be read.");
		return;
	}
	state = QJsonDocument::fromJson(ToQ(plain)).object();
	enabled = state.contains(QStringLiteral("identity"));
	const QString stored_server = state.value(QStringLiteral("server")).toString();
	server = stored_server.isEmpty() ? QString::fromLatin1(kDefaultServer) : stored_server;
	const QJsonObject settings = state.value(QStringLiteral("settings")).toObject();
	my_state = settings.value(QStringLiteral("state")).toString(QStringLiteral("online"));
	my_free = settings.value(QStringLiteral("free")).toString();
}

void FriendsClient::Save()
{
	if(folder.isEmpty())
		return;
	QDir().mkpath(folder);
	fc::Key32 key{};
	QString key_hex = secret_name.isEmpty() ? QString() : keystore::Load(secret_name);
	QFile key_file(QDir(folder).filePath(QStringLiteral("friends.key")));
	if(key_hex.isEmpty() && key_file.open(QIODevice::ReadOnly))
	{
		key_hex = QString::fromLatin1(key_file.readAll().trimmed());
		key_file.close();
	}
	if(!Unhex(key_hex, key))
	{
		coplay::RandomBytes(key.data(), key.size());
		key_hex = Hex(key);
		if(secret_name.isEmpty() || !keystore::Save(secret_name, key_hex))
		{
			// Without a password store: a file only this user may read, next to the data
			if(key_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
			{
				key_file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
				key_file.write(key_hex.toLatin1());
				key_file.close();
			}
		}
	}
	QJsonObject settings = state.value(QStringLiteral("settings")).toObject();
	settings[QStringLiteral("state")] = my_state;
	settings[QStringLiteral("free")] = my_free;
	state[QStringLiteral("settings")] = settings;
	const QByteArray json = QJsonDocument(state).toJson(QJsonDocument::Compact);
	uint8_t nonce[12];
	coplay::RandomBytes(nonce, sizeof(nonce));
	fc::Bytes sealed;
	if(!fc::Seal(key.data(), nonce, fc::Label("psrpt friends store"), ToBytes(json), sealed))
		return;
	fc::Bytes all(nonce, nonce + 12);
	all.insert(all.end(), sealed.begin(), sealed.end());
	QSaveFile file(QDir(folder).filePath(QStringLiteral("friends.dat")));
	if(!file.open(QIODevice::WriteOnly))
		return;
	file.write(kFileHead);
	file.write(B64(all).toLatin1());
	file.commit();
}

// ------------------------------------------------------------------ identity and keys

bool FriendsClient::Identity(fc::Identity &out) const
{
	const QJsonObject id = state.value(QStringLiteral("identity")).toObject();
	if(!PairFrom(id.value(QStringLiteral("iks")).toString(), out.iks) || !PairFrom(id.value(QStringLiteral("ikd")).toString(), out.ikd))
		return false;
	return fc::CompleteIdentity(out);
}

void FriendsClient::enable()
{
	if(enabled)
		return;
	generation++;
	// A file that could not be read is kept aside, not written over: a key found later still opens it
	const QString path = QDir(folder).filePath(QStringLiteral("friends.dat"));
	if(QFile::exists(path))
		QFile::rename(path, QDir(folder).filePath(QStringLiteral("friends-%1.unreadable").arg(QDateTime::currentSecsSinceEpoch())));
	fc::Identity id;
	if(!fc::MakeIdentity(id))
	{
		problem = tr("No keys could be made on this computer.");
		emit changed();
		return;
	}
	QJsonObject identity;
	identity[QStringLiteral("iks")] = Hex(id.iks.d);
	identity[QStringLiteral("ikd")] = Hex(id.ikd.d);
	state = QJsonObject();
	state[QStringLiteral("v")] = 1;
	state[QStringLiteral("identity")] = identity;
	state[QStringLiteral("fp")] = B64(id.fp);
	state[QStringLiteral("server")] = ServerAddress();
	fc::DeviceId device{};
	coplay::RandomBytes(device.data(), device.size());
	QJsonObject dev;
	dev[QStringLiteral("id")] = Hex(device);
	state[QStringLiteral("device")] = dev;
	fc::Id16 lk{};
	coplay::RandomBytes(lk.data(), lk.size());
	state[QStringLiteral("lk")] = Hex(lk);
	// The friends key for statuses: a new epoch now
	fc::Key32 fk{};
	coplay::RandomBytes(fk.data(), fk.size());
	QJsonObject fko;
	fko[QStringLiteral("epoch")] = 1;
	fko[QStringLiteral("key")] = Hex(fk);
	fko[QStringLiteral("start")] = static_cast<double>(NowMs());
	state[QStringLiteral("fk")] = fko;
	// A token for the own devices' inboxes
	fc::Id16 self_token{};
	coplay::RandomBytes(self_token.data(), self_token.size());
	state[QStringLiteral("self_token")] = B64(self_token);
	enabled = true;
	problem.clear();
	Save();
	PublishCard();
	poll_timer.start();
	status_timer.start();
	emit changed();
}

void FriendsClient::disable()
{
	poll_timer.stop();
	status_timer.stop();
	cancelLinking();
	const int now = ++generation;
	auto done = std::make_shared<bool>(false);
	auto wipe = [this, now, done]() {
		// Only once, and never over an identity made since
		if(*done || generation != now)
			return;
		*done = true;
		state = QJsonObject();
		statuses.clear();
		card_fetched.clear();
		QFile::remove(QDir(folder).filePath(QStringLiteral("friends.dat")));
		QFile::remove(QDir(folder).filePath(QStringLiteral("friends.key")));
		if(!secret_name.isEmpty())
			keystore::Remove(secret_name);
		emit changed();
	};
	if(!enabled)
	{
		wipe();
		return;
	}
	// Off at once for the player; off the card first, so that friends stop writing to this
	// device, then everything here goes (after a while at the latest)
	enabled = false;
	emit changed();
	PublishCard(state.value(QStringLiteral("device")).toObject().value(QStringLiteral("id")).toString(), wipe);
	QTimer::singleShot(10000, this, wipe);
}

void FriendsClient::removeDevice(const QString &id)
{
	if(!enabled || id == state.value(QStringLiteral("device")).toObject().value(QStringLiteral("id")).toString())
		return;
	PublishCard(id, [this]() { emit changed(); });
}

QVariantList FriendsClient::Devices() const
{
	QVariantList out;
	if(!enabled)
		return out;
	// The newest card known: the one this device put, or one fetched since
	fc::Card card, other;
	fc::Bytes bytes;
	bool have = UnB64(state.value(QStringLiteral("card")).toString(), bytes) && fc::DecodeCard(bytes, card);
	if(UnB64(state.value(QStringLiteral("cards")).toObject().value(OwnFp(state)).toString(), bytes) && fc::DecodeCard(bytes, other)
		&& (!have || other.seq > card.seq))
	{
		card = other;
		have = true;
	}
	if(!have)
		return out;
	const QString mine = state.value(QStringLiteral("device")).toObject().value(QStringLiteral("id")).toString();
	for(const fc::CardDevice &d : card.devices)
		out.append(QVariantMap{ { QStringLiteral("id"), Hex(d.id) }, { QStringLiteral("kind"), d.kind }, { QStringLiteral("me"), Hex(d.id) == mine } });
	return out;
}

/** This device's signed prekey, made or renewed; the one before stays for a while. */
static bool EnsureSignedPrekey(QJsonObject &dev)
{
	const qint64 made = static_cast<qint64>(dev.value(QStringLiteral("spk_made")).toDouble());
	if(dev.contains(QStringLiteral("spk")) && NowMs() - made < kSpkRenewMs)
		return false;
	fc::KeyPair spk;
	if(!fc::GenerateKeyPair(spk))
		return false;
	if(dev.contains(QStringLiteral("spk")))
	{
		dev[QStringLiteral("spk_prev")] = dev.value(QStringLiteral("spk"));
		dev[QStringLiteral("spk_prev_id")] = dev.value(QStringLiteral("spk_id"));
	}
	dev[QStringLiteral("spk")] = Hex(spk.d);
	dev[QStringLiteral("spk_id")] = dev.value(QStringLiteral("spk_id")).toInt() + 1;
	dev[QStringLiteral("spk_made")] = static_cast<double>(NowMs());
	return true;
}

void FriendsClient::PublishCard(const QString &without, std::function<void()> after)
{
	fc::Identity id;
	if(!Identity(id))
	{
		if(after)
			after();
		return;
	}
	QJsonObject dev = state.value(QStringLiteral("device")).toObject();
	EnsureSignedPrekey(dev);
	state[QStringLiteral("device")] = dev;
	Save();
	// The card on the server lists the other own devices too: they stay
	Call(QStringLiteral("card_get"), { { QStringLiteral("fp"), B64(id.fp) } }, false, [this, without, after](bool ok, const QJsonObject &answer) {
		fc::Identity id;
		if(!Identity(id))
		{
			if(after)
				after();
			return;
		}
		fc::Card card;
		fc::Card current;
		fc::Bytes bytes;
		uint32_t seq = static_cast<uint32_t>(state.value(QStringLiteral("seq")).toInt());
		if(ok && UnB64(answer.value(QStringLiteral("card")).toString(), bytes) && fc::DecodeCard(bytes, current) && current.fp == id.fp)
		{
			seq = std::max(seq, current.seq);
			card.devices = current.devices;
		}
		const QJsonObject dev = state.value(QStringLiteral("device")).toObject();
		fc::CardDevice mine;
		fc::KeyPair spk;
		if(!Unhex(dev.value(QStringLiteral("id")).toString(), mine.id) || !PairFrom(dev.value(QStringLiteral("spk")).toString(), spk))
			return;
		// A device the card did not list yet: the friends hear of it, so they write to it too
		const bool new_device = std::none_of(card.devices.begin(), card.devices.end(), [&](const fc::CardDevice &d) { return d.id == mine.id; });
		mine.kind = 1;
		mine.spk_id = static_cast<uint32_t>(dev.value(QStringLiteral("spk_id")).toInt());
		mine.spk = spk.pub;
		fc::SignPrekey(id, mine.id, mine.spk_id, mine.spk, mine.spk_sig);
		card.devices.erase(std::remove_if(card.devices.begin(), card.devices.end(),
			[&](const fc::CardDevice &d) { return d.id == mine.id || Hex(d.id) == without; }), card.devices.end());
		// This device leaves the card when it is the one taken off (friends switched off here)
		const bool leaving = Hex(mine.id) == without;
		if(!leaving)
			card.devices.insert(card.devices.begin(), mine);
		if(card.devices.empty())
		{
			if(after)
				after();
			return;
		}
		if(card.devices.size() > fc::kMaxDevices)
			card.devices.resize(fc::kMaxDevices);
		card.seq = seq + 1;
		fc::Id16 lk{};
		Unhex(state.value(QStringLiteral("lk")).toString(), lk);
		QJsonObject profile;
		profile[QStringLiteral("name")] = profile_name;
		if(!profile_avatar.isEmpty())
			profile[QStringLiteral("avatar")] = QString::fromLatin1(profile_avatar.toBase64());
		fc::SealProfile(lk, id.fp, QJsonDocument(profile).toJson(QJsonDocument::Compact).toStdString(), card.profile);
		fc::Bytes out;
		if(!fc::EncodeCard(id, card, out))
			return;
		state[QStringLiteral("seq")] = static_cast<int>(card.seq);
		state[QStringLiteral("card")] = B64(out);
		Save();
		Call(QStringLiteral("card_put"), { { QStringLiteral("card"), B64(out) } }, true, [this, new_device, leaving, after](bool put_ok, const QJsonObject &) {
			if(after)
				after();
			if(!put_ok || leaving)
				return;
			card_fetched.remove(OwnFp(state));
			if(new_device)
			{
				const QJsonObject friends = state.value(QStringLiteral("friends")).toObject();
				for(auto it = friends.constBegin(); it != friends.constEnd(); ++it)
				{
					if(it.value().toObject().value(QStringLiteral("state")).toString() == QLatin1String("friend"))
						SendPayload(it.key(), QJsonObject{ { QStringLiteral("t"), QStringLiteral("card") } }, false);
				}
				SendPayload(OwnFp(state), QJsonObject{ { QStringLiteral("t"), QStringLiteral("card") } }, false);
			}
			// The request box of the current link, and the own devices' token
			fc::Id16 lk{};
			Unhex(state.value(QStringLiteral("lk")).toString(), lk);
			Call(QStringLiteral("requests_set"), { { QStringLiteral("box"), B64(fc::RequestBox(lk)) } }, true, nullptr);
			fc::Bytes token;
			UnB64(state.value(QStringLiteral("self_token")).toString(), token);
			const fc::Key32 h = fc::Sha256(token);
			Call(QStringLiteral("token_add"), { { QStringLiteral("hash"), B64(h) } }, true, nullptr);
			// The first one-time prekeys; later ones when the server says it has few left
			if(!state.value(QStringLiteral("device")).toObject().contains(QStringLiteral("next_opk")))
				TopUpPrekeys();
		});
	});
}

void FriendsClient::TopUpPrekeys()
{
	QJsonObject dev = state.value(QStringLiteral("device")).toObject();
	QJsonObject opks = dev.value(QStringLiteral("opks")).toObject();
	int next = dev.value(QStringLiteral("next_opk")).toInt(1);
	QByteArray keys;
	for(int i = 0; i < kPrekeyBatch; i++)
	{
		fc::KeyPair kp;
		if(!fc::GenerateKeyPair(kp))
			return;
		const uint32_t id = static_cast<uint32_t>(next++);
		opks[QString::number(id)] = Hex(kp.d);
		const char idb[4] = { static_cast<char>(id >> 24), static_cast<char>(id >> 16), static_cast<char>(id >> 8), static_cast<char>(id) };
		keys.append(idb, 4);
		keys.append(reinterpret_cast<const char *>(kp.pub.data()), 65);
	}
	// Prekeys the server gave out but nobody used are not kept forever: the oldest go
	if(opks.size() > 300)
	{
		QList<uint32_t> ids;
		for(const QString &k : opks.keys())
			ids.append(k.toUInt());
		std::sort(ids.begin(), ids.end());
		for(qsizetype i = 0; i < ids.size() - 300; i++)
			opks.remove(QString::number(ids[i]));
	}
	dev[QStringLiteral("opks")] = opks;
	dev[QStringLiteral("next_opk")] = next;
	state[QStringLiteral("device")] = dev;
	Save();
	Call(QStringLiteral("prekeys_put"), { { QStringLiteral("device"), B64(ToBytes(QByteArray::fromHex(dev.value(QStringLiteral("id")).toString().toLatin1()))) },
		{ QStringLiteral("keys"), B64(ToBytes(keys)) } }, true, nullptr);
}

// ------------------------------------------------------------------ talking to the server

void FriendsClient::Call(const QString &op, const QMap<QString, QString> &fields, bool sign, Reply done, bool again)
{
	QUrlQuery form;
	form.addQueryItem(QStringLiteral("v"), QStringLiteral("1"));
	form.addQueryItem(QStringLiteral("op"), op);
	QMap<QString, QString> all = fields;
	if(sign)
	{
		fc::Identity id;
		if(!Identity(id))
		{
			if(done)
				done(false, QJsonObject());
			return;
		}
		all[QStringLiteral("fp")] = B64(id.fp);
		// A few random bytes: two requests in the same second are not the same request
		uint8_t nonce[8];
		coplay::RandomBytes(nonce, sizeof(nonce));
		all[QStringLiteral("n")] = B64(nonce, sizeof(nonce));
		// The server's time where this computer's clock is off
		const QString t = QString::number(QDateTime::currentSecsSinceEpoch() + clock_offset);
		std::map<std::string, std::string> signed_fields;
		for(auto it = all.constBegin(); it != all.constEnd(); ++it)
			signed_fields[it.key().toStdString()] = it.value().toStdString();
		fc::Sig sig;
		fc::Sign(id.iks, fc::RequestMessage(op.toStdString(), t.toStdString(), signed_fields), sig);
		form.addQueryItem(QStringLiteral("t"), t);
		form.addQueryItem(QStringLiteral("sig"), B64(sig));
	}
	for(auto it = all.constBegin(); it != all.constEnd(); ++it)
		form.addQueryItem(it.key(), it.value());
	QNetworkRequest request{ QUrl(server) };
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
	request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("PSRemotePlayTogether/%1").arg(QCoreApplication::applicationVersion()));
	request.setTransferTimeout(15000);
	request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
	request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
	QNetworkReply *reply = network->post(request, form.toString(QUrl::FullyEncoded).toLatin1());
	pending_calls++;
	QPointer<FriendsClient> self(this);
	connect(reply, &QNetworkReply::finished, this, [self, reply, done, op, fields, sign, again]() {
		reply->deleteLater();
		if(!self)
			return;
		self->pending_calls--;
		const QJsonObject answer = QJsonDocument::fromJson(reply->read(4 * 1024 * 1024)).object();
		const bool ok = reply->error() == QNetworkReply::NoError && answer.value(QStringLiteral("ok")).toBool();
		if(!ok)
			qCDebug(lcFriends) << op << "failed:" << reply->error() << answer.value(QStringLiteral("error")).toString();
		// The clock here is off: the server's time from now on, and this request once more
		if(answer.value(QStringLiteral("error")).toString() == QLatin1String("stale_time") && answer.value(QStringLiteral("now")).isDouble())
		{
			self->clock_offset = static_cast<qint64>(answer.value(QStringLiteral("now")).toDouble()) - QDateTime::currentSecsSinceEpoch();
			if(sign && !again)
			{
				self->Call(op, fields, sign, done, true);
				return;
			}
		}
		const bool was_online = self->online;
		self->online = reply->error() == QNetworkReply::NoError && !answer.isEmpty();
		if(self->online != was_online)
			emit self->changed();
		if(done)
			done(ok, answer);
	});
}

void FriendsClient::refresh()
{
	Poll();
}

void FriendsClient::Poll()
{
	if(!enabled)
		return;
	const QJsonObject dev = state.value(QStringLiteral("device")).toObject();
	const QString device = B64(ToBytes(QByteArray::fromHex(dev.value(QStringLiteral("id")).toString().toLatin1())));
	// The inbox of this device
	Call(QStringLiteral("inbox_get"), { { QStringLiteral("device"), device } }, true, [this, device](bool ok, const QJsonObject &answer) {
		if(!ok)
			return;
		TakeNotes(answer.value(QStringLiteral("notes")).toArray(), false, QStringLiteral("device"), device);
		// More waiting than one answer holds: again once these are taken away
		if(answer.value(QStringLiteral("more")).toBool())
			QTimer::singleShot(1500, this, &FriendsClient::Poll);
		if(answer.contains(QStringLiteral("prekeys")) && answer.value(QStringLiteral("prekeys")).toInt() < kPrekeyLow)
			TopUpPrekeys();
	});
	// The groups' boxes
	PollGroups();
	// The request box of the current link
	fc::Id16 lk{};
	Unhex(state.value(QStringLiteral("lk")).toString(), lk);
	const QString box = B64(fc::RequestBox(lk));
	Call(QStringLiteral("inbox_get"), { { QStringLiteral("box"), box } }, true, [this, box](bool ok, const QJsonObject &answer) {
		if(ok)
			TakeNotes(answer.value(QStringLiteral("notes")).toArray(), true, QStringLiteral("box"), box);
	});
	// The friends' statuses: each under a name worked out from their friends key and the day
	// (today's, and yesterday's around midnight, when clocks differ)
	const QJsonObject friends = state.value(QStringLiteral("friends")).toObject();
	QStringList boxes;
	QHash<QString, QStringList> box_of_friend;
	const uint32_t today = static_cast<uint32_t>(NowMs() / kDayMs);
	for(auto it = friends.constBegin(); it != friends.constEnd() && boxes.size() < 400; ++it)
	{
		const QJsonObject f = it.value().toObject();
		fc::Key32 key{};
		if(f.value(QStringLiteral("state")).toString() != QLatin1String("friend")
			|| !Unhex(f.value(QStringLiteral("fk")).toObject().value(QStringLiteral("key")).toString(), key))
			continue;
		for(uint32_t day : { today, today - 1 })
		{
			const QString box = B64(fc::StatusBox(key, day));
			boxes.append(box);
			box_of_friend[it.key()].append(box);
		}
	}
	if(!boxes.isEmpty())
	{
		Call(QStringLiteral("status_get"), { { QStringLiteral("boxes"), boxes.join(QLatin1Char(',')) } }, false,
			[this, box_of_friend](bool ok, const QJsonObject &answer) {
				if(!ok)
					return;
				const QJsonObject notes = answer.value(QStringLiteral("notes")).toObject();
				const QJsonObject friends = state.value(QStringLiteral("friends")).toObject();
				for(auto it = friends.constBegin(); it != friends.constEnd(); ++it)
				{
					const QJsonObject f = it.value().toObject();
					fc::Bytes note;
					fc::Key32 key{};
					fc::Id16 fp{};
					const QJsonObject fk = f.value(QStringLiteral("fk")).toObject();
					// Today's note if there is one, else yesterday's
					QString found;
					for(const QString &box : box_of_friend.value(it.key()))
					{
						if(found.isEmpty() && notes.contains(box))
							found = notes.value(box).toString();
					}
					if(!UnB64(found, note) || !Unhex(fk.value(QStringLiteral("key")).toString(), key) || !UnB64(it.key(), fp))
					{
						statuses.remove(it.key());
						continue;
					}
					std::string json;
					if(!fc::OpenStatus(key, static_cast<uint32_t>(fk.value(QStringLiteral("epoch")).toInt()), fp, note, json))
						continue;
					QJsonObject st = QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();
					// Only a fresh one: a server could hand out an old note of the same key again
					const double written = st.value(QStringLiteral("at")).toDouble();
					if(std::abs(static_cast<double>(NowMs()) - written) > 10.0 * 60 * 1000)
						continue;
					if(st.contains(QStringLiteral("room")))
					{
						const QJsonObject clean = CleanRoom(st.value(QStringLiteral("room")).toObject());
						if(clean.isEmpty())
							st.remove(QStringLiteral("room"));
						else
							st[QStringLiteral("room")] = clean;
					}
					const QJsonObject before = statuses.value(it.key());
					st[QStringLiteral("seen")] = static_cast<double>(NowMs());
					statuses.insert(it.key(), st);
					// Somebody opened a room since the last look
					const QJsonObject room_now = st.value(QStringLiteral("room")).toObject();
					if(!room_now.isEmpty() && before.value(QStringLiteral("room")).toObject().isEmpty())
						emit friendHosting(it.key(), f.value(QStringLiteral("name")).toString(), room_now.value(QStringLiteral("title")).toString());
				}
				emit changed();
			});
	}
}

void FriendsClient::PutStatus()
{
	if(!enabled)
		return;
	if(status_in_flight)
	{
		status_again = true;
		return;
	}
	fc::Identity id;
	if(!Identity(id))
		return;
	const QString show = ShowStatus();
	// Invisible, or nobody to show it to: the note on the server goes at once (an empty one)
	if(my_state == QLatin1String("invisible") || show == QLatin1String("nobody"))
	{
		if(status_hidden)
			return;
		status_hidden = true;
		status_in_flight = true;
		Call(QStringLiteral("status_put"), { { QStringLiteral("note"), QString() } }, true, [this](bool, const QJsonObject &) {
			status_in_flight = false;
			if(status_again)
			{
				status_again = false;
				PutStatus();
			}
		});
		return;
	}
	status_hidden = false;
	QJsonObject fk = state.value(QStringLiteral("fk")).toObject();
	qint64 start = static_cast<qint64>(fk.value(QStringLiteral("start")).toDouble());
	qint64 day = (NowMs() - start) / kDayMs;
	if(day > 365)
	{
		// A year on one epoch: a new one, sent to every friend
		fc::Key32 key{};
		coplay::RandomBytes(key.data(), key.size());
		fk[QStringLiteral("epoch")] = fk.value(QStringLiteral("epoch")).toInt() + 1;
		fk[QStringLiteral("key")] = Hex(key);
		fk[QStringLiteral("start")] = static_cast<double>(NowMs());
		state[QStringLiteral("fk")] = fk;
		Save();
		day = 0;
		const QJsonObject friends = state.value(QStringLiteral("friends")).toObject();
		for(auto it = friends.constBegin(); it != friends.constEnd(); ++it)
		{
			if(it.value().toObject().value(QStringLiteral("state")).toString() == QLatin1String("friend"))
				SendFriendsKey(it.key());
		}
		SyncOwn(QString(), true);
	}
	QJsonObject st;
	st[QStringLiteral("state")] = my_state;
	st[QStringLiteral("web")] = false;
	if(ShowGame() && !activity.isEmpty())
		st[QStringLiteral("activity")] = activity;
	if(FriendsJoin() != QLatin1String("never") && !room.isEmpty())
		st[QStringLiteral("room")] = QJsonObject::fromVariantMap(room);
	if(!my_free.isEmpty())
		st[QStringLiteral("free")] = my_free;
	st[QStringLiteral("at")] = static_cast<double>(NowMs());
	fc::Key32 key{};
	Unhex(fk.value(QStringLiteral("key")).toString(), key);
	fc::Bytes note;
	if(!fc::SealStatus(key, static_cast<uint32_t>(fk.value(QStringLiteral("epoch")).toInt()), static_cast<uint32_t>(day), id.fp,
		QJsonDocument(st).toJson(QJsonDocument::Compact).toStdString(), note))
		return;
	last_status_put = NowMs();
	status_in_flight = true;
	const QString box = B64(fc::StatusBox(key, static_cast<uint32_t>(NowMs() / kDayMs)));
	Call(QStringLiteral("status_put"), { { QStringLiteral("note"), B64(note) }, { QStringLiteral("box"), box } }, true, [this](bool, const QJsonObject &) {
		status_in_flight = false;
		if(status_again)
		{
			status_again = false;
			PutStatus();
		}
	});
}

// ------------------------------------------------------------------ properties

QString FriendsClient::Fingerprint() const
{
	return enabled ? OwnFp(state) : QString();
}

QString FriendsClient::Link() const
{
	if(!enabled)
		return QString();
	QString link = QStringLiteral("psrpt://friend/%1.%2").arg(OwnFp(state),
		B64(ToBytes(QByteArray::fromHex(state.value(QStringLiteral("lk")).toString().toLatin1()))));
	if(server != QLatin1String(kDefaultServer))
		link += QStringLiteral("?at=") + QString::fromLatin1(QUrl::toPercentEncoding(server));
	return link;
}

QString FriendsClient::WebLink() const
{
	if(!enabled)
		return QString();
	QString link = QStringLiteral("%1#f=%2.%3").arg(QString::fromLatin1(kDefaultPage), OwnFp(state),
		B64(ToBytes(QByteArray::fromHex(state.value(QStringLiteral("lk")).toString().toLatin1()))));
	if(server != QLatin1String(kDefaultServer))
		link += QStringLiteral("?at=") + QString::fromLatin1(QUrl::toPercentEncoding(server));
	return link;
}

static QString AvatarUrl(const QJsonObject &f)
{
	const QString avatar = f.value(QStringLiteral("avatar")).toString();
	return avatar.isEmpty() ? QString() : QStringLiteral("data:image/jpeg;base64,") + avatar;
}

QVariantList FriendsClient::Friends() const
{
	QVariantList out;
	if(!enabled)
		return out;
	const QJsonObject friends = state.value(QStringLiteral("friends")).toObject();
	fc::Id16 own{};
	UnB64(OwnFp(state), own);
	for(auto it = friends.constBegin(); it != friends.constEnd(); ++it)
	{
		const QJsonObject f = it.value().toObject();
		if(f.value(QStringLiteral("state")).toString() != QLatin1String("friend"))
			continue;
		QVariantMap m;
		m[QStringLiteral("fp")] = it.key();
		m[QStringLiteral("name")] = f.value(QStringLiteral("name")).toString();
		m[QStringLiteral("avatar")] = AvatarUrl(f);
		const QJsonObject st = statuses.value(it.key());
		const bool fresh = !st.isEmpty() && NowMs() - static_cast<qint64>(st.value(QStringLiteral("seen")).toDouble()) < kStatusFreshMs;
		QString s = fresh ? st.value(QStringLiteral("state")).toString(QStringLiteral("online")) : QStringLiteral("offline");
		if(fresh && st.value(QStringLiteral("web")).toBool() && s == QLatin1String("online"))
			s = QStringLiteral("web");
		m[QStringLiteral("state")] = s;
		m[QStringLiteral("activity")] = fresh ? st.value(QStringLiteral("activity")).toString() : QString();
		m[QStringLiteral("room")] = fresh && st.contains(QStringLiteral("room")) ? st.value(QStringLiteral("room")).toObject().toVariantMap() : QVariant();
		m[QStringLiteral("free")] = fresh ? st.value(QStringLiteral("free")).toString() : QString();
		m[QStringLiteral("checked")] = f.value(QStringLiteral("checked")).toBool();
		fc::Id16 theirs{};
		UnB64(it.key(), theirs);
		m[QStringLiteral("safety")] = QString::fromStdString(fc::SafetyCode(own, theirs));
		out.append(m);
	}
	// Who plays or is online first, then by name
	std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
		const QVariantMap x = a.toMap(), y = b.toMap();
		const bool xo = x.value(QStringLiteral("state")).toString() != QLatin1String("offline");
		const bool yo = y.value(QStringLiteral("state")).toString() != QLatin1String("offline");
		if(xo != yo)
			return xo;
		return x.value(QStringLiteral("name")).toString().compare(y.value(QStringLiteral("name")).toString(), Qt::CaseInsensitive) < 0;
	});
	return out;
}

QVariantList FriendsClient::Requests() const
{
	QVariantList out;
	if(!enabled)
		return out;
	const QJsonObject friends = state.value(QStringLiteral("friends")).toObject();
	for(auto it = friends.constBegin(); it != friends.constEnd(); ++it)
	{
		const QJsonObject f = it.value().toObject();
		const QString s = f.value(QStringLiteral("state")).toString();
		if(s != QLatin1String("incoming") && s != QLatin1String("requested"))
			continue;
		QVariantMap m;
		m[QStringLiteral("fp")] = it.key();
		m[QStringLiteral("name")] = f.value(QStringLiteral("name")).toString();
		m[QStringLiteral("avatar")] = AvatarUrl(f);
		m[QStringLiteral("text")] = f.value(QStringLiteral("text")).toString();
		m[QStringLiteral("incoming")] = s == QLatin1String("incoming");
		m[QStringLiteral("unsent")] = f.value(QStringLiteral("unsent")).toBool();
		m[QStringLiteral("at")] = f.value(QStringLiteral("since")).toDouble();
		out.append(m);
	}
	return out;
}

QVariantList FriendsClient::Blocked() const
{
	QVariantList out;
	if(!enabled)
		return out;
	const QJsonObject friends = state.value(QStringLiteral("friends")).toObject();
	for(auto it = friends.constBegin(); it != friends.constEnd(); ++it)
	{
		const QJsonObject f = it.value().toObject();
		if(f.value(QStringLiteral("state")).toString() == QLatin1String("blocked"))
			out.append(QVariantMap{ { QStringLiteral("fp"), it.key() }, { QStringLiteral("name"), f.value(QStringLiteral("name")).toString() } });
	}
	return out;
}

QVariantList FriendsClient::Chats() const
{
	QVariantList out;
	if(!enabled)
		return out;
	const QJsonObject chats = state.value(QStringLiteral("chats")).toObject();
	const QJsonObject friends = state.value(QStringLiteral("friends")).toObject();
	const QJsonObject groups = state.value(QStringLiteral("groups")).toObject();
	// Every chat with messages, and every group (a new one has none yet)
	QStringList ids = chats.keys();
	for(const QString &gid : groups.keys())
	{
		if(!ids.contains(gid))
			ids.append(gid);
	}
	for(const QString &id : ids)
	{
		const QJsonObject c = chats.value(id).toObject();
		const QJsonArray msgs = c.value(QStringLiteral("m")).toArray();
		const QJsonObject g = groups.value(id).toObject();
		const bool is_group = !g.isEmpty();
		if(msgs.isEmpty() && !is_group)
			continue;
		const QJsonObject last = msgs.isEmpty() ? QJsonObject() : msgs.last().toObject();
		const QJsonObject f = friends.value(id).toObject();
		QVariantMap m;
		m[QStringLiteral("id")] = id;
		m[QStringLiteral("kind")] = is_group ? QStringLiteral("group") : QStringLiteral("friend");
		m[QStringLiteral("title")] = is_group ? g.value(QStringLiteral("name")).toString() : f.value(QStringLiteral("name")).toString();
		m[QStringLiteral("avatar")] = is_group ? QString() : AvatarUrl(f);
		if(is_group)
		{
			m[QStringLiteral("members")] = static_cast<int>(g.value(QStringLiteral("members")).toArray().size());
			m[QStringLiteral("left")] = g.value(QStringLiteral("left")).toBool();
		}
		const QJsonObject card = last.value(QStringLiteral("join")).toObject();
		const QString card_title = card.value(QStringLiteral("title")).toString(tr("a session"));
		QString line = msgs.isEmpty() ? tr("No messages yet")
			: last.value(QStringLiteral("deleted")).toBool() ? tr("Message deleted")
			: card.isEmpty() ? last.value(QStringLiteral("text")).toString()
			: last.value(QStringLiteral("mine")).toBool() ? tr("You invited them to %1").arg(card_title) : tr("Invites you to %1").arg(card_title);
		// In a group: who wrote it
		if(is_group && !last.isEmpty() && !last.value(QStringLiteral("system")).toBool())
		{
			const QString from = last.value(QStringLiteral("from")).toString();
			const QString who = last.value(QStringLiteral("mine")).toBool() ? tr("You")
				: friends.value(from).toObject().value(QStringLiteral("name")).toString(g.value(QStringLiteral("names")).toObject().value(from).toString());
			if(!who.isEmpty())
				line = who + QStringLiteral(": ") + line;
		}
		m[QStringLiteral("last")] = line;
		m[QStringLiteral("at")] = msgs.isEmpty() ? g.value(QStringLiteral("made")).toDouble() : last.value(QStringLiteral("at")).toDouble();
		const double seen = c.value(QStringLiteral("seen")).toDouble();
		int unread = 0;
		int mentions = 0;
		for(const QJsonValue &v : msgs)
		{
			const QJsonObject msg = v.toObject();
			if(!msg.value(QStringLiteral("mine")).toBool() && !msg.value(QStringLiteral("system")).toBool() && !msg.value(QStringLiteral("deleted")).toBool()
				&& msg.value(QStringLiteral("at")).toDouble() > seen)
			{
				unread++;
				mentions += chattext::Mentions(msg.value(QStringLiteral("text")).toString(), profile_name) ? 1 : 0;
			}
		}
		m[QStringLiteral("unread")] = unread;
		// Unread messages that mention this player (they count even in a muted chat)
		m[QStringLiteral("mentions")] = mentions;
		m[QStringLiteral("muted")] = Muted(id);
		out.append(m);
	}
	std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
		return a.toMap().value(QStringLiteral("at")).toDouble() > b.toMap().value(QStringLiteral("at")).toDouble();
	});
	return out;
}

int FriendsClient::Unread() const
{
	// A muted chat counts only what mentions this player
	int n = 0;
	for(const QVariant &c : Chats())
	{
		const QVariantMap m = c.toMap();
		n += m.value(m.value(QStringLiteral("muted")).toBool() ? QStringLiteral("mentions") : QStringLiteral("unread")).toInt();
	}
	return n;
}

void FriendsClient::SetMyState(const QString &s)
{
	if(s == my_state || (s != QLatin1String("online") && s != QLatin1String("away") && s != QLatin1String("busy") && s != QLatin1String("invisible")))
		return;
	my_state = s;
	Save();
	PutStatus();
	emit changed();
}

void FriendsClient::SetMyFree(const QString &text)
{
	if(text == my_free)
		return;
	my_free = text.left(80);
	Save();
	PutStatus();
	emit changed();
}

QString FriendsClient::ShowStatus() const
{
	return state.value(QStringLiteral("settings")).toObject().value(QStringLiteral("show_status")).toString(QStringLiteral("all"));
}

void FriendsClient::SetShowStatus(const QString &who)
{
	QJsonObject s = state.value(QStringLiteral("settings")).toObject();
	s[QStringLiteral("show_status")] = who == QLatin1String("nobody") ? QStringLiteral("nobody") : QStringLiteral("all");
	state[QStringLiteral("settings")] = s;
	Save();
	PutStatus();
	emit changed();
}

bool FriendsClient::ShowGame() const
{
	return state.value(QStringLiteral("settings")).toObject().value(QStringLiteral("show_game")).toBool(true);
}

void FriendsClient::SetShowGame(bool on)
{
	QJsonObject s = state.value(QStringLiteral("settings")).toObject();
	s[QStringLiteral("show_game")] = on;
	state[QStringLiteral("settings")] = s;
	Save();
	PutStatus();
	emit changed();
}

QString FriendsClient::FriendsJoin() const
{
	// On or off (the settings say so); "ask" from older states counts as on: the session's own
	// join rule (Ask to join, as viewers, to play) decides how they come in
	const QString join = state.value(QStringLiteral("settings")).toObject().value(QStringLiteral("friends_join")).toString(QStringLiteral("always"));
	return join == QLatin1String("never") ? join : QStringLiteral("always");
}

void FriendsClient::SetFriendsJoin(const QString &who)
{
	QJsonObject s = state.value(QStringLiteral("settings")).toObject();
	s[QStringLiteral("friends_join")] = who == QLatin1String("never") ? QStringLiteral("never") : QStringLiteral("always");
	state[QStringLiteral("settings")] = s;
	Save();
	PutStatus();
	emit changed();
}

// ------------------------------------------------------------------ friend links and requests

/** Reads "psrpt://friend/FP.LK", "https://.../#f=FP.LK" or "FP.LK". */
static bool ParseFriendLink(const QString &text, fc::Id16 &fp, fc::Id16 &lk, QString *server_out)
{
	QString s = text.trimmed();
	const int f = s.indexOf(QStringLiteral("#f="));
	if(f >= 0)
		s = s.mid(f + 3);
	else if(s.startsWith(QStringLiteral("psrpt://friend/")))
		s = s.mid(15);
	QString at;
	const int q = s.indexOf(QLatin1Char('?'));
	if(q >= 0)
	{
		const QUrlQuery query(s.mid(q + 1));
		at = query.queryItemValue(QStringLiteral("at"), QUrl::FullyDecoded);
		s = s.left(q);
	}
	const QStringList parts = s.split(QLatin1Char('.'));
	if(parts.size() != 2 || !UnB64(parts[0], fp) || !UnB64(parts[1], lk))
		return false;
	if(server_out)
		*server_out = at;
	return true;
}

void FriendsClient::lookUp(const QString &link)
{
	fc::Id16 fp{}, lk{};
	QString at;
	if(!enabled || !ParseFriendLink(link, fp, lk, &at))
	{
		emit linkLooked(false, QString(), QString(), QString(), tr("That is no friend link."));
		return;
	}
	// Friends meet on one server: a link from another one leads nowhere here
	if((at.isEmpty() ? QString::fromLatin1(kDefaultServer) : at) != server)
	{
		emit linkLooked(false, B64(fp), QString(), QString(),
			tr("This link is for another friends server (%1). Both of you need the same one: Settings → Friends.").arg(at.isEmpty() ? tr("the default one") : at));
		return;
	}
	if(B64(fp) == OwnFp(state))
	{
		emit linkLooked(false, B64(fp), QString(), QString(), tr("That is your own friend link."));
		return;
	}
	Call(QStringLiteral("card_get"), { { QStringLiteral("fp"), B64(fp) } }, false, [this, fp, lk](bool ok, const QJsonObject &answer) {
		fc::Bytes bytes;
		fc::Card card;
		std::string json;
		if(!ok || !UnB64(answer.value(QStringLiteral("card")).toString(), bytes) || !fc::DecodeCard(bytes, card) || card.fp != fp
			|| !fc::OpenProfile(lk, fp, card.profile, json))
		{
			emit linkLooked(false, B64(fp), QString(), QString(), tr("Nobody uses this link any more."));
			return;
		}
		const QJsonObject profile = QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();
		const QString avatar = profile.value(QStringLiteral("avatar")).toString();
		emit linkLooked(true, B64(fp), profile.value(QStringLiteral("name")).toString().left(32),
			avatar.isEmpty() ? QString() : QStringLiteral("data:image/jpeg;base64,") + avatar, QString());
	});
}

/** A write token for a friend: random, its hash put on the server for the own inboxes. */
static QString NewToken(QJsonObject &state, const QString &fp)
{
	fc::Id16 token{};
	coplay::RandomBytes(token.data(), token.size());
	QJsonObject issued = state.value(QStringLiteral("issued")).toObject();
	issued[B64(token)] = fp;
	state[QStringLiteral("issued")] = issued;
	return B64(token);
}

static QString TokenHash(const QString &token)
{
	fc::Bytes t;
	UnB64(token, t);
	return B64(fc::Sha256(t));
}

void FriendsClient::sendRequest(const QString &link, const QString &text)
{
	fc::Id16 fp{}, lk{};
	QString at;
	if(!enabled || !ParseFriendLink(link, fp, lk, &at) || (at.isEmpty() ? QString::fromLatin1(kDefaultServer) : at) != server)
		return;
	const QString key = B64(fp);
	if(key == OwnFp(state))
		return;
	QJsonObject friends = state.value(QStringLiteral("friends")).toObject();
	QJsonObject f = friends.value(key).toObject();
	if(f.value(QStringLiteral("state")).toString() == QLatin1String("friend"))
		return;
	// Sent again: the token handed out with the request before goes
	if(!f.value(QStringLiteral("my_token")).toString().isEmpty())
		Call(QStringLiteral("token_remove"), { { QStringLiteral("hash"), TokenHash(f.value(QStringLiteral("my_token")).toString()) } }, true, nullptr);
	const QString my_token = NewToken(state, key);
	f[QStringLiteral("state")] = QStringLiteral("requested");
	f[QStringLiteral("lk")] = Hex(lk);
	f[QStringLiteral("my_token")] = my_token;
	f[QStringLiteral("text")] = text.left(200);
	f[QStringLiteral("since")] = static_cast<double>(NowMs());
	friends[key] = f;
	state[QStringLiteral("friends")] = friends;
	f.remove(QStringLiteral("unsent"));
	friends[key] = f;
	state[QStringLiteral("friends")] = friends;
	Save();
	SyncOwn(key);
	emit changed();
	QJsonObject payload;
	payload[QStringLiteral("t")] = QStringLiteral("request");
	payload[QStringLiteral("lk")] = B64(ToBytes(QByteArray::fromHex(state.value(QStringLiteral("lk")).toString().toLatin1())));
	payload[QStringLiteral("text")] = text.left(200);
	payload[QStringLiteral("token")] = my_token;
	// The token is on the server before they can use it: their answer comes with it. Then their
	// card: their name for the list, and the devices the request goes to
	Call(QStringLiteral("token_add"), { { QStringLiteral("hash"), TokenHash(my_token) } }, true, [this, key, payload](bool, const QJsonObject &) {
		QPointer<FriendsClient> self(this);
		WithCard(key, [self, key, payload](bool ok, const fc::Card &card) {
			if(!self)
				return;
			auto unsent = [self, key]() {
				QJsonObject friends = self->state.value(QStringLiteral("friends")).toObject();
				QJsonObject f = friends.value(key).toObject();
				if(f.value(QStringLiteral("state")).toString() != QLatin1String("requested"))
					return;
				f[QStringLiteral("unsent")] = true;
				friends[key] = f;
				self->state[QStringLiteral("friends")] = friends;
				self->Save();
				emit self->changed();
			};
			if(!ok)
			{
				unsent();
				return;
			}
			RefreshProfile(self->state, key, card);
			self->Save();
			emit self->changed();
			self->SendPayload(key, payload, true, [unsent](bool sent) {
				if(!sent)
					unsent();
			});
		});
	});
}

void FriendsClient::resendRequest(const QString &fp)
{
	const QJsonObject f = state.value(QStringLiteral("friends")).toObject().value(fp).toObject();
	fc::Id16 lk{};
	if(f.value(QStringLiteral("state")).toString() != QLatin1String("requested") || !Unhex(f.value(QStringLiteral("lk")).toString(), lk))
		return;
	QString link = QStringLiteral("psrpt://friend/%1.%2").arg(fp, B64(lk));
	if(server != QLatin1String(kDefaultServer))
		link += QStringLiteral("?at=") + QString::fromLatin1(QUrl::toPercentEncoding(server));
	sendRequest(link, f.value(QStringLiteral("text")).toString());
}

void FriendsClient::accept(const QString &fp)
{
	QJsonObject friends = state.value(QStringLiteral("friends")).toObject();
	QJsonObject f = friends.value(fp).toObject();
	if(f.value(QStringLiteral("state")).toString() != QLatin1String("incoming"))
		return;
	const QString my_token = NewToken(state, fp);
	f[QStringLiteral("state")] = QStringLiteral("friend");
	f[QStringLiteral("my_token")] = my_token;
	f[QStringLiteral("since")] = static_cast<double>(NowMs());
	friends[fp] = f;
	state[QStringLiteral("friends")] = friends;
	Save();
	const QJsonObject fk = state.value(QStringLiteral("fk")).toObject();
	QJsonObject payload;
	payload[QStringLiteral("t")] = QStringLiteral("accept");
	payload[QStringLiteral("lk")] = B64(ToBytes(QByteArray::fromHex(state.value(QStringLiteral("lk")).toString().toLatin1())));
	payload[QStringLiteral("token")] = my_token;
	payload[QStringLiteral("fk")] = QJsonObject{ { QStringLiteral("key"), fk.value(QStringLiteral("key")) }, { QStringLiteral("epoch"), fk.value(QStringLiteral("epoch")) } };
	// The token is on the server before they can use it: their answer comes with it
	Call(QStringLiteral("token_add"), { { QStringLiteral("hash"), TokenHash(my_token) } }, true, [this, fp, payload](bool, const QJsonObject &) {
		SendPayload(fp, payload, true);
	});
	SyncOwn(fp);
	PutStatus();
	emit changed();
}

void FriendsClient::decline(const QString &fp)
{
	QJsonObject friends = state.value(QStringLiteral("friends")).toObject();
	const QJsonObject f = friends.value(fp).toObject();
	if(f.value(QStringLiteral("state")).toString() == QLatin1String("incoming"))
		SendPayload(fp, QJsonObject{ { QStringLiteral("t"), QStringLiteral("decline") } }, true);
	friends.remove(fp);
	state[QStringLiteral("friends")] = friends;
	Save();
	SyncOwn(fp);
	emit changed();
}

/** A new friends key epoch: the one before stays with whoever was removed. */
static void NewFriendsEpoch(QJsonObject &state)
{
	QJsonObject fk = state.value(QStringLiteral("fk")).toObject();
	fc::Key32 key{};
	coplay::RandomBytes(key.data(), key.size());
	fk[QStringLiteral("epoch")] = fk.value(QStringLiteral("epoch")).toInt() + 1;
	fk[QStringLiteral("key")] = Hex(key);
	fk[QStringLiteral("start")] = static_cast<double>(NowMs());
	state[QStringLiteral("fk")] = fk;
}

void FriendsClient::unfriend(const QString &fp)
{
	QJsonObject friends = state.value(QStringLiteral("friends")).toObject();
	const QJsonObject f = friends.value(fp).toObject();
	if(f.isEmpty())
		return;
	SendPayload(fp, QJsonObject{ { QStringLiteral("t"), QStringLiteral("unfriend") } }, true);
	const QString my_token = f.value(QStringLiteral("my_token")).toString();
	if(!my_token.isEmpty())
		Call(QStringLiteral("token_remove"), { { QStringLiteral("hash"), TokenHash(my_token) } }, true, nullptr);
	friends.remove(fp);
	state[QStringLiteral("friends")] = friends;
	NewFriendsEpoch(state);
	Save();
	statuses.remove(fp);
	// Everybody else gets the new key, so the removed one reads no status any more
	for(auto it = friends.constBegin(); it != friends.constEnd(); ++it)
	{
		if(it.value().toObject().value(QStringLiteral("state")).toString() == QLatin1String("friend"))
			SendFriendsKey(it.key());
	}
	SyncOwn(fp, true);
	PutStatus();
	emit changed();
}

void FriendsClient::block(const QString &fp)
{
	QJsonObject friends = state.value(QStringLiteral("friends")).toObject();
	QJsonObject f = friends.value(fp).toObject();
	const QString my_token = f.value(QStringLiteral("my_token")).toString();
	if(!my_token.isEmpty())
		Call(QStringLiteral("token_remove"), { { QStringLiteral("hash"), TokenHash(my_token) } }, true, nullptr);
	const bool was_friend = f.value(QStringLiteral("state")).toString() == QLatin1String("friend");
	QJsonObject blocked;
	blocked[QStringLiteral("state")] = QStringLiteral("blocked");
	blocked[QStringLiteral("name")] = f.value(QStringLiteral("name"));
	friends[fp] = blocked;
	state[QStringLiteral("friends")] = friends;
	// Their conversations go
	QJsonObject sessions = state.value(QStringLiteral("sessions")).toObject();
	for(const QString &k : sessions.keys())
	{
		if(k.startsWith(fp + QLatin1Char(':')))
			sessions.remove(k);
	}
	state[QStringLiteral("sessions")] = sessions;
	if(was_friend)
		NewFriendsEpoch(state);
	Save();
	statuses.remove(fp);
	if(was_friend)
	{
		for(auto it = friends.constBegin(); it != friends.constEnd(); ++it)
		{
			if(it.value().toObject().value(QStringLiteral("state")).toString() == QLatin1String("friend"))
				SendFriendsKey(it.key());
		}
		PutStatus();
	}
	SyncOwn(fp, was_friend);
	emit changed();
}

void FriendsClient::SendFriendsKey(const QString &fp)
{
	const QJsonObject fk = state.value(QStringLiteral("fk")).toObject();
	SendPayload(fp, QJsonObject{ { QStringLiteral("t"), QStringLiteral("fk") }, { QStringLiteral("key"), fk.value(QStringLiteral("key")) },
		{ QStringLiteral("epoch"), fk.value(QStringLiteral("epoch")) } }, false);
}

void FriendsClient::unblock(const QString &fp)
{
	QJsonObject friends = state.value(QStringLiteral("friends")).toObject();
	if(friends.value(fp).toObject().value(QStringLiteral("state")).toString() != QLatin1String("blocked"))
		return;
	friends.remove(fp);
	state[QStringLiteral("friends")] = friends;
	Save();
	SyncOwn(fp);
	emit changed();
}

void FriendsClient::setChecked(const QString &fp, bool checked)
{
	QJsonObject friends = state.value(QStringLiteral("friends")).toObject();
	QJsonObject f = friends.value(fp).toObject();
	if(f.isEmpty())
		return;
	f[QStringLiteral("checked")] = checked;
	friends[fp] = f;
	state[QStringLiteral("friends")] = friends;
	Save();
	SyncOwn(fp);
	emit changed();
}

void FriendsClient::newLink()
{
	if(!enabled)
		return;
	fc::Id16 lk{};
	coplay::RandomBytes(lk.data(), lk.size());
	state[QStringLiteral("lk")] = Hex(lk);
	Save();
	// The card's profile is sealed with the new key; the friends get it to keep reading it
	PublishCard();
	const QJsonObject friends = state.value(QStringLiteral("friends")).toObject();
	for(auto it = friends.constBegin(); it != friends.constEnd(); ++it)
	{
		if(it.value().toObject().value(QStringLiteral("state")).toString() == QLatin1String("friend"))
			SendPayload(it.key(), QJsonObject{ { QStringLiteral("t"), QStringLiteral("lk") }, { QStringLiteral("lk"), B64(lk) } }, false);
	}
	SyncOwn(QString(), false, true);
	emit changed();
}

// ------------------------------------------------------------------ chats

void FriendsClient::AddMessage(const QString &chat, const QJsonObject &message)
{
	QJsonObject chats = state.value(QStringLiteral("chats")).toObject();
	QJsonObject c = chats.value(chat).toObject();
	QJsonArray msgs = c.value(QStringLiteral("m")).toArray();
	const QString id = message.value(QStringLiteral("id")).toString();
	for(int i = 0; i < msgs.size(); i++)
	{
		if(msgs[i].toObject().value(QStringLiteral("id")).toString() == id)
		{
			// The same message again (a resend, or the own copy): only its state changes
			QJsonObject m = msgs[i].toObject();
			if(message.contains(QStringLiteral("state")))
				m[QStringLiteral("state")] = message.value(QStringLiteral("state"));
			msgs[i] = m;
			c[QStringLiteral("m")] = msgs;
			chats[chat] = c;
			state[QStringLiteral("chats")] = chats;
			Save();
			emit changed();
			return;
		}
	}
	// Deleted by its writer before it came: only a stand-in
	QJsonObject adding = message;
	const QJsonArray gone = c.value(QStringLiteral("gone")).toArray();
	for(const QJsonValue &g : gone)
	{
		if(g.toObject().value(QStringLiteral("id")).toString() == id && g.toObject().value(QStringLiteral("by")).toString() == message.value(QStringLiteral("from")).toString()
			&& !message.value(QStringLiteral("system")).toBool())
		{
			adding = QJsonObject();
			for(const char *key : { "id", "at", "from", "mine", "state" })
			{
				if(message.contains(QLatin1String(key)))
					adding[QLatin1String(key)] = message.value(QLatin1String(key));
			}
			adding[QStringLiteral("deleted")] = true;
			adding[QStringLiteral("text")] = QString();
		}
	}
	// In the order they were written: two sent at once may reach the server the other way round
	const double at = message.value(QStringLiteral("at")).toDouble();
	qsizetype place = msgs.size();
	while(place > 0 && msgs[place - 1].toObject().value(QStringLiteral("at")).toDouble() > at)
		place--;
	msgs.insert(place, adding);
	while(msgs.size() > kMessagesKept)
		msgs.removeFirst();
	c[QStringLiteral("m")] = msgs;
	chats[chat] = c;
	state[QStringLiteral("chats")] = chats;
	Save();
	emit changed();
}

QVariantList FriendsClient::messages(const QString &chat) const
{
	QVariantList out;
	if(!enabled)
		return out;
	const QJsonObject friends = state.value(QStringLiteral("friends")).toObject();
	const QJsonObject names = state.value(QStringLiteral("groups")).toObject().value(chat).toObject().value(QStringLiteral("names")).toObject();
	const QJsonArray msgs = state.value(QStringLiteral("chats")).toObject().value(chat).toObject().value(QStringLiteral("m")).toArray();
	const QJsonObject read = state.value(QStringLiteral("chats")).toObject().value(chat).toObject().value(QStringLiteral("read")).toObject();
	const QString me = OwnFp(state);
	auto name_of = [&](const QString &fp) {
		if(fp == me)
			return profile_name;
		return friends.value(fp).toObject().value(QStringLiteral("name")).toString(names.value(fp).toString());
	};
	// Who can be mentioned here, drawn as mentions in the text
	QStringList mention_names;
	for(const QVariant &v : mentionable(chat))
		mention_names.append(v.toMap().value(QStringLiteral("name")).toString());
	if(!profile_name.isEmpty())
		mention_names.append(profile_name);
	QHash<QString, qsizetype> index;
	for(qsizetype i = 0; i < msgs.size(); i++)
		index.insert(msgs[i].toObject().value(QStringLiteral("id")).toString(), i);
	for(const QJsonValue &v : msgs)
	{
		QVariantMap m = v.toObject().toVariantMap();
		const QString from = m.value(QStringLiteral("from")).toString();
		m[QStringLiteral("name")] = m.value(QStringLiteral("mine")).toBool() ? profile_name : name_of(from);
		m[QStringLiteral("html")] = chattext::Html(m.value(QStringLiteral("text")).toString(), mention_names);
		m[QStringLiteral("mentionsMe")] = !m.value(QStringLiteral("mine")).toBool() && !m.value(QStringLiteral("deleted")).toBool()
			&& chattext::Mentions(m.value(QStringLiteral("text")).toString(), profile_name);
		// The message it answers: as it is here, else the copy that came with it
		const QString reply = m.value(QStringLiteral("reply")).toString();
		if(!reply.isEmpty())
		{
			QVariantMap to{ { QStringLiteral("id"), reply } };
			if(index.contains(reply))
			{
				const QJsonObject original = msgs[index.value(reply)].toObject();
				const bool gone = original.value(QStringLiteral("deleted")).toBool();
				to[QStringLiteral("found")] = true;
				to[QStringLiteral("deleted")] = gone;
				to[QStringLiteral("name")] = original.value(QStringLiteral("mine")).toBool() ? profile_name : name_of(original.value(QStringLiteral("from")).toString());
				to[QStringLiteral("text")] = gone ? tr("Deleted message") : ClipUtf8(original.value(QStringLiteral("text")).toString(), kMaxQuoteBytes);
			}
			else
			{
				const QVariantMap quote = m.value(QStringLiteral("quote")).toMap();
				to[QStringLiteral("found")] = false;
				to[QStringLiteral("deleted")] = false;
				to[QStringLiteral("name")] = quote.isEmpty() ? QString() : name_of(quote.value(QStringLiteral("from")).toString());
				to[QStringLiteral("text")] = quote.isEmpty() ? tr("A message from earlier") : quote.value(QStringLiteral("text")).toString();
			}
			m[QStringLiteral("replyTo")] = to;
		}
		// The preview's picture as the chat shows it
		if(m.contains(QStringLiteral("embed")))
		{
			QVariantMap embed = m.value(QStringLiteral("embed")).toMap();
			const QString image = embed.value(QStringLiteral("image")).toString();
			embed[QStringLiteral("image")] = image.isEmpty() ? QString() : QStringLiteral("data:image/jpeg;base64,") + image;
			m[QStringLiteral("embed")] = embed;
		}
		// Read: by the friend, or by how many in a group (both ways off with read receipts off)
		if(m.value(QStringLiteral("mine")).toBool() && ReadReceipts())
		{
			const double at = m.value(QStringLiteral("at")).toDouble();
			if(names.isEmpty() && !isGroup(chat))
				m[QStringLiteral("read")] = read.value(chat).toDouble() >= at;
			else
			{
				int by = 0;
				for(auto it = read.constBegin(); it != read.constEnd(); ++it)
					by += it.key() != OwnFp(state) && it.value().toDouble() >= at ? 1 : 0;
				m[QStringLiteral("readBy")] = by;
			}
		}
		out.append(m);
	}
	return out;
}

void FriendsClient::send(const QString &chat, const QString &text, const QString &reply)
{
	// At most 3500 bytes of UTF-8: with what sealing adds, a note stays under the server's 8 KB
	QByteArray utf8 = chattext::WithEmoji(text.trimmed()).toUtf8();
	if(utf8.size() > 3500)
		utf8.truncate(3500);
	QString trimmed = QString::fromUtf8(utf8);
	while(trimmed.endsWith(QChar(QChar::ReplacementCharacter)))
		trimmed.chop(1);
	if(!enabled || trimmed.isEmpty())
		return;
	QJsonObject payload;
	payload[QStringLiteral("t")] = QStringLiteral("text");
	payload[QStringLiteral("id")] = RandomId();
	payload[QStringLiteral("at")] = static_cast<double>(NowMs());
	payload[QStringLiteral("text")] = trimmed;
	// An answer: the message's id, and the start of it for whoever does not have it
	if(!reply.isEmpty())
	{
		const QJsonObject original = FindMessage(chat, reply);
		if(!original.isEmpty() && !original.value(QStringLiteral("deleted")).toBool() && !original.value(QStringLiteral("system")).toBool())
		{
			payload[QStringLiteral("reply")] = reply;
			payload[QStringLiteral("quote")] = QJsonObject{ { QStringLiteral("from"), original.value(QStringLiteral("from")).toString() },
				{ QStringLiteral("text"), ClipUtf8(original.value(QStringLiteral("text")).toString(), kMaxQuoteBytes) } };
		}
	}
	QJsonObject mine = payload;
	mine[QStringLiteral("from")] = OwnFp(state);
	mine[QStringLiteral("mine")] = true;
	mine[QStringLiteral("state")] = QStringLiteral("sending");
	AddMessage(chat, mine);
	const QUrl link = link_previews ? CoPlayLinkPreview::FirstLink(trimmed) : QUrl();
	if(link.isEmpty())
	{
		SendOwnMessage(chat, payload);
		return;
	}
	// The preview first, as in a session's chat: only this app opens the link, once, and the
	// others get what it found, sealed with the message; they never contact the site themselves
	QPointer<FriendsClient> self(this);
	const int now = generation;
	pending_calls++;
	CoPlayLinkPreview::Fetch(this, link, [self, now, chat, payload](const coplay::ChatEmbed &embed, const QByteArray &image) {
		if(!self)
			return;
		self->pending_calls--;
		if(!self->enabled || self->generation != now)
			return;
		// Deleted while its preview was made: it never goes out
		if(self->FindMessage(chat, payload.value(QStringLiteral("id")).toString()).value(QStringLiteral("deleted")).toBool())
			return;
		QJsonObject p = payload;
		const QJsonObject e = MakeEmbed(embed, image, p);
		if(!e.isEmpty())
		{
			p[QStringLiteral("embed")] = e;
			self->SetOwnEmbed(chat, p.value(QStringLiteral("id")).toString(), e);
		}
		self->SendOwnMessage(chat, p);
	});
}

void FriendsClient::SendOwnMessage(const QString &chat, const QJsonObject &payload)
{
	QPointer<FriendsClient> self(this);
	const QString id = payload.value(QStringLiteral("id")).toString();
	SendToChat(chat, payload, [self, chat, id](bool ok) {
		if(self)
			self->AddMessage(chat, QJsonObject{ { QStringLiteral("id"), id }, { QStringLiteral("state"), ok ? QStringLiteral("sent") : QStringLiteral("failed") } });
	});
}

void FriendsClient::SetOwnEmbed(const QString &chat, const QString &id, const QJsonObject &embed)
{
	QJsonObject chats = state.value(QStringLiteral("chats")).toObject();
	QJsonObject c = chats.value(chat).toObject();
	QJsonArray msgs = c.value(QStringLiteral("m")).toArray();
	for(int i = 0; i < msgs.size(); i++)
	{
		QJsonObject m = msgs[i].toObject();
		if(m.value(QStringLiteral("id")).toString() != id || !m.value(QStringLiteral("mine")).toBool())
			continue;
		m[QStringLiteral("embed")] = embed;
		msgs[i] = m;
		c[QStringLiteral("m")] = msgs;
		chats[chat] = c;
		state[QStringLiteral("chats")] = chats;
		Save();
		emit changed();
		return;
	}
}

QJsonObject FriendsClient::FindMessage(const QString &chat, const QString &id) const
{
	if(id.isEmpty())
		return QJsonObject();
	for(const QJsonValue &v : state.value(QStringLiteral("chats")).toObject().value(chat).toObject().value(QStringLiteral("m")).toArray())
	{
		if(v.toObject().value(QStringLiteral("id")).toString() == id)
			return v.toObject();
	}
	return QJsonObject();
}

void FriendsClient::deleteMessage(const QString &chat, const QString &message)
{
	if(!enabled)
		return;
	const QJsonObject m = FindMessage(chat, message);
	if(!m.value(QStringLiteral("mine")).toBool() || !ApplyDelete(chat, message, OwnFp(state)))
		return;
	// To the friend and the own other devices, or to the group (the own devices in it too)
	SendToChat(chat, QJsonObject{ { QStringLiteral("t"), QStringLiteral("delete") }, { QStringLiteral("msg"), message } }, nullptr);
}

bool FriendsClient::ApplyDelete(const QString &chat, const QString &message, const QString &by)
{
	if(message.isEmpty() || message.size() > kMaxIdLength || by.isEmpty())
		return false;
	QJsonObject chats = state.value(QStringLiteral("chats")).toObject();
	QJsonObject c = chats.value(chat).toObject();
	QJsonArray msgs = c.value(QStringLiteral("m")).toArray();
	for(int i = 0; i < msgs.size(); i++)
	{
		const QJsonObject m = msgs[i].toObject();
		if(m.value(QStringLiteral("id")).toString() != message)
			continue;
		// Only the writer, only a message, only once
		if(m.value(QStringLiteral("from")).toString() != by || m.value(QStringLiteral("system")).toBool() || m.value(QStringLiteral("deleted")).toBool())
			return false;
		QJsonObject gone;
		for(const char *key : { "id", "at", "from", "mine", "state" })
		{
			if(m.contains(QLatin1String(key)))
				gone[QLatin1String(key)] = m.value(QLatin1String(key));
		}
		gone[QStringLiteral("deleted")] = true;
		gone[QStringLiteral("text")] = QString();
		msgs[i] = gone;
		c[QStringLiteral("m")] = msgs;
		chats[chat] = c;
		state[QStringLiteral("chats")] = chats;
		Save();
		emit changed();
		return true;
	}
	// Not here yet (the delete overtook it): kept out when it comes
	QJsonArray kept = c.value(QStringLiteral("gone")).toArray();
	kept.append(QJsonObject{ { QStringLiteral("id"), message }, { QStringLiteral("by"), by } });
	while(kept.size() > kGoneKept)
		kept.removeFirst();
	c[QStringLiteral("gone")] = kept;
	chats[chat] = c;
	state[QStringLiteral("chats")] = chats;
	Save();
	return false;
}

bool FriendsClient::Muted(const QString &chat) const
{
	return state.value(QStringLiteral("chats")).toObject().value(chat).toObject().value(QStringLiteral("mute")).toDouble() > static_cast<double>(NowMs());
}

double FriendsClient::mutedUntil(const QString &chat) const
{
	const double until = state.value(QStringLiteral("chats")).toObject().value(chat).toObject().value(QStringLiteral("mute")).toDouble();
	return until > static_cast<double>(NowMs()) ? until : 0;
}

void FriendsClient::mute(const QString &chat, int minutes)
{
	if(!enabled || chat.isEmpty())
		return;
	QJsonObject chats = state.value(QStringLiteral("chats")).toObject();
	QJsonObject c = chats.value(chat).toObject();
	if(minutes == 0)
		c.remove(QStringLiteral("mute"));
	else
		c[QStringLiteral("mute")] = minutes < 0 ? kMutedForGood : static_cast<double>(NowMs()) + minutes * 60000.0;
	chats[chat] = c;
	state[QStringLiteral("chats")] = chats;
	Save();
	emit changed();
}

QVariantList FriendsClient::mentionable(const QString &chat) const
{
	QVariantList out;
	if(!enabled)
		return out;
	const QJsonObject friends = state.value(QStringLiteral("friends")).toObject();
	const QString me = OwnFp(state);
	const QJsonObject g = state.value(QStringLiteral("groups")).toObject().value(chat).toObject();
	QStringList fps;
	if(g.isEmpty())
		fps.append(chat);
	else
		fps = Fps(g.value(QStringLiteral("members")));
	const QJsonObject names = g.value(QStringLiteral("names")).toObject();
	for(const QString &fp : fps)
	{
		if(fp == me)
			continue;
		const QJsonObject f = friends.value(fp).toObject();
		const QString name = f.value(QStringLiteral("name")).toString(names.value(fp).toString());
		if(!name.isEmpty())
			out.append(QVariantMap{ { QStringLiteral("fp"), fp }, { QStringLiteral("name"), name }, { QStringLiteral("avatar"), AvatarUrl(f) } });
	}
	return out;
}

void FriendsClient::Notify(const QString &chat, const QString &name, const QString &text)
{
	if(Muted(chat) && !chattext::Mentions(text, profile_name))
		return;
	emit messageArrived(chat, name, text);
}

void FriendsClient::sendJoin(const QString &chat, const QVariantMap &r)
{
	if(!enabled)
		return;
	QJsonObject payload;
	payload[QStringLiteral("t")] = QStringLiteral("join");
	payload[QStringLiteral("id")] = RandomId();
	payload[QStringLiteral("at")] = static_cast<double>(NowMs());
	const QJsonObject card = CleanRoom(QJsonObject::fromVariantMap(r));
	if(card.isEmpty())
		return;
	payload[QStringLiteral("join")] = card;
	// For whoever shows only text (a notification, an older app)
	payload[QStringLiteral("text")] = tr("Invites you to %1").arg(card.value(QStringLiteral("title")).toString(tr("a session")));
	QJsonObject mine = payload;
	mine[QStringLiteral("from")] = OwnFp(state);
	mine[QStringLiteral("mine")] = true;
	mine[QStringLiteral("state")] = QStringLiteral("sending");
	AddMessage(chat, mine);
	QPointer<FriendsClient> self(this);
	const QString id = payload.value(QStringLiteral("id")).toString();
	SendToChat(chat, payload, [self, chat, id](bool ok) {
		if(self)
			self->AddMessage(chat, QJsonObject{ { QStringLiteral("id"), id }, { QStringLiteral("state"), ok ? QStringLiteral("sent") : QStringLiteral("failed") } });
	});
}

void FriendsClient::markRead(const QString &chat)
{
	QJsonObject chats = state.value(QStringLiteral("chats")).toObject();
	QJsonObject c = chats.value(chat).toObject();
	if(c.isEmpty())
		return;
	// Only when something was unread: a change says so to everybody who shows the chat
	const double seen = c.value(QStringLiteral("seen")).toDouble();
	bool unread = false;
	for(const QJsonValue &v : c.value(QStringLiteral("m")).toArray())
	{
		const QJsonObject m = v.toObject();
		unread = unread || (!m.value(QStringLiteral("mine")).toBool() && !m.value(QStringLiteral("deleted")).toBool() && m.value(QStringLiteral("at")).toDouble() > seen);
	}
	if(!unread)
		return;
	// The newest message's time, not this clock's: a friend's clock ahead of ours would stay unread
	double newest = static_cast<double>(NowMs());
	QString upto;
	double upto_at = 0;
	for(const QJsonValue &v : c.value(QStringLiteral("m")).toArray())
	{
		const QJsonObject m = v.toObject();
		newest = std::max(newest, m.value(QStringLiteral("at")).toDouble());
		if(!m.value(QStringLiteral("mine")).toBool() && !m.value(QStringLiteral("system")).toBool() && m.value(QStringLiteral("at")).toDouble() >= upto_at)
		{
			upto = m.value(QStringLiteral("id")).toString();
			upto_at = m.value(QStringLiteral("at")).toDouble();
		}
	}
	c[QStringLiteral("seen")] = newest;
	chats[chat] = c;
	state[QStringLiteral("chats")] = chats;
	Save();
	emit changed();
	if(upto.isEmpty())
		return;
	// "Read up to here": to the others if the player shows that, to the own other devices always
	const QJsonObject seen_payload{ { QStringLiteral("t"), QStringLiteral("seen") }, { QStringLiteral("upto"), upto },
		{ QStringLiteral("upto_at"), upto_at } };
	if(ReadReceipts())
		SendToChat(chat, seen_payload, nullptr);
	else if(!isGroup(chat))
		SendPayload(OwnFp(state), QJsonObject{ { QStringLiteral("t"), QStringLiteral("sync") }, { QStringLiteral("to"), chat },
			{ QStringLiteral("of"), seen_payload } }, false);
}

void FriendsClient::ApplyRead(const QString &chat, const QString &fp, double at)
{
	QJsonObject chats = state.value(QStringLiteral("chats")).toObject();
	QJsonObject c = chats.value(chat).toObject();
	QJsonObject read = c.value(QStringLiteral("read")).toObject();
	if(read.value(fp).toDouble() >= at)
		return;
	read[fp] = at;
	c[QStringLiteral("read")] = read;
	chats[chat] = c;
	state[QStringLiteral("chats")] = chats;
	Save();
	emit changed();
}

void FriendsClient::MarkSeenUpTo(const QString &chat, double at)
{
	QJsonObject chats = state.value(QStringLiteral("chats")).toObject();
	QJsonObject c = chats.value(chat).toObject();
	if(c.isEmpty() || c.value(QStringLiteral("seen")).toDouble() >= at)
		return;
	c[QStringLiteral("seen")] = at;
	chats[chat] = c;
	state[QStringLiteral("chats")] = chats;
	Save();
	emit changed();
}

bool FriendsClient::ReadReceipts() const
{
	return state.value(QStringLiteral("settings")).toObject().value(QStringLiteral("read_receipts")).toBool(true);
}

void FriendsClient::SetReadReceipts(bool on)
{
	QJsonObject s = state.value(QStringLiteral("settings")).toObject();
	s[QStringLiteral("read_receipts")] = on;
	state[QStringLiteral("settings")] = s;
	Save();
	emit changed();
}

// ------------------------------------------------------------------ sending sealed payloads

void FriendsClient::WithCard(const QString &fp, std::function<void(bool, const fc::Card &)> done)
{
	fc::Card card;
	fc::Bytes bytes;
	const QString cached = state.value(QStringLiteral("cards")).toObject().value(fp).toString();
	if(NowMs() - card_fetched.value(fp) < 10 * 60 * 1000 && UnB64(cached, bytes) && fc::DecodeCard(bytes, card))
	{
		done(true, card);
		return;
	}
	QPointer<FriendsClient> self(this);
	Call(QStringLiteral("card_get"), { { QStringLiteral("fp"), fp } }, false, [self, fp, done](bool ok, const QJsonObject &answer) {
		if(!self)
			return;
		fc::Card card;
		if(ok && KeepCard(self->state, fp, answer.value(QStringLiteral("card")).toString(), card))
		{
			self->card_fetched[fp] = NowMs();
			RefreshProfile(self->state, fp, card);
			self->Save();
			done(true, card);
			return;
		}
		done(false, card);
	});
}

void FriendsClient::SendPayload(const QString &fp, QJsonObject payload, bool to_own_devices, std::function<void(bool)> done)
{
	if(!enabled)
	{
		if(done)
			done(false);
		return;
	}
	if(!payload.contains(QStringLiteral("id")))
		payload[QStringLiteral("id")] = RandomId();
	if(!payload.contains(QStringLiteral("at")))
		payload[QStringLiteral("at")] = static_cast<double>(NowMs());
	const QByteArray plain = QJsonDocument(payload).toJson(QJsonDocument::Compact);
	const QJsonObject f = state.value(QStringLiteral("friends")).toObject().value(fp).toObject();
	const bool own = fp == OwnFp(state);
	// A request goes into their request box; everything else into their devices' inboxes, with their token
	const bool request_box = payload.value(QStringLiteral("t")).toString() == QLatin1String("request");
	QString target;
	if(request_box)
	{
		fc::Id16 lk{};
		if(Unhex(f.value(QStringLiteral("lk")).toString(), lk))
			target = B64(fc::RequestBox(lk));
	}
	else
		target = own ? state.value(QStringLiteral("self_token")).toString() : f.value(QStringLiteral("token")).toString();
	if(target.isEmpty())
	{
		if(done)
			done(false);
		return;
	}
	// The own other devices get a copy, so each of them shows the whole chat
	if(to_own_devices && !own)
		SendPayload(OwnFp(state), QJsonObject{ { QStringLiteral("t"), QStringLiteral("sync") }, { QStringLiteral("to"), fp }, { QStringLiteral("of"), payload } }, false);
	SendSealed(fp, plain, request_box ? Route::RequestBox : Route::Inbox, target, QString(), done);
}

void FriendsClient::SendSealed(const QString &fp, const QByteArray &plain, Route route, const QString &target, const QString &token,
	std::function<void(bool)> done)
{
	const bool own = fp == OwnFp(state);
	QPointer<FriendsClient> self(this);
	WithCard(fp, [self, fp, own, plain, route, target, token, done](bool ok, const fc::Card &card) {
		if(!self)
			return;
		if(!ok)
		{
			if(done)
				done(false);
			return;
		}
		fc::DeviceId own_device{};
		OwnDevice(self->state, own_device);
		auto send_all = [self, fp, own, own_device, plain, route, target, token, done](const fc::Card &card, const QJsonObject &prekeys) {
			auto fan = std::make_shared<Fanout>();
			fan->done = done;
			for(const fc::CardDevice &d : card.devices)
			{
				if(own && d.id == own_device)
					continue;
				fan->pending++;
				self->SendToDevice(fp, card, d, prekeys.value(B64(d.id)).toObject(), plain, route, target, token,
					[fan](bool device_ok) { fan->Finish(device_ok); });
			}
			fan->Finish(true);
		};
		// Devices without a conversation yet get a one-time prekey each; the others need none
		QStringList missing;
		for(const fc::CardDevice &d : card.devices)
		{
			if(!(own && d.id == own_device) && SessionsOf(self->state, SessionKey(fp, d.id)).isEmpty())
				missing.append(B64(d.id));
		}
		if(missing.isEmpty())
		{
			send_all(card, QJsonObject());
			return;
		}
		self->Call(QStringLiteral("bundle_get"), { { QStringLiteral("fp"), fp }, { QStringLiteral("devices"), missing.join(QLatin1Char(',')) } }, false,
			[self, fp, card, send_all](bool bundle_ok, const QJsonObject &answer) {
				if(!self)
					return;
				fc::Card fresh = card;
				fc::Card from_bundle;
				if(bundle_ok && KeepCard(self->state, fp, answer.value(QStringLiteral("card")).toString(), from_bundle))
					fresh = from_bundle;
				send_all(fresh, bundle_ok ? answer.value(QStringLiteral("prekeys")).toObject() : QJsonObject());
			});
	});
}

void FriendsClient::SendToDevice(const QString &fp, const fc::Card &card, const fc::CardDevice &device, const QJsonObject &opk,
	const QByteArray &plain, Route route, const QString &target, const QString &token, std::function<void(bool)> done)
{
	fc::Identity id;
	fc::DeviceId own_device{};
	if(!Identity(id) || !OwnDevice(state, own_device))
	{
		done(false);
		return;
	}
	const QString key = SessionKey(fp, device.id);
	QJsonArray list = SessionsOf(state, key);
	QJsonObject session = list.isEmpty() ? QJsonObject() : list.first().toObject();
	fc::Ratchet ratchet;
	fc::Bytes saved;
	if(!UnB64(session.value(QStringLiteral("r")).toString(), saved) || !fc::Ratchet::Load(saved, ratchet))
	{
		// A new conversation with this device: X3DH with its signed prekey, and a one-time prekey if the server had one
		fc::PrekeyBundle bundle;
		bundle.fp = card.fp;
		bundle.ikd = card.ikd;
		bundle.device = device.id;
		bundle.spk_id = device.spk_id;
		bundle.spk = device.spk;
		if(!opk.isEmpty() && UnB64(opk.value(QStringLiteral("pub")).toString(), bundle.opk) && fc::ValidPublic(bundle.opk))
			bundle.opk_id = static_cast<uint32_t>(opk.value(QStringLiteral("id")).toDouble());
		fc::X3dhStart start;
		if(!fc::X3dhInitiate(id, own_device, bundle, start) || !ratchet.InitInitiator(start.sk, device.spk))
		{
			done(false);
			return;
		}
		session = QJsonObject();
		session[QStringLiteral("ad")] = B64(start.ad);
		QJsonObject x3dh;
		x3dh[QStringLiteral("ek")] = B64(start.ek.pub);
		x3dh[QStringLiteral("spk_id")] = static_cast<double>(bundle.spk_id);
		x3dh[QStringLiteral("opk_id")] = static_cast<double>(bundle.opk_id);
		session[QStringLiteral("x3dh")] = x3dh;
		list.prepend(session);
	}
	fc::Envelope e;
	e.from_fp = id.fp;
	e.from_device = own_device;
	e.to_device = device.id;
	// Until the other device has answered, every message carries what X3DH needs
	const QJsonObject x3dh = session.value(QStringLiteral("x3dh")).toObject();
	if(!x3dh.isEmpty())
	{
		e.kind = 1;
		UnB64(x3dh.value(QStringLiteral("ek")).toString(), e.ek);
		e.spk_id = static_cast<uint32_t>(x3dh.value(QStringLiteral("spk_id")).toDouble());
		e.opk_id = static_cast<uint32_t>(x3dh.value(QStringLiteral("opk_id")).toDouble());
	}
	fc::Bytes ad;
	UnB64(session.value(QStringLiteral("ad")).toString(), ad);
	if(!ratchet.Encrypt(ad, ToBytes(plain), e.header, e.sealed))
	{
		done(false);
		return;
	}
	// Kept before it goes out: one message key is never used twice, even after a crash
	session[QStringLiteral("r")] = B64(ratchet.Save());
	list[0] = session;
	StoreSessions(state, key, list);
	Save();
	const QString note = B64(e.Encode());
	if(route == Route::RequestBox)
		Call(QStringLiteral("request_put"), { { QStringLiteral("box"), target }, { QStringLiteral("note"), note } }, false,
			[done](bool ok, const QJsonObject &) { done(ok); });
	else if(route == Route::GroupBox)
		Call(QStringLiteral("group_put"), { { QStringLiteral("box"), target }, { QStringLiteral("token"), token }, { QStringLiteral("note"), note } }, false,
			[done](bool ok, const QJsonObject &) { done(ok); });
	else
		Call(QStringLiteral("inbox_put"), { { QStringLiteral("fp"), fp }, { QStringLiteral("device"), B64(device.id) }, { QStringLiteral("token"), target },
			{ QStringLiteral("note"), note } }, false, [done](bool ok, const QJsonObject &) { done(ok); });
}

void FriendsClient::SyncOwn(const QString &friend_fp, bool fk, bool lk)
{
	if(!enabled)
		return;
	QJsonObject p;
	p[QStringLiteral("t")] = QStringLiteral("own");
	if(!friend_fp.isEmpty())
	{
		const QJsonObject friends = state.value(QStringLiteral("friends")).toObject();
		QJsonObject record = friends.value(friend_fp).toObject();
		// The picture comes from their card on the other device too
		record.remove(QStringLiteral("avatar"));
		record[QStringLiteral("fp")] = friend_fp;
		if(!friends.contains(friend_fp))
			record[QStringLiteral("removed")] = true;
		p[QStringLiteral("friend")] = record;
	}
	if(fk)
		p[QStringLiteral("fk")] = state.value(QStringLiteral("fk"));
	if(lk)
		p[QStringLiteral("lk")] = state.value(QStringLiteral("lk"));
	SendPayload(OwnFp(state), p, false);
}

// ------------------------------------------------------------------ receiving

void FriendsClient::TakeNotes(const QJsonArray &notes, bool from_request_box, const QString &ack_field, const QString &ack_value)
{
	fc::DeviceId own_device{};
	OwnDevice(state, own_device);
	// Who starts a conversation is looked up first: X3DH needs their card
	const QJsonObject cards = state.value(QStringLiteral("cards")).toObject();
	QSet<QString> unknown;
	for(const QJsonValue &v : notes)
	{
		fc::Bytes note;
		fc::Envelope e;
		if(UnB64(v.toObject().value(QStringLiteral("note")).toString(), note) && fc::Envelope::Decode(note, e) && e.to_device == own_device
			&& e.kind == 1 && !cards.contains(B64(e.from_fp)))
			unknown.insert(B64(e.from_fp));
	}
	QPointer<FriendsClient> self(this);
	// Senders the server has no card of: their notes can never be opened and go (a box full of
	// them would keep real requests out)
	auto gone = std::make_shared<QSet<QString>>();
	auto handle = [self, notes, from_request_box, ack_field, ack_value, own_device, gone]() {
		if(!self)
			return;
		QStringList ids;
		for(const QJsonValue &v : notes)
		{
			const QJsonObject n = v.toObject();
			fc::Bytes note;
			fc::Envelope e;
			const bool readable = UnB64(n.value(QStringLiteral("note")).toString(), note) && fc::Envelope::Decode(note, e);
			// The request box is shared by the own devices: each takes away only its own notes
			if(readable && from_request_box && e.to_device != own_device)
				continue;
			if(!readable || self->HandleEnvelope(ToQ(note), from_request_box) || gone->contains(B64(e.from_fp)))
				ids.append(n.value(QStringLiteral("id")).toString());
			else
				qCDebug(lcFriends) << "note kept for later: the sender's card is missing";
		}
		if(!ids.isEmpty())
			self->Call(QStringLiteral("inbox_ack"), { { ack_field, ack_value }, { QStringLiteral("ids"), ids.join(QLatin1Char(',')) } }, true, nullptr);
	};
	if(unknown.isEmpty())
	{
		handle();
		return;
	}
	auto left = std::make_shared<int>(static_cast<int>(unknown.size()));
	for(const QString &fp : unknown)
	{
		Call(QStringLiteral("card_get"), { { QStringLiteral("fp"), fp } }, false, [self, fp, left, handle, gone](bool ok, const QJsonObject &answer) {
			if(!self)
				return;
			fc::Card card;
			if(ok && KeepCard(self->state, fp, answer.value(QStringLiteral("card")).toString(), card))
				self->card_fetched[fp] = NowMs();
			else if(answer.value(QStringLiteral("error")).toString() == QLatin1String("unknown"))
				gone->insert(fp);
			if(--*left == 0)
				handle();
		});
	}
}

bool FriendsClient::HandleEnvelope(const QByteArray &note, bool from_request_box, const QString &group)
{
	fc::Envelope e;
	fc::Identity id;
	fc::DeviceId own_device{};
	if(!fc::Envelope::Decode(ToBytes(note), e) || !Identity(id) || !OwnDevice(state, own_device) || e.to_device != own_device)
		return true;
	const QString from = B64(e.from_fp);
	const bool own = from == OwnFp(state);
	const QJsonObject f = state.value(QStringLiteral("friends")).toObject().value(from).toObject();
	if(f.value(QStringLiteral("state")).toString() == QLatin1String("blocked") || (own && e.from_device == own_device))
		return true;
	// Strangers reach only the request box, and members of a shared group its box
	const bool member = !group.isEmpty() && state.value(QStringLiteral("groups")).toObject().value(group).toObject()
		.value(QStringLiteral("members")).toArray().contains(from);
	if(!own && f.isEmpty() && !from_request_box && !member)
		return true;
	const QString key = SessionKey(from, e.from_device);
	QJsonArray list = SessionsOf(state, key);
	fc::Bytes plain;
	QJsonObject session;
	int used = -1;
	// A conversation already there: the newest first, then the older ones
	for(int i = 0; i < list.size() && used < 0; i++)
	{
		QJsonObject s = list[i].toObject();
		if(e.kind == 1 && s.value(QStringLiteral("their_ek")).toString() != B64(e.ek))
			continue;
		fc::Ratchet r;
		fc::Bytes saved, ad;
		if(!UnB64(s.value(QStringLiteral("r")).toString(), saved) || !fc::Ratchet::Load(saved, r) || !UnB64(s.value(QStringLiteral("ad")).toString(), ad))
			continue;
		if(!r.Decrypt(ad, e.header, e.sealed, plain, static_cast<uint64_t>(NowMs())))
			continue;
		s[QStringLiteral("r")] = B64(r.Save());
		// They answered: our messages need no X3DH part any more
		s.remove(QStringLiteral("x3dh"));
		session = s;
		used = i;
	}
	QJsonObject dev = state.value(QStringLiteral("device")).toObject();
	if(used < 0)
	{
		// A first message that started a conversation before is a copy: starting it again would
		// put a second conversation next to the one the other side goes on with
		if(e.kind != 1 || SeenEk(state, key, B64(e.ek)))
		{
			qCDebug(lcFriends) << "note not opened, kind" << e.kind;
			return true;
		}
		// A new conversation: X3DH with this device's prekeys and their identity from their card
		fc::Card card;
		fc::Bytes card_bytes;
		if(!UnB64(state.value(QStringLiteral("cards")).toObject().value(from).toString(), card_bytes) || !fc::DecodeCard(card_bytes, card))
			return false;
		fc::KeyPair spk;
		QString spk_hex;
		if(static_cast<uint32_t>(dev.value(QStringLiteral("spk_id")).toInt()) == e.spk_id)
			spk_hex = dev.value(QStringLiteral("spk")).toString();
		else if(static_cast<uint32_t>(dev.value(QStringLiteral("spk_prev_id")).toInt()) == e.spk_id)
			spk_hex = dev.value(QStringLiteral("spk_prev")).toString();
		if(!PairFrom(spk_hex, spk))
		{
			qCDebug(lcFriends) << "first message for a signed prekey gone:" << e.spk_id;
			return true;
		}
		fc::KeyPair opk;
		QJsonObject opks = dev.value(QStringLiteral("opks")).toObject();
		if(e.opk_id && !PairFrom(opks.value(QString::number(e.opk_id)).toString(), opk))
		{
			qCDebug(lcFriends) << "first message for a one-time prekey used already:" << e.opk_id;
			return true;
		}
		fc::Key32 sk{};
		fc::Bytes ad;
		if(!fc::X3dhRespond(id, own_device, spk, e.opk_id ? &opk : nullptr, e.from_fp, card.ikd, e.from_device, e.ek, sk, ad))
			return true;
		fc::Ratchet r;
		r.InitResponder(sk, spk);
		if(!r.Decrypt(ad, e.header, e.sealed, plain, static_cast<uint64_t>(NowMs())))
		{
			qCDebug(lcFriends) << "first message did not open";
			return true;
		}
		session[QStringLiteral("their_ek")] = B64(e.ek);
		session[QStringLiteral("ad")] = B64(ad);
		session[QStringLiteral("r")] = B64(r.Save());
		if(e.opk_id)
		{
			opks.remove(QString::number(e.opk_id));
			dev[QStringLiteral("opks")] = opks;
			state[QStringLiteral("device")] = dev;
		}
		list.prepend(session);
		used = 0;
	}
	// The one that worked is the one to answer with
	list.removeAt(used);
	list.prepend(session);
	StoreSessions(state, key, list);
	if(e.kind == 1)
		RememberEk(state, key, B64(e.ek));
	Save();
	const QJsonObject payload = QJsonDocument::fromJson(ToQ(plain)).object();
	qCDebug(lcFriends) << "opened" << payload.value(QStringLiteral("t")).toString() << "from" << from.left(6) << Hex(e.from_device).left(4)
		<< "kind" << e.kind << "conversation" << used;
	HandlePayload(from, payload, own, Hex(e.from_device), group);
	return true;
}

void FriendsClient::HandlePayload(const QString &from, const QJsonObject &p, bool own_device, const QString &from_device, const QString &group)
{
	const QString t = p.value(QStringLiteral("t")).toString();
	// Somebody who is no friend: from the request box only a request, from a group's box only
	// what the group needs
	const QString from_state = state.value(QStringLiteral("friends")).toObject().value(from).toObject().value(QStringLiteral("state")).toString();
	if(!own_device && from_state != QLatin1String("friend"))
	{
		if(!group.isEmpty() ? (t != QLatin1String("group") && t != QLatin1String("senderkey") && t != QLatin1String("leave"))
			: (t != QLatin1String("request") && t != QLatin1String("accept") && t != QLatin1String("decline") && t != QLatin1String("card")))
			return;
	}
	// What concerns a group: from any member (friends or not), through any way it came
	if(t == QLatin1String("group"))
	{
		ApplyGroup(from, p);
		return;
	}
	if(t == QLatin1String("senderkey") || t == QLatin1String("leave"))
	{
		const QString gid = p.value(QStringLiteral("group")).toString();
		QJsonObject groups = state.value(QStringLiteral("groups")).toObject();
		QJsonObject g = groups.value(gid).toObject();
		if(g.isEmpty() || g.value(QStringLiteral("left")).toBool() || !g.value(QStringLiteral("members")).toArray().contains(from))
			return;
		if(t == QLatin1String("leave"))
		{
			// One admin takes them out (the one with the lowest fingerprint), on one of its devices:
			// two would each make a token of their own
			QList<fc::Id16> admins;
			for(const QString &a : Fps(g.value(QStringLiteral("admins"))))
			{
				fc::Id16 id{};
				if(UnB64(a, id))
					admins.append(id);
			}
			std::sort(admins.begin(), admins.end());
			if(!own_device && !admins.isEmpty() && B64(admins.first()) == OwnFp(state) && ActingDevice())
				removeFromGroup(gid, from, false);
			return;
		}
		fc::Key32 ck{};
		fc::Pub pub{};
		if(from_device.isEmpty() || !Unhex(p.value(QStringLiteral("ck")).toString(), ck) || !UnB64(p.value(QStringLiteral("pub")).toString(), pub) || !fc::ValidPublic(pub))
			return;
		QJsonObject chains = g.value(QStringLiteral("chains")).toObject();
		QJsonObject c;
		c[QStringLiteral("chain")] = p.value(QStringLiteral("chain")).toDouble();
		c[QStringLiteral("iter")] = p.value(QStringLiteral("iter")).toDouble();
		c[QStringLiteral("ck")] = Hex(ck);
		c[QStringLiteral("pub")] = B64(pub);
		chains[from + QLatin1Char(':') + from_device] = c;
		g[QStringLiteral("chains")] = chains;
		groups[gid] = g;
		state[QStringLiteral("groups")] = groups;
		Save();
		sender_keys_taken++;
		// Messages that waited for this key
		const QJsonArray waiting = g.value(QStringLiteral("waiting")).toArray();
		if(!waiting.isEmpty())
		{
			g[QStringLiteral("waiting")] = QJsonArray();
			groups[gid] = g;
			state[QStringLiteral("groups")] = groups;
			TakeGroupNotes(gid, waiting);
		}
		return;
	}
	Q_UNUSED(group);
	QJsonObject friends = state.value(QStringLiteral("friends")).toObject();
	QJsonObject f = friends.value(from).toObject();
	const QString f_state = f.value(QStringLiteral("state")).toString();

	// Their link key opens the profile on their card (which came before the first message)
	auto read_profile = [&](QJsonObject &record) {
		fc::Bytes bytes;
		fc::Card card;
		fc::Id16 lk{};
		if(!UnB64(p.value(QStringLiteral("lk")).toString(), lk))
			return false;
		record[QStringLiteral("lk")] = Hex(lk);
		std::string json;
		if(UnB64(state.value(QStringLiteral("cards")).toObject().value(from).toString(), bytes) && fc::DecodeCard(bytes, card)
			&& fc::OpenProfile(lk, card.fp, card.profile, json))
		{
			const QJsonObject profile = QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();
			record[QStringLiteral("name")] = profile.value(QStringLiteral("name")).toString().left(32);
			const QString avatar = profile.value(QStringLiteral("avatar")).toString();
			record[QStringLiteral("avatar")] = avatar.size() <= 16000 ? avatar : QString();
		}
		return true;
	};

	// Their card changed (a new device): fetched again before the next message
	if(t == QLatin1String("card"))
	{
		card_fetched.remove(from);
		return;
	}
	if(own_device)
	{
		// What another device of the own identity did
		if(t == QLatin1String("sync"))
		{
			const QJsonObject of = p.value(QStringLiteral("of")).toObject();
			const QString to = p.value(QStringLiteral("to")).toString();
			const QString of_t = of.value(QStringLiteral("t")).toString();
			if(of_t == QLatin1String("react"))
				ApplyReaction(to, of.value(QStringLiteral("msg")).toString(), OwnFp(state), of.value(QStringLiteral("emoji")).toString(),
					of.value(QStringLiteral("on")).toBool());
			else if(of_t == QLatin1String("seen"))
				MarkSeenUpTo(to, of.value(QStringLiteral("upto_at")).toDouble());
			else if(of_t == QLatin1String("delete"))
				ApplyDelete(to, of.value(QStringLiteral("msg")).toString(), OwnFp(state));
			else if(of_t == QLatin1String("vote"))
				ApplyVote(to, of.value(QStringLiteral("plan")).toString(), OwnFp(state), of.value(QStringLiteral("answers")));
			else if(of_t == QLatin1String("text") || of_t == QLatin1String("join") || of_t == QLatin1String("plan"))
			{
				QJsonObject mine = of;
				mine[QStringLiteral("from")] = OwnFp(state);
				mine[QStringLiteral("mine")] = true;
				mine[QStringLiteral("state")] = QStringLiteral("sent");
				mine.remove(QStringLiteral("embed"));
				mine.remove(QStringLiteral("deleted"));
				TakeReply(of, mine);
				if(of_t == QLatin1String("text"))
				{
					const QJsonObject embed = CleanEmbed(of.value(QStringLiteral("embed")).toObject(), of.value(QStringLiteral("text")).toString());
					if(!embed.isEmpty())
						mine[QStringLiteral("embed")] = embed;
				}
				AddMessage(to, mine);
			}
		}
		else if(t == QLatin1String("own"))
		{
			// A friend's record, the friends key or the link key changed on another own device
			const QJsonObject record = p.value(QStringLiteral("friend")).toObject();
			const QString who = record.value(QStringLiteral("fp")).toString();
			if(!who.isEmpty() && who != OwnFp(state))
			{
				if(record.value(QStringLiteral("removed")).toBool())
				{
					friends.remove(who);
					statuses.remove(who);
				}
				else
				{
					QJsonObject merged = friends.value(who).toObject();
					for(auto it = record.constBegin(); it != record.constEnd(); ++it)
					{
						if(it.key() != QLatin1String("fp"))
							merged[it.key()] = it.value();
					}
					friends[who] = merged;
				}
				state[QStringLiteral("friends")] = friends;
				// Their name and picture come with their card, fetched next time
				card_fetched.remove(who);
			}
			if(p.value(QStringLiteral("fk")).isObject())
				state[QStringLiteral("fk")] = p.value(QStringLiteral("fk"));
			if(p.value(QStringLiteral("lk")).isString())
				state[QStringLiteral("lk")] = p.value(QStringLiteral("lk"));
			Save();
			emit changed();
		}
		return;
	}

	if(t == QLatin1String("request"))
	{
		if(f_state == QLatin1String("friend") || f_state == QLatin1String("blocked"))
			return;
		QJsonObject record = f;
		if(!read_profile(record))
			return;
		// Both asked each other at once: that is a yes from both sides
		const bool both = f_state == QLatin1String("requested");
		record[QStringLiteral("state")] = both ? QStringLiteral("friend") : QStringLiteral("incoming");
		record[QStringLiteral("token")] = p.value(QStringLiteral("token")).toString();
		record[QStringLiteral("text")] = p.value(QStringLiteral("text")).toString().left(200);
		record[QStringLiteral("since")] = static_cast<double>(NowMs());
		friends[from] = record;
		state[QStringLiteral("friends")] = friends;
		Save();
		if(both)
			SendFriendsKey(from);
		else
			emit requestArrived(from, record.value(QStringLiteral("name")).toString());
		emit changed();
	}
	else if(t == QLatin1String("accept"))
	{
		if(f_state != QLatin1String("requested"))
			return;
		QJsonObject record = f;
		if(!read_profile(record))
			return;
		record[QStringLiteral("state")] = QStringLiteral("friend");
		record[QStringLiteral("token")] = p.value(QStringLiteral("token")).toString();
		record[QStringLiteral("fk")] = p.value(QStringLiteral("fk")).toObject();
		record[QStringLiteral("since")] = static_cast<double>(NowMs());
		friends[from] = record;
		state[QStringLiteral("friends")] = friends;
		Save();
		// And our friends key for them
		SendFriendsKey(from);
		PutStatus();
		emit changed();
	}
	else if(f_state != QLatin1String("friend"))
	{
		// From here on: friends only, but a "no" to our request
		if(t == QLatin1String("decline") && f_state == QLatin1String("requested"))
		{
			const QString my_token = f.value(QStringLiteral("my_token")).toString();
			if(!my_token.isEmpty())
				Call(QStringLiteral("token_remove"), { { QStringLiteral("hash"), TokenHash(my_token) } }, true, nullptr);
			friends.remove(from);
			state[QStringLiteral("friends")] = friends;
			Save();
			emit changed();
		}
	}
	else if(t == QLatin1String("fk"))
	{
		QJsonObject fk;
		fk[QStringLiteral("key")] = p.value(QStringLiteral("key"));
		fk[QStringLiteral("epoch")] = p.value(QStringLiteral("epoch"));
		f[QStringLiteral("fk")] = fk;
		friends[from] = f;
		state[QStringLiteral("friends")] = friends;
		Save();
	}
	else if(t == QLatin1String("lk"))
	{
		fc::Id16 lk{};
		if(UnB64(p.value(QStringLiteral("lk")).toString(), lk))
		{
			f[QStringLiteral("lk")] = Hex(lk);
			friends[from] = f;
			state[QStringLiteral("friends")] = friends;
			Save();
		}
	}
	else if(t == QLatin1String("vote"))
		ApplyVote(from, p.value(QStringLiteral("plan")).toString(), from, p.value(QStringLiteral("answers")));
	else if(t == QLatin1String("react"))
		ApplyReaction(from, p.value(QStringLiteral("msg")).toString(), from, p.value(QStringLiteral("emoji")).toString(), p.value(QStringLiteral("on")).toBool());
	else if(t == QLatin1String("delete"))
		ApplyDelete(from, p.value(QStringLiteral("msg")).toString(), from);
	else if(t == QLatin1String("seen"))
		ApplyRead(from, from, p.value(QStringLiteral("upto_at")).toDouble());
	else if(t == QLatin1String("text") || t == QLatin1String("join") || t == QLatin1String("plan"))
	{
		QJsonObject m;
		m[QStringLiteral("id")] = p.value(QStringLiteral("id"));
		m[QStringLiteral("at")] = p.value(QStringLiteral("at"));
		m[QStringLiteral("text")] = p.value(QStringLiteral("text")).toString().left(4000);
		m[QStringLiteral("from")] = from;
		m[QStringLiteral("mine")] = false;
		if(t == QLatin1String("text"))
		{
			TakeReply(p, m);
			const QJsonObject embed = CleanEmbed(p.value(QStringLiteral("embed")).toObject(), m.value(QStringLiteral("text")).toString());
			if(!embed.isEmpty())
				m[QStringLiteral("embed")] = embed;
		}
		if(t == QLatin1String("join"))
		{
			const QJsonObject card = CleanRoom(p.value(QStringLiteral("join")).toObject());
			if(card.isEmpty())
				return;
			m[QStringLiteral("join")] = card;
		}
		if(t == QLatin1String("plan"))
		{
			const QJsonObject plan = CleanPlan(p.value(QStringLiteral("plan")).toObject());
			if(plan.isEmpty())
				return;
			m[QStringLiteral("plan")] = plan;
		}
		AddMessage(from, m);
		Notify(from, f.value(QStringLiteral("name")).toString(), m.value(QStringLiteral("text")).toString());
	}
	else if(t == QLatin1String("unfriend"))
	{
		// Their write token for this identity's inboxes goes too
		const QString my_token = f.value(QStringLiteral("my_token")).toString();
		if(!my_token.isEmpty())
			Call(QStringLiteral("token_remove"), { { QStringLiteral("hash"), TokenHash(my_token) } }, true, nullptr);
		friends.remove(from);
		state[QStringLiteral("friends")] = friends;
		Save();
		statuses.remove(from);
		emit changed();
	}
}

// ------------------------------------------------------------------ groups

namespace {

const int kMaxGroupMembers = 50;
/** A sender key is made anew at least this often (and whenever who is in the group changes). */
const qint64 kSenderKeyRenewMs = 7 * kDayMs;
/** Group messages whose writer's sender key has not come yet wait this long, at most this many. */
const qint64 kWaitingMs = 7 * kDayMs;
const int kWaitingMax = 200;

/**
 * What an admin signs for a version of a group: "psrpt group" || id || u32 version || u16 name ||
 * u8 add (1: everybody may add people) || members, admins, banned (each u8 count and the
 * fingerprints in byte order).
 */
fc::Bytes GroupSigned(const fc::Id16 &gid, uint32_t version, const QString &name, const QString &add,
	const QStringList &members, const QStringList &admins, const QStringList &banned)
{
	fc::Bytes out = fc::Label("psrpt group");
	out.insert(out.end(), gid.begin(), gid.end());
	for(int shift = 24; shift >= 0; shift -= 8)
		out.push_back(static_cast<uint8_t>(version >> shift));
	const QByteArray n = name.toUtf8().left(200);
	out.push_back(static_cast<uint8_t>(n.size() >> 8));
	out.push_back(static_cast<uint8_t>(n.size()));
	out.insert(out.end(), n.begin(), n.end());
	out.push_back(add == QLatin1String("all") ? 1 : 0);
	for(const QStringList &list : { members, admins, banned })
	{
		// In the order of their bytes (base64url text sorts differently)
		std::vector<fc::Id16> ids;
		for(const QString &fp : list)
		{
			fc::Id16 id{};
			UnB64(fp, id);
			ids.push_back(id);
		}
		std::sort(ids.begin(), ids.end());
		out.push_back(static_cast<uint8_t>(ids.size()));
		for(const fc::Id16 &id : ids)
			out.insert(out.end(), id.begin(), id.end());
	}
	return out;
}

QJsonObject SaveChain(const fc::SenderChain &c, bool own)
{
	QJsonObject out;
	out[QStringLiteral("chain")] = static_cast<double>(c.chain);
	out[QStringLiteral("iter")] = static_cast<double>(c.iter);
	out[QStringLiteral("ck")] = Hex(c.ck);
	if(own)
		out[QStringLiteral("sign")] = Hex(c.sign.d);
	else
	{
		out[QStringLiteral("pub")] = B64(c.sign_pub);
		QJsonObject skipped;
		for(const auto &k : c.skipped)
			skipped[QString::number(k.first)] = Hex(k.second);
		out[QStringLiteral("skipped")] = skipped;
	}
	return out;
}

bool LoadChain(const QJsonObject &c, fc::SenderChain &out, bool own)
{
	out = fc::SenderChain();
	out.chain = static_cast<uint32_t>(c.value(QStringLiteral("chain")).toDouble());
	out.iter = static_cast<uint32_t>(c.value(QStringLiteral("iter")).toDouble());
	if(!Unhex(c.value(QStringLiteral("ck")).toString(), out.ck))
		return false;
	if(own)
		return PairFrom(c.value(QStringLiteral("sign")).toString(), out.sign);
	if(!UnB64(c.value(QStringLiteral("pub")).toString(), out.sign_pub))
		return false;
	const QJsonObject skipped = c.value(QStringLiteral("skipped")).toObject();
	for(auto it = skipped.constBegin(); it != skipped.constEnd(); ++it)
	{
		fc::Key32 mk{};
		if(Unhex(it.value().toString(), mk))
			out.skipped[it.key().toUInt()] = mk;
	}
	return true;
}

} // namespace

bool FriendsClient::isGroup(const QString &chat) const
{
	return enabled && state.value(QStringLiteral("groups")).toObject().contains(chat);
}

QVariantMap FriendsClient::group(const QString &chat) const
{
	const QJsonObject g = GroupOf(state, chat);
	if(g.isEmpty() || !enabled)
		return QVariantMap();
	const QString me = OwnFp(state);
	const QStringList admins = Fps(g.value(QStringLiteral("admins")));
	const QJsonObject friends = state.value(QStringLiteral("friends")).toObject();
	const QJsonObject names = g.value(QStringLiteral("names")).toObject();
	auto name_of = [&](const QString &fp) {
		if(fp == me)
			return profile_name;
		const QString name = friends.value(fp).toObject().value(QStringLiteral("name")).toString();
		return name.isEmpty() ? names.value(fp).toString(tr("A member")) : name;
	};
	QVariantList members;
	for(const QString &fp : Fps(g.value(QStringLiteral("members"))))
	{
		members.append(QVariantMap{ { QStringLiteral("fp"), fp }, { QStringLiteral("name"), name_of(fp) },
			{ QStringLiteral("admin"), admins.contains(fp) }, { QStringLiteral("me"), fp == me },
			{ QStringLiteral("friend"), friends.value(fp).toObject().value(QStringLiteral("state")).toString() == QLatin1String("friend") } });
	}
	QVariantList banned;
	for(const QString &fp : Fps(g.value(QStringLiteral("banned"))))
		banned.append(QVariantMap{ { QStringLiteral("fp"), fp }, { QStringLiteral("name"), name_of(fp) } });
	return QVariantMap{ { QStringLiteral("id"), chat }, { QStringLiteral("name"), g.value(QStringLiteral("name")).toString() },
		{ QStringLiteral("admin"), admins.contains(me) }, { QStringLiteral("add"), g.value(QStringLiteral("add")).toString(QStringLiteral("admins")) },
		{ QStringLiteral("left"), g.value(QStringLiteral("left")).toBool() }, { QStringLiteral("members"), members },
		{ QStringLiteral("banned"), banned } };
}

QString FriendsClient::createGroup(const QString &name, const QStringList &list)
{
	fc::Identity id;
	if(!enabled || !Identity(id))
		return QString();
	fc::Id16 gid{};
	coplay::RandomBytes(gid.data(), gid.size());
	// Its first byte tells a group message from an envelope in the group's box (kind 1 or 2 there)
	gid[0] |= 0x80;
	fc::KeyPair owner;
	fc::Id16 token{};
	if(!fc::GenerateKeyPair(owner))
		return QString();
	coplay::RandomBytes(token.data(), token.size());
	const QString me = OwnFp(state);
	const QJsonObject friends = state.value(QStringLiteral("friends")).toObject();
	QStringList members{ me };
	for(const QString &fp : list)
	{
		if(!members.contains(fp) && members.size() < kMaxGroupMembers
			&& friends.value(fp).toObject().value(QStringLiteral("state")).toString() == QLatin1String("friend"))
			members.append(fp);
	}
	const QString key = B64(gid);
	QJsonObject g;
	g[QStringLiteral("name")] = name.simplified().left(48).isEmpty() ? tr("Group") : name.simplified().left(48);
	g[QStringLiteral("version")] = 0;
	g[QStringLiteral("members")] = ToArray(members);
	g[QStringLiteral("admins")] = ToArray({ me });
	g[QStringLiteral("banned")] = QJsonArray();
	g[QStringLiteral("add")] = QStringLiteral("admins");
	g[QStringLiteral("token")] = B64(token);
	g[QStringLiteral("owner")] = Hex(owner.d);
	g[QStringLiteral("box")] = B64(fc::GroupBox(gid));
	g[QStringLiteral("after")] = 0;
	g[QStringLiteral("made")] = static_cast<double>(NowMs());
	PutGroup(state, key, g);
	Save();
	// The box on the server first; then the group's first version to everybody, then the sender key
	Call(QStringLiteral("group_new"), { { QStringLiteral("box"), B64(fc::GroupBox(gid)) }, { QStringLiteral("owner"), B64(owner.pub) },
		{ QStringLiteral("hash"), B64(fc::Sha256(fc::B(token))) } }, false, [this, key](bool ok, const QJsonObject &) {
			if(!ok)
				return;
			PublishGroup(key);
			NewSenderKey(key);
		});
	emit changed();
	return key;
}

void FriendsClient::PublishGroup(const QString &gid, const QStringList &also_to)
{
	fc::Identity id;
	fc::Id16 gidb{};
	QJsonObject g = GroupOf(state, gid);
	if(!Identity(id) || !UnB64(gid, gidb) || g.isEmpty())
		return;
	const QString me = OwnFp(state);
	const uint32_t version = static_cast<uint32_t>(g.value(QStringLiteral("version")).toDouble()) + 1;
	const QStringList members = Fps(g.value(QStringLiteral("members")));
	const QStringList admins = Fps(g.value(QStringLiteral("admins")));
	const QStringList banned = Fps(g.value(QStringLiteral("banned")));
	const QString name = g.value(QStringLiteral("name")).toString();
	const QString add = g.value(QStringLiteral("add")).toString(QStringLiteral("admins"));
	fc::Sig sig;
	if(!fc::Sign(id.iks, GroupSigned(gidb, version, name, add, members, admins, banned), sig))
		return;
	// The names the members go by, for those who are no friends of each other
	const QJsonObject friends = state.value(QStringLiteral("friends")).toObject();
	QJsonObject names = g.value(QStringLiteral("names")).toObject();
	names[me] = profile_name;
	for(const QString &fp : members)
	{
		const QString n = friends.value(fp).toObject().value(QStringLiteral("name")).toString();
		if(!n.isEmpty())
			names[fp] = n;
	}
	g[QStringLiteral("names")] = names;
	g[QStringLiteral("version")] = static_cast<double>(version);
	PutGroup(state, gid, g);
	Save();
	QJsonObject p;
	p[QStringLiteral("t")] = QStringLiteral("group");
	p[QStringLiteral("group")] = gid;
	p[QStringLiteral("version")] = static_cast<double>(version);
	p[QStringLiteral("name")] = name;
	p[QStringLiteral("add")] = add;
	p[QStringLiteral("members")] = ToArray(members);
	p[QStringLiteral("admins")] = ToArray(admins);
	p[QStringLiteral("banned")] = ToArray(banned);
	p[QStringLiteral("by")] = me;
	p[QStringLiteral("sig")] = B64(sig);
	p[QStringLiteral("names")] = names;
	// Whoever was taken out learns it, without the new token
	for(const QString &fp : also_to)
	{
		if(fp == me || members.contains(fp))
			continue;
		if(friends.value(fp).toObject().value(QStringLiteral("state")).toString() == QLatin1String("friend"))
			SendPayload(fp, p, false);
		else
			SendInGroup(gid, fp, p);
	}
	p[QStringLiteral("token")] = g.value(QStringLiteral("token")).toString();
	QJsonObject for_admins = p;
	for_admins[QStringLiteral("owner")] = g.value(QStringLiteral("owner")).toString();
	for(const QString &fp : members)
	{
		const QJsonObject &payload = admins.contains(fp) ? for_admins : p;
		if(fp == me)
			SendPayload(me, payload, false); // the own other devices
		else if(friends.value(fp).toObject().value(QStringLiteral("state")).toString() == QLatin1String("friend"))
			SendPayload(fp, payload, false);
		else
			SendInGroup(gid, fp, payload);
	}
}

bool FriendsClient::ApplyGroup(const QString &from, const QJsonObject &p)
{
	const QString gid = p.value(QStringLiteral("group")).toString();
	const QString by = p.value(QStringLiteral("by")).toString();
	fc::Id16 gidb{};
	// Only who signed it hands a version on
	if(!UnB64(gid, gidb) || !(gidb[0] & 0x80) || by != from)
		return false;
	QJsonObject g = GroupOf(state, gid);
	// Versions count up a little at a time: a jump to the end of the numbers would freeze the group
	const double sent_version = p.value(QStringLiteral("version")).toDouble();
	const double known_version = g.value(QStringLiteral("version")).toDouble();
	if(sent_version < 1 || sent_version > 2147483647.0 || sent_version != std::floor(sent_version)
		|| (!g.isEmpty() && (sent_version <= known_version || sent_version > known_version + 16)))
		return false;
	const uint32_t version = static_cast<uint32_t>(sent_version);
	const QStringList members = Fps(p.value(QStringLiteral("members")));
	const QStringList admins = Fps(p.value(QStringLiteral("admins")));
	const QStringList banned = Fps(p.value(QStringLiteral("banned")));
	const QString name = p.value(QStringLiteral("name")).toString().simplified().left(48);
	const QString add = p.value(QStringLiteral("add")).toString() == QLatin1String("all") ? QStringLiteral("all") : QStringLiteral("admins");
	if(members.isEmpty() || members.size() > kMaxGroupMembers || admins.isEmpty())
		return false;
	for(const QString &a : admins)
	{
		if(!members.contains(a))
			return false;
	}
	// A group new here comes only from a friend, and only with this player in it: nobody else
	// puts anybody into a group (the first version reaches a new member through their inbox)
	const QString me_fp = OwnFp(state);
	if(g.isEmpty() && from != me_fp
		&& (state.value(QStringLiteral("friends")).toObject().value(from).toObject().value(QStringLiteral("state")).toString() != QLatin1String("friend")
			|| !members.contains(me_fp)))
		return false;
	// Who may: an admin of the version known so far (of this one, for a group new here); where
	// everybody may add people, a member who adds somebody who is not banned, and changes nothing else
	const QStringList old_members = Fps(g.value(QStringLiteral("members")));
	const bool by_admin = g.isEmpty() ? admins.contains(by) : Fps(g.value(QStringLiteral("admins"))).contains(by);
	bool allowed = by_admin;
	if(!allowed && !g.isEmpty() && g.value(QStringLiteral("add")).toString() == QLatin1String("all") && old_members.contains(by))
	{
		bool only_adds = members.size() > old_members.size() && name == g.value(QStringLiteral("name")).toString() && add == QLatin1String("all")
			&& Sorted(admins) == Sorted(Fps(g.value(QStringLiteral("admins")))) && Sorted(banned) == Sorted(Fps(g.value(QStringLiteral("banned"))));
		for(const QString &m : old_members)
			only_adds = only_adds && members.contains(m);
		for(const QString &m : members)
			only_adds = only_adds && (old_members.contains(m) || !banned.contains(m));
		allowed = only_adds;
	}
	if(!allowed)
		return false;
	// Signed with their identity key, from their card
	fc::Card card;
	fc::Bytes card_bytes;
	fc::Sig sig;
	if(!UnB64(state.value(QStringLiteral("cards")).toObject().value(by).toString(), card_bytes) || !fc::DecodeCard(card_bytes, card)
		|| !UnB64(p.value(QStringLiteral("sig")).toString(), sig) || !fc::Verify(card.iks, GroupSigned(gidb, version, name, add, members, admins, banned), sig))
		return false;
	const QString me = OwnFp(state);
	const bool fresh = g.isEmpty();
	const bool was_member = !fresh && !g.value(QStringLiteral("left")).toBool();
	const bool member = members.contains(me);
	g[QStringLiteral("name")] = name.isEmpty() ? tr("Group") : name;
	g[QStringLiteral("version")] = static_cast<double>(version);
	g[QStringLiteral("members")] = ToArray(members);
	g[QStringLiteral("admins")] = ToArray(admins);
	g[QStringLiteral("banned")] = ToArray(banned);
	g[QStringLiteral("add")] = add;
	g[QStringLiteral("box")] = B64(fc::GroupBox(gidb));
	QJsonObject names = g.value(QStringLiteral("names")).toObject();
	const QJsonObject sent_names = p.value(QStringLiteral("names")).toObject();
	for(const QString &fp : members)
	{
		const QString n = sent_names.value(fp).toString().simplified().left(32);
		if(!n.isEmpty())
			names[fp] = n;
	}
	g[QStringLiteral("names")] = names;
	// The write token and the owner key only from an admin: a member who only adds somebody
	// hands on neither
	fc::KeyPair sent_owner;
	if(by_admin && p.value(QStringLiteral("token")).isString())
		g[QStringLiteral("token")] = p.value(QStringLiteral("token")).toString();
	if(by_admin && admins.contains(me) && PairFrom(p.value(QStringLiteral("owner")).toString(), sent_owner))
		g[QStringLiteral("owner")] = p.value(QStringLiteral("owner")).toString();
	else if(!admins.contains(me))
		g.remove(QStringLiteral("owner"));
	if(fresh)
		g[QStringLiteral("after")] = 0;
	const QString who = state.value(QStringLiteral("friends")).toObject().value(by).toObject().value(QStringLiteral("name")).toString(names.value(by).toString());
	QString note;
	if(!member)
	{
		g[QStringLiteral("left")] = true;
		g.remove(QStringLiteral("mine"));
		g.remove(QStringLiteral("chains"));
		g.remove(QStringLiteral("owner"));
		if(was_member)
			note = tr("%1 took you out of the group").arg(who);
	}
	else
	{
		g[QStringLiteral("left")] = false;
		if(fresh || !was_member)
			note = tr("%1 added you to the group").arg(who);
	}
	PutGroup(state, gid, g);
	Save();
	if(!note.isEmpty())
		AddMessage(gid, QJsonObject{ { QStringLiteral("id"), QStringLiteral("v%1").arg(static_cast<qulonglong>(version)) }, { QStringLiteral("at"), static_cast<double>(NowMs()) },
			{ QStringLiteral("text"), note }, { QStringLiteral("system"), true }, { QStringLiteral("from"), by } });
	// A new sender key whenever who is in it changed: whoever left reads nothing new
	if(member && (Sorted(old_members) != Sorted(members) || !g.contains(QStringLiteral("mine"))))
		NewSenderKey(gid);
	if(fresh && member)
		Notify(gid, who, note);
	emit changed();
	return true;
}

void FriendsClient::NewSenderKey(const QString &gid)
{
	QJsonObject g = GroupOf(state, gid);
	if(g.isEmpty() || g.value(QStringLiteral("left")).toBool())
		return;
	fc::SenderChain c;
	if(!c.New(static_cast<uint32_t>(g.value(QStringLiteral("mine")).toObject().value(QStringLiteral("chain")).toDouble()) + 1))
		return;
	QJsonObject mine = SaveChain(c, true);
	mine[QStringLiteral("made")] = static_cast<double>(NowMs());
	g[QStringLiteral("mine")] = mine;
	PutGroup(state, gid, g);
	Save();
	QJsonObject p;
	p[QStringLiteral("t")] = QStringLiteral("senderkey");
	p[QStringLiteral("group")] = gid;
	p[QStringLiteral("chain")] = static_cast<double>(c.chain);
	p[QStringLiteral("iter")] = static_cast<double>(c.iter);
	p[QStringLiteral("ck")] = Hex(c.ck);
	p[QStringLiteral("pub")] = B64(c.sign.pub);
	// To every member's devices and the own other ones, through the group's box
	for(const QString &fp : Fps(g.value(QStringLiteral("members"))))
		SendInGroup(gid, fp, p);
}

void FriendsClient::SendInGroup(const QString &gid, const QString &fp, const QJsonObject &payload)
{
	const QJsonObject g = GroupOf(state, gid);
	if(g.isEmpty() || g.value(QStringLiteral("token")).toString().isEmpty())
		return;
	QJsonObject p = payload;
	if(!p.contains(QStringLiteral("id")))
		p[QStringLiteral("id")] = RandomId();
	if(!p.contains(QStringLiteral("at")))
		p[QStringLiteral("at")] = static_cast<double>(NowMs());
	SendSealed(fp, QJsonDocument(p).toJson(QJsonDocument::Compact), Route::GroupBox, g.value(QStringLiteral("box")).toString(),
		g.value(QStringLiteral("token")).toString(), nullptr);
}

void FriendsClient::SendGroup(const QString &gid, QJsonObject payload, std::function<void(bool)> done)
{
	fc::Identity id;
	fc::DeviceId own_device{};
	fc::Id16 gidb{};
	QJsonObject g = GroupOf(state, gid);
	if(!Identity(id) || !OwnDevice(state, own_device) || !UnB64(gid, gidb) || g.isEmpty() || g.value(QStringLiteral("left")).toBool())
	{
		if(done)
			done(false);
		return;
	}
	const qint64 made = static_cast<qint64>(g.value(QStringLiteral("mine")).toObject().value(QStringLiteral("made")).toDouble());
	if(!g.contains(QStringLiteral("mine")) || NowMs() - made > kSenderKeyRenewMs)
	{
		NewSenderKey(gid);
		g = GroupOf(state, gid);
	}
	fc::SenderChain c;
	if(!LoadChain(g.value(QStringLiteral("mine")).toObject(), c, true))
	{
		if(done)
			done(false);
		return;
	}
	if(!payload.contains(QStringLiteral("id")))
		payload[QStringLiteral("id")] = RandomId();
	if(!payload.contains(QStringLiteral("at")))
		payload[QStringLiteral("at")] = static_cast<double>(NowMs());
	fc::Bytes note;
	if(!fc::GroupSeal(c, gidb, id.fp, own_device, ToBytes(QJsonDocument(payload).toJson(QJsonDocument::Compact)), note))
	{
		if(done)
			done(false);
		return;
	}
	// Kept before it goes out: one message key is never used twice
	QJsonObject mine = SaveChain(c, true);
	mine[QStringLiteral("made")] = g.value(QStringLiteral("mine")).toObject().value(QStringLiteral("made"));
	g[QStringLiteral("mine")] = mine;
	PutGroup(state, gid, g);
	Save();
	Call(QStringLiteral("group_put"), { { QStringLiteral("box"), g.value(QStringLiteral("box")).toString() },
		{ QStringLiteral("token"), g.value(QStringLiteral("token")).toString() }, { QStringLiteral("note"), B64(note) } }, false,
		[done](bool ok, const QJsonObject &) {
			if(done)
				done(ok);
		});
}

void FriendsClient::PollGroups()
{
	const QJsonObject groups = state.value(QStringLiteral("groups")).toObject();
	for(auto it = groups.constBegin(); it != groups.constEnd(); ++it)
	{
		const QJsonObject g = it.value().toObject();
		if(g.value(QStringLiteral("left")).toBool() || g.value(QStringLiteral("token")).toString().isEmpty())
			continue;
		const QString gid = it.key();
		Call(QStringLiteral("group_get"), { { QStringLiteral("box"), g.value(QStringLiteral("box")).toString() },
			{ QStringLiteral("token"), g.value(QStringLiteral("token")).toString() },
			{ QStringLiteral("after"), QString::number(static_cast<qint64>(g.value(QStringLiteral("after")).toDouble())) } }, false,
			[this, gid](bool ok, const QJsonObject &answer) {
				if(!ok)
				{
					// Not even an old token opens it: taken out (or gone a month)
					if(answer.value(QStringLiteral("error")).toString() == QLatin1String("bad_token")
						|| answer.value(QStringLiteral("error")).toString() == QLatin1String("unknown"))
					{
						QJsonObject g = GroupOf(state, gid);
						if(!g.isEmpty() && !g.value(QStringLiteral("left")).toBool())
						{
							g[QStringLiteral("left")] = true;
							g.remove(QStringLiteral("mine"));
							PutGroup(state, gid, g);
							Save();
							AddMessage(gid, QJsonObject{ { QStringLiteral("id"), RandomId() }, { QStringLiteral("at"), static_cast<double>(NowMs()) },
								{ QStringLiteral("text"), tr("You are no longer in this group") }, { QStringLiteral("system"), true } });
						}
					}
					return;
				}
				TakeGroupNotes(gid, answer.value(QStringLiteral("notes")).toArray());
				if(answer.value(QStringLiteral("more")).toBool())
					QTimer::singleShot(1500, this, &FriendsClient::PollGroups);
			});
	}
}

void FriendsClient::TakeGroupNotes(const QString &gid, const QJsonArray &notes)
{
	fc::DeviceId own_device{};
	OwnDevice(state, own_device);
	// Who starts a conversation through the box is looked up first: X3DH needs their card
	const QJsonObject cards = state.value(QStringLiteral("cards")).toObject();
	QSet<QString> unknown;
	for(const QJsonValue &v : notes)
	{
		fc::Bytes note;
		fc::Envelope e;
		if(UnB64(v.toObject().value(QStringLiteral("note")).toString(), note) && note.size() > 1 && (note[1] == 1 || note[1] == 2)
			&& fc::Envelope::Decode(note, e) && e.to_device == own_device && e.kind == 1 && !cards.contains(B64(e.from_fp)))
			unknown.insert(B64(e.from_fp));
	}
	QPointer<FriendsClient> self(this);
	auto handle = [self, gid, notes, own_device]() {
		if(!self)
			return;
		const int keys_before = self->sender_keys_taken;
		double after = GroupOf(self->state, gid).value(QStringLiteral("after")).toDouble();
		QJsonArray wait;
		for(const QJsonValue &v : notes)
		{
			const QJsonObject n = v.toObject();
			after = std::max(after, n.value(QStringLiteral("id")).toDouble());
			fc::Bytes note;
			if(!UnB64(n.value(QStringLiteral("note")).toString(), note) || note.size() < 2)
				continue;
			bool done = true;
			if(note[1] == 1 || note[1] == 2)
			{
				fc::Envelope e;
				if(fc::Envelope::Decode(note, e) && e.to_device == own_device)
					done = self->HandleEnvelope(ToQ(note), false, gid);
			}
			else
				done = self->HandleGroupMessage(gid, ToQ(note));
			if(!done)
			{
				QJsonObject w = n;
				if(!w.contains(QStringLiteral("since")))
					w[QStringLiteral("since")] = static_cast<double>(NowMs());
				wait.append(w);
			}
		}
		// A sender key in this very batch: what waited for it in the batch gets one more try
		if(!wait.isEmpty() && self->sender_keys_taken != keys_before)
		{
			QJsonArray still;
			for(const QJsonValue &v : wait)
			{
				fc::Bytes note;
				if(UnB64(v.toObject().value(QStringLiteral("note")).toString(), note) && !self->HandleGroupMessage(gid, ToQ(note)))
					still.append(v);
			}
			wait = still;
		}
		QJsonObject g = GroupOf(self->state, gid);
		if(g.isEmpty())
			return;
		g[QStringLiteral("after")] = after;
		// What still waits for a sender key: a while, not for ever
		QJsonArray waiting;
		QSet<double> ids;
		for(const QJsonValue &v : g.value(QStringLiteral("waiting")).toArray() + wait)
		{
			const QJsonObject w = v.toObject();
			const double id = w.value(QStringLiteral("id")).toDouble();
			if(ids.contains(id) || NowMs() - static_cast<qint64>(w.value(QStringLiteral("since")).toDouble()) > kWaitingMs)
				continue;
			ids.insert(id);
			waiting.append(w);
		}
		while(waiting.size() > kWaitingMax)
			waiting.removeFirst();
		g[QStringLiteral("waiting")] = waiting;
		PutGroup(self->state, gid, g);
		self->Save();
	};
	if(unknown.isEmpty())
	{
		handle();
		return;
	}
	auto left = std::make_shared<int>(static_cast<int>(unknown.size()));
	for(const QString &fp : unknown)
	{
		Call(QStringLiteral("card_get"), { { QStringLiteral("fp"), fp } }, false, [self, fp, left, handle](bool ok, const QJsonObject &answer) {
			if(!self)
				return;
			fc::Card card;
			if(ok && KeepCard(self->state, fp, answer.value(QStringLiteral("card")).toString(), card))
				self->card_fetched[fp] = NowMs();
			if(--*left == 0)
				handle();
		});
	}
}

bool FriendsClient::HandleGroupMessage(const QString &gid, const QByteArray &bytes)
{
	fc::GroupMessage m;
	fc::DeviceId own_device{};
	if(!fc::GroupMessage::Decode(ToBytes(bytes), m) || B64(m.group) != gid || !OwnDevice(state, own_device))
		return true;
	const QString from = B64(m.fp);
	const QString me = OwnFp(state);
	QJsonObject g = GroupOf(state, gid);
	if(g.isEmpty() || g.value(QStringLiteral("left")).toBool() || !Fps(g.value(QStringLiteral("members"))).contains(from))
		return true;
	// This device's own, shown when it was written; a blocked player's, not at all
	if((from == me && m.device == own_device)
		|| state.value(QStringLiteral("friends")).toObject().value(from).toObject().value(QStringLiteral("state")).toString() == QLatin1String("blocked"))
		return true;
	const QString key = from + QLatin1Char(':') + Hex(m.device);
	QJsonObject chains = g.value(QStringLiteral("chains")).toObject();
	const QJsonObject c = chains.value(key).toObject();
	fc::SenderChain chain;
	if(c.isEmpty() || !LoadChain(c, chain, false))
		return false;
	// A newer chain whose key has not come yet waits; an older one is over
	if(m.chain != chain.chain)
		return m.chain < chain.chain;
	fc::Bytes plain;
	if(!fc::GroupOpen(chain, m, plain))
		return true;
	chains[key] = SaveChain(chain, false);
	g[QStringLiteral("chains")] = chains;
	PutGroup(state, gid, g);
	Save();
	const QJsonObject p = QJsonDocument::fromJson(ToQ(plain)).object();
	const QString t = p.value(QStringLiteral("t")).toString();
	const QString name = from == me ? profile_name
		: state.value(QStringLiteral("friends")).toObject().value(from).toObject().value(QStringLiteral("name"))
			.toString(g.value(QStringLiteral("names")).toObject().value(from).toString(tr("A member")));
	if(t == QLatin1String("vote"))
	{
		ApplyVote(gid, p.value(QStringLiteral("plan")).toString(), from, p.value(QStringLiteral("answers")));
		return true;
	}
	if(t == QLatin1String("react"))
	{
		ApplyReaction(gid, p.value(QStringLiteral("msg")).toString(), from, p.value(QStringLiteral("emoji")).toString(), p.value(QStringLiteral("on")).toBool());
		return true;
	}
	if(t == QLatin1String("delete"))
	{
		ApplyDelete(gid, p.value(QStringLiteral("msg")).toString(), from);
		return true;
	}
	if(t == QLatin1String("seen"))
	{
		if(from == me)
			MarkSeenUpTo(gid, p.value(QStringLiteral("upto_at")).toDouble());
		else
			ApplyRead(gid, from, p.value(QStringLiteral("upto_at")).toDouble());
		return true;
	}
	if(t != QLatin1String("text") && t != QLatin1String("join") && t != QLatin1String("plan"))
		return true;
	QJsonObject msg;
	msg[QStringLiteral("id")] = p.value(QStringLiteral("id"));
	msg[QStringLiteral("at")] = p.value(QStringLiteral("at"));
	msg[QStringLiteral("text")] = p.value(QStringLiteral("text")).toString().left(4000);
	msg[QStringLiteral("from")] = from;
	msg[QStringLiteral("mine")] = from == me;
	if(from == me)
		msg[QStringLiteral("state")] = QStringLiteral("sent");
	if(t == QLatin1String("text"))
	{
		TakeReply(p, msg);
		const QJsonObject embed = CleanEmbed(p.value(QStringLiteral("embed")).toObject(), msg.value(QStringLiteral("text")).toString());
		if(!embed.isEmpty())
			msg[QStringLiteral("embed")] = embed;
	}
	if(t == QLatin1String("join"))
	{
		const QJsonObject card = CleanRoom(p.value(QStringLiteral("join")).toObject());
		if(card.isEmpty())
			return true;
		msg[QStringLiteral("join")] = card;
	}
	if(t == QLatin1String("plan"))
	{
		const QJsonObject plan = CleanPlan(p.value(QStringLiteral("plan")).toObject());
		if(plan.isEmpty())
			return true;
		msg[QStringLiteral("plan")] = plan;
	}
	AddMessage(gid, msg);
	if(from != me)
		Notify(gid, name, msg.value(QStringLiteral("text")).toString());
	return true;
}

void FriendsClient::addToGroup(const QString &chat, const QStringList &list)
{
	QJsonObject g = GroupOf(state, chat);
	const QString me = OwnFp(state);
	if(g.isEmpty() || g.value(QStringLiteral("left")).toBool())
		return;
	const bool admin = Fps(g.value(QStringLiteral("admins"))).contains(me);
	if(!admin && g.value(QStringLiteral("add")).toString() != QLatin1String("all"))
		return;
	QStringList members = Fps(g.value(QStringLiteral("members")));
	const QStringList banned = Fps(g.value(QStringLiteral("banned")));
	const QJsonObject friends = state.value(QStringLiteral("friends")).toObject();
	bool more = false;
	for(const QString &fp : list)
	{
		if(members.contains(fp) || members.size() >= kMaxGroupMembers || (banned.contains(fp) && !admin)
			|| friends.value(fp).toObject().value(QStringLiteral("state")).toString() != QLatin1String("friend"))
			continue;
		members.append(fp);
		more = true;
	}
	if(!more)
		return;
	// An admin lifts a ban by adding them again
	QStringList still_banned;
	for(const QString &fp : banned)
	{
		if(!members.contains(fp))
			still_banned.append(fp);
	}
	g[QStringLiteral("members")] = ToArray(members);
	g[QStringLiteral("banned")] = ToArray(still_banned);
	PutGroup(state, chat, g);
	Save();
	PublishGroup(chat);
	// The newcomers get the own sender key with everybody's new ones
	NewSenderKey(chat);
	emit changed();
}

void FriendsClient::CallOwner(const QString &op, QMap<QString, QString> fields, const fc::KeyPair &owner, Reply done)
{
	// As every signed request, but with the group's owner key and without fp
	uint8_t nonce[8];
	coplay::RandomBytes(nonce, sizeof(nonce));
	fields[QStringLiteral("n")] = B64(nonce, sizeof(nonce));
	const QString t = QString::number(QDateTime::currentSecsSinceEpoch() + clock_offset);
	std::map<std::string, std::string> signed_fields;
	for(auto it = fields.constBegin(); it != fields.constEnd(); ++it)
		signed_fields[it.key().toStdString()] = it.value().toStdString();
	fc::Sig sig;
	fc::Sign(owner, fc::RequestMessage(op.toStdString(), t.toStdString(), signed_fields), sig);
	fields[QStringLiteral("t")] = t;
	fields[QStringLiteral("sig")] = B64(sig);
	Call(op, fields, false, std::move(done));
}

void FriendsClient::removeFromGroup(const QString &chat, const QString &fp, bool ban)
{
	QJsonObject g = GroupOf(state, chat);
	const QString me = OwnFp(state);
	fc::KeyPair owner;
	if(g.isEmpty() || fp == me || !Fps(g.value(QStringLiteral("admins"))).contains(me) || !PairFrom(g.value(QStringLiteral("owner")).toString(), owner))
		return;
	QStringList members = Fps(g.value(QStringLiteral("members")));
	QStringList admins = Fps(g.value(QStringLiteral("admins")));
	QStringList banned = Fps(g.value(QStringLiteral("banned")));
	members.removeAll(fp);
	admins.removeAll(fp);
	if(ban && !banned.contains(fp))
		banned.append(fp);
	const bool was_admin = Fps(g.value(QStringLiteral("admins"))).contains(fp);
	QPointer<FriendsClient> self(this);
	// A new write token: they write nothing more into the box. An admin taken out also had the
	// owner key: a new one first, which they do not get
	auto new_token_then_publish = [self, chat, fp, members, admins, banned]() {
		if(!self)
			return;
		fc::KeyPair current;
		const QJsonObject g = GroupOf(self->state, chat);
		if(!PairFrom(g.value(QStringLiteral("owner")).toString(), current))
			return;
		fc::Id16 token{};
		coplay::RandomBytes(token.data(), token.size());
		const QString new_token = B64(token);
		self->CallOwner(QStringLiteral("group_token"), { { QStringLiteral("box"), g.value(QStringLiteral("box")).toString() },
			{ QStringLiteral("hash"), B64(fc::Sha256(fc::B(token))) } }, current,
			[self, chat, fp, members, admins, banned, new_token](bool ok, const QJsonObject &) {
			if(!self || !ok)
				return;
			QJsonObject g = GroupOf(self->state, chat);
			g[QStringLiteral("members")] = ToArray(members);
			g[QStringLiteral("admins")] = ToArray(admins);
			g[QStringLiteral("banned")] = ToArray(banned);
			g[QStringLiteral("token")] = new_token;
			PutGroup(self->state, chat, g);
			self->Save();
			self->PublishGroup(chat, { fp });
			self->NewSenderKey(chat);
			emit self->changed();
		});
	};
	if(was_admin)
		RotateOwner(chat, [new_token_then_publish](bool ok) {
			if(ok)
				new_token_then_publish();
		});
	else
		new_token_then_publish();
	Q_UNUSED(owner);
}

void FriendsClient::RotateOwner(const QString &gid, std::function<void(bool)> then)
{
	const QJsonObject g = GroupOf(state, gid);
	fc::KeyPair old_owner, fresh;
	if(g.isEmpty() || !PairFrom(g.value(QStringLiteral("owner")).toString(), old_owner) || !fc::GenerateKeyPair(fresh))
	{
		then(false);
		return;
	}
	QPointer<FriendsClient> self(this);
	CallOwner(QStringLiteral("group_owner"), { { QStringLiteral("box"), g.value(QStringLiteral("box")).toString() },
		{ QStringLiteral("owner"), B64(fresh.pub) } }, old_owner, [self, gid, fresh, then](bool ok, const QJsonObject &) {
			if(!self)
				return;
			if(ok)
			{
				QJsonObject g = GroupOf(self->state, gid);
				g[QStringLiteral("owner")] = Hex(fresh.d);
				PutGroup(self->state, gid, g);
				self->Save();
			}
			then(ok);
		});
}

bool FriendsClient::ActingDevice() const
{
	// The one with the lowest id among the identity's devices
	const QString mine = state.value(QStringLiteral("device")).toObject().value(QStringLiteral("id")).toString();
	for(const QVariant &d : Devices())
	{
		if(d.toMap().value(QStringLiteral("id")).toString() < mine)
			return false;
	}
	return true;
}

void FriendsClient::setGroupAdmin(const QString &chat, const QString &fp, bool admin)
{
	QJsonObject g = GroupOf(state, chat);
	const QString me = OwnFp(state);
	QStringList admins = Fps(g.value(QStringLiteral("admins")));
	if(g.isEmpty() || !admins.contains(me) || !Fps(g.value(QStringLiteral("members"))).contains(fp) || admins.contains(fp) == admin)
		return;
	if(admin)
		admins.append(fp);
	else
		admins.removeAll(fp);
	if(admins.isEmpty())
		return;
	g[QStringLiteral("admins")] = ToArray(admins);
	PutGroup(state, chat, g);
	Save();
	if(admin)
		PublishGroup(chat);
	else
	{
		// No longer an admin: a new owner key they do not get
		QPointer<FriendsClient> self(this);
		RotateOwner(chat, [self, chat](bool) {
			if(self)
				self->PublishGroup(chat);
		});
	}
	emit changed();
}

void FriendsClient::renameGroup(const QString &chat, const QString &name)
{
	QJsonObject g = GroupOf(state, chat);
	const QString clean = name.simplified().left(48);
	if(g.isEmpty() || clean.isEmpty() || !Fps(g.value(QStringLiteral("admins"))).contains(OwnFp(state)))
		return;
	g[QStringLiteral("name")] = clean;
	PutGroup(state, chat, g);
	Save();
	PublishGroup(chat);
	emit changed();
}

void FriendsClient::setGroupAdd(const QString &chat, const QString &who)
{
	QJsonObject g = GroupOf(state, chat);
	if(g.isEmpty() || !Fps(g.value(QStringLiteral("admins"))).contains(OwnFp(state)))
		return;
	g[QStringLiteral("add")] = who == QLatin1String("all") ? QStringLiteral("all") : QStringLiteral("admins");
	PutGroup(state, chat, g);
	Save();
	PublishGroup(chat);
	emit changed();
}

void FriendsClient::leaveGroup(const QString &chat)
{
	QJsonObject g = GroupOf(state, chat);
	const QString me = OwnFp(state);
	if(g.isEmpty() || g.value(QStringLiteral("left")).toBool())
		return;
	QStringList admins = Fps(g.value(QStringLiteral("admins")));
	QStringList members = Fps(g.value(QStringLiteral("members")));
	if(admins.contains(me))
	{
		// The last admin hands it on to whoever has been in it longest
		members.removeAll(me);
		admins.removeAll(me);
		if(admins.isEmpty() && !members.isEmpty())
			admins.append(members.first());
		g[QStringLiteral("members")] = ToArray(members);
		g[QStringLiteral("admins")] = ToArray(admins);
		PutGroup(state, chat, g);
		Save();
		// A new owner key for those who stay, then the version without this player
		if(!members.isEmpty())
		{
			QPointer<FriendsClient> self(this);
			RotateOwner(chat, [self, chat, me](bool) {
				if(!self)
					return;
				self->PublishGroup(chat, { me });
				QJsonObject g = GroupOf(self->state, chat);
				g.remove(QStringLiteral("owner"));
				PutGroup(self->state, chat, g);
				self->Save();
			});
		}
	}
	else
	{
		// The admins take them out
		for(const QString &fp : members)
		{
			if(fp != me)
				SendInGroup(chat, fp, QJsonObject{ { QStringLiteral("t"), QStringLiteral("leave") }, { QStringLiteral("group"), chat } });
		}
	}
	g = GroupOf(state, chat);
	g[QStringLiteral("left")] = true;
	g.remove(QStringLiteral("mine"));
	g.remove(QStringLiteral("chains"));
	g.remove(QStringLiteral("owner"));
	PutGroup(state, chat, g);
	Save();
	AddMessage(chat, QJsonObject{ { QStringLiteral("id"), RandomId() }, { QStringLiteral("at"), static_cast<double>(NowMs()) },
		{ QStringLiteral("text"), tr("You left the group") }, { QStringLiteral("system"), true } });
	emit changed();
}

// ------------------------------------------------------------------ plans, chats of every kind, reports

void FriendsClient::SendToChat(const QString &chat, const QJsonObject &payload, std::function<void(bool)> done)
{
	if(isGroup(chat))
		SendGroup(chat, payload, std::move(done));
	else
		SendPayload(chat, payload, true, std::move(done));
}

bool FriendsClient::ApplyReaction(const QString &chat, const QString &message, const QString &fp, const QString &emoji, bool on)
{
	// The same rules as in a session's chat: one emoji, at most 3 of one player, 12 on a message
	const std::string e = emoji.toStdString();
	if(!coplay::ValidReaction(e))
		return false;
	QJsonObject chats = state.value(QStringLiteral("chats")).toObject();
	QJsonObject c = chats.value(chat).toObject();
	QJsonArray msgs = c.value(QStringLiteral("m")).toArray();
	for(int i = 0; i < msgs.size(); i++)
	{
		QJsonObject m = msgs[i].toObject();
		if(m.value(QStringLiteral("id")).toString() != message || m.value(QStringLiteral("system")).toBool())
			continue;
		QJsonObject reactions = m.value(QStringLiteral("reactions")).toObject();
		QJsonArray who = reactions.value(emoji).toArray();
		const bool has = who.contains(fp);
		if(on == has)
			return false;
		if(on)
		{
			int own = 0;
			for(auto it = reactions.constBegin(); it != reactions.constEnd(); ++it)
				own += it.value().toArray().contains(fp) ? 1 : 0;
			if((!reactions.contains(emoji) && reactions.size() >= static_cast<int>(coplay::kMaxReactionKinds))
				|| own >= static_cast<int>(coplay::kMaxReactionsPerPlayer))
				return false;
			who.append(fp);
		}
		else
		{
			for(int k = 0; k < who.size(); k++)
			{
				if(who[k].toString() == fp)
				{
					who.removeAt(k);
					break;
				}
			}
		}
		if(who.isEmpty())
			reactions.remove(emoji);
		else
			reactions[emoji] = who;
		m[QStringLiteral("reactions")] = reactions;
		msgs[i] = m;
		c[QStringLiteral("m")] = msgs;
		chats[chat] = c;
		state[QStringLiteral("chats")] = chats;
		Save();
		emit changed();
		return true;
	}
	return false;
}

void FriendsClient::react(const QString &chat, const QString &message, const QString &emoji)
{
	if(!enabled || !coplay::ValidReaction(emoji.toStdString()))
		return;
	// Given already: taken back
	bool given = false;
	for(const QJsonValue &v : state.value(QStringLiteral("chats")).toObject().value(chat).toObject().value(QStringLiteral("m")).toArray())
	{
		const QJsonObject m = v.toObject();
		if(m.value(QStringLiteral("id")).toString() == message)
			given = m.value(QStringLiteral("reactions")).toObject().value(emoji).toArray().contains(OwnFp(state));
	}
	// Only what changed here goes out (not over the limits, not for a message that is not here)
	if(!ApplyReaction(chat, message, OwnFp(state), emoji, !given))
		return;
	QJsonObject payload;
	payload[QStringLiteral("t")] = QStringLiteral("react");
	payload[QStringLiteral("msg")] = message;
	payload[QStringLiteral("emoji")] = emoji;
	payload[QStringLiteral("on")] = !given;
	SendToChat(chat, payload, nullptr);
}

QVariantList FriendsClient::readers(const QString &chat) const
{
	QVariantList out;
	if(!enabled || !ReadReceipts())
		return out;
	const QJsonObject read = state.value(QStringLiteral("chats")).toObject().value(chat).toObject().value(QStringLiteral("read")).toObject();
	const QJsonObject friends = state.value(QStringLiteral("friends")).toObject();
	const QJsonObject names = state.value(QStringLiteral("groups")).toObject().value(chat).toObject().value(QStringLiteral("names")).toObject();
	for(auto it = read.constBegin(); it != read.constEnd(); ++it)
	{
		if(it.key() == OwnFp(state))
			continue;
		const QJsonObject f = friends.value(it.key()).toObject();
		out.append(QVariantMap{ { QStringLiteral("fp"), it.key() },
			{ QStringLiteral("name"), f.value(QStringLiteral("name")).toString(names.value(it.key()).toString()) },
			{ QStringLiteral("avatar"), AvatarUrl(f) }, { QStringLiteral("at"), it.value().toDouble() } });
	}
	return out;
}

void FriendsClient::ApplyVote(const QString &chat, const QString &plan, const QString &fp, const QJsonValue &answers)
{
	QJsonObject chats = state.value(QStringLiteral("chats")).toObject();
	QJsonObject c = chats.value(chat).toObject();
	QJsonArray msgs = c.value(QStringLiteral("m")).toArray();
	for(int i = 0; i < msgs.size(); i++)
	{
		QJsonObject m = msgs[i].toObject();
		if(m.value(QStringLiteral("id")).toString() != plan || !m.contains(QStringLiteral("plan")))
			continue;
		QJsonObject votes = m.value(QStringLiteral("votes")).toObject();
		votes[fp] = CleanAnswers(answers, static_cast<int>(m.value(QStringLiteral("plan")).toObject().value(QStringLiteral("times")).toArray().size()));
		m[QStringLiteral("votes")] = votes;
		msgs[i] = m;
		c[QStringLiteral("m")] = msgs;
		chats[chat] = c;
		state[QStringLiteral("chats")] = chats;
		Save();
		emit changed();
		return;
	}
}

void FriendsClient::sendPlan(const QString &chat, const QString &title, const QVariantList &times)
{
	if(!enabled)
		return;
	QJsonObject plan;
	plan[QStringLiteral("title")] = title;
	plan[QStringLiteral("times")] = QJsonArray::fromVariantList(times);
	plan = CleanPlan(plan);
	if(plan.isEmpty())
		return;
	QJsonObject payload;
	payload[QStringLiteral("t")] = QStringLiteral("plan");
	payload[QStringLiteral("id")] = RandomId();
	payload[QStringLiteral("at")] = static_cast<double>(NowMs());
	payload[QStringLiteral("plan")] = plan;
	payload[QStringLiteral("text")] = plan.value(QStringLiteral("title")).toString().isEmpty() ? tr("When shall we play?")
		: tr("When: %1?").arg(plan.value(QStringLiteral("title")).toString());
	QJsonObject mine = payload;
	mine[QStringLiteral("from")] = OwnFp(state);
	mine[QStringLiteral("mine")] = true;
	mine[QStringLiteral("state")] = QStringLiteral("sending");
	AddMessage(chat, mine);
	QPointer<FriendsClient> self(this);
	const QString id = payload.value(QStringLiteral("id")).toString();
	SendToChat(chat, payload, [self, chat, id](bool ok) {
		if(self)
			self->AddMessage(chat, QJsonObject{ { QStringLiteral("id"), id }, { QStringLiteral("state"), ok ? QStringLiteral("sent") : QStringLiteral("failed") } });
	});
}

void FriendsClient::vote(const QString &chat, const QString &plan, const QVariantList &answers)
{
	if(!enabled)
		return;
	const QJsonValue a = QJsonArray::fromVariantList(answers);
	ApplyVote(chat, plan, OwnFp(state), a);
	QJsonObject payload;
	payload[QStringLiteral("t")] = QStringLiteral("vote");
	payload[QStringLiteral("plan")] = plan;
	payload[QStringLiteral("answers")] = a;
	SendToChat(chat, payload, nullptr);
}

void FriendsClient::report(const QString &chat, const QStringList &ids, const QString &why)
{
	if(!enabled || ids.isEmpty())
		return;
	// Only what the player chose, opened: the server cannot read anything else
	QJsonArray picked;
	for(const QJsonValue &v : state.value(QStringLiteral("chats")).toObject().value(chat).toObject().value(QStringLiteral("m")).toArray())
	{
		const QJsonObject m = v.toObject();
		if(!ids.contains(m.value(QStringLiteral("id")).toString()) || m.value(QStringLiteral("mine")).toBool())
			continue;
		picked.append(QJsonObject{ { QStringLiteral("from"), m.value(QStringLiteral("from")) }, { QStringLiteral("at"), m.value(QStringLiteral("at")) },
			{ QStringLiteral("text"), m.value(QStringLiteral("text")) } });
	}
	if(picked.isEmpty())
		return;
	QJsonObject text;
	text[QStringLiteral("why")] = why.left(500);
	text[QStringLiteral("by")] = OwnFp(state);
	text[QStringLiteral("group")] = isGroup(chat);
	text[QStringLiteral("messages")] = picked;
	Call(QStringLiteral("report"), { { QStringLiteral("text"), QString::fromUtf8(QJsonDocument(text).toJson(QJsonDocument::Compact)) } }, false, nullptr);
}

// ------------------------------------------------------------------ backup

QJsonObject FriendsClient::Bundle() const
{
	QJsonObject bundle = state;
	// Another device of the identity has its own device keys and conversations
	bundle.remove(QStringLiteral("device"));
	bundle.remove(QStringLiteral("sessions"));
	bundle.remove(QStringLiteral("bundle_opks"));
	QJsonObject groups = bundle.value(QStringLiteral("groups")).toObject();
	for(auto it = groups.begin(); it != groups.end(); ++it)
	{
		QJsonObject g = it.value().toObject();
		g.remove(QStringLiteral("mine"));
		g.remove(QStringLiteral("chains"));
		g.remove(QStringLiteral("waiting"));
		it.value() = g;
	}
	bundle[QStringLiteral("groups")] = groups;
	return bundle;
}

bool FriendsClient::saveBackup(const QString &path, const QString &passphrase)
{
	if(!enabled || passphrase.size() < 8)
		return false;
	std::string text;
	if(!fc::SealBackup(passphrase.normalized(QString::NormalizationForm_C).toStdString(),
		QJsonDocument(Bundle()).toJson(QJsonDocument::Compact).toStdString(), text))
		return false;
	QSaveFile file(path);
	if(!file.open(QIODevice::WriteOnly))
		return false;
	file.write(text.data(), static_cast<qint64>(text.size()));
	return file.commit();
}

bool FriendsClient::restoreBackup(const QString &path, const QString &passphrase)
{
	QFile file(path);
	if(!file.open(QIODevice::ReadOnly))
		return false;
	std::string json;
	if(!fc::OpenBackup(passphrase.normalized(QString::NormalizationForm_C).toStdString(), file.readAll().toStdString(), json))
		return false;
	return RestoreFrom(QJsonDocument::fromJson(QByteArray::fromStdString(json)).object());
}

bool FriendsClient::RestoreFrom(QJsonObject restored)
{
	if(!restored.contains(QStringLiteral("identity")))
		return false;
	const QString path_now = QDir(folder).filePath(QStringLiteral("friends.dat"));
	if(!enabled && QFile::exists(path_now))
		QFile::rename(path_now, QDir(folder).filePath(QStringLiteral("friends-%1.unreadable").arg(QDateTime::currentSecsSinceEpoch())));
	// This computer becomes a new device of the identity, on the server it uses now
	fc::DeviceId device{};
	coplay::RandomBytes(device.data(), device.size());
	QJsonObject dev;
	dev[QStringLiteral("id")] = Hex(device);
	restored[QStringLiteral("device")] = dev;
	restored[QStringLiteral("server")] = ServerAddress();
	const QJsonObject before = state;
	state = restored;
	// The keys have to be the identity the bundle names
	fc::Identity id;
	if(!Identity(id) || B64(id.fp) != OwnFp(state))
	{
		state = before;
		return false;
	}
	const QJsonObject settings = state.value(QStringLiteral("settings")).toObject();
	my_state = settings.value(QStringLiteral("state")).toString(QStringLiteral("online"));
	my_free = settings.value(QStringLiteral("free")).toString();
	statuses.clear();
	card_fetched.clear();
	enabled = true;
	generation++;
	problem.clear();
	Save();
	PublishCard();
	// The own sender keys in every group, for the other members
	for(const QString &gid : state.value(QStringLiteral("groups")).toObject().keys())
		NewSenderKey(gid);
	poll_timer.start();
	status_timer.start();
	emit changed();
	return true;
}

// ------------------------------------------------------------------ linking another device

void FriendsClient::LinkFailed(const QString &why)
{
	link_timer.stop();
	link_state = QStringLiteral("failed");
	link_code.clear();
	problem = why;
	link_busy = false;
	emit changed();
}

void FriendsClient::startLinking()
{
	if(!enabled)
		return;
	cancelLinking();
	coplay::RandomBytes(link_secret.data(), link_secret.size());
	if(!fc::GenerateKeyPair(link_ek))
		return;
	link_side = 1;
	link_state = QStringLiteral("waiting");
	link_started_ms = NowMs();
	link_code.clear();
	link_text = QStringLiteral("psrpt://link/") + B64(link_secret);
	if(server != QLatin1String(kDefaultServer))
		link_text += QStringLiteral("?at=") + QString::fromLatin1(QUrl::toPercentEncoding(server));
	problem.clear();
	// Part 0: only the hash of this side's key. The key itself comes once the other side has put
	// its own, so that nobody can try keys until the six digits happen to match
	Call(QStringLiteral("link_put"), { { QStringLiteral("box"), B64(fc::LinkBox(link_secret)) }, { QStringLiteral("side"), QStringLiteral("1") },
		{ QStringLiteral("part"), QStringLiteral("0") }, { QStringLiteral("data"), B64(fc::Sha256(fc::B(link_ek.pub))) } }, false,
		[this](bool ok, const QJsonObject &) {
			if(!ok)
				LinkFailed(tr("The friends server did not take the link."));
		});
	link_timer.start();
	emit changed();
}

void FriendsClient::joinLink(const QString &text)
{
	if(enabled)
		return;
	cancelLinking();
	QString s = text.trimmed();
	const int at_link = s.indexOf(QStringLiteral("psrpt://link/"));
	if(at_link >= 0)
		s = s.mid(at_link + 13);
	const int q = s.indexOf(QLatin1Char('?'));
	if(q >= 0)
	{
		const QString at = QUrlQuery(s.mid(q + 1)).queryItemValue(QStringLiteral("at"), QUrl::FullyDecoded);
		s = s.left(q);
		if(!at.isEmpty())
			SetServer(at);
	}
	else
		SetServer(QString());
	if(!UnB64(s, link_secret) || !fc::GenerateKeyPair(link_ek))
	{
		LinkFailed(tr("That is no link from another device."));
		return;
	}
	// The other side's commitment comes first; this side's key is put once it is there
	link_side = 2;
	link_state = QStringLiteral("waiting");
	link_joined_ms = NowMs();
	link_commit.fill(0);
	link_text.clear();
	problem.clear();
	link_timer.start();
	emit changed();
}

void FriendsClient::cancelLinking()
{
	link_timer.stop();
	if(link_side != 0 && !link_state.isEmpty() && link_state != QLatin1String("done") && link_state != QLatin1String("failed"))
		Call(QStringLiteral("link_put"), { { QStringLiteral("box"), B64(fc::LinkBox(link_secret)) }, { QStringLiteral("side"), QString::number(link_side) },
			{ QStringLiteral("part"), QStringLiteral("0") }, { QStringLiteral("data"), QString() } }, false, nullptr);
	link_side = 0;
	link_state.clear();
	link_code.clear();
	link_text.clear();
	link_busy = false;
	link_key.fill(0);
	link_commit.fill(0);
	emit changed();
}

bool FriendsClient::LinkKeyFrom(const fc::Bytes &data)
{
	fc::Pub theirs{};
	fc::Key32 dh{};
	if(data.size() != 65)
		return false;
	std::memcpy(theirs.data(), data.data(), 65);
	if(!fc::ValidPublic(theirs) || !fc::Dh(link_ek, theirs, dh))
		return false;
	link_key = fc::LinkKey(link_secret, dh);
	link_code = QString::fromStdString(fc::LinkCode(link_key));
	link_state = QStringLiteral("code");
	return true;
}

void FriendsClient::PollLink()
{
	if(link_busy || link_side == 0)
		return;
	const QString box = B64(fc::LinkBox(link_secret));
	auto get = [this, box](const QString &side, int part, std::function<void(bool, const fc::Bytes &, const QJsonObject &)> then) {
		link_busy = true;
		Call(QStringLiteral("link_get"), { { QStringLiteral("box"), box }, { QStringLiteral("side"), side }, { QStringLiteral("part"), QString::number(part) } }, false,
			[this, then](bool ok, const QJsonObject &answer) {
				link_busy = false;
				fc::Bytes data;
				UnB64(answer.value(QStringLiteral("data")).toString(), data);
				then(ok, data, answer);
			});
	};
	auto gone = [this](const QJsonObject &answer) {
		// Taken away by the other device, or run out
		if(answer.value(QStringLiteral("error")).toString() == QLatin1String("unknown") && link_state != QLatin1String("waiting"))
			LinkFailed(tr("The link has run out. Start it again on the other device."));
	};
	// The device that shows the link: nobody came within the link's two minutes
	if(link_side == 1 && link_state == QLatin1String("waiting") && link_started_ms && NowMs() - link_started_ms > 120000)
	{
		LinkFailed(tr("Nobody used the link in time. Start it again."));
		return;
	}
	if(link_side == 1)
	{
		// The new device's key: then this side's own, out of hiding, and the digits
		get(QStringLiteral("2"), 0, [this, box, gone](bool ok, const fc::Bytes &data, const QJsonObject &answer) {
			if(!ok)
			{
				gone(answer);
				return;
			}
			if(data.empty())
				return;
			if(!LinkKeyFrom(data))
			{
				LinkFailed(tr("The other device's answer is broken."));
				return;
			}
			link_timer.stop();
			Call(QStringLiteral("link_put"), { { QStringLiteral("box"), box }, { QStringLiteral("side"), QStringLiteral("1") },
				{ QStringLiteral("part"), QStringLiteral("1") }, { QStringLiteral("data"), B64(link_ek.pub) } }, false,
				[this](bool put_ok, const QJsonObject &) {
					if(!put_ok)
						LinkFailed(tr("The friends server did not take the link."));
				});
			emit changed();
		});
		return;
	}
	// The new device: the commitment, then its own key, then the other key, then the parts
	const bool committed = std::any_of(link_commit.begin(), link_commit.end(), [](uint8_t b) { return b != 0; });
	if(!committed)
	{
		get(QStringLiteral("1"), 0, [this, box](bool ok, const fc::Bytes &data, const QJsonObject &answer) {
			// The server answers a box that is gone as an empty one: after a grace for the other
			// side to put its part up, the link has run out (or was stopped there)
			if(ok && answer.value(QStringLiteral("parts")).toArray().isEmpty() && NowMs() - link_joined_ms > 10000)
			{
				LinkFailed(tr("The link has run out. Start it again on the other device."));
				return;
			}
			if(!ok || data.size() != link_commit.size())
				return;
			std::memcpy(link_commit.data(), data.data(), data.size());
			Call(QStringLiteral("link_put"), { { QStringLiteral("box"), box }, { QStringLiteral("side"), QStringLiteral("2") },
				{ QStringLiteral("part"), QStringLiteral("0") }, { QStringLiteral("data"), B64(link_ek.pub) } }, false,
				[this](bool put_ok, const QJsonObject &) {
					if(!put_ok)
						LinkFailed(tr("The link has run out. Start it again on the other device."));
				});
		});
		return;
	}
	if(link_state == QLatin1String("waiting"))
	{
		get(QStringLiteral("1"), 1, [this, gone](bool ok, const fc::Bytes &data, const QJsonObject &answer) {
			if(!ok)
			{
				gone(answer);
				return;
			}
			if(data.empty())
				return;
			// The key has to be the one committed to
			if(fc::Sha256(data) != link_commit || !LinkKeyFrom(data))
			{
				LinkFailed(tr("The other device's answer is broken."));
				return;
			}
			emit changed();
		});
		return;
	}
	// Part 2 says how many parts there are (2 to n + 1), then all of them
	get(QStringLiteral("1"), 2, [this, box, gone](bool ok, const fc::Bytes &data, const QJsonObject &answer) {
		if(!ok)
		{
			gone(answer);
			return;
		}
		if(data.size() < 2)
			return;
		const int parts = (data[0] << 8) | data[1];
		link_state = QStringLiteral("receiving");
		link_timer.stop();
		emit changed();
		auto all = std::make_shared<fc::Bytes>(data.begin() + 2, data.end());
		auto next = std::make_shared<std::function<void(int)>>();
		*next = [this, box, parts, all, next](int part) {
			if(part > parts + 1)
			{
				// Opened with the key both sides worked out
				fc::Bytes plain;
				const fc::Bytes ad = fc::B(fc::LinkBox(link_secret));
				if(all->size() < 12 + 16 || !fc::Open(link_key.data(), all->data(), ad, fc::Bytes(all->begin() + 12, all->end()), plain)
					|| !RestoreFrom(QJsonDocument::fromJson(ToQ(plain)).object()))
				{
					LinkFailed(tr("What came from the other device does not open."));
					return;
				}
				Call(QStringLiteral("link_put"), { { QStringLiteral("box"), box }, { QStringLiteral("side"), QStringLiteral("2") },
					{ QStringLiteral("part"), QStringLiteral("0") }, { QStringLiteral("data"), QString() } }, false, nullptr);
				link_state = QStringLiteral("done");
				link_key.fill(0);
				emit changed();
				return;
			}
			Call(QStringLiteral("link_get"), { { QStringLiteral("box"), box }, { QStringLiteral("side"), QStringLiteral("1") }, { QStringLiteral("part"), QString::number(part) } }, false,
				[this, part, all, next](bool part_ok, const QJsonObject &part_answer) {
					fc::Bytes chunk;
					if(!part_ok || !UnB64(part_answer.value(QStringLiteral("data")).toString(), chunk))
					{
						LinkFailed(tr("Not everything came from the other device."));
						return;
					}
					all->insert(all->end(), chunk.begin(), chunk.end());
					(*next)(part + 1);
				});
		};
		(*next)(3);
	});
}

void FriendsClient::confirmLinking()
{
	if(link_side != 1 || link_state != QLatin1String("code"))
		return;
	// Everything of the identity, sealed with the key only the two devices have
	uint8_t nonce[12];
	coplay::RandomBytes(nonce, sizeof(nonce));
	fc::Bytes sealed;
	if(!fc::Seal(link_key.data(), nonce, fc::B(fc::LinkBox(link_secret)), ToBytes(QJsonDocument(Bundle()).toJson(QJsonDocument::Compact)), sealed))
		return;
	fc::Bytes all(nonce, nonce + 12);
	all.insert(all.end(), sealed.begin(), sealed.end());
	// In parts of at most 192 KB (parts 2 to n + 1); the first says how many there are
	const size_t chunk = 192 * 1024;
	const int parts = static_cast<int>((all.size() + 2 + chunk - 1) / chunk);
	fc::Bytes first{ static_cast<uint8_t>(parts >> 8), static_cast<uint8_t>(parts) };
	link_state = QStringLiteral("sending");
	emit changed();
	const QString box = B64(fc::LinkBox(link_secret));
	std::vector<fc::Bytes> datas;
	size_t at = 0;
	for(int part = 1; part <= parts; part++)
	{
		fc::Bytes data = part == 1 ? first : fc::Bytes();
		const size_t take = std::min(chunk - data.size(), all.size() - at);
		data.insert(data.end(), all.begin() + static_cast<std::ptrdiff_t>(at), all.begin() + static_cast<std::ptrdiff_t>(at + take));
		at += take;
		datas.push_back(std::move(data));
	}
	// The rest first, the first part (which says how many) last: the new device starts once it sees it
	auto put = [this, box](int part, const fc::Bytes &data, std::function<void(bool)> done) {
		Call(QStringLiteral("link_put"), { { QStringLiteral("box"), box }, { QStringLiteral("side"), QStringLiteral("1") },
			{ QStringLiteral("part"), QString::number(part) }, { QStringLiteral("data"), B64(data) } }, false,
			[done](bool ok, const QJsonObject &) { done(ok); });
	};
	auto fan = std::make_shared<Fanout>();
	fan->done = [this, put, first_part = datas.front()](bool ok) {
		if(!ok)
		{
			LinkFailed(tr("The friends server did not take everything."));
			return;
		}
		put(2, first_part, [this](bool first_ok) {
			if(!first_ok)
			{
				LinkFailed(tr("The friends server did not take everything."));
				return;
			}
			link_state = QStringLiteral("done");
			link_key.fill(0);
			emit changed();
		});
	};
	for(int part = 2; part <= parts; part++)
	{
		fan->pending++;
		put(part + 1, datas[static_cast<size_t>(part - 1)], [fan](bool ok) { fan->Finish(ok); });
	}
	fan->Finish(true);
}
