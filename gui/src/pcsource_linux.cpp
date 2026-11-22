// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// This computer as the game source on Linux (docs/pc-source.md):
//  - the system's own screen sharing (xdg-desktop-portal's ScreenCast) asks which window or
//    screen, on Wayland and X11 alike, and gives a PipeWire stream of it;
//  - the pictures come through PipeWire in memory, scaled into the picture size keeping
//    their shape (pcscale.cpp);
//  - the sound is the game's own (its PipeWire streams, linked to ours), else every
//    program's but this app's and voice chats';
//  - the game is found among the processes the way the GamerNova client finds it, with
//    Steam's own name (SteamAppId) where Steam started it, Proton games included.
// libpipewire is opened when needed, so the app also starts where there is none.

#include "pcsource_p.h"
#include "pcscale.h"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QPointer>
#include <QSet>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>

#include <pipewire/pipewire.h>
#include <spa/buffer/meta.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/video/format-utils.h>
#include <spa/pod/builder.h>
#include <spa/utils/dict.h>

extern "C" {
#include <libavutil/imgutils.h>
}

// ------------------------------------------------------------------ libpipewire, opened when needed

namespace {

struct PwApi
{
	bool ok = false;
	std::string error;
	decltype(&::pw_init) init = nullptr;
	decltype(&::pw_thread_loop_new) thread_loop_new = nullptr;
	decltype(&::pw_thread_loop_destroy) thread_loop_destroy = nullptr;
	decltype(&::pw_thread_loop_start) thread_loop_start = nullptr;
	decltype(&::pw_thread_loop_stop) thread_loop_stop = nullptr;
	decltype(&::pw_thread_loop_lock) thread_loop_lock = nullptr;
	decltype(&::pw_thread_loop_unlock) thread_loop_unlock = nullptr;
	decltype(&::pw_thread_loop_get_loop) thread_loop_get_loop = nullptr;
	decltype(&::pw_context_new) context_new = nullptr;
	decltype(&::pw_context_destroy) context_destroy = nullptr;
	decltype(&::pw_context_connect) context_connect = nullptr;
	decltype(&::pw_context_connect_fd) context_connect_fd = nullptr;
	decltype(&::pw_core_disconnect) core_disconnect = nullptr;
	decltype(&::pw_properties_new) properties_new = nullptr;
	decltype(&::pw_stream_new) stream_new = nullptr;
	decltype(&::pw_stream_destroy) stream_destroy = nullptr;
	decltype(&::pw_stream_add_listener) stream_add_listener = nullptr;
	decltype(&::pw_stream_connect) stream_connect = nullptr;
	decltype(&::pw_stream_disconnect) stream_disconnect = nullptr;
	decltype(&::pw_stream_update_params) stream_update_params = nullptr;
	decltype(&::pw_stream_dequeue_buffer) stream_dequeue_buffer = nullptr;
	decltype(&::pw_stream_queue_buffer) stream_queue_buffer = nullptr;
	decltype(&::pw_stream_get_node_id) stream_get_node_id = nullptr;
	decltype(&::pw_proxy_destroy) proxy_destroy = nullptr;
};

const PwApi &Pw()
{
	static PwApi api;
	static std::once_flag once;
	std::call_once(once, [] {
		void *lib = dlopen("libpipewire-0.3.so.0", RTLD_NOW | RTLD_LOCAL);
		if(!lib)
		{
			api.error = "PipeWire is not installed (libpipewire-0.3.so.0)";
			return;
		}
		bool all = true;
		auto get = [&](auto &fn, const char *name) {
			fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(lib, name));
			if(!fn)
			{
				all = false;
				api.error = std::string("PipeWire has no ") + name;
			}
		};
		get(api.init, "pw_init");
		get(api.thread_loop_new, "pw_thread_loop_new");
		get(api.thread_loop_destroy, "pw_thread_loop_destroy");
		get(api.thread_loop_start, "pw_thread_loop_start");
		get(api.thread_loop_stop, "pw_thread_loop_stop");
		get(api.thread_loop_lock, "pw_thread_loop_lock");
		get(api.thread_loop_unlock, "pw_thread_loop_unlock");
		get(api.thread_loop_get_loop, "pw_thread_loop_get_loop");
		get(api.context_new, "pw_context_new");
		get(api.context_destroy, "pw_context_destroy");
		get(api.context_connect, "pw_context_connect");
		get(api.context_connect_fd, "pw_context_connect_fd");
		get(api.core_disconnect, "pw_core_disconnect");
		get(api.properties_new, "pw_properties_new");
		get(api.stream_new, "pw_stream_new");
		get(api.stream_destroy, "pw_stream_destroy");
		get(api.stream_add_listener, "pw_stream_add_listener");
		get(api.stream_connect, "pw_stream_connect");
		get(api.stream_disconnect, "pw_stream_disconnect");
		get(api.stream_update_params, "pw_stream_update_params");
		get(api.stream_dequeue_buffer, "pw_stream_dequeue_buffer");
		get(api.stream_queue_buffer, "pw_stream_queue_buffer");
		get(api.stream_get_node_id, "pw_stream_get_node_id");
		get(api.proxy_destroy, "pw_proxy_destroy");
		if(!all)
			return;
		api.init(nullptr, nullptr);
		api.ok = true;
	});
	return api;
}

/** A PipeWire connection with its own thread: to the portal's stream (fd) or to the system's PipeWire. */
class PwConnection
{
	public:
		~PwConnection() { Close(); }

		bool Open(const char *name, int fd, std::string *error)
		{
			const PwApi &pw = Pw();
			if(!pw.ok)
			{
				*error = pw.error;
				if(fd >= 0)
					close(fd);
				return false;
			}
			loop = pw.thread_loop_new(name, nullptr);
			context = loop ? pw.context_new(pw.thread_loop_get_loop(loop), nullptr, 0) : nullptr;
			if(!context || pw.thread_loop_start(loop) < 0)
			{
				*error = "PipeWire could not be started";
				if(fd >= 0)
					close(fd);
				return false;
			}
			Lock();
			// pw_context_connect_fd() owns the fd from here on, failing or not
			core = fd >= 0 ? pw.context_connect_fd(context, fd, nullptr, 0) : pw.context_connect(context, nullptr, 0);
			Unlock();
			if(!core)
			{
				*error = fd >= 0 ? "the screen sharing's PipeWire connection failed" : "PipeWire does not answer";
				return false;
			}
			return true;
		}

		void Close()
		{
			const PwApi &pw = Pw();
			if(core)
			{
				Lock();
				pw.core_disconnect(core);
				core = nullptr;
				Unlock();
			}
			if(loop)
				pw.thread_loop_stop(loop);
			if(context)
			{
				pw.context_destroy(context);
				context = nullptr;
			}
			if(loop)
			{
				pw.thread_loop_destroy(loop);
				loop = nullptr;
			}
		}

		void Lock() { Pw().thread_loop_lock(loop); }
		void Unlock() { Pw().thread_loop_unlock(loop); }

		pw_thread_loop *loop = nullptr;
		pw_context *context = nullptr;
		pw_core *core = nullptr;
};

} // namespace

// ------------------------------------------------------------------ the system's screen sharing (portal)

static const char kPortalService[] = "org.freedesktop.portal.Desktop";
static const char kPortalPath[] = "/org/freedesktop/portal/desktop";
static const char kScreenCast[] = "org.freedesktop.portal.ScreenCast";

/** A property of the ScreenCast portal, or an invalid QVariant if there is no such portal. */
static QVariant PortalProperty(const char *name, int timeout_ms = 3000)
{
	QDBusMessage get = QDBusMessage::createMethodCall(QLatin1String(kPortalService), QLatin1String(kPortalPath),
		QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
	get << QLatin1String(kScreenCast) << QLatin1String(name);
	const QDBusMessage reply = QDBusConnection::sessionBus().call(get, QDBus::Block, timeout_ms);
	if(reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
		return QVariant();
	return reply.arguments().first().value<QDBusVariant>().variant();
}

/** One ScreenCast session: CreateSession, SelectSources, Start, OpenPipeWireRemote. */
class PsrptPortalSession : public QObject
{
	Q_OBJECT

	public:
		std::function<void(const PcPortal::Stream &, const QString &)> done;
		QString token;
		QString session_path;

		~PsrptPortalSession() override
		{
			if(!session_path.isEmpty())
			{
				// Ends the sharing; nobody waits for the answer
				QDBusMessage close = QDBusMessage::createMethodCall(QLatin1String(kPortalService), session_path,
					QStringLiteral("org.freedesktop.portal.Session"), QStringLiteral("Close"));
				QDBusConnection::sessionBus().asyncCall(close);
			}
		}

		void Begin()
		{
			QDBusConnection bus = QDBusConnection::sessionBus();
			if(!bus.isConnected())
			{
				Finish(PcPortal::Stream(), tr("There is no session bus to ask the system for its screen sharing."));
				return;
			}
			const QVariant types = PortalProperty("AvailableSourceTypes");
			if(!types.isValid())
			{
				Finish(PcPortal::Stream(), tr("This system has no screen sharing (xdg-desktop-portal with a ScreenCast portal for your desktop)."));
				return;
			}
			source_types = types.toUInt() & 3u;
			if(!source_types)
				source_types = 1;
			cursor_modes = PortalProperty("AvailableCursorModes").toUInt();
			version = PortalProperty("version").toUInt();
			// The answers come to request objects named after this connection and a token
			sender = bus.baseService().mid(1).replace(QLatin1Char('.'), QLatin1Char('_'));
			QVariantMap options;
			options[QStringLiteral("session_handle_token")] = QStringLiteral("psrpt_session_%1").arg(Next());
			Call(QStringLiteral("CreateSession"), {}, options);
		}

	private slots:
		void OnResponse(uint response, const QVariantMap &results)
		{
			QDBusConnection::sessionBus().disconnect(QLatin1String(kPortalService), request_path,
				QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
				this, SLOT(OnResponse(uint, QVariantMap)));
			if(response == 1)
			{
				// Cancelled: no words needed
				Finish(PcPortal::Stream(), QString());
				return;
			}
			if(response != 0)
			{
				Finish(PcPortal::Stream(), tr("The system's screen sharing said no."));
				return;
			}
			if(step == 1)
			{
				session_path = results.value(QStringLiteral("session_handle")).toString();
				if(session_path.isEmpty())
				{
					Finish(PcPortal::Stream(), tr("The system's screen sharing gave no session."));
					return;
				}
				QVariantMap options;
				options[QStringLiteral("types")] = source_types;
				options[QStringLiteral("multiple")] = false;
				// The mouse pointer in the picture, where the system can
				if(cursor_modes & 2u)
					options[QStringLiteral("cursor_mode")] = 2u;
				if(version >= 4)
				{
					// Shared again next time without asking, until the person says otherwise
					options[QStringLiteral("persist_mode")] = 2u;
					if(!token.isEmpty())
						options[QStringLiteral("restore_token")] = token;
				}
				Call(QStringLiteral("SelectSources"), { QVariant::fromValue(QDBusObjectPath(session_path)) }, options);
				return;
			}
			if(step == 2)
			{
				Call(QStringLiteral("Start"), { QVariant::fromValue(QDBusObjectPath(session_path)), QString() }, QVariantMap());
				return;
			}
			PcPortal::Stream stream;
			stream.token = results.value(QStringLiteral("restore_token")).toString();
			const QDBusArgument streams = results.value(QStringLiteral("streams")).value<QDBusArgument>();
			bool found = false;
			streams.beginArray();
			while(!streams.atEnd())
			{
				uint node = 0;
				QVariantMap props;
				streams.beginStructure();
				streams >> node >> props;
				streams.endStructure();
				if(found)
					continue;
				found = true;
				stream.node = node;
				stream.window = props.value(QStringLiteral("source_type")).toUInt() == 2;
				const QDBusArgument size = props.value(QStringLiteral("size")).value<QDBusArgument>();
				if(size.currentType() == QDBusArgument::StructureType)
				{
					int w = 0, h = 0;
					size.beginStructure();
					size >> w >> h;
					size.endStructure();
					stream.width = w;
					stream.height = h;
				}
			}
			streams.endArray();
			if(!found)
			{
				Finish(PcPortal::Stream(), tr("Nothing was chosen to share."));
				return;
			}
			QDBusMessage open = QDBusMessage::createMethodCall(QLatin1String(kPortalService), QLatin1String(kPortalPath),
				QLatin1String(kScreenCast), QStringLiteral("OpenPipeWireRemote"));
			open << QVariant::fromValue(QDBusObjectPath(session_path)) << QVariantMap();
			const QDBusMessage reply = QDBusConnection::sessionBus().call(open, QDBus::Block, 5000);
			const QDBusUnixFileDescriptor fd = reply.arguments().isEmpty()
				? QDBusUnixFileDescriptor() : reply.arguments().first().value<QDBusUnixFileDescriptor>();
			if(reply.type() != QDBusMessage::ReplyMessage || !fd.isValid())
			{
				Finish(PcPortal::Stream(), tr("The system's screen sharing gave no picture: %1").arg(reply.errorMessage()));
				return;
			}
			// Our own copy: the QDBusUnixFileDescriptor closes its own
			stream.fd = fcntl(fd.fileDescriptor(), F_DUPFD_CLOEXEC, 3);
			Finish(stream, QString());
		}

	private:
		int Next() { return ++counter; }

		void Call(const QString &method, QVariantList args, QVariantMap options)
		{
			step++;
			const QString handle = QStringLiteral("psrpt_request_%1").arg(Next());
			options[QStringLiteral("handle_token")] = handle;
			request_path = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, handle);
			QDBusConnection bus = QDBusConnection::sessionBus();
			// Listening before asking: the answer may come at once
			bus.connect(QLatin1String(kPortalService), request_path, QStringLiteral("org.freedesktop.portal.Request"),
				QStringLiteral("Response"), this, SLOT(OnResponse(uint, QVariantMap)));
			QDBusMessage call = QDBusMessage::createMethodCall(QLatin1String(kPortalService), QLatin1String(kPortalPath),
				QLatin1String(kScreenCast), method);
			args.append(options);
			call.setArguments(args);
			auto watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
			connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, method](QDBusPendingCallWatcher *w) {
				w->deleteLater();
				if(w->isError())
					Finish(PcPortal::Stream(), tr("The system's screen sharing failed (%1: %2).").arg(method, w->error().message()));
			});
		}

		void Finish(const PcPortal::Stream &stream, const QString &error)
		{
			if(finished)
			{
				if(stream.fd >= 0)
					close(stream.fd);
				return;
			}
			finished = true;
			if(done)
			{
				// Called once and let go: what it holds (the PcPortal itself) must not keep this alive
				auto callback = std::move(done);
				done = nullptr;
				callback(stream, error);
			}
			else if(stream.fd >= 0)
				close(stream.fd);
		}

		QString sender;
		QString request_path;
		uint source_types = 3;
		uint cursor_modes = 0;
		uint version = 0;
		int step = 0;
		int counter = 0;
		bool finished = false;
};

struct PcPortal::Private
{
	QPointer<PsrptPortalSession> session;
};

PcPortal::PcPortal() : d(std::make_unique<Private>())
{
}

PcPortal::~PcPortal()
{
	delete d->session.data();
}

void PcPortal::Open(const QString &token, std::function<void(const Stream &stream, const QString &error)> done)
{
	delete d->session.data();
	auto session = new PsrptPortalSession;
	d->session = session;
	session->token = token;
	session->done = [done](const Stream &stream, const QString &error) {
		// Never from inside the D-Bus answer: whoever waits may delete this
		QMetaObject::invokeMethod(qApp, [done, stream, error]() { done(stream, error); }, Qt::QueuedConnection);
	};
	session->Begin();
}

// ------------------------------------------------------------------ what can be shared

QString PcLinux::Unsupported()
{
	const PwApi &pw = Pw();
	if(!pw.ok)
		return QCoreApplication::translate("PcSource", "Sharing a game on this computer needs PipeWire, which this system does not have.");
	// Asked once: the answer does not change while the app runs
	static int portal = -1;
	if(portal < 0)
		portal = PortalProperty("AvailableSourceTypes").isValid() ? 1 : 0;
	if(!portal)
		return QCoreApplication::translate("PcSource", "Sharing a game on this computer needs the system's screen sharing (xdg-desktop-portal with the portal of your desktop, e.g. xdg-desktop-portal-gnome, -kde or -wlr).");
	return QString();
}

QList<PcSource::Target> PcLinux::Targets()
{
	// The system shows its own list when sharing starts
	PcSource::Target t;
	t.kind = QStringLiteral("portal");
	t.handle = 0;
	t.title = QCoreApplication::translate("PcSource", "A window or a screen");
	return { t };
}

// ------------------------------------------------------------------ which game

namespace {

struct Process
{
	int pid = 0;
	int ppid = 0;
	quint64 started = 0;
	/** The program: for Wine's programs the Windows program it runs. */
	QString program;
	/** The program's file name, lower case, without .exe. */
	QString binary;
	QString steam_app;
	QString steam_root;
	bool game = false;
};

QByteArray ReadSmall(const QString &path, qint64 max = 64 * 1024)
{
	QFile f(path);
	if(!f.open(QIODevice::ReadOnly | QIODevice::Unbuffered))
		return QByteArray();
	return f.read(max);
}

/** A Wine program's Windows path as a path here where it can be told ("Z:\\home\\..." is "/home/..."). */
QString FromWine(QString path, const QString &prefix)
{
	path.replace(QLatin1Char('\\'), QLatin1Char('/'));
	if(path.size() > 2 && path.at(1) == QLatin1Char(':'))
	{
		const QChar drive = path.at(0).toLower();
		if(drive == QLatin1Char('z'))
			return path.mid(2);
		if(drive == QLatin1Char('c') && !prefix.isEmpty())
			return prefix + QStringLiteral("/drive_c") + path.mid(2);
	}
	return path;
}

std::vector<Process> Processes()
{
	std::vector<Process> list;
	const uint me = getuid();
	const QStringList entries = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
	for(const QString &entry : entries)
	{
		bool number = false;
		const int pid = entry.toInt(&number);
		if(!number || pid == getpid())
			continue;
		const QString dir = QStringLiteral("/proc/") + entry;
		if(QFileInfo(dir).ownerId() != me)
			continue;
		Process p;
		p.pid = pid;
		// stat: "pid (name) state ppid ... starttime" (22nd field); the name may hold spaces and brackets
		const QByteArray stat = ReadSmall(dir + QStringLiteral("/stat"), 4096);
		const int end = stat.lastIndexOf(')');
		if(end < 0)
			continue;
		const QList<QByteArray> fields = stat.mid(end + 2).split(' ');
		if(fields.size() > 19)
		{
			p.ppid = fields.at(1).toInt();
			p.started = fields.at(19).toULongLong();
		}
		const QByteArray cmdline = ReadSmall(dir + QStringLiteral("/cmdline"), 8192);
		const QString first = QString::fromUtf8(cmdline.left(cmdline.indexOf('\0')));
		const QString exe = QFileInfo(dir + QStringLiteral("/exe")).symLinkTarget();
		QString prefix;
		const QList<QByteArray> environ = ReadSmall(dir + QStringLiteral("/environ"), 256 * 1024).split('\0');
		for(const QByteArray &var : environ)
		{
			if(var.startsWith("SteamAppId="))
				p.steam_app = QString::fromUtf8(var.mid(11));
			else if(var.startsWith("SteamGameId=") && p.steam_app.isEmpty())
				p.steam_app = QString::fromUtf8(var.mid(12));
			else if(var.startsWith("STEAM_COMPAT_CLIENT_INSTALL_PATH="))
				p.steam_root = QString::fromUtf8(var.mid(33));
			else if(var.startsWith("WINEPREFIX="))
				prefix = QString::fromUtf8(var.mid(11));
		}
		if(p.steam_app == QLatin1String("0"))
			p.steam_app.clear();
		// Wine's programs (Proton too) run as wine-preloader with the Windows program first
		const QString exe_name = QFileInfo(exe).fileName().toLower();
		if(exe_name.startsWith(QLatin1String("wine")) && first.contains(QLatin1Char('\\')))
			p.program = FromWine(first, prefix);
		else if(first.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive))
			p.program = FromWine(first, prefix);
		else
			p.program = exe.isEmpty() ? first : exe;
		p.binary = p.program.section(QLatin1Char('/'), -1).toLower();
		if(p.binary.endsWith(QLatin1String(".exe")))
			p.binary.chop(4);
		p.game = PcSource::LooksLikeGame(p.program);
		list.push_back(p);
	}
	return list;
}

QString SteamRoot(const Process &p)
{
	if(!p.steam_root.isEmpty())
		return p.steam_root;
	for(const QString &candidate : { QDir::homePath() + QStringLiteral("/.local/share/Steam"),
			QDir::homePath() + QStringLiteral("/.steam/steam"),
			QDir::homePath() + QStringLiteral("/.var/app/com.valvesoftware.Steam/.local/share/Steam") })
	{
		if(QFileInfo::exists(candidate + QStringLiteral("/steamapps")))
			return candidate;
	}
	return QString();
}

/** The game started last, and every process that is part of it (for its sound). */
struct FoundGame
{
	PcSource::Game game;
	QSet<int> pids;
	QString binary;
};

FoundGame FindGame()
{
	FoundGame found;
	const std::vector<Process> all = Processes();
	const Process *newest = nullptr;
	for(const Process &p : all)
	{
		if(p.game && (!newest || p.started >= newest->started))
			newest = &p;
	}
	if(!newest)
		return found;
	found.game.path = newest->program;
	found.binary = newest->binary;
	if(!newest->steam_app.isEmpty())
		found.game.title = PcSource::SteamNameById(SteamRoot(*newest), newest->steam_app);
	if(found.game.title.isEmpty())
	{
		const QString steam = PcSource::SteamName(newest->program);
		found.game.title = !steam.isEmpty() ? steam : QFileInfo(newest->program).completeBaseName();
	}
	// Steam marks a game's processes; otherwise the game and what it started
	found.pids.insert(newest->pid);
	if(!newest->steam_app.isEmpty())
	{
		for(const Process &p : all)
		{
			if(p.steam_app == newest->steam_app)
				found.pids.insert(p.pid);
		}
	}
	for(bool more = true; more;)
	{
		more = false;
		for(const Process &p : all)
		{
			if(!found.pids.contains(p.pid) && found.pids.contains(p.ppid))
			{
				found.pids.insert(p.pid);
				more = true;
			}
		}
	}
	return found;
}

} // namespace

PcSource::Game PcLinux::DetectGame(const PcSource::Target &target)
{
	(void)target;
	// Which window the system shared is not told: the game started last, as on a screen
	return FindGame().game;
}

// ------------------------------------------------------------------ the picture

namespace {

/** Where red, green and blue sit in a pixel of a PipeWire format; false for formats not asked for. */
bool FromSpa(uint32_t format, PcScaler::Layout *layout)
{
	switch(format)
	{
		case SPA_VIDEO_FORMAT_BGRx:
		case SPA_VIDEO_FORMAT_BGRA: *layout = { 2, 1, 0 }; return true;
		case SPA_VIDEO_FORMAT_RGBx:
		case SPA_VIDEO_FORMAT_RGBA: *layout = { 0, 1, 2 }; return true;
		case SPA_VIDEO_FORMAT_xRGB: *layout = { 1, 2, 3 }; return true;
		case SPA_VIDEO_FORMAT_xBGR: *layout = { 3, 2, 1 }; return true;
		default: return false;
	}
}

class PwVideo : public PcFrameSource
{
	public:
		~PwVideo() override
		{
			const PwApi &pw = Pw();
			if(stream)
			{
				connection.Lock();
				pw.stream_disconnect(stream);
				pw.stream_destroy(stream);
				stream = nullptr;
				connection.Unlock();
			}
			connection.Close();
			if(latest)
				av_frame_free(&latest);
		}

		bool Open(const PcCapture::Config &config, AVPixelFormat out, std::string *error)
		{
			width = config.width;
			height = config.height;
			fps = config.fps;
			format_out = out;
			if(out != AV_PIX_FMT_NV12 && out != AV_PIX_FMT_YUV420P)
			{
				*error = "the picture is made NV12 or YUV420P";
				return false;
			}
			if(config.portal_fd < 0)
			{
				*error = "the system's screen sharing gave no stream";
				return false;
			}
			// A copy each time: the encoders are tried one after the other
			const int fd = fcntl(config.portal_fd, F_DUPFD_CLOEXEC, 3);
			if(!connection.Open("psrpt-picture", fd, error))
				return false;
			const PwApi &pw = Pw();
			connection.Lock();
			{
				spa_zero(core_events);
				core_events.version = PW_VERSION_CORE_EVENTS;
				core_events.error = &PwVideo::OnCoreError;
				pw_core_add_listener(connection.core, &core_listener, &core_events, this);
			}
			stream = pw.stream_new(connection.core, "PSRemotePlayTogether",
				pw.properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE, "Screen", nullptr));
			if(!stream)
			{
				connection.Unlock();
				*error = "PipeWire gave no stream";
				return false;
			}
			spa_zero(stream_events);
			stream_events.version = PW_VERSION_STREAM_EVENTS;
			stream_events.state_changed = &PwVideo::OnState;
			stream_events.param_changed = &PwVideo::OnParam;
			stream_events.process = &PwVideo::OnProcess;
			pw.stream_add_listener(stream, &stream_listener, &stream_events, this);

			// Pictures in memory, in any of the usual RGB layouts, as large as they are
			uint8_t buffer[1024];
			spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
			spa_rectangle size_def = { 1920, 1080 }, size_min = { 1, 1 }, size_max = { 16384, 16384 };
			spa_fraction rate_zero = { 0, 1 }, rate_def = { static_cast<uint32_t>(fps), 1 }, rate_min = { 1, 1 },
				rate_max = { static_cast<uint32_t>(fps), 1 };
			spa_pod_frame f;
			spa_pod_builder_push_object(&b, &f, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
			spa_pod_builder_add(&b, SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video), 0);
			spa_pod_builder_add(&b, SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), 0);
			spa_pod_builder_add(&b, SPA_FORMAT_VIDEO_format, SPA_POD_CHOICE_ENUM_Id(7, SPA_VIDEO_FORMAT_BGRx,
				SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRA, SPA_VIDEO_FORMAT_RGBx, SPA_VIDEO_FORMAT_RGBA,
				SPA_VIDEO_FORMAT_xRGB, SPA_VIDEO_FORMAT_xBGR), 0);
			spa_pod_builder_add(&b, SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(&size_def, &size_min, &size_max), 0);
			spa_pod_builder_add(&b, SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction(&rate_zero), 0);
			spa_pod_builder_add(&b, SPA_FORMAT_VIDEO_maxFramerate, SPA_POD_CHOICE_RANGE_Fraction(&rate_def, &rate_min, &rate_max), 0);
			const spa_pod *params[1] = { static_cast<const spa_pod *>(spa_pod_builder_pop(&b, &f)) };
			const int err = pw.stream_connect(stream, PW_DIRECTION_INPUT, config.portal_node,
				static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS), params, 1);
			connection.Unlock();
			if(err < 0)
			{
				*error = std::string("the screen sharing's stream could not be connected: ") + strerror(-err);
				return false;
			}
			return true;
		}

		AVFrame *Next(bool *ended_out, std::string *error) override
		{
			std::unique_lock<std::mutex> lock(mutex);
			changed.wait_for(lock, std::chrono::seconds(1), [this] { return latest || ended; });
			if(latest)
			{
				AVFrame *f = latest;
				latest = nullptr;
				return f;
			}
			if(ended)
			{
				*ended_out = true;
				*error = why;
			}
			return nullptr;
		}

	private:
		static void OnCoreError(void *data, uint32_t id, int seq, int res, const char *message)
		{
			(void)seq;
			(void)res;
			auto self = static_cast<PwVideo *>(data);
			if(id == PW_ID_CORE)
				self->End(std::string("the system stopped the screen sharing") + (message ? std::string(" (") + message + ")" : ""));
		}

		static void OnState(void *data, pw_stream_state old, pw_stream_state state, const char *error)
		{
			auto self = static_cast<PwVideo *>(data);
			if(state == PW_STREAM_STATE_ERROR)
				self->End(std::string("the screen sharing failed") + (error ? std::string(": ") + error : ""));
			else if(state == PW_STREAM_STATE_UNCONNECTED && old != PW_STREAM_STATE_UNCONNECTED && old != PW_STREAM_STATE_CONNECTING)
				self->End("the system stopped the screen sharing");
		}

		static void OnParam(void *data, uint32_t id, const spa_pod *param)
		{
			auto self = static_cast<PwVideo *>(data);
			if(id != SPA_PARAM_Format || !param)
				return;
			uint32_t media_type = 0, media_subtype = 0;
			if(spa_format_parse(param, &media_type, &media_subtype) < 0 || media_type != SPA_MEDIA_TYPE_video
				|| media_subtype != SPA_MEDIA_SUBTYPE_raw)
				return;
			spa_video_info_raw info;
			spa_zero(info);
			if(spa_format_video_raw_parse(param, &info) < 0)
				return;
			self->format_known = FromSpa(info.format, &self->layout);
			self->in_width = static_cast<int>(info.size.width);
			self->in_height = static_cast<int>(info.size.height);
			// Buffers in memory, and where a window's picture lies in them
			uint8_t buffer[512];
			spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
			const spa_pod *params[2];
			params[0] = static_cast<const spa_pod *>(spa_pod_builder_add_object(&b,
				SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
				SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int(8, 2, 16),
				SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int((1 << SPA_DATA_MemPtr) | (1 << SPA_DATA_MemFd))));
			params[1] = static_cast<const spa_pod *>(spa_pod_builder_add_object(&b,
				SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
				SPA_PARAM_META_type, SPA_POD_Id(SPA_META_VideoCrop),
				SPA_PARAM_META_size, SPA_POD_Int(sizeof(spa_meta_region))));
			Pw().stream_update_params(self->stream, params, 2);
		}

		static void OnProcess(void *data)
		{
			auto self = static_cast<PwVideo *>(data);
			const PwApi &pw = Pw();
			// Only the newest picture counts
			pw_buffer *newest = nullptr;
			while(pw_buffer *b = pw.stream_dequeue_buffer(self->stream))
			{
				if(newest)
					pw.stream_queue_buffer(self->stream, newest);
				newest = b;
			}
			if(!newest)
				return;
			self->Take(newest->buffer);
			pw.stream_queue_buffer(self->stream, newest);
		}

		void Take(spa_buffer *buf)
		{
			if(!buf || buf->n_datas < 1 || !format_known)
				return;
			const spa_data &d = buf->datas[0];
			if(!d.data || !d.chunk || d.chunk->size == 0 || (d.chunk->flags & SPA_CHUNK_FLAG_CORRUPTED))
				return;
			// At most the frame rate the guests get
			const auto now = std::chrono::steady_clock::now();
			if(have_last && now - last_frame < std::chrono::microseconds(800000 / std::max(1, fps)))
				return;
			int x = 0, y = 0, w = in_width, h = in_height;
			if(auto crop = static_cast<spa_meta_region *>(spa_buffer_find_meta_data(buf, SPA_META_VideoCrop, sizeof(spa_meta_region))))
			{
				if(crop->region.size.width > 0 && crop->region.size.height > 0)
				{
					x = std::clamp(crop->region.position.x, 0, std::max(0, in_width - 1));
					y = std::clamp(crop->region.position.y, 0, std::max(0, in_height - 1));
					w = std::min(static_cast<int>(crop->region.size.width), in_width - x);
					h = std::min(static_cast<int>(crop->region.size.height), in_height - y);
				}
			}
			if(w < 2 || h < 2)
				return;
			const int stride = d.chunk->stride > 0 ? d.chunk->stride : in_width * 4;
			const uint8_t *pixels = static_cast<const uint8_t *>(d.data) + d.chunk->offset;
			const size_t needed = d.chunk->offset + static_cast<size_t>(y + h - 1) * stride + static_cast<size_t>(x + w) * 4;
			if(needed > d.maxsize)
				return;
			AVFrame *f = Convert(pixels + static_cast<size_t>(y) * stride + static_cast<size_t>(x) * 4, stride, w, h);
			if(!f)
				return;
			have_last = true;
			last_frame = now;
			{
				std::lock_guard<std::mutex> lock(mutex);
				if(latest)
					av_frame_free(&latest);
				latest = f;
			}
			changed.notify_one();
		}

		/** Into the picture size, keeping the shape: black bars where it does not fill it. */
		AVFrame *Convert(const uint8_t *pixels, int stride, int w, int h)
		{
			const double fit = std::min(static_cast<double>(width) / w, static_cast<double>(height) / h);
			const int fw = std::clamp(static_cast<int>(w * fit) & ~1, 2, width);
			const int fh = std::clamp(static_cast<int>(h * fit) & ~1, 2, height);
			const int ox = ((width - fw) / 2) & ~1;
			const int oy = ((height - fh) / 2) & ~1;
			AVFrame *f = av_frame_alloc();
			f->format = format_out;
			f->width = width;
			f->height = height;
			if(av_frame_get_buffer(f, 0) < 0)
			{
				av_frame_free(&f);
				return nullptr;
			}
			const bool planar = format_out == AV_PIX_FMT_YUV420P;
			if(fw != width || fh != height)
			{
				for(int row = 0; row < height; row++)
					memset(f->data[0] + row * f->linesize[0], 16, static_cast<size_t>(width));
				for(int row = 0; row < height / 2; row++)
				{
					memset(f->data[1] + row * f->linesize[1], 128, static_cast<size_t>(planar ? width / 2 : width));
					if(planar)
						memset(f->data[2] + row * f->linesize[2], 128, static_cast<size_t>(width / 2));
				}
			}
			// The screen's full-range RGB into BT.709 video range, as the encoder says it is
			scaler.Convert(pixels, stride, w, h, layout,
				f->data[0] + oy * f->linesize[0] + ox, f->linesize[0],
				f->data[1] + (oy / 2) * f->linesize[1] + (planar ? ox / 2 : ox), f->linesize[1],
				planar ? f->data[2] + (oy / 2) * f->linesize[2] + ox / 2 : nullptr, planar ? f->linesize[2] : 0,
				fw, fh);
			return f;
		}

		void End(const std::string &reason)
		{
			{
				std::lock_guard<std::mutex> lock(mutex);
				if(ended)
					return;
				ended = true;
				why = reason;
			}
			changed.notify_all();
		}

		PwConnection connection;
		pw_stream *stream = nullptr;
		spa_hook stream_listener = {};
		pw_stream_events stream_events = {};
		spa_hook core_listener = {};
		pw_core_events core_events = {};

		int width = 1920, height = 1080, fps = 60;
		AVPixelFormat format_out = AV_PIX_FMT_NV12;
		bool format_known = false;
		PcScaler::Layout layout;
		int in_width = 0, in_height = 0;
		PcScaler scaler;

		bool have_last = false;
		std::chrono::steady_clock::time_point last_frame;

		std::mutex mutex;
		std::condition_variable changed;
		AVFrame *latest = nullptr;
		bool ended = false;
		std::string why;
};

} // namespace

std::unique_ptr<PcFrameSource> PcLinux::OpenVideo(const PcCapture::Config &config, AVPixelFormat format, std::string *error)
{
	auto video = std::make_unique<PwVideo>();
	if(!video->Open(config, format, error))
		return nullptr;
	return video;
}

// ------------------------------------------------------------------ the sound

namespace {

/** Programs whose sound never goes to the guests: voice chats (they would hear themselves). */
const char *const kVoiceChats[] = {
	"discord", "vesktop", "webcord", "armcord", "teamspeak", "ts3client", "mumble", "skype", "zoom", "telegram",
	"signal", "element", "slack", "teams", "whatsapp", "jitsi", "guilded", "revolt",
	// What browsers and Electron apps call their calls' sound
	"webrtc voiceengine",
};

class PwSound : public PcSoundSource
{
	public:
		~PwSound() override
		{
			watching = false;
			if(watcher.joinable())
				watcher.join();
			const PwApi &pw = Pw();
			if(connection.loop)
			{
				connection.Lock();
				for(auto &link : links)
					pw.proxy_destroy(link.second);
				links.clear();
				for(auto &node : nodes)
				{
					if(node.second.proxy)
						pw.proxy_destroy(node.second.proxy);
				}
				nodes.clear();
				if(registry)
					pw.proxy_destroy(reinterpret_cast<pw_proxy *>(registry));
				registry = nullptr;
				if(stream)
				{
					pw.stream_disconnect(stream);
					pw.stream_destroy(stream);
					stream = nullptr;
				}
				connection.Unlock();
			}
			connection.Close();
		}

		bool Open(PcSamples out, PcSoundInfo *info, std::string *error)
		{
			samples = std::move(out);
			if(!connection.Open("psrpt-sound", -1, error))
				return false;
			const PwApi &pw = Pw();
			Refresh();
			connection.Lock();
			// Not linked by the system: this app links what is to be heard
			stream = pw.stream_new(connection.core, "PSRemotePlayTogether game sound",
				pw.properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE, "Game",
					PW_KEY_NODE_NAME, "psremoteplaytogether.game-sound", PW_KEY_NODE_AUTOCONNECT, "false", nullptr));
			if(!stream)
			{
				connection.Unlock();
				*error = "PipeWire gave no stream";
				return false;
			}
			spa_zero(stream_events);
			stream_events.version = PW_VERSION_STREAM_EVENTS;
			stream_events.state_changed = &PwSound::OnState;
			stream_events.process = &PwSound::OnProcess;
			pw.stream_add_listener(stream, &stream_listener, &stream_events, this);
			uint8_t buffer[512];
			spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
			spa_audio_info_raw raw;
			spa_zero(raw);
			raw.format = SPA_AUDIO_FORMAT_S16;
			raw.rate = PcCapture::kAudioRate;
			raw.channels = PcCapture::kAudioChannels;
			raw.position[0] = SPA_AUDIO_CHANNEL_FL;
			raw.position[1] = SPA_AUDIO_CHANNEL_FR;
			const spa_pod *params[1] = { spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &raw) };
			int err = pw.stream_connect(stream, PW_DIRECTION_INPUT, PW_ID_ANY,
				static_cast<pw_stream_flags>(PW_STREAM_FLAG_MAP_BUFFERS), params, 1);
			if(err >= 0)
			{
				registry = pw_core_get_registry(connection.core, PW_VERSION_REGISTRY, 0);
				spa_zero(registry_events);
				registry_events.version = PW_VERSION_REGISTRY_EVENTS;
				registry_events.global = &PwSound::OnGlobal;
				registry_events.global_remove = &PwSound::OnGlobalRemove;
				if(registry)
					pw_registry_add_listener(registry, &registry_listener, &registry_events, this);
			}
			connection.Unlock();
			if(err < 0 || !registry)
			{
				*error = "the sound stream could not be connected";
				return false;
			}
			info->what = "the game's own sound through PipeWire (without a game: every program's but this app's and voice chats')";
			// The game may start, end or change while sharing
			watching = true;
			watcher = std::thread([this] {
				while(watching)
				{
					for(int i = 0; i < 30 && watching; i++)
						std::this_thread::sleep_for(std::chrono::milliseconds(100));
					if(!watching)
						break;
					Refresh();
					connection.Lock();
					Relink();
					connection.Unlock();
				}
			});
			return true;
		}

	private:
		/** A program's sound stream; which program it is comes with the node's info. */
		struct Node
		{
			/** From the node's info: what a PulseAudio program (most games) says it is. */
			int pid = 0;
			QString binary;
			QString name;
			/** The PipeWire client: for PipeWire's own programs it knows the process. */
			uint32_t client = SPA_ID_INVALID;
			bool output_stream = false;
			PwSound *owner = nullptr;
			uint32_t id = 0;
			pw_proxy *proxy = nullptr;
			spa_hook listener = {};
		};
		struct Port
		{
			uint32_t node = 0;
			bool output = false;
			bool monitor = false;
			QString channel;
		};

		/** The game's processes now (on the watcher's thread). */
		void Refresh()
		{
			const FoundGame found = FindGame();
			std::lock_guard<std::mutex> lock(game_mutex);
			game_pids = found.pids;
			game_binary = found.binary;
		}

		static void OnState(void *data, pw_stream_state old, pw_stream_state state, const char *error)
		{
			(void)old;
			(void)error;
			auto self = static_cast<PwSound *>(data);
			if(state == PW_STREAM_STATE_PAUSED || state == PW_STREAM_STATE_STREAMING)
			{
				const uint32_t id = Pw().stream_get_node_id(self->stream);
				if(id != self->own_node)
				{
					self->own_node = id;
					self->Relink();
				}
			}
		}

		static void OnProcess(void *data)
		{
			auto self = static_cast<PwSound *>(data);
			const PwApi &pw = Pw();
			pw_buffer *b = pw.stream_dequeue_buffer(self->stream);
			if(!b)
				return;
			spa_buffer *buf = b->buffer;
			if(buf->n_datas > 0 && buf->datas[0].data && buf->datas[0].chunk)
			{
				const spa_data &d = buf->datas[0];
				const uint32_t offset = std::min(d.chunk->offset, d.maxsize);
				const uint32_t size = std::min(d.chunk->size, d.maxsize - offset);
				const size_t frames = size / (sizeof(int16_t) * PcCapture::kAudioChannels);
				if(frames > 0 && self->samples)
					self->samples(reinterpret_cast<const int16_t *>(static_cast<const uint8_t *>(d.data) + offset), frames);
			}
			pw.stream_queue_buffer(self->stream, b);
		}

		static void OnGlobal(void *data, uint32_t id, uint32_t permissions, const char *type, uint32_t version, const spa_dict *props)
		{
			(void)permissions;
			(void)version;
			auto self = static_cast<PwSound *>(data);
			if(!props)
				return;
			if(strcmp(type, PW_TYPE_INTERFACE_Node) == 0)
			{
				const char *media_class = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
				if(!media_class || strcmp(media_class, "Stream/Output/Audio") != 0)
					return;
				Node &n = self->nodes[id];
				n.output_stream = true;
				n.owner = self;
				n.id = id;
				if(const char *client = spa_dict_lookup(props, PW_KEY_CLIENT_ID))
					n.client = static_cast<uint32_t>(atoi(client));
				// The program behind it is only in the node's own info, not in the registry's
				n.proxy = static_cast<pw_proxy *>(pw_registry_bind(self->registry, id, PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, 0));
				if(n.proxy)
				{
					static const pw_node_events events = [] {
						pw_node_events e;
						spa_zero(e);
						e.version = PW_VERSION_NODE_EVENTS;
						e.info = &PwSound::OnNodeInfo;
						return e;
					}();
					pw_node_add_listener(reinterpret_cast<pw_node *>(n.proxy), &n.listener, &events, &n);
				}
				return;
			}
			else if(strcmp(type, PW_TYPE_INTERFACE_Client) == 0)
			{
				// The process at the other end of the client's connection, as the kernel tells it
				if(const char *pid = spa_dict_lookup(props, PW_KEY_SEC_PID))
					self->client_pids[id] = atoi(pid);
			}
			else if(strcmp(type, PW_TYPE_INTERFACE_Port) == 0)
			{
				Port p;
				if(const char *node = spa_dict_lookup(props, PW_KEY_NODE_ID))
					p.node = static_cast<uint32_t>(atoi(node));
				const char *direction = spa_dict_lookup(props, PW_KEY_PORT_DIRECTION);
				p.output = direction && strcmp(direction, "out") == 0;
				const char *monitor = spa_dict_lookup(props, PW_KEY_PORT_MONITOR);
				p.monitor = monitor && strcmp(monitor, "true") == 0;
				if(const char *channel = spa_dict_lookup(props, PW_KEY_AUDIO_CHANNEL))
					p.channel = QString::fromUtf8(channel);
				self->ports[id] = p;
			}
			else
				return;
			self->Relink();
		}

		static void OnNodeInfo(void *data, const pw_node_info *info)
		{
			auto n = static_cast<Node *>(data);
			if(!info || !info->props || !(info->change_mask & PW_NODE_CHANGE_MASK_PROPS))
				return;
			if(const char *pid = spa_dict_lookup(info->props, PW_KEY_APP_PROCESS_ID))
				n->pid = atoi(pid);
			if(const char *binary = spa_dict_lookup(info->props, PW_KEY_APP_PROCESS_BINARY))
			{
				n->binary = QString::fromUtf8(binary).toLower();
				if(n->binary.endsWith(QLatin1String(".exe")))
					n->binary.chop(4);
			}
			if(const char *name = spa_dict_lookup(info->props, PW_KEY_APP_NAME))
				n->name = QString::fromUtf8(name).toLower();
			n->owner->Relink();
		}

		static void OnGlobalRemove(void *data, uint32_t id)
		{
			auto self = static_cast<PwSound *>(data);
			self->client_pids.erase(id);
			auto node = self->nodes.find(id);
			if(node != self->nodes.end())
			{
				if(node->second.proxy)
					Pw().proxy_destroy(node->second.proxy);
				self->nodes.erase(node);
			}
			self->ports.erase(id);
			// Links of a port that went are gone with it
			for(auto it = self->links.begin(); it != self->links.end();)
			{
				if(it->first.first == id || it->first.second == id)
				{
					Pw().proxy_destroy(it->second);
					it = self->links.erase(it);
				}
				else
					++it;
			}
		}

		bool IsVoiceChat(const QString &binary) const
		{
			for(const char *name : kVoiceChats)
			{
				if(binary.contains(QLatin1String(name)))
					return true;
			}
			return false;
		}

		/** The links there should be, made and unmade (on PipeWire's thread, or with its lock). */
		void Relink()
		{
			if(own_node == SPA_ID_INVALID || !registry)
				return;
			uint32_t own_left = SPA_ID_INVALID, own_right = SPA_ID_INVALID;
			for(const auto &port : ports)
			{
				if(port.second.node != own_node || port.second.output)
					continue;
				if(port.second.channel == QLatin1String("FL"))
					own_left = port.first;
				else if(port.second.channel == QLatin1String("FR"))
					own_right = port.first;
			}
			if(own_left == SPA_ID_INVALID || own_right == SPA_ID_INVALID)
				return;
			QSet<int> pids;
			QString binary;
			{
				std::lock_guard<std::mutex> lock(game_mutex);
				pids = game_pids;
				binary = game_binary;
			}
			const int me = getpid();
			auto pid_of = [this](const Node &n) {
				if(n.pid)
					return n.pid;
				auto client = client_pids.find(n.client);
				return client != client_pids.end() ? client->second : 0;
			};
			// The game's streams, if it plays any; else everybody's but this app's and voice chats'
			std::vector<uint32_t> sources;
			for(const auto &node : nodes)
			{
				const Node &n = node.second;
				const int pid = pid_of(n);
				if(n.output_stream && pid != me && ((pid && pids.contains(pid)) || (!binary.isEmpty() && n.binary == binary)))
					sources.push_back(node.first);
			}
			if(sources.empty())
			{
				// Only programs that are known: whose info came, so a voice chat is never let in unseen
				for(const auto &node : nodes)
				{
					const Node &n = node.second;
					const int pid = pid_of(n);
					if(n.output_stream && !n.name.isEmpty() && pid && pid != me && !IsVoiceChat(n.binary) && !IsVoiceChat(n.name))
						sources.push_back(node.first);
				}
			}
			std::map<std::pair<uint32_t, uint32_t>, bool> wanted;
			for(const auto &port : ports)
			{
				const Port &p = port.second;
				if(!p.output || p.monitor || std::find(sources.begin(), sources.end(), p.node) == sources.end())
					continue;
				const QString &ch = p.channel;
				const bool left = ch == QLatin1String("FL") || ch == QLatin1String("SL") || ch == QLatin1String("RL")
					|| ch == QLatin1String("MONO") || ch == QLatin1String("FC") || ch.isEmpty();
				const bool right = ch == QLatin1String("FR") || ch == QLatin1String("SR") || ch == QLatin1String("RR")
					|| ch == QLatin1String("MONO") || ch == QLatin1String("FC") || ch.isEmpty();
				if(left)
					wanted[{ port.first, own_left }] = true;
				if(right)
					wanted[{ port.first, own_right }] = true;
			}
			const PwApi &pw = Pw();
			for(auto it = links.begin(); it != links.end();)
			{
				if(!wanted.count(it->first))
				{
					pw.proxy_destroy(it->second);
					it = links.erase(it);
				}
				else
					++it;
			}
			for(const auto &w : wanted)
			{
				if(links.count(w.first))
					continue;
				const uint32_t out_port = w.first.first, in_port = w.first.second;
				const std::string out_node = std::to_string(ports[out_port].node), in_node = std::to_string(own_node);
				const std::string out_id = std::to_string(out_port), in_id = std::to_string(in_port);
				spa_dict_item items[] = {
					SPA_DICT_ITEM_INIT(PW_KEY_LINK_OUTPUT_NODE, out_node.c_str()),
					SPA_DICT_ITEM_INIT(PW_KEY_LINK_OUTPUT_PORT, out_id.c_str()),
					SPA_DICT_ITEM_INIT(PW_KEY_LINK_INPUT_NODE, in_node.c_str()),
					SPA_DICT_ITEM_INIT(PW_KEY_LINK_INPUT_PORT, in_id.c_str()),
					SPA_DICT_ITEM_INIT(PW_KEY_OBJECT_LINGER, "false"),
				};
				const spa_dict dict = SPA_DICT_INIT_ARRAY(items);
				auto proxy = static_cast<pw_proxy *>(pw_core_create_object(connection.core, "link-factory",
					PW_TYPE_INTERFACE_Link, PW_VERSION_LINK, &dict, 0));
				if(proxy)
					links[w.first] = proxy;
			}
		}

		PwConnection connection;
		PcSamples samples;
		pw_stream *stream = nullptr;
		spa_hook stream_listener = {};
		pw_stream_events stream_events = {};
		pw_registry *registry = nullptr;
		spa_hook registry_listener = {};
		pw_registry_events registry_events = {};
		uint32_t own_node = SPA_ID_INVALID;
		std::map<uint32_t, Node> nodes;
		std::map<uint32_t, int> client_pids;
		std::map<uint32_t, Port> ports;
		std::map<std::pair<uint32_t, uint32_t>, pw_proxy *> links;

		std::mutex game_mutex;
		QSet<int> game_pids;
		QString game_binary;
		std::atomic<bool> watching{false};
		std::thread watcher;
};

} // namespace

std::unique_ptr<PcSoundSource> PcLinux::OpenSound(const PcCapture::Config &config, PcSamples samples, PcSoundInfo *info, std::string *error)
{
	(void)config;
	auto sound = std::make_unique<PwSound>();
	if(!sound->Open(std::move(samples), info, error))
		return nullptr;
	return sound;
}

#include "pcsource_linux.moc"
