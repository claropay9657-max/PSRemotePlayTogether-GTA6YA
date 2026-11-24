// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Friends end to end, as the app does them (gui/src/friends.cpp): clients with folders of their
// own against a real friends server (server/friends/psrpt_friends.php). test/friends-client.sh
// starts the server with php -S and runs this with its address. Nothing here touches the
// player's settings or password store: the clients get no store name, their keys stay in the
// temporary folders.
//
// A request and a yes, statuses, chats both ways, link previews, a second device of the same
// identity from a backup, a new friend link, unfriending and blocking, and the state read again
// after a restart. The second argument is the web server's folder: the pages whose links get a
// preview are written there.
//
// With --peer it is instead the app on the other side for another client's test (the browser's,
// test/web/friends-client.test.mjs): it prints "LINK <friend link>" and "FP <fingerprint>",
// accepts every request ("ACCEPTED <fp>"), answers every message with "echo: <text>" as a reply
// to it ("GOT <fp> <text>"; "QUOTE <fp> <text>" if the message was itself a reply, with the
// text it quotes), deletes its last echo when asked "delete your echo" ("DELETED <id>"), says
// which of the other's messages were deleted ("GONE <id>"), shows "Playing Peer Game on PS5" as
// its status, and ends after the given seconds or when "quit" comes on its input.
//
//   friends-client-test <server> --peer <folder> <name> <seconds>

#include "friends.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QUrl>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>

#include <QSocketNotifier>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>

static int failures = 0;
static int checks = 0;

#define CHECK(what) \
	do { \
		checks++; \
		if(!(what)) \
		{ \
			failures++; \
			std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #what); \
		} \
	} while(0)

static void Spin(int ms)
{
	QEventLoop loop;
	QTimer::singleShot(ms, &loop, &QEventLoop::quit);
	loop.exec();
}

/** Until every client has no request under way (and a little longer, for what the answers start). */
static bool Settle(std::initializer_list<FriendsClient *> clients, int ms = 20000)
{
	QElapsedTimer t;
	t.start();
	int quiet = 0;
	while(t.elapsed() < ms)
	{
		Spin(50);
		bool busy = false;
		for(FriendsClient *c : clients)
			busy = busy || c->Pending() > 0;
		quiet = busy ? 0 : quiet + 1;
		if(quiet >= 4)
			return true;
	}
	return false;
}

/** Asks the server again (each client in turn) and lets everything that follows finish. */
static void Refresh(std::initializer_list<FriendsClient *> clients)
{
	for(FriendsClient *c : clients)
	{
		c->refresh();
		Settle(clients);
	}
}

static QVariantMap Entry(const QVariantList &list, const QString &key, const QString &value)
{
	for(const QVariant &v : list)
	{
		if(v.toMap().value(key).toString() == value)
			return v.toMap();
	}
	return QVariantMap();
}

static QStringList Texts(const FriendsClient &c, const QString &chat, bool mine)
{
	QStringList out;
	for(const QVariant &v : c.messages(chat))
	{
		if(v.toMap().value(QStringLiteral("mine")).toBool() == mine)
			out.append(v.toMap().value(QStringLiteral("text")).toString());
	}
	return out;
}

static int Peer(const QString &server, const QString &folder, const QString &name, int seconds)
{
	FriendsClient peer;
	peer.SetServer(server);
	peer.SetStorage(folder, QString());
	peer.SetProfile(name, QByteArray());
	if(!peer.Enabled())
		peer.enable();
	Settle({ &peer });
	peer.SetActivity(QStringLiteral("Playing Peer Game on PS5"), QVariantMap());
	Settle({ &peer });
	std::printf("LINK %s\nFP %s\n", qPrintable(peer.Link()), qPrintable(peer.Fingerprint()));
	std::fflush(stdout);
	QObject::connect(&peer, &FriendsClient::messageArrived, [&](const QString &chat, const QString &, const QString &text) {
		std::printf("GOT %s %s\n", qPrintable(chat), qPrintable(text));
		const QVariantList messages = peer.messages(chat);
		QVariantMap got;
		for(const QVariant &v : messages)
		{
			if(!v.toMap().value(QStringLiteral("mine")).toBool() && v.toMap().value(QStringLiteral("text")).toString() == text)
				got = v.toMap();
		}
		if(got.contains(QStringLiteral("replyTo")))
			std::printf("QUOTE %s %s\n", qPrintable(chat), qPrintable(got.value(QStringLiteral("replyTo")).toMap().value(QStringLiteral("text")).toString()));
		std::fflush(stdout);
		if(text == QLatin1String("delete your echo"))
		{
			// The newest own echo goes, for both
			QString last;
			for(const QVariant &v : messages)
			{
				if(v.toMap().value(QStringLiteral("mine")).toBool() && !v.toMap().value(QStringLiteral("deleted")).toBool())
					last = v.toMap().value(QStringLiteral("id")).toString();
			}
			peer.deleteMessage(chat, last);
			std::printf("DELETED %s\n", qPrintable(last));
			std::fflush(stdout);
			return;
		}
		peer.send(chat, QStringLiteral("echo: ") + text, got.value(QStringLiteral("id")).toString());
	});
	QSet<QString> gone;
	bool quit = false;
	QSocketNotifier input(0, QSocketNotifier::Read);
	QObject::connect(&input, &QSocketNotifier::activated, [&]() {
		char line[256] = {};
		if(!std::fgets(line, sizeof(line), stdin) || std::strncmp(line, "quit", 4) == 0)
			quit = true;
	});
	QElapsedTimer t;
	t.start();
	while(!quit && t.elapsed() < seconds * 1000ll)
	{
		peer.refresh();
		Settle({ &peer }, 5000);
		// The other's messages deleted meanwhile
		for(const QVariant &c : peer.Chats())
		{
			for(const QVariant &v : peer.messages(c.toMap().value(QStringLiteral("id")).toString()))
			{
				const QVariantMap m = v.toMap();
				const QString id = m.value(QStringLiteral("id")).toString();
				if(m.value(QStringLiteral("deleted")).toBool() && !m.value(QStringLiteral("mine")).toBool() && !gone.contains(id))
				{
					gone.insert(id);
					std::printf("GONE %s\n", qPrintable(id));
					std::fflush(stdout);
				}
			}
		}
		for(const QVariant &v : peer.Requests())
		{
			const QVariantMap r = v.toMap();
			if(r.value(QStringLiteral("incoming")).toBool())
			{
				peer.accept(r.value(QStringLiteral("fp")).toString());
				std::printf("ACCEPTED %s\n", qPrintable(r.value(QStringLiteral("fp")).toString()));
				std::fflush(stdout);
			}
		}
		Spin(300);
	}
	Settle({ &peer }, 5000);
	return 0;
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	if(argc < 2)
	{
		std::printf("usage: friends-client-test <friends server address> [--peer <folder> <name> <seconds>]\n");
		return 2;
	}
	const QString server = QString::fromLocal8Bit(argv[1]);
	if(argc >= 6 && std::strcmp(argv[2], "--peer") == 0)
		return Peer(server, QString::fromLocal8Bit(argv[3]), QString::fromLocal8Bit(argv[4]), std::atoi(argv[5]));
	QTemporaryDir dir;
	CHECK(dir.isValid());
	// A page with a preview and its picture, beside the friends server
	const QString www = argc >= 3 ? QString::fromLocal8Bit(argv[2]) : QString();
	const QUrl page = QUrl(server).resolved(QUrl(QStringLiteral("track.html")));
	if(!www.isEmpty())
	{
		QImage picture(1280, 720, QImage::Format_RGB32);
		QPainter painter(&picture);
		QLinearGradient sky(0, 0, 1280, 720);
		sky.setColorAt(0, QColor(0x8c, 0x52, 0xff));
		sky.setColorAt(1, QColor(0x00, 0xf2, 0xff));
		painter.fillRect(picture.rect(), sky);
		for(int i = 0; i < 40; i++)
			painter.fillRect((i * 97) % 1280, (i * 53) % 720, 60, 30, QColor::fromHsv((i * 37) % 360, 200, 230));
		painter.end();
		CHECK(picture.save(QDir(www).filePath(QStringLiteral("track.jpg")), "JPEG", 95));
		QFile html(QDir(www).filePath(QStringLiteral("track.html")));
		CHECK(html.open(QIODevice::WriteOnly));
		html.write("<html><head><meta property=\"og:title\" content=\"Friday&#39;s track\">"
			"<meta property=\"og:site_name\" content=\"Racing club\">"
			"<meta property=\"og:description\" content=\"Twelve corners, one long straight.\">"
			"<meta property=\"og:image\" content=\"track.jpg\"></head><body>Track</body></html>");
		html.close();
	}

	FriendsClient a, b;
	a.SetStorage(dir.filePath(QStringLiteral("a")), QString());
	b.SetStorage(dir.filePath(QStringLiteral("b")), QString());
	a.SetServer(server);
	b.SetServer(server);
	a.SetProfile(QStringLiteral("Alice"), QByteArray());
	b.SetProfile(QStringLiteral("Bob"), QByteArray());
	CHECK(!a.Enabled() && a.Link().isEmpty());

	// ---- on: an identity each, cards on the server
	a.enable();
	b.enable();
	CHECK(Settle({ &a, &b }));
	CHECK(a.Enabled() && b.Enabled());
	CHECK(a.Online() && b.Online());
	CHECK(a.Link().startsWith(QStringLiteral("psrpt://friend/")));
	CHECK(b.WebLink().contains(QStringLiteral("#f=")));
	const QString fa = a.Fingerprint(), fb = b.Fingerprint();
	CHECK(!fa.isEmpty() && fa != fb);

	// ---- a link looked up shows the name behind it
	bool looked = false;
	QString looked_name;
	QObject::connect(&a, &FriendsClient::linkLooked, [&](bool ok, const QString &, const QString &name, const QString &, const QString &) {
		looked = ok;
		looked_name = name;
	});
	a.lookUp(b.Link());
	Settle({ &a });
	CHECK(looked && looked_name == QLatin1String("Bob"));
	a.lookUp(QStringLiteral("not a link"));
	Settle({ &a });
	CHECK(!looked);

	// ---- a request, and a yes
	int requests_seen = 0;
	QObject::connect(&b, &FriendsClient::requestArrived, [&](const QString &fp, const QString &name) {
		if(fp == fa && name == QLatin1String("Alice"))
			requests_seen++;
	});
	a.sendRequest(b.Link(), QStringLiteral("Hi Bob, from the forum"));
	Settle({ &a, &b });
	QVariantMap out = Entry(a.Requests(), QStringLiteral("fp"), fb);
	CHECK(!out.isEmpty() && !out.value(QStringLiteral("incoming")).toBool() && !out.value(QStringLiteral("unsent")).toBool());
	CHECK(out.value(QStringLiteral("name")).toString() == QLatin1String("Bob"));
	Refresh({ &b });
	QVariantMap in = Entry(b.Requests(), QStringLiteral("fp"), fa);
	CHECK(in.value(QStringLiteral("incoming")).toBool());
	CHECK(in.value(QStringLiteral("name")).toString() == QLatin1String("Alice"));
	CHECK(in.value(QStringLiteral("text")).toString() == QLatin1String("Hi Bob, from the forum"));
	CHECK(requests_seen == 1);
	b.accept(fa);
	Settle({ &a, &b });
	Refresh({ &a, &b, &a });
	CHECK(b.Requests().isEmpty() && a.Requests().isEmpty());
	CHECK(Entry(a.Friends(), QStringLiteral("fp"), fb).value(QStringLiteral("name")).toString() == QLatin1String("Bob"));
	CHECK(Entry(b.Friends(), QStringLiteral("fp"), fa).value(QStringLiteral("name")).toString() == QLatin1String("Alice"));
	// Both see the same safety code
	const QString safety = Entry(a.Friends(), QStringLiteral("fp"), fb).value(QStringLiteral("safety")).toString();
	CHECK(safety.size() == 29 && safety == Entry(b.Friends(), QStringLiteral("fp"), fa).value(QStringLiteral("safety")).toString());

	// ---- statuses: what one does, the other sees
	b.SetActivity(QStringLiteral("Playing Astro Bot on PS5"), QVariantMap());
	a.SetMyState(QStringLiteral("busy"));
	a.SetMyFree(QStringLiteral("free after 8"));
	Settle({ &a, &b });
	Refresh({ &a, &b });
	QVariantMap bob = Entry(a.Friends(), QStringLiteral("fp"), fb);
	CHECK(bob.value(QStringLiteral("state")).toString() == QLatin1String("online"));
	CHECK(bob.value(QStringLiteral("activity")).toString() == QLatin1String("Playing Astro Bot on PS5"));
	QVariantMap alice = Entry(b.Friends(), QStringLiteral("fp"), fa);
	CHECK(alice.value(QStringLiteral("state")).toString() == QLatin1String("busy"));
	CHECK(alice.value(QStringLiteral("free")).toString() == QLatin1String("free after 8"));
	// The game hidden: the status stays, without it
	b.SetShowGame(false);
	Settle({ &b });
	Refresh({ &a });
	bob = Entry(a.Friends(), QStringLiteral("fp"), fb);
	CHECK(bob.value(QStringLiteral("state")).toString() == QLatin1String("online") && bob.value(QStringLiteral("activity")).toString().isEmpty());

	// ---- chats, both ways, several in a row
	int messages_seen = 0;
	QObject::connect(&b, &FriendsClient::messageArrived, [&](const QString &chat, const QString &name, const QString &) {
		if(chat == fa && name == QLatin1String("Alice"))
			messages_seen++;
	});
	a.send(fb, QStringLiteral("Hello Bob"));
	a.send(fb, QStringLiteral("Tonight at 8?"));
	Settle({ &a });
	const QVariantList sent = a.messages(fb);
	CHECK(sent.size() == 2);
	CHECK(!sent.isEmpty() && sent.last().toMap().value(QStringLiteral("state")).toString() == QLatin1String("sent"));
	Refresh({ &b });
	CHECK(Texts(b, fa, false) == QStringList({ QStringLiteral("Hello Bob"), QStringLiteral("Tonight at 8?") }));
	CHECK(messages_seen == 2);
	CHECK(b.Unread() == 2);
	// Not read yet: nothing under Alice's messages
	CHECK(!a.messages(fb).last().toMap().value(QStringLiteral("read")).toBool());
	b.markRead(fa);
	CHECK(b.Unread() == 0);
	Settle({ &b });
	Refresh({ &a });
	CHECK(a.messages(fb).last().toMap().value(QStringLiteral("read")).toBool());
	CHECK(a.messages(fb).first().toMap().value(QStringLiteral("read")).toBool());
	// Bob's picture goes under the last message he read: he is one of the readers, as far as that
	CHECK(a.readers(fb).size() == 1 && a.readers(fb).first().toMap().value(QStringLiteral("name")).toString() == QLatin1String("Bob"));
	// A reaction, and taken back
	const QString hello = Entry(b.messages(fa), QStringLiteral("text"), QStringLiteral("Hello Bob")).value(QStringLiteral("id")).toString();
	b.react(fa, hello, QStringLiteral("🔥"));
	Settle({ &b });
	Refresh({ &a });
	CHECK(Entry(a.messages(fb), QStringLiteral("id"), hello).value(QStringLiteral("reactions")).toMap().value(QStringLiteral("🔥")).toList()
		== QVariantList({ fb }));
	b.react(fa, hello, QStringLiteral("🔥"));
	Settle({ &b });
	Refresh({ &a });
	CHECK(Entry(a.messages(fb), QStringLiteral("id"), hello).value(QStringLiteral("reactions")).toMap().isEmpty());
	b.send(fa, QStringLiteral("Yes!"));
	b.send(fa, QStringLiteral("I'll host."));
	a.send(fb, QStringLiteral("Great"));
	Settle({ &a, &b });
	Refresh({ &a, &b });
	CHECK(Texts(a, fb, false) == QStringList({ QStringLiteral("Yes!"), QStringLiteral("I'll host.") }));
	CHECK(Texts(b, fa, false).size() == 3 && Texts(b, fa, false).last() == QLatin1String("Great"));
	// A join card
	b.sendJoin(fa, QVariantMap{ { QStringLiteral("invite"), QStringLiteral("https://example.invalid/#r=room") }, { QStringLiteral("title"), QStringLiteral("Bob's room") } });
	Settle({ &b });
	Refresh({ &a });
	const QVariantList with_join = a.messages(fb);
	CHECK(!with_join.isEmpty() && with_join.last().toMap().value(QStringLiteral("join")).toMap().value(QStringLiteral("title")).toString() == QLatin1String("Bob's room"));

	// A link gets a preview: made by Bob's app, sealed with the message, Alice never opens the link
	if(!www.isEmpty())
	{
		const QString link_text = QStringLiteral("Look at this track: %1 !").arg(page.toString());
		b.send(fa, link_text);
		Settle({ &b });
		Refresh({ &a });
		const QVariantMap got = Entry(a.messages(fb), QStringLiteral("text"), link_text);
		const QVariantMap embed = got.value(QStringLiteral("embed")).toMap();
		CHECK(embed.value(QStringLiteral("url")).toString() == page.toString());
		CHECK(embed.value(QStringLiteral("title")).toString() == QLatin1String("Friday's track"));
		CHECK(embed.value(QStringLiteral("site")).toString() == QLatin1String("Racing club"));
		CHECK(embed.value(QStringLiteral("description")).toString() == QLatin1String("Twelve corners, one long straight."));
		const QString image = embed.value(QStringLiteral("image")).toString();
		CHECK(image.startsWith(QStringLiteral("data:image/jpeg;base64,")));
		const QByteArray jpeg = QByteArray::fromBase64(image.mid(23).toLatin1());
		CHECK(jpeg.size() > 1200 && jpeg.size() <= 5600 && !QImage::fromData(jpeg, "JPEG").isNull());
		// Bob sees the same on his message
		const QVariantMap own = Entry(b.messages(fa), QStringLiteral("text"), link_text);
		CHECK(own.value(QStringLiteral("embed")).toMap().value(QStringLiteral("title")).toString() == QLatin1String("Friday's track"));
		CHECK(own.value(QStringLiteral("state")).toString() == QLatin1String("sent"));
		// A long message: the preview gets what room is left, and the message still fits a note
		const QString long_text = QString(3300, QLatin1Char('x')) + QLatin1Char(' ') + page.toString();
		b.send(fa, long_text);
		Settle({ &b });
		Refresh({ &a });
		const QVariantMap long_got = Entry(a.messages(fb), QStringLiteral("text"), long_text);
		CHECK(!long_got.isEmpty());
		CHECK(long_got.value(QStringLiteral("embed")).toMap().value(QStringLiteral("title")).toString() == QLatin1String("Friday's track"));
		CHECK(Entry(b.messages(fa), QStringLiteral("text"), long_text).value(QStringLiteral("state")).toString() == QLatin1String("sent"));
		// Previews off: the link stays a link
		b.SetLinkPreviews(false);
		const QString plain_text = QStringLiteral("Again: %1").arg(page.toString());
		b.send(fa, plain_text);
		Settle({ &b });
		Refresh({ &a });
		const QVariantMap plain = Entry(a.messages(fb), QStringLiteral("text"), plain_text);
		CHECK(!plain.isEmpty() && !plain.contains(QStringLiteral("embed")));
		b.SetLinkPreviews(true);
	}

	// ---- a second device of Alice, from a backup
	const QString backup = dir.filePath(QStringLiteral("alice.psrpt-backup"));
	CHECK(!a.saveBackup(backup, QStringLiteral("short")));
	CHECK(a.saveBackup(backup, QStringLiteral("correct horse battery")));
	FriendsClient a2;
	a2.SetStorage(dir.filePath(QStringLiteral("a2")), QString());
	a2.SetServer(server);
	a2.SetProfile(QStringLiteral("Alice"), QByteArray());
	CHECK(!a2.restoreBackup(backup, QStringLiteral("wrong horse battery")));
	CHECK(a2.restoreBackup(backup, QStringLiteral("correct horse battery")));
	CHECK(a2.Fingerprint() == fa);
	Settle({ &a, &a2, &b });
	Refresh({ &b, &a });
	CHECK(Entry(a2.Friends(), QStringLiteral("fp"), fb).value(QStringLiteral("name")).toString() == QLatin1String("Bob"));
	// Bob writes once: both of Alice's devices have it
	b.send(fa, QStringLiteral("To both of you"));
	Settle({ &b });
	Refresh({ &a, &a2 });
	CHECK(Texts(a, fb, false).contains(QStringLiteral("To both of you")));
	CHECK(Texts(a2, fb, false).contains(QStringLiteral("To both of you")));
	// Read on one device: read on the other too
	CHECK(Entry(a2.Chats(), QStringLiteral("id"), fb).value(QStringLiteral("unread")).toInt() > 0);
	a.markRead(fb);
	Settle({ &a });
	Refresh({ &a2, &b });
	CHECK(Entry(a2.Chats(), QStringLiteral("id"), fb).value(QStringLiteral("unread")).toInt() == 0);
	// The new device writes: Bob has it, the first device has it as its own
	a2.send(fb, QStringLiteral("Now from my laptop"));
	Settle({ &a2 });
	Refresh({ &b, &a });
	CHECK(Texts(b, fa, false).contains(QStringLiteral("Now from my laptop")));

	// ---- a reply: the message it answers, and a copy of its start
	const QString laptop = Entry(b.messages(fa), QStringLiteral("text"), QStringLiteral("Now from my laptop")).value(QStringLiteral("id")).toString();
	b.send(fa, QStringLiteral("Nice laptop"), laptop);
	Settle({ &b });
	Refresh({ &a, &a2 });
	const QVariantMap answer = Entry(a.messages(fb), QStringLiteral("text"), QStringLiteral("Nice laptop"));
	const QVariantMap to = answer.value(QStringLiteral("replyTo")).toMap();
	CHECK(to.value(QStringLiteral("id")).toString() == laptop && to.value(QStringLiteral("found")).toBool());
	CHECK(to.value(QStringLiteral("name")).toString() == QLatin1String("Alice") && to.value(QStringLiteral("text")).toString() == QLatin1String("Now from my laptop"));
	CHECK(Entry(b.messages(fa), QStringLiteral("text"), QStringLiteral("Nice laptop")).value(QStringLiteral("replyTo")).toMap()
		.value(QStringLiteral("name")).toString() == QLatin1String("Alice"));
	// Bob's own copy on its way to Bob's devices carried the quote: the copy is what the others keep
	CHECK(Entry(a2.messages(fb), QStringLiteral("text"), QStringLiteral("Nice laptop")).value(QStringLiteral("quote")).toMap()
		.value(QStringLiteral("text")).toString() == QLatin1String("Now from my laptop"));

	// ---- deleting: Alice's laptop deletes its message, for Bob and for Alice's other device
	int alice_notes = 0;
	QObject::connect(&a, &FriendsClient::messageArrived, [&](const QString &chat, const QString &, const QString &) {
		if(chat == fb)
			alice_notes++;
	});
	a2.deleteMessage(fb, laptop);
	CHECK(Entry(a2.messages(fb), QStringLiteral("id"), laptop).value(QStringLiteral("deleted")).toBool());
	Settle({ &a2 });
	Refresh({ &b, &a });
	const QVariantMap gone = Entry(b.messages(fa), QStringLiteral("id"), laptop);
	CHECK(gone.value(QStringLiteral("deleted")).toBool() && gone.value(QStringLiteral("text")).toString().isEmpty());
	CHECK(Entry(a.messages(fb), QStringLiteral("id"), laptop).value(QStringLiteral("deleted")).toBool());
	// The answer to it now quotes a deleted message
	const QVariantMap after = Entry(b.messages(fa), QStringLiteral("text"), QStringLiteral("Nice laptop")).value(QStringLiteral("replyTo")).toMap();
	CHECK(after.value(QStringLiteral("deleted")).toBool() && after.value(QStringLiteral("text")).toString() == QLatin1String("Deleted message"));
	// Only one's own: Bob cannot delete Alice's message, Alice's deletes nothing that is Bob's
	const QString bobs = Entry(a.messages(fb), QStringLiteral("text"), QStringLiteral("Nice laptop")).value(QStringLiteral("id")).toString();
	a.deleteMessage(fb, bobs);
	Settle({ &a });
	Refresh({ &b });
	CHECK(!Entry(b.messages(fa), QStringLiteral("id"), bobs).value(QStringLiteral("deleted")).toBool());
	CHECK(!Entry(a.messages(fb), QStringLiteral("id"), bobs).value(QStringLiteral("deleted")).toBool());

	// ---- muted: no notification and not in the total, unless a message mentions Alice
	a.mute(fb, 60);
	CHECK(a.mutedUntil(fb) > static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
	CHECK(Entry(a.Chats(), QStringLiteral("id"), fb).value(QStringLiteral("muted")).toBool());
	a.markRead(fb);
	Settle({ &a });
	const int notes_before = alice_notes;
	b.send(fa, QStringLiteral("Are you there?"));
	Settle({ &b });
	Refresh({ &a });
	CHECK(alice_notes == notes_before);
	CHECK(Entry(a.Chats(), QStringLiteral("id"), fb).value(QStringLiteral("unread")).toInt() == 1);
	CHECK(a.Unread() == 0);
	b.send(fa, QStringLiteral("@alice, look!"));
	Settle({ &b });
	Refresh({ &a });
	CHECK(alice_notes == notes_before + 1);
	CHECK(a.Unread() == 1);
	const QVariantMap mention = Entry(a.messages(fb), QStringLiteral("text"), QStringLiteral("@alice, look!"));
	CHECK(mention.value(QStringLiteral("mentionsMe")).toBool());
	CHECK(mention.value(QStringLiteral("html")).toString().contains(QStringLiteral("<font color=\"#b18cff\"><b>@alice</b></font>")));
	CHECK(!Entry(a.messages(fb), QStringLiteral("text"), QStringLiteral("Are you there?")).value(QStringLiteral("mentionsMe")).toBool());
	a.mute(fb, 0);
	CHECK(a.mutedUntil(fb) == 0 && a.Unread() == 2);
	a.mute(fb, -1);
	CHECK(a.mutedUntil(fb) > 1.0e15);
	a.mute(fb, 0);
	a.markRead(fb);
	Settle({ &a });
	// The laptop's message is the first device's own too (deleted by now, as a stand-in)
	CHECK(Entry(a.messages(fb), QStringLiteral("id"), laptop).value(QStringLiteral("mine")).toBool());
	// And the first device's own messages reach the second one
	a.send(fb, QStringLiteral("Back on the desktop"));
	Settle({ &a });
	Refresh({ &a2, &b });
	CHECK(Texts(a2, fb, true).contains(QStringLiteral("Back on the desktop")));
	CHECK(Texts(b, fa, false).contains(QStringLiteral("Back on the desktop")));

	// ---- read again after a restart
	{
		FriendsClient again;
		again.SetServer(server);
		again.SetStorage(dir.filePath(QStringLiteral("a")), QString());
		CHECK(again.Enabled() && again.Fingerprint() == fa);
		CHECK(Entry(again.Friends(), QStringLiteral("fp"), fb).value(QStringLiteral("name")).toString() == QLatin1String("Bob"));
		CHECK(again.messages(fb).size() == a.messages(fb).size());
		CHECK(again.MyState() == QLatin1String("busy"));
		// Gone before it asks the server: the folder is the first device's
	}

	// ---- a third device of Alice, linked from the first one (no backup file)
	{
		FriendsClient a3;
		a3.SetStorage(dir.filePath(QStringLiteral("a3")), QString());
		a3.SetProfile(QStringLiteral("Alice"), QByteArray());
		a.startLinking();
		Settle({ &a });
		CHECK(a.LinkText().startsWith(QStringLiteral("psrpt://link/")));
		a3.joinLink(a.LinkText());
		auto wait_for = [](const std::function<bool()> &done) {
			QElapsedTimer t;
			t.start();
			while(!done() && t.elapsed() < 20000)
				Spin(100);
			return done();
		};
		CHECK(wait_for([&]() { return a.LinkState() == QLatin1String("code") && a3.LinkState() == QLatin1String("code"); }));
		CHECK(!a.LinkCodeText().isEmpty() && a.LinkCodeText() == a3.LinkCodeText());
		a.confirmLinking();
		CHECK(wait_for([&]() { return a3.LinkState() == QLatin1String("done"); }));
		CHECK(a.LinkState() == QLatin1String("done"));
		CHECK(a3.Enabled() && a3.Fingerprint() == fa);
		Settle({ &a3, &a });
		Refresh({ &b, &a3 });
		CHECK(Entry(a3.Friends(), QStringLiteral("fp"), fb).value(QStringLiteral("name")).toString() == QLatin1String("Bob"));
		// Three devices on the card now, this one marked
		const QVariantList devices = a3.Devices();
		CHECK(devices.size() == 3);
		int marked = 0;
		for(const QVariant &d : devices)
			marked += d.toMap().value(QStringLiteral("me")).toBool() ? 1 : 0;
		CHECK(marked == 1);
		b.send(fa, QStringLiteral("Three devices now?"));
		Settle({ &b });
		Refresh({ &a3, &a, &a2 });
		CHECK(Texts(a3, fb, false).contains(QStringLiteral("Three devices now?")));
		a3.disable();
		Settle({ &a3 });
		// A link that was never made leads nowhere
		FriendsClient a4;
		a4.SetStorage(dir.filePath(QStringLiteral("a4")), QString());
		a4.SetServer(server);
		a4.joinLink(QStringLiteral("not a link"));
		CHECK(a4.LinkState() == QLatin1String("failed") && !a4.Enabled());
	}

	// ---- a group: Alice (both devices), Bob, and Carol, who is Alice's friend but not Bob's
	{
		FriendsClient carol;
		carol.SetStorage(dir.filePath(QStringLiteral("carol")), QString());
		carol.SetServer(server);
		carol.SetProfile(QStringLiteral("Carol"), QByteArray());
		carol.enable();
		Settle({ &carol });
		carol.sendRequest(a.Link(), QString());
		Settle({ &carol });
		Refresh({ &a });
		a.accept(carol.Fingerprint());
		Settle({ &a });
		Refresh({ &carol, &a2, &a });
		const QString fcarol = carol.Fingerprint();
		const QString gid = a.createGroup(QStringLiteral("Friday racing"), { fb, fcarol });
		CHECK(!gid.isEmpty() && a.isGroup(gid));
		Settle({ &a });
		// The group to everybody, then everybody's sender keys through its box
		for(int round = 0; round < 3; round++)
			Refresh({ &b, &carol, &a2, &a });
		CHECK(b.isGroup(gid) && carol.isGroup(gid) && a2.isGroup(gid));
		const QVariantMap seen_by_carol = carol.group(gid);
		CHECK(seen_by_carol.value(QStringLiteral("name")).toString() == QLatin1String("Friday racing"));
		CHECK(seen_by_carol.value(QStringLiteral("members")).toList().size() == 3);
		CHECK(!seen_by_carol.value(QStringLiteral("admin")).toBool() && a.group(gid).value(QStringLiteral("admin")).toBool());
		CHECK(Entry(carol.Chats(), QStringLiteral("id"), gid).value(QStringLiteral("kind")).toString() == QLatin1String("group"));

		b.send(gid, QStringLiteral("Hi all"));
		Settle({ &b });
		Refresh({ &a, &a2, &carol });
		// A preview in a group: Carol, who is no friend of Bob, gets it too
		if(!www.isEmpty())
		{
			const QString group_link = QStringLiteral("Track for Friday %1").arg(page.toString());
			b.send(gid, group_link);
			Settle({ &b });
			Refresh({ &carol, &a2 });
			CHECK(Entry(carol.messages(gid), QStringLiteral("text"), group_link).value(QStringLiteral("embed")).toMap()
				.value(QStringLiteral("image")).toString().startsWith(QStringLiteral("data:image/jpeg;base64,")));
			CHECK(Entry(a2.messages(gid), QStringLiteral("text"), group_link).value(QStringLiteral("embed")).toMap()
				.value(QStringLiteral("title")).toString() == QLatin1String("Friday's track"));
		}
		CHECK(Texts(a, gid, false).contains(QStringLiteral("Hi all")));
		CHECK(Texts(a2, gid, false).contains(QStringLiteral("Hi all")));
		CHECK(Texts(carol, gid, false).contains(QStringLiteral("Hi all")));
		// Carol knows Bob only from the group: his name comes with it
		CHECK(Entry(carol.messages(gid), QStringLiteral("text"), QStringLiteral("Hi all")).value(QStringLiteral("name")).toString() == QLatin1String("Bob"));
		carol.send(gid, QStringLiteral("Hey, Carol here"));
		a2.send(gid, QStringLiteral("Alice on the laptop"));
		Settle({ &carol, &a2 });
		Refresh({ &b, &a });
		CHECK(Texts(b, gid, false).contains(QStringLiteral("Hey, Carol here")));
		CHECK(Texts(b, gid, false).contains(QStringLiteral("Alice on the laptop")));
		CHECK(Texts(a, gid, true).contains(QStringLiteral("Alice on the laptop")));
		// Who can be mentioned: the others, Carol's Bob by the name the group gave him
		QStringList carols;
		for(const QVariant &v : carol.mentionable(gid))
			carols.append(v.toMap().value(QStringLiteral("name")).toString());
		carols.sort();
		CHECK(carols == QStringList({ QStringLiteral("Alice"), QStringLiteral("Bob") }));
		// A reply in the group, and one deleted: Carol's copy and Alice's devices follow
		const QString carol_hi = Entry(carol.messages(gid), QStringLiteral("text"), QStringLiteral("Hey, Carol here")).value(QStringLiteral("id")).toString();
		b.send(gid, QStringLiteral("@Carol welcome!"), carol_hi);
		b.send(gid, QStringLiteral("oops, wrong chat"));
		Settle({ &b });
		Refresh({ &carol, &a, &a2 });
		const QVariantMap welcome = Entry(carol.messages(gid), QStringLiteral("text"), QStringLiteral("@Carol welcome!"));
		CHECK(welcome.value(QStringLiteral("mentionsMe")).toBool());
		CHECK(welcome.value(QStringLiteral("replyTo")).toMap().value(QStringLiteral("name")).toString() == QLatin1String("Carol"));
		CHECK(Entry(a.messages(gid), QStringLiteral("text"), QStringLiteral("@Carol welcome!")).value(QStringLiteral("replyTo")).toMap()
			.value(QStringLiteral("text")).toString() == QLatin1String("Hey, Carol here"));
		CHECK(!Entry(a.messages(gid), QStringLiteral("text"), QStringLiteral("@Carol welcome!")).value(QStringLiteral("mentionsMe")).toBool());
		const QString oops = Entry(b.messages(gid), QStringLiteral("text"), QStringLiteral("oops, wrong chat")).value(QStringLiteral("id")).toString();
		b.deleteMessage(gid, oops);
		Settle({ &b });
		Refresh({ &carol, &a, &a2 });
		CHECK(Entry(carol.messages(gid), QStringLiteral("id"), oops).value(QStringLiteral("deleted")).toBool());
		CHECK(Entry(a2.messages(gid), QStringLiteral("id"), oops).value(QStringLiteral("deleted")).toBool());
		CHECK(Entry(carol.Chats(), QStringLiteral("id"), gid).value(QStringLiteral("last")).toString().endsWith(QStringLiteral("Message deleted")));
		// Carol (no friend of Bob) reacts to Bob's "Hi all"
		const QString hi = Entry(carol.messages(gid), QStringLiteral("text"), QStringLiteral("Hi all")).value(QStringLiteral("id")).toString();
		carol.react(gid, hi, QStringLiteral("👍"));
		Settle({ &carol });
		Refresh({ &b });
		CHECK(Entry(b.messages(gid), QStringLiteral("id"), hi).value(QStringLiteral("reactions")).toMap().value(QStringLiteral("👍")).toList()
			== QVariantList({ fcarol }));
		// Bob's "Hi all", read by Alice and Carol
		a.markRead(gid);
		carol.markRead(gid);
		Settle({ &a, &carol });
		Refresh({ &b });
		CHECK(Entry(b.messages(gid), QStringLiteral("text"), QStringLiteral("Hi all")).value(QStringLiteral("readBy")).toInt() == 2);
		// Read receipts off: none sent, none shown
		b.SetReadReceipts(false);
		CHECK(!Entry(b.messages(gid), QStringLiteral("text"), QStringLiteral("Hi all")).contains(QStringLiteral("readBy")));
		b.SetReadReceipts(true);

		// A plan, and answers to it
		const double t1 = static_cast<double>(QDateTime::currentMSecsSinceEpoch() + 3600000);
		b.sendPlan(gid, QStringLiteral("Racing"), { t1, t1 + 3600000 });
		Settle({ &b });
		Refresh({ &a, &carol });
		QString plan;
		for(const QVariant &v : a.messages(gid))
		{
			if(v.toMap().contains(QStringLiteral("plan")))
				plan = v.toMap().value(QStringLiteral("id")).toString();
		}
		CHECK(!plan.isEmpty());
		a.vote(gid, plan, { 2, 1 });
		carol.vote(gid, plan, { 0, 2 });
		Settle({ &a, &carol });
		Refresh({ &b });
		QVariantMap votes;
		for(const QVariant &v : b.messages(gid))
		{
			if(v.toMap().value(QStringLiteral("id")).toString() == plan)
				votes = v.toMap().value(QStringLiteral("votes")).toMap();
		}
		CHECK(votes.value(fa).toList() == QVariantList({ 2, 1 }));
		CHECK(votes.value(fcarol).toList() == QVariantList({ 0, 2 }));

		// Carol taken out and banned: she reads nothing new, and cannot be added again by others
		a.removeFromGroup(gid, fcarol, true);
		Settle({ &a });
		for(int round = 0; round < 2; round++)
			Refresh({ &carol, &b, &a2, &a });
		CHECK(carol.group(gid).value(QStringLiteral("left")).toBool());
		CHECK(b.group(gid).value(QStringLiteral("members")).toList().size() == 2);
		CHECK(b.group(gid).value(QStringLiteral("banned")).toList().size() == 1);
		b.send(gid, QStringLiteral("Only us now"));
		Settle({ &b });
		Refresh({ &carol, &a });
		CHECK(Texts(a, gid, false).contains(QStringLiteral("Only us now")));
		CHECK(!Texts(carol, gid, false).contains(QStringLiteral("Only us now")));

		// Bob leaves: the admin takes him out
		b.leaveGroup(gid);
		Settle({ &b });
		for(int round = 0; round < 2; round++)
			Refresh({ &a, &a2, &b });
		CHECK(b.group(gid).value(QStringLiteral("left")).toBool());
		CHECK(a.group(gid).value(QStringLiteral("members")).toList().size() == 1);
	}

	// ---- a new link: the old one leads nowhere, the friends stay
	const QString old_link = b.Link();
	b.newLink();
	Settle({ &b });
	CHECK(b.Link() != old_link);
	a.lookUp(old_link);
	Settle({ &a });
	CHECK(!looked);
	a.lookUp(b.Link());
	Settle({ &a });
	CHECK(looked && looked_name == QLatin1String("Bob"));
	Refresh({ &a });
	CHECK(!Entry(a.Friends(), QStringLiteral("fp"), fb).isEmpty());

	// ---- unfriending: gone on both sides, and on Alice's second device
	b.unfriend(fa);
	Settle({ &b });
	Refresh({ &a, &a2 });
	CHECK(b.Friends().isEmpty());
	CHECK(Entry(a.Friends(), QStringLiteral("fp"), fb).isEmpty());
	CHECK(Entry(a2.Friends(), QStringLiteral("fp"), fb).isEmpty());

	// ---- blocking: a new request comes, Bob blocks, the next one never shows
	a.sendRequest(b.Link(), QStringLiteral("Sorry, again?"));
	Settle({ &a });
	Refresh({ &b });
	CHECK(Entry(b.Requests(), QStringLiteral("fp"), fa).value(QStringLiteral("incoming")).toBool());
	b.block(fa);
	Settle({ &b });
	CHECK(b.Requests().isEmpty());
	a.decline(fb);
	a.sendRequest(b.Link(), QStringLiteral("Please?"));
	Settle({ &a });
	Refresh({ &b });
	CHECK(b.Requests().isEmpty() && b.Friends().isEmpty());

	// ---- off: nothing left in the folder
	a2.disable();
	CHECK(!a2.Enabled() && a2.Friends().isEmpty());
	// Off the card first, then the folder is emptied
	Settle({ &a2 });
	CHECK(!QFile::exists(dir.filePath(QStringLiteral("a2/friends.dat"))));

	std::printf("%d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
