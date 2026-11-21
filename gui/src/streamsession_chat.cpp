// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Play Together text chat: the list the QML side shows, sending with a link
// preview, and what arrives from the host or the guests.

#include <streamsession.h>
#include <coplaylinkpreview.h>

#include <coplay/client.h>
#include <coplay/host.h>

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>

struct CoPlayChatState
{
	QVariantList messages; // oldest first
	int unread = 0;
	int next_local = 0;
	bool previews = true;
	QHash<QString, QString> image_urls; // picture id -> data: URL
};

/** Messages kept for the list; older ones scroll away for good. */
static const int kMaxChatEntries = 200;
/** A message the host has not confirmed by then is shown as not sent. */
static const int kChatSendTimeoutMs = 22000;

static QString HashKey(const coplay::BlobHash &hash)
{
	return QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(hash.data()), static_cast<int>(hash.size())).toHex());
}

static QString JpegUrl(const coplay::BlobData &data)
{
	if(!data || data->empty())
		return QString();
	QByteArray raw(reinterpret_cast<const char *>(data->data()), static_cast<int>(data->size()));
	return QStringLiteral("data:image/jpeg;base64,") + QString::fromLatin1(raw.toBase64());
}

/** The text as rich text for a Text item: escaped, links clickable, line breaks kept. */
static QString ChatHtml(const QString &text)
{
	static const QRegularExpression link(QStringLiteral("https?://[^\\s<>\"']+"), QRegularExpression::CaseInsensitiveOption);
	QString out;
	qsizetype last = 0;
	auto it = link.globalMatch(text);
	while(it.hasNext())
	{
		QRegularExpressionMatch m = it.next();
		QString url = m.captured(0);
		while(!url.isEmpty() && QStringLiteral(".,;:!?)]}").contains(url.back()))
			url.chop(1);
		out += text.mid(last, m.capturedStart(0) - last).toHtmlEscaped();
		out += QStringLiteral("<a href=\"%1\">%2</a>").arg(url.toHtmlEscaped(), url.toHtmlEscaped());
		last = m.capturedStart(0) + url.size();
	}
	out += text.mid(last).toHtmlEscaped();
	out.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
	return out;
}

/** :shortcodes: and a few faces typed as text become emoji. */
static QString WithEmoji(QString text)
{
	static const QList<QPair<QString, QString>> codes = {
		{ QStringLiteral(":smile:"), QStringLiteral("😄") }, { QStringLiteral(":joy:"), QStringLiteral("😂") },
		{ QStringLiteral(":wink:"), QStringLiteral("😉") }, { QStringLiteral(":heart:"), QStringLiteral("❤️") },
		{ QStringLiteral(":fire:"), QStringLiteral("🔥") }, { QStringLiteral(":thumbsup:"), QStringLiteral("👍") },
		{ QStringLiteral(":+1:"), QStringLiteral("👍") }, { QStringLiteral(":thumbsdown:"), QStringLiteral("👎") },
		{ QStringLiteral(":clap:"), QStringLiteral("👏") }, { QStringLiteral(":gg:"), QStringLiteral("🎮") },
		{ QStringLiteral(":skull:"), QStringLiteral("💀") }, { QStringLiteral(":eyes:"), QStringLiteral("👀") },
		{ QStringLiteral(":cry:"), QStringLiteral("😢") }, { QStringLiteral(":rage:"), QStringLiteral("😡") },
		{ QStringLiteral(":party:"), QStringLiteral("🥳") }, { QStringLiteral(":trophy:"), QStringLiteral("🏆") },
		{ QStringLiteral(":100:"), QStringLiteral("💯") }, { QStringLiteral(":rocket:"), QStringLiteral("🚀") },
		{ QStringLiteral(":ok:"), QStringLiteral("👌") }, { QStringLiteral(":wave:"), QStringLiteral("👋") },
		{ QStringLiteral(":thinking:"), QStringLiteral("🤔") }, { QStringLiteral(":sweat:"), QStringLiteral("😅") },
		{ QStringLiteral(":cool:"), QStringLiteral("😎") }, { QStringLiteral(":pray:"), QStringLiteral("🙏") },
	};
	for(const auto &c : codes)
		text.replace(c.first, c.second, Qt::CaseInsensitive);
	// Faces only where they stand on their own, so that links and times stay as they are
	static const QList<QPair<QRegularExpression, QString>> faces = {
		{ QRegularExpression(QStringLiteral("(^|\\s):-?\\)(?=\\s|$)")), QStringLiteral("\\1🙂") },
		{ QRegularExpression(QStringLiteral("(^|\\s):-?D(?=\\s|$)")), QStringLiteral("\\1😄") },
		{ QRegularExpression(QStringLiteral("(^|\\s);-?\\)(?=\\s|$)")), QStringLiteral("\\1😉") },
		{ QRegularExpression(QStringLiteral("(^|\\s):-?\\((?=\\s|$)")), QStringLiteral("\\1🙁") },
		{ QRegularExpression(QStringLiteral("(^|\\s):-?[pP](?=\\s|$)")), QStringLiteral("\\1😛") },
		{ QRegularExpression(QStringLiteral("(^|\\s)<3(?=\\s|$)")), QStringLiteral("\\1❤️") },
		{ QRegularExpression(QStringLiteral("(^|\\s)[xX]D(?=\\s|$)")), QStringLiteral("\\1😆") },
	};
	for(const auto &f : faces)
		text.replace(f.first, f.second);
	return text;
}

void StreamSession::CoPlayChatInit(bool previews)
{
	coplay_chat = new CoPlayChatState;
	coplay_chat->previews = previews;
}

void StreamSession::CoPlayChatFini()
{
	delete coplay_chat;
	coplay_chat = nullptr;
}

QVariantList StreamSession::GetCoPlayChat() const
{
	return coplay_chat ? coplay_chat->messages : QVariantList();
}

QJsonArray StreamSession::OverlayChat() const
{
	QJsonArray out;
	if(!coplay_chat)
		return out;
	// The last few that were really sent; the page fades them out by itself
	int start = qMax(0, static_cast<int>(coplay_chat->messages.size()) - 8);
	for(int i = start; i < coplay_chat->messages.size(); i++)
	{
		QVariantMap e = coplay_chat->messages[i].toMap();
		if(e.value("pending").toBool() || e.value("failed").toBool())
			continue;
		QJsonObject m;
		m["key"] = e.value("key").toString();
		m["slot"] = e.value("slot").toInt();
		m["name"] = e.value("name").toString();
		m["host"] = e.value("host").toBool();
		m["text"] = e.value("text").toString();
		m["time"] = e.value("time").toDouble();
		QVariantMap embed = e.value("embed").toMap();
		if(!embed.isEmpty())
		{
			m["site"] = embed.value("site").toString();
			m["title"] = embed.value("title").toString();
		}
		out.append(m);
	}
	return out;
}

int StreamSession::GetCoPlayChatUnread() const
{
	return coplay_chat ? coplay_chat->unread : 0;
}

void StreamSession::coplayMarkChatRead()
{
	if(!coplay_chat || !coplay_chat->unread)
		return;
	coplay_chat->unread = 0;
	emit CoPlayChatChanged();
}

QVariantMap StreamSession::CoPlayChatEntry(const coplay::ChatMessage &m, bool mine)
{
	QVariantMap e;
	e["key"] = QStringLiteral("m%1-%2").arg(m.time_ms).arg(m.seq);
	e["seq"] = static_cast<qint64>(m.seq);
	e["slot"] = m.slot;
	e["name"] = QString::fromStdString(m.name);
	e["you"] = mine;
	e["host"] = m.slot == 0;
	QString text = QString::fromStdString(m.text);
	e["text"] = text;
	e["html"] = ChatHtml(text);
	e["time"] = static_cast<double>(m.time_ms);
	e["history"] = (m.flags & coplay::kChatHistory) != 0;
	e["pending"] = false;
	e["failed"] = false;
	if(m.embed.Valid())
	{
		QVariantMap embed;
		QString url = QString::fromStdString(m.embed.url);
		QUrl parsed(url);
		// Only what can be opened in a browser is offered as a link
		embed["url"] = (parsed.scheme() == QLatin1String("https") || parsed.scheme() == QLatin1String("http")) ? url : QString();
		embed["site"] = QString::fromStdString(m.embed.site);
		embed["title"] = QString::fromStdString(m.embed.title);
		embed["description"] = QString::fromStdString(m.embed.description);
		QString image_key = m.embed.image.Valid() ? HashKey(m.embed.image.hash) : QString();
		embed["imageKey"] = image_key;
		embed["image"] = image_key.isEmpty() ? QString() : coplay_chat->image_urls.value(image_key);
		e["embed"] = embed;
	}
	return e;
}

void StreamSession::CoPlayChatAdd(const QVariantMap &entry, bool notify)
{
	QString key = entry.value("key").toString();
	for(const QVariant &v : std::as_const(coplay_chat->messages))
	{
		if(v.toMap().value("key").toString() == key)
			return; // came again, after joining again for one
	}
	coplay_chat->messages.append(entry);
	while(coplay_chat->messages.size() > kMaxChatEntries)
		coplay_chat->messages.removeFirst();
	if(notify)
		coplay_chat->unread++;
	emit CoPlayChatChanged();
	if(notify)
		emit coplayChatMessage(entry);
}

void StreamSession::coplaySendChat(const QString &raw_text)
{
	if(!coplay_chat || !IsCoPlayActive())
		return;
	QString text = WithEmoji(raw_text.trimmed());
	if(text.isEmpty())
		return;
	text = QString::fromStdString(coplay::TruncateUtf8(text.toStdString(), coplay::kMaxChatText));

	// Shown at once; replaced by what everybody gets once it has gone out
	QString local_key = QStringLiteral("p%1").arg(++coplay_chat->next_local);
	QVariantMap pending;
	pending["key"] = local_key;
	pending["slot"] = IsCoPlayGuest() ? CoPlayOwnSlot() : 0;
	pending["name"] = CoPlayOwnName();
	pending["you"] = true;
	pending["host"] = !IsCoPlayGuest();
	pending["text"] = text;
	pending["html"] = ChatHtml(text);
	pending["time"] = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
	pending["history"] = false;
	pending["pending"] = true;
	pending["failed"] = false;
	CoPlayChatAdd(pending, false);

	QUrl link = coplay_chat->previews ? CoPlayLinkPreview::FirstLink(text) : QUrl();
	if(link.isEmpty())
	{
		CoPlayChatSend(local_key, text, coplay::ChatEmbed(), QByteArray());
		return;
	}
	QPointer<StreamSession> self(this);
	CoPlayLinkPreview::Fetch(this, link, [self, local_key, text](const coplay::ChatEmbed &embed, const QByteArray &image) {
		if(self && self->coplay_chat)
			self->CoPlayChatSend(local_key, text, embed, image);
	});
}

void StreamSession::CoPlayChatSend(const QString &local_key, const QString &text, const coplay::ChatEmbed &embed, const QByteArray &image)
{
	std::vector<uint8_t> picture(image.constBegin(), image.constEnd());
	auto find_pending = [this, &local_key]() -> int {
		for(int i = 0; i < coplay_chat->messages.size(); i++)
		{
			if(coplay_chat->messages[i].toMap().value("key").toString() == local_key)
				return i;
		}
		return -1;
	};

	if(coplay::Host *host = IsCoPlayHosting() ? CoPlayHost() : nullptr)
	{
		coplay::ChatMessage sent = host->SendChat(text.toStdString(), embed, picture);
		int i = find_pending();
		if(i < 0)
			return;
		if(!sent.seq)
		{
			QVariantMap e = coplay_chat->messages[i].toMap();
			e["pending"] = false;
			e["failed"] = true;
			coplay_chat->messages[i] = e;
			emit CoPlayChatChanged();
			return;
		}
		if(sent.embed.image.Valid())
			coplay_chat->image_urls.insert(HashKey(sent.embed.image.hash), JpegUrl(host->ChatImage(sent.embed.image.hash)));
		coplay_chat->messages[i] = CoPlayChatEntry(sent, true);
		emit CoPlayChatChanged();
		return;
	}

	std::shared_ptr<coplay::Client> client = CoPlayClient();
	uint32_t id = client ? client->SendChat(text.toStdString(), embed, picture) : 0;
	int i = find_pending();
	if(i < 0)
		return;
	QVariantMap e = coplay_chat->messages[i].toMap();
	if(!id)
	{
		e["pending"] = false;
		e["failed"] = true;
		coplay_chat->messages[i] = e;
		emit CoPlayChatChanged();
		return;
	}
	e["upId"] = static_cast<qint64>(id);
	if(!picture.empty() && embed.Valid())
	{
		// Our own preview is shown from what we have
		coplay::BlobRef ref = coplay::MakeBlobRef(picture);
		if(ref.Valid())
			coplay_chat->image_urls.insert(HashKey(ref.hash), QStringLiteral("data:image/jpeg;base64,") + QString::fromLatin1(image.toBase64()));
	}
	coplay_chat->messages[i] = e;
	emit CoPlayChatChanged();

	QPointer<StreamSession> self(this);
	QTimer::singleShot(kChatSendTimeoutMs, this, [self, local_key]() {
		if(!self || !self->coplay_chat)
			return;
		for(int j = 0; j < self->coplay_chat->messages.size(); j++)
		{
			QVariantMap entry = self->coplay_chat->messages[j].toMap();
			if(entry.value("key").toString() == local_key && entry.value("pending").toBool())
			{
				entry["pending"] = false;
				entry["failed"] = true;
				self->coplay_chat->messages[j] = entry;
				emit self->CoPlayChatChanged();
				return;
			}
		}
	});
}

void StreamSession::CoPlayOnChat(const coplay::ChatMessage &m)
{
	if(!coplay_chat || !coplay)
		return;
	bool mine = IsCoPlayGuest() && m.slot == CoPlayOwnSlot() && m.up_id != 0;
	if(mine)
	{
		// Our own message is back: it takes the place of the one shown while sending
		for(int i = 0; i < coplay_chat->messages.size(); i++)
		{
			QVariantMap e = coplay_chat->messages[i].toMap();
			if(e.value("pending").toBool() && e.value("upId").toLongLong() == static_cast<qint64>(m.up_id))
			{
				coplay_chat->messages[i] = CoPlayChatEntry(m, true);
				emit CoPlayChatChanged();
				return;
			}
		}
	}
	bool history = (m.flags & coplay::kChatHistory) != 0;
	CoPlayChatAdd(CoPlayChatEntry(m, mine), !mine && !history);
}

void StreamSession::CoPlayOnChatImage(const coplay::BlobRef &ref)
{
	if(!coplay_chat || !coplay)
		return;
	coplay::BlobData data;
	if(coplay::Host *host = CoPlayHost())
		data = host->ChatImage(ref.hash);
	else if(std::shared_ptr<coplay::Client> client = CoPlayClient())
		data = client->ChatImage(ref.hash);
	QString url = JpegUrl(data);
	if(url.isEmpty())
		return;
	QString key = HashKey(ref.hash);
	coplay_chat->image_urls.insert(key, url);
	bool changed = false;
	for(int i = 0; i < coplay_chat->messages.size(); i++)
	{
		QVariantMap e = coplay_chat->messages[i].toMap();
		QVariantMap embed = e.value("embed").toMap();
		if(embed.value("imageKey").toString() == key && embed.value("image").toString().isEmpty())
		{
			embed["image"] = url;
			e["embed"] = embed;
			coplay_chat->messages[i] = e;
			changed = true;
		}
	}
	if(changed)
		emit CoPlayChatChanged();
}
