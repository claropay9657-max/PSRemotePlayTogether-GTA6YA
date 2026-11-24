// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Pictures of the friends panel (gui/src/qml/FriendsPanel.qml) without starting the app, which
// would use the player's real settings: real friends clients against a friends server
// (test/friends-shot.sh starts one with php -S), the panel in an offscreen window with the
// app's own QML, icons and style, saved as PNG files.
//
//   friends-shot <friends server address> <folder for the pictures> [web server folder]
//
// With the web server's folder, Jonas's link to example.org gets a preview: the page and its
// picture are written there, and only that name is asked for through the friends server's
// address as a proxy (test/friends-shot.sh), so nothing goes out to the internet. Without it,
// nobody's links get a preview.
//
// Mara is "this computer". Her friends: Jonas hosts a session and has written to her, Lea is
// busy and free tonight, Sam is offline; Kim has sent a request. Pictures: the lists, the
// chats, the chat with Jonas, and the panel with friends switched off.

#include "chattext.h"
#include "friends.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QLinearGradient>
#include <QNetworkAccessManager>
#include <QNetworkProxyFactory>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QQmlNetworkAccessManagerFactory>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <rhi/qrhi.h>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <cstdio>
#include <initializer_list>
#include <memory>

/** What the panel reads of the app's settings. */
class ShotSettings : public QObject
{
	Q_OBJECT
	Q_PROPERTY(QString coplayName READ Name CONSTANT)
	Q_PROPERTY(QString coplayAvatarUrl READ Avatar CONSTANT)

	public:
		QString Name() const { return QStringLiteral("Mara"); }
		QString Avatar() const { return QString(); }
};

/** Stands in for the app's backend (Chiaki in QML) as far as the panel and its page use it. */
class ShotBackend : public QObject
{
	Q_OBJECT
	Q_PROPERTY(QObject *friends READ Friends NOTIFY friendsChanged)
	Q_PROPERTY(QObject *settings READ Settings CONSTANT)
	Q_PROPERTY(QObject *session READ Session CONSTANT)

	public:
		QObject *friends = nullptr;
		ShotSettings settings_object;
		QObject *Friends() const { return friends; }
		QObject *Settings() { return &settings_object; }
		QObject *Session() const { return nullptr; }
		void SetFriends(QObject *f)
		{
			friends = f;
			emit friendsChanged();
		}
		Q_INVOKABLE QString friendsSaveBackup(const QString &) { return QStringLiteral("cancel"); }
		Q_INVOKABLE QString friendsRestoreBackup(const QString &) { return QStringLiteral("cancel"); }

	signals:
		void friendsChanged();
};

/**
 * Game pictures come from Sony's image server; here every one of them is a picture made on the
 * spot, so that nothing goes out to the internet.
 */
class LocalCovers : public QNetworkAccessManager
{
	public:
		QString file;
		using QNetworkAccessManager::QNetworkAccessManager;

	protected:
		QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *data) override
		{
			if(request.url().host() == QLatin1String("image.api.playstation.com"))
				return QNetworkAccessManager::createRequest(op, QNetworkRequest(QUrl::fromLocalFile(file)), data);
			return QNetworkAccessManager::createRequest(op, request, data);
		}
};

class LocalCoversFactory : public QQmlNetworkAccessManagerFactory
{
	public:
		QString file;
		QNetworkAccessManager *create(QObject *parent) override
		{
			auto *manager = new LocalCovers(parent);
			manager->file = file;
			return manager;
		}
};

/** A stand-in key art: dusk over a track, speed lines, a big letter. */
static QString MakeCover(const QString &path)
{
	QImage image(512, 512, QImage::Format_RGB32);
	QPainter p(&image);
	p.setRenderHint(QPainter::Antialiasing);
	QLinearGradient sky(0, 0, 0, 512);
	sky.setColorAt(0.0, QColor(18, 24, 64));
	sky.setColorAt(0.55, QColor(214, 92, 58));
	sky.setColorAt(1.0, QColor(30, 16, 40));
	p.fillRect(image.rect(), sky);
	p.setBrush(QColor(255, 196, 120));
	p.setPen(Qt::NoPen);
	p.drawEllipse(QPointF(360, 250), 70, 70);
	QPainterPath road;
	road.moveTo(0, 512);
	road.lineTo(230, 300);
	road.lineTo(282, 300);
	road.lineTo(512, 512);
	p.fillPath(road, QColor(24, 20, 34));
	p.setPen(QPen(QColor(255, 230, 170, 200), 6, Qt::DashLine));
	p.drawLine(QPointF(256, 300), QPointF(256, 512));
	p.setPen(QPen(QColor(255, 255, 255, 60), 3));
	for(int i = 0; i < 9; i++)
		p.drawLine(QPointF(-40 + i * 30, 120 + i * 18), QPointF(140 + i * 30, 60 + i * 18));
	QFont font = p.font();
	font.setPixelSize(150);
	font.setBold(true);
	font.setItalic(true);
	p.setFont(font);
	p.setPen(QColor(255, 255, 255, 230));
	p.drawText(QRect(0, 340, 512, 170), Qt::AlignCenter, QStringLiteral("R9"));
	p.end();
	image.save(path, "PNG");
	return path;
}

/** Only example.org goes through the local server (as a proxy); everything else as it is. */
class ExampleOrgProxy : public QNetworkProxyFactory
{
	public:
		quint16 port = 0;
		QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery &query) override
		{
			if(query.peerHostName() == QLatin1String("example.org"))
				return { QNetworkProxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"), port) };
			return { QNetworkProxy(QNetworkProxy::NoProxy) };
		}
};

/** The page Jonas links to, with its picture: a track at night, as a site would show it. */
static void MakeTrackPage(const QDir &site)
{
	QImage image(1200, 630, QImage::Format_RGB32);
	QPainter p(&image);
	p.setRenderHint(QPainter::Antialiasing);
	QLinearGradient sky(0, 0, 0, 630);
	sky.setColorAt(0.0, QColor(14, 10, 40));
	sky.setColorAt(0.6, QColor(88, 40, 160));
	sky.setColorAt(1.0, QColor(10, 8, 24));
	p.fillRect(image.rect(), sky);
	// The track: a sweeping bend, kerbs, the racing line
	QPainterPath track;
	track.moveTo(-40, 600);
	track.cubicTo(300, 560, 420, 300, 760, 300);
	track.cubicTo(980, 300, 1100, 380, 1260, 360);
	p.setPen(QPen(QColor(28, 24, 44), 120, Qt::SolidLine, Qt::FlatCap));
	p.drawPath(track);
	p.setPen(QPen(QColor(255, 255, 255, 200), 6, Qt::DashLine, Qt::FlatCap));
	p.drawPath(track);
	p.setPen(QPen(QColor(0, 242, 255, 170), 4));
	p.drawPath(track.translated(0, -54));
	p.setPen(QPen(QColor(255, 70, 120, 170), 4));
	p.drawPath(track.translated(0, 54));
	// Floodlights
	p.setPen(Qt::NoPen);
	for(int i = 0; i < 6; i++)
	{
		QRadialGradient light(QPointF(120 + i * 200, 90), 70);
		light.setColorAt(0, QColor(255, 236, 200, 220));
		light.setColorAt(1, QColor(255, 236, 200, 0));
		p.setBrush(light);
		p.drawEllipse(QPointF(120 + i * 200, 90), 70, 70);
	}
	// A chequered flag
	for(int y = 0; y < 4; y++)
		for(int x = 0; x < 6; x++)
			p.fillRect(QRectF(960 + x * 30, 470 + y * 30, 30, 30), (x + y) % 2 ? QColor(240, 240, 250) : QColor(20, 18, 30));
	p.end();
	image.save(site.filePath(QStringLiteral("friday.jpg")), "JPEG", 90);
	QFile page(site.filePath(QStringLiteral("friday")));
	if(page.open(QIODevice::WriteOnly))
		page.write("<html><head><title>Friday night races</title>"
			"<meta property=\"og:site_name\" content=\"Night Circuit Club\">"
			"<meta property=\"og:title\" content=\"Friday night races: the track list\">"
			"<meta property=\"og:description\" content=\"Six tracks, two hours, one winner. Sign up in the chat.\">"
			"<meta property=\"og:image\" content=\"friday.jpg\"></head><body></body></html>");
}

/**
 * The window without a window: the scene graph drawn by the graphics card into a texture, as the
 * app draws its QML (shader effects included), and read back (as scripts/intro/filmshot.cpp).
 */
struct Offscreen
{
	QQuickRenderControl control;
	std::unique_ptr<QQuickWindow> window;
	QRhi *rhi = nullptr;
	std::unique_ptr<QRhiTexture> texture;
	std::unique_ptr<QRhiRenderBuffer> depth;
	std::unique_ptr<QRhiTextureRenderTarget> target;
	std::unique_ptr<QRhiRenderPassDescriptor> pass;

	bool Open(QQuickItem *root, int w, int h, qreal dpr)
	{
		window = std::make_unique<QQuickWindow>(&control);
		window->setColor(QColor(12, 10, 29));
		root->setParentItem(window->contentItem());
		root->setSize(QSizeF(w, h));
		window->contentItem()->setSize(QSizeF(w, h));
		window->setGeometry(0, 0, w, h);
		if(!control.initialize())
			return false;
		rhi = control.rhi();
		const QSize pixels(qRound(w * dpr), qRound(h * dpr));
		texture.reset(rhi->newTexture(QRhiTexture::RGBA8, pixels, 1, QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
		depth.reset(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, pixels, 1));
		if(!texture->create() || !depth->create())
			return false;
		QRhiTextureRenderTargetDescription desc((QRhiColorAttachment(texture.get())));
		desc.setDepthStencilBuffer(depth.get());
		target.reset(rhi->newTextureRenderTarget(desc));
		pass.reset(target->newCompatibleRenderPassDescriptor());
		target->setRenderPassDescriptor(pass.get());
		if(!target->create())
			return false;
		QQuickRenderTarget rt = QQuickRenderTarget::fromRhiRenderTarget(target.get());
		rt.setDevicePixelRatio(dpr);
		window->setRenderTarget(rt);
		return true;
	}

	QImage Grab()
	{
		QCoreApplication::processEvents();
		QCoreApplication::sendPostedEvents();
		control.polishItems();
		control.beginFrame();
		control.sync();
		control.render();
		QRhiReadbackResult result;
		bool done = false;
		QImage image;
		result.completed = [&]() {
			QImage wrapped(reinterpret_cast<const uchar *>(result.data.constData()), result.pixelSize.width(), result.pixelSize.height(),
				QImage::Format_RGBA8888_Premultiplied);
			image = rhi->isYUpInFramebuffer() ? wrapped.flipped(Qt::Vertical) : wrapped.copy();
			done = true;
		};
		QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();
		batch->readBackTexture(QRhiReadbackDescription(texture.get()), &result);
		control.commandBuffer()->resourceUpdate(batch);
		control.endFrame();
		if(!done)
			rhi->finish();
		return image;
	}
};

/** A session's chat as StreamSession gives it to RoomChat.qml: Mara hosts, Jonas and Lea joined. */
class ShotSession : public QObject
{
	Q_OBJECT
	Q_PROPERTY(bool coplayActive READ Active CONSTANT)
	Q_PROPERTY(QVariantList coplayChat READ Chat CONSTANT)
	Q_PROPERTY(QVariantList coplayPlayers READ Players CONSTANT)
	Q_PROPERTY(QVariantMap coplayChatMarks READ Marks CONSTANT)

	public:
		bool Active() const { return true; }
		QVariantList Chat() const
		{
			const double t = static_cast<double>(QDateTime::currentMSecsSinceEpoch()) - 300000;
			const QStringList names{ QStringLiteral("Mara"), QStringLiteral("Jonas"), QStringLiteral("Lea") };
			auto message = [&](int seq, int slot, const QString &name, bool you, const QString &text, double at) {
				return QVariantMap{ { QStringLiteral("key"), QStringLiteral("m%1").arg(seq) }, { QStringLiteral("seq"), seq },
					{ QStringLiteral("slot"), slot }, { QStringLiteral("name"), name }, { QStringLiteral("you"), you },
					{ QStringLiteral("host"), slot == 0 }, { QStringLiteral("text"), text }, { QStringLiteral("html"), chattext::Html(text, names) },
					{ QStringLiteral("time"), at }, { QStringLiteral("pending"), false }, { QStringLiteral("failed"), false },
					{ QStringLiteral("mentionsMe"), !you && chattext::Mentions(text, QStringLiteral("Mara")) } };
			};
			QVariantMap with_link = message(3, 2, QStringLiteral("Lea"), false, QStringLiteral("Look at this lap"), t + 60000);
			with_link[QStringLiteral("html")] = QStringLiteral("Look at this lap <a href=\"https://example.org/lap\">https://example.org/lap</a>");
			with_link[QStringLiteral("embed")] = QVariantMap{ { QStringLiteral("url"), QStringLiteral("https://example.org/lap") },
				{ QStringLiteral("site"), QStringLiteral("example.org") }, { QStringLiteral("title"), QStringLiteral("The fastest lap on Night Circuit") },
				{ QStringLiteral("description"), QStringLiteral("1:23.4 with the GT3, no assists.") } };
			// Mara answers Jonas; Lea deleted one; Jonas mentions Mara
			QVariantMap answer = message(4, 0, QStringLiteral("Mara"), true, QStringLiteral("Yes! Starting the race now"), t + 90000);
			answer[QStringLiteral("reply")] = 2;
			QVariantMap gone = message(5, 2, QStringLiteral("Lea"), false, QString(), t + 100000);
			gone[QStringLiteral("deleted")] = true;
			gone[QStringLiteral("html")] = QString();
			return { message(1, 1, QStringLiteral("Jonas"), false, QStringLiteral("Ready when you are 🏁"), t),
				message(2, 1, QStringLiteral("Jonas"), false, QStringLiteral("Same cars as last week?"), t + 20000), with_link, answer, gone,
				message(6, 1, QStringLiteral("Jonas"), false, QStringLiteral("@Mara can you bring the wheel?"), t + 110000) };
		}
		QVariantList Players() const
		{
			return { QVariantMap{ { QStringLiteral("slot"), 0 }, { QStringLiteral("name"), QStringLiteral("Mara") }, { QStringLiteral("you"), true } },
				QVariantMap{ { QStringLiteral("slot"), 1 }, { QStringLiteral("name"), QStringLiteral("Jonas") }, { QStringLiteral("you"), false } },
				QVariantMap{ { QStringLiteral("slot"), 2 }, { QStringLiteral("name"), QStringLiteral("Lea") }, { QStringLiteral("you"), false } } };
		}
		QVariantMap Marks() const
		{
			// Jonas has read everything, Lea up to her own link
			const QVariantMap read{ { QStringLiteral("1"), 6 }, { QStringLiteral("2"), 3 } };
			const QVariantMap reactions{
				{ QStringLiteral("2"), QVariantList{ QVariantMap{ { QStringLiteral("emoji"), QStringLiteral("👍") }, { QStringLiteral("slots"), QVariantList{ 0, 2 } }, { QStringLiteral("mine"), true } } } },
				{ QStringLiteral("3"), QVariantList{ QVariantMap{ { QStringLiteral("emoji"), QStringLiteral("🔥") }, { QStringLiteral("slots"), QVariantList{ 0, 1 } }, { QStringLiteral("mine"), true } },
					QVariantMap{ { QStringLiteral("emoji"), QStringLiteral("😮") }, { QStringLiteral("slots"), QVariantList{ 1 } }, { QStringLiteral("mine"), false } } } } };
			return { { QStringLiteral("read"), read }, { QStringLiteral("reactions"), reactions } };
		}
		Q_INVOKABLE void coplaySendChat(const QString &, qint64) {}
		Q_INVOKABLE void coplayDeleteChat(qint64) {}
		Q_INVOKABLE void coplayMarkChatRead() {}
		Q_INVOKABLE void coplayReact(qint64, const QString &) {}
};

static void Spin(int ms)
{
	QEventLoop loop;
	QTimer::singleShot(ms, &loop, &QEventLoop::quit);
	loop.exec();
}

static void Settle(std::initializer_list<FriendsClient *> clients)
{
	QElapsedTimer t;
	t.start();
	int quiet = 0;
	while(t.elapsed() < 20000 && quiet < 4)
	{
		Spin(50);
		bool busy = false;
		for(FriendsClient *c : clients)
			busy = busy || c->Pending() > 0;
		quiet = busy ? 0 : quiet + 1;
	}
}

static void Refresh(std::initializer_list<FriendsClient *> clients)
{
	for(FriendsClient *c : clients)
	{
		c->refresh();
		Settle(clients);
	}
}

static const char kStage[] = R"(
import QtQuick
import QtQuick.Controls

Item {
    id: root
    width: 1180
    height: 760

    function showConfirmDialog(title, text, callback) {}
    function showSettingsDialog(page) {}
    function showInfoDialog(title, text) {}
    function joinFriendRoom(room) {}

    property alias panel: panel
    property bool settings: false
    property var roomSession: null

    // One message with the quick reactions open, as on hover
    property bool bubbleDemo: false
    Rectangle {
        z: 1
        visible: root.bubbleDemo
        width: 460
        height: 260
        color: "#0c0a1d"

        ChatBubble {
            x: 0
            y: 50
            width: 460
            barOpen: true
            name: "Jonas"
            nameColor: "#00f2ff"
            html: "Who's in for Friday? 🏁"
            status: "21:03"
            canReply: true
            reactions: [{ emoji: "👍", count: 2, mine: true }, { emoji: "🔥", count: 1, mine: false }]
        }
    }

    // A session's chat, as beside the game picture
    Rectangle {
        z: 1
        visible: !!root.roomSession
        x: 0
        y: 0
        width: 420
        height: root.height
        color: "#0c0a1d"

        RoomChat {
            id: roomChat
            objectName: "roomChat"
            anchors.fill: parent
            anchors.margins: 10
            session: root.roomSession
            open: false
        }
    }

    // The friends settings page, as the settings window shows it
    Flickable {
        z: 1
        visible: root.settings
        x: 0
        y: 0
        width: 760
        height: root.height
        contentHeight: settingsPage.implicitHeight + 40
        clip: true

        Loader {
            id: settingsPage
            x: 24
            y: 20
            width: 712
            active: root.settings
            source: "qrc:/settings/FriendsPage.qml"
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#0c0a1d"
    }

    // Placed by this item: without a shown window the overlay never gets a size
    FriendsPanel {
        id: panel
        parent: root
    }
}
)";

int main(int argc, char **argv)
{
	if(argc < 3)
	{
		std::printf("usage: friends-shot <friends server address> <folder for the pictures>\n");
		return 2;
	}
	qputenv("QT_MAC_DISABLE_FOREGROUND_APPLICATION_TRANSFORM", "1");
	QGuiApplication app(argc, argv);
	const QString server = QString::fromLocal8Bit(argv[1]);
	const QDir out(QString::fromLocal8Bit(argv[2]));
	QDir().mkpath(out.path());
	QTemporaryDir dir;
	// Link previews only for example.org, from the local server
	const bool previews = argc >= 4;
	if(previews)
	{
		const QDir site(QDir(QString::fromLocal8Bit(argv[3])).filePath(QStringLiteral("example.org")));
		QDir().mkpath(site.path());
		MakeTrackPage(site);
		auto *proxy = new ExampleOrgProxy;
		proxy->port = static_cast<quint16>(QUrl(server).port());
		QNetworkProxyFactory::setApplicationProxyFactory(proxy);
	}

	// ---- the people
	FriendsClient mara, jonas, lea, sam, kim, off;
	struct Person { FriendsClient *c; const char *name; } people[] = {
		{ &mara, "Mara" }, { &jonas, "Jonas" }, { &lea, "Lea" }, { &sam, "Sam" }, { &kim, "Kim" } };
	for(const Person &p : people)
	{
		p.c->SetServer(server);
		p.c->SetStorage(dir.filePath(QString::fromLatin1(p.name)), QString());
		p.c->SetProfile(QString::fromLatin1(p.name), QByteArray());
		// Only Jonas's link gets a preview (it is the only one that goes to example.org)
		p.c->SetLinkPreviews(previews && p.c == &jonas);
		p.c->enable();
	}
	off.SetServer(server);
	off.SetStorage(dir.filePath(QStringLiteral("off")), QString());
	Settle({ &mara, &jonas, &lea, &sam, &kim });
	// Sam shows nothing: offline to everybody
	sam.SetMyState(QStringLiteral("invisible"));
	for(FriendsClient *f : { &jonas, &lea, &sam })
	{
		f->sendRequest(mara.Link(), QString());
		Settle({ f });
	}
	Refresh({ &mara });
	for(FriendsClient *f : { &jonas, &lea, &sam })
		mara.accept(f->Fingerprint());
	Settle({ &mara });
	Refresh({ &jonas, &lea, &sam, &mara });

	// Jonas hosts: his session as the app describes it (StreamSession::GetCoPlayInviteCard)
	const QVariantMap jonas_card{ { QStringLiteral("invite"), QStringLiteral("psrpt://join/example") },
		{ QStringLiteral("web"), QStringLiteral("https://crypto90.github.io/PSRemotePlayTogether/#j=example") },
		{ QStringLiteral("title"), QStringLiteral("Friday racing") }, { QStringLiteral("host"), QStringLiteral("Jonas") },
		{ QStringLiteral("game"), QStringLiteral("Night Circuit 9") },
		{ QStringLiteral("cover"), QStringLiteral("https://image.api.playstation.com/vulcan/stand-in/cover.png") },
		{ QStringLiteral("console"), QStringLiteral("ps5") }, { QStringLiteral("policy"), QStringLiteral("ask") },
		{ QStringLiteral("control"), QStringLiteral("all") }, { QStringLiteral("players"), 2 }, { QStringLiteral("max"), 4 } };
	jonas.SetActivity(QStringLiteral("Playing Night Circuit 9 with friends (2 / 4)"), jonas_card);
	lea.SetMyState(QStringLiteral("busy"));
	lea.SetMyFree(QStringLiteral("tonight from 8"));
	lea.SetActivity(QStringLiteral("Playing Astro Bot on PS5"), QVariantMap());
	mara.SetActivity(QStringLiteral("Playing Gran Turismo 7 on PS5"), QVariantMap());
	Settle({ &jonas, &lea, &mara });

	jonas.send(mara.Fingerprint(), QStringLiteral("Hey Mara! Racing tonight? :fire:"));
	Settle({ &jonas });
	Refresh({ &mara });
	mara.markRead(jonas.Fingerprint());
	mara.send(jonas.Fingerprint(), QStringLiteral("Sure, after dinner. Same cars as last week?"));
	Settle({ &mara });
	Refresh({ &jonas });
	jonas.markRead(mara.Fingerprint());
	Settle({ &jonas });
	// Reactions both ways
	for(const QVariant &v : jonas.messages(mara.Fingerprint()))
	{
		const QVariantMap m = v.toMap();
		if(m.value(QStringLiteral("text")).toString().startsWith(QStringLiteral("Sure, after dinner")))
			jonas.react(mara.Fingerprint(), m.value(QStringLiteral("id")).toString(), QStringLiteral("🔥"));
	}
	Settle({ &jonas });
	jonas.send(mara.Fingerprint(), QStringLiteral("Yes! Track list: http://example.org/friday"));
	jonas.sendJoin(mara.Fingerprint(), jonas_card);
	lea.send(mara.Fingerprint(), QStringLiteral("Can't today, maybe tomorrow 👋"));
	// Lea only wants to talk: no game, no picture
	lea.sendJoin(mara.Fingerprint(), QVariantMap{ { QStringLiteral("invite"), QStringLiteral("psrpt://join/lea") },
		{ QStringLiteral("title"), QStringLiteral("Lea's corner") }, { QStringLiteral("host"), QStringLiteral("Lea") },
		{ QStringLiteral("policy"), QStringLiteral("open") }, { QStringLiteral("players"), 1 }, { QStringLiteral("max"), 3 },
		{ QStringLiteral("talk"), true } });
	Settle({ &jonas, &lea });
	kim.sendRequest(mara.Link(), QStringLiteral("Hi, Kim from the forum. Up for some co-op?"));
	Settle({ &kim });
	Refresh({ &mara });

	// A group: Mara, Jonas and Lea; Jonas writes, Lea asks when, Mara and Jonas answer
	const QString gid = mara.createGroup(QStringLiteral("Friday racing"), { jonas.Fingerprint(), lea.Fingerprint() });
	Settle({ &mara });
	for(int round = 0; round < 3; round++)
		Refresh({ &jonas, &lea, &mara });
	jonas.send(gid, QStringLiteral("Who's in tonight? 🏁"));
	Settle({ &jonas });
	const QDateTime evening(QDate::currentDate().addDays(1), QTime(20, 0));
	lea.sendPlan(gid, QStringLiteral("Racing"), { static_cast<double>(evening.toMSecsSinceEpoch()),
		static_cast<double>(evening.addSecs(5400).toMSecsSinceEpoch()), static_cast<double>(evening.addDays(1).toMSecsSinceEpoch()) });
	Settle({ &lea });
	Refresh({ &mara, &jonas });
	QString plan;
	for(const QVariant &v : mara.messages(gid))
	{
		if(v.toMap().contains(QStringLiteral("plan")))
			plan = v.toMap().value(QStringLiteral("id")).toString();
	}
	jonas.vote(gid, plan, { 2, 1, 0 });
	lea.vote(gid, plan, { 2, 2, 1 });
	Settle({ &jonas, &lea });
	Refresh({ &mara });
	mara.vote(gid, plan, { 2, 0, 1 });
	Settle({ &mara });
	// Mara writes in the group; Jonas reads it, Lea not yet
	mara.send(gid, QStringLiteral("Tomorrow 20:00 then, I'll host 🏁"));
	Settle({ &mara });
	Refresh({ &jonas, &lea });
	jonas.markRead(gid);
	Settle({ &jonas });
	for(FriendsClient *who : { &jonas, &lea })
	{
		for(const QVariant &v : who->messages(gid))
		{
			const QVariantMap m = v.toMap();
			if(m.value(QStringLiteral("text")).toString().startsWith(QStringLiteral("Tomorrow 20:00")))
				who->react(gid, m.value(QStringLiteral("id")).toString(), QStringLiteral("🔥"));
		}
	}
	Settle({ &jonas, &lea });
	Refresh({ &mara });
	for(const QVariant &v : mara.messages(gid))
	{
		const QVariantMap m = v.toMap();
		if(m.value(QStringLiteral("text")).toString().startsWith(QStringLiteral("Who's in")))
			mara.react(gid, m.value(QStringLiteral("id")).toString(), QStringLiteral("👍"));
	}
	Settle({ &mara });
	// A reply, a mention of Mara, one deleted, and Lea's chat muted
	QString whos_in;
	for(const QVariant &v : mara.messages(gid))
	{
		if(v.toMap().value(QStringLiteral("text")).toString().startsWith(QStringLiteral("Who's in")))
			whos_in = v.toMap().value(QStringLiteral("id")).toString();
	}
	jonas.send(gid, QStringLiteral("sorry, wrong chat"));
	Settle({ &jonas });
	for(const QVariant &v : jonas.messages(gid))
	{
		if(v.toMap().value(QStringLiteral("text")).toString() == QLatin1String("sorry, wrong chat"))
			jonas.deleteMessage(gid, v.toMap().value(QStringLiteral("id")).toString());
	}
	Settle({ &jonas });
	lea.send(gid, QStringLiteral("@Mara can you bring the wheel?"));
	Settle({ &lea });
	Refresh({ &mara });
	mara.send(gid, QStringLiteral("Me, of course! And yes, @Lea"), whos_in);
	Settle({ &mara });
	mara.mute(lea.Fingerprint(), -1);

	// ---- the panel, offscreen
	ShotBackend backend;
	backend.SetFriends(&mara);
	qmlRegisterSingletonInstance("org.streetpea.chiaking", 1, 0, "Chiaki", &backend);
	QQmlEngine engine;
	LocalCoversFactory covers;
	covers.file = MakeCover(dir.filePath(QStringLiteral("cover.png")));
	engine.setNetworkAccessManagerFactory(&covers);
	QQmlComponent component(&engine);
	component.setData(kStage, QUrl(QStringLiteral("qrc:/friends-stage.qml")));
	QObject *object = component.create();
	if(!object)
	{
		for(const QQmlError &e : component.errors())
			std::fprintf(stderr, "%s\n", qPrintable(e.toString()));
		return 1;
	}
	auto *stage = qobject_cast<QQuickItem *>(object);
	Offscreen screen;
	const int width = static_cast<int>(stage->width()), height = static_cast<int>(stage->height());
	if(!screen.Open(stage, width, height, 2.0))
	{
		std::fprintf(stderr, "the scene graph could not be set up\n");
		return 1;
	}
	QObject *panel = object->property("panel").value<QObject *>();

	auto shot = [&](const QString &name) {
		// A few frames: pictures load and the panel fades in meanwhile
		QImage image;
		for(int i = 0; i < 8; i++)
		{
			Spin(80);
			image = screen.Grab();
		}
		if(qEnvironmentVariableIsSet("FRIENDS_SHOT_DEBUG"))
			std::fprintf(stderr, "panel visible %d opened %d opacity %f parent %p\n", panel->property("visible").toBool(),
				panel->property("opened").toBool(), panel->property("opacity").toReal(), panel->property("parent").value<QObject *>());
		const qreal scale = static_cast<qreal>(image.width()) / width;
		const int w = static_cast<int>(panel->property("width").toReal() * scale);
		image.copy(image.width() - w, 0, w, image.height()).save(out.filePath(name));
		std::printf("%s\n", qPrintable(out.filePath(name)));
	};

	QMetaObject::invokeMethod(panel, "show", Q_ARG(QVariant, QString()));
	shot(QStringLiteral("friends-lists.png"));
	panel->setProperty("tab", 1);
	shot(QStringLiteral("friends-chats.png"));
	panel->setProperty("chat", jonas.Fingerprint());
	shot(QStringLiteral("friends-chat.png"));
	panel->setProperty("chat", lea.Fingerprint());
	shot(QStringLiteral("friends-chat-talk.png"));
	panel->setProperty("chat", gid);
	shot(QStringLiteral("friends-group.png"));
	// Answering, with "@" being typed
	if(QObject *composer = panel->findChild<QObject *>(QStringLiteral("composer")))
	{
		panel->setProperty("replyTo", QVariantMap{ { QStringLiteral("id"), whos_in }, { QStringLiteral("name"), QStringLiteral("Jonas") },
			{ QStringLiteral("text"), QStringLiteral("Who's in tonight? 🏁") } });
		QMetaObject::invokeMethod(composer, "insert", Q_ARG(QVariant, QStringLiteral("@")));
		shot(QStringLiteral("friends-group-writing.png"));
		QMetaObject::invokeMethod(composer, "clear");
		panel->setProperty("replyTo", QVariant());
	}
	panel->setProperty("sheet", QStringLiteral("members"));
	shot(QStringLiteral("friends-group-members.png"));
	panel->setProperty("sheet", QStringLiteral("plan"));
	shot(QStringLiteral("friends-plan-new.png"));
	panel->setProperty("sheet", QString());
	panel->setProperty("chat", QString());
	panel->setProperty("tab", 0);
	// The quick reactions
	{
		panel->setProperty("visible", false);
		object->setProperty("bubbleDemo", true);
		QImage image;
		for(int i = 0; i < 6; i++)
		{
			Spin(80);
			image = screen.Grab();
		}
		const qreal scale = static_cast<qreal>(image.width()) / width;
		image.copy(0, 0, static_cast<int>(460 * scale), static_cast<int>(260 * scale)).save(out.filePath(QStringLiteral("reactions-bar.png")));
		object->setProperty("bubbleDemo", false);
		panel->setProperty("visible", true);
	}

	// A session's chat with the same messages, reactions and read marks
	{
		ShotSession room;
		panel->setProperty("visible", false);
		object->setProperty("roomSession", QVariant::fromValue(static_cast<QObject *>(&room)));
		QImage image;
		for(int i = 0; i < 8; i++)
		{
			Spin(80);
			image = screen.Grab();
		}
		const qreal scale = static_cast<qreal>(image.width()) / width;
		image.copy(0, 0, static_cast<int>(420 * scale), image.height()).save(out.filePath(QStringLiteral("room-chat.png")));
		// Answering Jonas, with "@" being typed
		QObject *room_chat = object->findChild<QObject *>(QStringLiteral("roomChat"));
		QObject *composer = room_chat ? room_chat->findChild<QObject *>(QStringLiteral("composer")) : nullptr;
		if(composer)
		{
			room_chat->setProperty("replyTo", QVariantMap{ { QStringLiteral("seq"), 6 }, { QStringLiteral("name"), QStringLiteral("Jonas") },
				{ QStringLiteral("text"), QStringLiteral("@Mara can you bring the wheel?") } });
			QMetaObject::invokeMethod(composer, "insert", Q_ARG(QVariant, QStringLiteral("@")));
			for(int i = 0; i < 6; i++)
			{
				Spin(80);
				image = screen.Grab();
			}
			image.copy(0, 0, static_cast<int>(420 * scale), image.height()).save(out.filePath(QStringLiteral("room-chat-writing.png")));
			QMetaObject::invokeMethod(composer, "clear");
			room_chat->setProperty("replyTo", QVariant());
		}
		object->setProperty("roomSession", QVariant());
		panel->setProperty("visible", true);
	}

	// The settings page, with this device's link to another device open
	panel->setProperty("visible", false);
	object->setProperty("settings", true);
	{
		QImage image;
		for(int i = 0; i < 8; i++)
		{
			Spin(80);
			image = screen.Grab();
		}
		const qreal scale = static_cast<qreal>(image.width()) / width;
		image.copy(0, 0, static_cast<int>(760 * scale), image.height()).save(out.filePath(QStringLiteral("friends-settings.png")));
	}
	object->setProperty("settings", false);
	panel->setProperty("visible", true);
	backend.SetFriends(&off);
	shot(QStringLiteral("friends-off.png"));
	delete object;
	return 0;
}

#include "friends-shot.moc"
