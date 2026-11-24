// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Play Together text chat: the list the QML side shows, sending with a link
// preview, and what arrives from the host or the guests.

#include <streamsession.h>
#include <coplaylinkpreview.h>
#include <chattext.h>

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
	/** Unread messages that mention this player. */
	int mentions = 0;
	int next_local = 0;
	bool previews = true;
	QHash<QString, QString> image_urls; // picture id -> data: URL
	/** Who read how far, and the reactions, as the session last said. */
	coplay::ChatMarks marks;
	/** How far this player's read mark went out. */
	uint32_t read_sent = 0;
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

// The text as rich text (with the players' names drawn as mentions) and the emoji shortcodes:
// chattext.h, shared with the friends' chats
static QString ChatHtml(const QString &text, const QStringList &names)
{
	return chattext::Html(text, names);
}

static QString WithEmoji(const QString &text)
{
	return chattext::WithEmoji(text);
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
		if(e.value("pending").toBool() || e.value("failed").toBool() || e.value("deleted").toBool())
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

bool StreamSession::GetCoPlayChatMentioned() const
{
	return coplay_chat && coplay_chat->unread > 0 && coplay_chat->mentions > 0;
}

QStringList StreamSession::CoPlayNames() const
{
	QStringList names;
	for(const QVariant &v : GetCoPlayPlayers())
	{
		const QString name = v.toMap().value(QStringLiteral("name")).toString();
		if(!name.isEmpty())
			names.append(name);
	}
	return names;
}

void StreamSession::CoPlayChatMarkDeleted(QVariantMap &e) const
{
	e["deleted"] = true;
	e["text"] = QString();
	e["html"] = QString();
	e["mentionsMe"] = false;
	e.remove("embed");
	e.remove("reply");
}

void StreamSession::coplayDeleteChat(qint64 seq)
{
	if(!coplay_chat || !IsCoPlayActive() || seq <= 0)
		return;
	// Only an own message, by the host's number
	for(const QVariant &v : std::as_const(coplay_chat->messages))
	{
		const QVariantMap e = v.toMap();
		if(e.value("seq").toLongLong() == seq && e.value("you").toBool() && !e.value("deleted").toBool())
		{
			CoPlaySendDelete(static_cast<uint32_t>(seq));
			return;
		}
	}
}

void StreamSession::coplayMarkChatRead()
{
	if(!coplay_chat)
		return;
	// The read mark: up to the newest message everybody has (pending ones have no number yet)
	uint32_t newest = 0;
	for(const QVariant &v : std::as_const(coplay_chat->messages))
		newest = std::max(newest, static_cast<uint32_t>(v.toMap().value(QStringLiteral("seq")).toLongLong()));
	if(newest > coplay_chat->read_sent && IsCoPlayActive())
	{
		coplay_chat->read_sent = newest;
		CoPlaySendChatRead(newest);
	}
	if(!coplay_chat->unread)
		return;
	coplay_chat->unread = 0;
	coplay_chat->mentions = 0;
	emit CoPlayChatChanged();
}

void StreamSession::CoPlayOnChatMarks()
{
	if(!coplay_chat)
		return;
	coplay_chat->marks = CoPlayFetchChatMarks();
	// Messages their writers deleted: stand-ins
	for(int i = 0; i < coplay_chat->messages.size(); i++)
	{
		QVariantMap e = coplay_chat->messages[i].toMap();
		const qint64 seq = e.value("seq").toLongLong();
		if(seq > 0 && !e.value("deleted").toBool() && coplay_chat->marks.deleted.count(static_cast<uint32_t>(seq)))
		{
			CoPlayChatMarkDeleted(e);
			coplay_chat->messages[i] = e;
		}
	}
	emit CoPlayChatChanged();
}

QVariantMap StreamSession::GetCoPlayChatMarks() const
{
	QVariantMap out;
	if(!coplay_chat)
		return out;
	const int own = IsCoPlayActive() ? CoPlayOwnSlot() : -1;
	QVariantMap read;
	for(const auto &r : coplay_chat->marks.read)
		read[QString::number(r.first)] = static_cast<qint64>(r.second);
	QVariantMap reactions;
	for(const auto &message : coplay_chat->marks.reactions)
	{
		QVariantList kinds;
		for(const auto &kind : message.second)
		{
			// ("slots" is a word Qt keeps for itself)
			QVariantList who;
			bool mine = false;
			for(uint8_t slot : kind.second)
			{
				who.append(slot);
				mine = mine || slot == own;
			}
			QVariantMap entry;
			entry[QStringLiteral("emoji")] = QString::fromStdString(kind.first);
			entry[QStringLiteral("slots")] = who;
			entry[QStringLiteral("mine")] = mine;
			kinds.append(entry);
		}
		reactions[QString::number(message.first)] = kinds;
	}
	out[QStringLiteral("read")] = read;
	out[QStringLiteral("reactions")] = reactions;
	return out;
}

void StreamSession::coplayReact(qint64 seq, const QString &emoji)
{
	if(!coplay_chat || !IsCoPlayActive() || seq <= 0 || !coplay::ValidReaction(emoji.toStdString()))
		return;
	const int own = CoPlayOwnSlot();
	bool given = false;
	const auto message = coplay_chat->marks.reactions.find(static_cast<uint32_t>(seq));
	if(message != coplay_chat->marks.reactions.end())
	{
		const auto kind = message->second.find(emoji.toStdString());
		given = kind != message->second.end() && std::find(kind->second.begin(), kind->second.end(), own) != kind->second.end();
		// Over the limits the host would refuse it: not asked at all
		if(!given)
		{
			size_t mine = 0;
			for(const auto &k : message->second)
				mine += std::find(k.second.begin(), k.second.end(), own) != k.second.end() ? 1 : 0;
			if(mine >= coplay::kMaxReactionsPerPlayer || (kind == message->second.end() && message->second.size() >= coplay::kMaxReactionKinds))
				return;
		}
	}
	CoPlaySendReaction(static_cast<uint32_t>(seq), emoji.toStdString(), !given);
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
	e["html"] = ChatHtml(text, CoPlayNames());
	e["reply"] = static_cast<qint64>(m.reply);
	e["mentionsMe"] = !mine && chattext::Mentions(text, CoPlayOwnName());
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
	// Deleted already (it came again after that)
	if(coplay_chat->marks.deleted.count(m.seq))
		CoPlayChatMarkDeleted(e);
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
	{
		coplay_chat->unread++;
		if(entry.value("mentionsMe").toBool())
			coplay_chat->mentions++;
	}
	emit CoPlayChatChanged();
	if(notify)
		emit coplayChatMessage(entry);
}

void StreamSession::coplaySendChat(const QString &raw_text, qint64 reply_seq)
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
	pending["html"] = ChatHtml(text, CoPlayNames());
	const uint32_t reply = reply_seq > 0 && reply_seq <= 0xffffffffLL ? static_cast<uint32_t>(reply_seq) : 0;
	pending["reply"] = static_cast<qint64>(reply);
	pending["time"] = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
	pending["history"] = false;
	pending["pending"] = true;
	pending["failed"] = false;
	CoPlayChatAdd(pending, false);

	QUrl link = coplay_chat->previews ? CoPlayLinkPreview::FirstLink(text) : QUrl();
	if(link.isEmpty())
	{
		CoPlayChatSend(local_key, text, coplay::ChatEmbed(), QByteArray(), reply);
		return;
	}
	QPointer<StreamSession> self(this);
	CoPlayLinkPreview::Fetch(this, link, [self, local_key, text, reply](const coplay::ChatEmbed &embed, const QByteArray &image) {
		if(self && self->coplay_chat)
			self->CoPlayChatSend(local_key, text, embed, image, reply);
	});
}

void StreamSession::CoPlayChatSend(const QString &local_key, const QString &text, const coplay::ChatEmbed &embed, const QByteArray &image, uint32_t reply)
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
		coplay::ChatMessage sent = host->SendChat(text.toStdString(), embed, picture, reply);
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
	uint32_t id = client ? client->SendChat(text.toStdString(), embed, picture, reply) : 0;
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
