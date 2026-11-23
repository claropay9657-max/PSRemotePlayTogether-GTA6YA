// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "coplayrecent.h"

#include "coplaymeet.h"
#include "coplayrooms.h"
#include "settings.h"

#include <coplay/invite.h>
#include <coplay/meet.h>
#include <coplay/protocol.h>

#include <QDateTime>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtConcurrentRun>

/** The invite's key in its text form, or empty for what is no invite. */
static QString KeyOf(const QString &invite_text, coplay::Invite *out = nullptr)
{
	coplay::Invite invite;
	if(!coplay::DecodeInvite(invite_text.trimmed().toStdString(), invite, nullptr))
		return QString();
	if(out)
		*out = invite;
	return QString::fromStdString(coplay::SecretToString(invite.secret));
}

static bool IsConsole(const QString &console)
{
	return console.isEmpty() || CoPlayRooms::IsConsole(console);
}

CoPlayRecent::CoPlayRecent(Settings *settings, QObject *parent)
	: QObject(parent)
	, settings(settings)
{
	Reload();
	connect(settings, &Settings::CurrentProfileChanged, this, [this] {
		Reload();
		emit Changed();
	});
}

void CoPlayRecent::Reload()
{
	generation++;
	entries.clear();
	status.clear();
	looking.clear();
	const QJsonDocument doc = QJsonDocument::fromJson(settings->GetCoPlayRecent().toUtf8());
	QHash<QString, bool> seen;
	for(const QVariant &item : doc.array().toVariantList())
	{
		QVariantMap entry = item.toMap();
		const QString invite = entry.value(QStringLiteral("invite")).toString().trimmed();
		const QString key = KeyOf(invite);
		// What does not read as it should is left out rather than shown half
		if(key.isEmpty() || seen.contains(key) || !IsConsole(entry.value(QStringLiteral("console")).toString()))
			continue;
		const QString cover = entry.value(QStringLiteral("cover")).toString();
		seen.insert(key, true);
		QVariantMap clean;
		clean[QStringLiteral("key")] = key;
		clean[QStringLiteral("invite")] = invite;
		clean[QStringLiteral("host")] = entry.value(QStringLiteral("host")).toString().left(static_cast<int>(coplay::kMaxNameLength));
		clean[QStringLiteral("console")] = entry.value(QStringLiteral("console")).toString();
		clean[QStringLiteral("title")] = entry.value(QStringLiteral("title")).toString().left(80);
		clean[QStringLiteral("game")] = entry.value(QStringLiteral("game")).toString().left(80);
		clean[QStringLiteral("cover")] = CoPlayRooms::IsCover(cover) ? cover : QString();
		clean[QStringLiteral("joined")] = entry.value(QStringLiteral("joined")).toDouble();
		entries.append(clean);
		if(entries.size() == kMax)
			break;
	}
}

void CoPlayRecent::Save()
{
	QVariantList stored;
	for(const QVariant &item : entries)
	{
		QVariantMap entry = item.toMap();
		entry.remove(QStringLiteral("key")); // the invite says it
		stored.append(entry);
	}
	settings->SetCoPlayRecent(QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(stored)).toJson(QJsonDocument::Compact)));
}

QVariantList CoPlayRecent::List() const
{
	QVariantList out;
	for(const QVariant &item : entries)
	{
		QVariantMap entry = item.toMap();
		entry[QStringLiteral("status")] = status.value(entry.value(QStringLiteral("key")).toString(), Unknown);
		out.append(entry);
	}
	return out;
}

void CoPlayRecent::Joined(const QString &invite_text, const QString &host, const QString &console)
{
	const QString invite = invite_text.trimmed();
	const QString key = KeyOf(invite);
	if(key.isEmpty())
		return;

	QVariantMap entry;
	for(int i = 0; i < entries.size(); ++i)
	{
		if(entries[i].toMap().value(QStringLiteral("key")).toString() == key)
		{
			// What a public room said about itself stays when it is not on the list now
			entry = entries.takeAt(i).toMap();
			break;
		}
	}
	entry[QStringLiteral("key")] = key;
	entry[QStringLiteral("invite")] = invite;
	entry[QStringLiteral("host")] = host.trimmed().left(static_cast<int>(coplay::kMaxNameLength));
	entry[QStringLiteral("console")] = IsConsole(console) ? console : QString();
	entry[QStringLiteral("joined")] = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
	for(const QVariant &item : CoPlayRoomList::Instance()->Rooms())
	{
		const QVariantMap room = item.toMap();
		if(KeyOf(room.value(QStringLiteral("invite")).toString()) != key)
			continue;
		entry[QStringLiteral("title")] = room.value(QStringLiteral("title")).toString().left(80);
		entry[QStringLiteral("game")] = room.value(QStringLiteral("game")).toString().left(80);
		const QString cover = room.value(QStringLiteral("cover")).toString();
		entry[QStringLiteral("cover")] = CoPlayRooms::IsCover(cover) ? cover : QString();
		break;
	}
	for(const char *name : { "title", "game", "cover" })
	{
		if(!entry.contains(QLatin1String(name)))
			entry[QLatin1String(name)] = QString();
	}
	entries.prepend(entry);
	while(entries.size() > kMax)
		entries.removeLast();
	// Just in: the host is there
	status[key] = Open;
	Save();
	emit Changed();
}

void CoPlayRecent::Forget(const QString &key)
{
	for(int i = 0; i < entries.size(); ++i)
	{
		if(entries[i].toMap().value(QStringLiteral("key")).toString() == key)
		{
			entries.removeAt(i);
			status.remove(key);
			Save();
			emit Changed();
			return;
		}
	}
}

void CoPlayRecent::Look(const QString &meet_setting)
{
	const bool allowed = CoPlayMeet::Allowed(meet_setting);
	const quint64 asked = generation;
	bool changed = false;
	for(const QVariant &item : entries)
	{
		const QString key = item.toMap().value(QStringLiteral("key")).toString();
		coplay::Invite invite;
		KeyOf(item.toMap().value(QStringLiteral("invite")).toString(), &invite);
		// The own setting says no meeting point: then nobody is asked, and nothing is said
		const std::string url = allowed ? coplay::MeetUrlFromInvite(invite.meet, CoPlayMeet::DefaultUrl()) : std::string();
		if(url.empty())
		{
			changed |= status.value(key, Unknown) != Unknown;
			status.remove(key);
			continue;
		}
		if(looking.contains(key))
			continue;
		looking.insert(key, true);
		if(!status.contains(key))
		{
			status[key] = Looking;
			changed = true;
		}
		const coplay::Secret secret = invite.secret;
		auto watcher = new QFutureWatcher<int>(this);
		connect(watcher, &QFutureWatcher<int>::finished, this, [this, watcher, key, asked]() {
			const int result = watcher->result();
			watcher->deleteLater();
			if(asked != generation)
				return; // another setting set's list by now
			looking.remove(key);
			if(status.value(key, -1) == result)
				return;
			status[key] = result;
			emit Changed();
		});
		watcher->setFuture(QtConcurrent::run([url, secret]() {
			switch(coplay::LookForHost(url, &CoPlayMeet::Post, secret))
			{
				case coplay::HostLook::There:
					return static_cast<int>(Open);
				case coplay::HostLook::NotThere:
					return static_cast<int>(Closed);
				default:
					return static_cast<int>(Unknown);
			}
		}));
	}
	if(changed)
		emit Changed();
}

QString CoPlayRecent::ConsoleOf(bool ps5, int game_source)
{
	switch(static_cast<coplay::GameSource>(game_source))
	{
		case coplay::GameSource::Computer:
			return QStringLiteral("pc");
		case coplay::GameSource::XboxApp:
			return QStringLiteral("xbox");
		default:
			return ps5 ? QStringLiteral("ps5") : QStringLiteral("ps4");
	}
}
