// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "livecontroller.h"
#include "keystore.h"
#include "livestream.h"
#include "settings.h"
#include "streamsession.h"

#include <QFile>
#include <QUrl>

#include <algorithm>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(chiakiGui)

namespace {

/** Twitch's own choice of the nearest ingest server, over TLS (ingest.twitch.tv/ingests, "Default"). */
const char kTwitchServer[] = "rtmps://ingest.global-contribute.live-video.net/app";

struct Quality
{
	const char *name;
	int width;
	int height;
	int fps;
	/** What Twitch recommends for it, kbit/s. */
	int kbps;
};

const Quality kQualities[] = {
	{ "720p30", 1280, 720, 30, 3000 },
	{ "720p60", 1280, 720, 60, 4500 },
	{ "1080p30", 1920, 1080, 30, 4500 },
	{ "1080p60", 1920, 1080, 60, 6000 },
};

const Quality &FindQuality(const QString &name)
{
	for(const Quality &q : kQualities)
	{
		if(name == QLatin1String(q.name))
			return q;
	}
	return kQualities[0];
}

struct Key
{
	const char *name;
	QVariant default_value;
};

const Key kKeys[] = {
	// "twitch" or "custom"
	{ "service", QStringLiteral("twitch") },
	// rtmp:// or rtmps:// address of another service
	{ "server", QString() },
	{ "quality", QStringLiteral("720p30") },
	// 0: what the quality calls for
	{ "video_kbps", 0 },
	// what goes into the stream
	{ "overlays", true },
	{ "voices", true },
	{ "own_mic", true },
	// percent, 0..200
	{ "game_level", 100 },
	{ "voices_level", 100 },
	{ "mic_level", 100 },
	// the others leave us out of their streams
	{ "leave_me_out", false },
};

} // namespace

LiveController::LiveController(Settings *settings, QObject *parent)
	: QObject(parent), settings(settings)
{
	for(const Key &k : kKeys)
	{
		QVariant v = settings->GetValue(QStringLiteral("live/%1").arg(QLatin1String(k.name)), k.default_value);
		// QSettings hands back text for everything it read from the file
		if(k.default_value.typeId() == QMetaType::Bool)
			v = v.toBool();
		else if(k.default_value.typeId() == QMetaType::Int)
			v = v.toInt();
		else
			v = v.toString();
		map.insert(QLatin1String(k.name), v);
	}
	connect(&map, &QQmlPropertyMap::valueChanged, this, [this](const QString &key, const QVariant &value) {
		Store(key, value);
		Apply(key);
	});
	poll_timer.setInterval(500);
	connect(&poll_timer, &QTimer::timeout, this, &LiveController::Poll);
}

LiveController::~LiveController()
{
	Shutdown();
}

void LiveController::Shutdown()
{
	poll_timer.stop();
	live::Engine::Get().Stop();
}

void LiveController::set(const QString &key, const QVariant &value)
{
	if(map.value(key) == value)
		return;
	map.insert(key, value);
	Store(key, value);
	Apply(key);
}

void LiveController::Store(const QString &key, const QVariant &value)
{
	settings->SetValue(QStringLiteral("live/%1").arg(key), value);
	emit valuesChanged();
}

void LiveController::Apply(const QString &key)
{
	live::Engine &engine = live::Engine::Get();
	if(key.endsWith(QLatin1String("_level")))
	{
		engine.SetLevels(map.value("game_level").toInt(), map.value("voices_level").toInt(), map.value("mic_level").toInt());
	}
	else if((key == QLatin1String("voices") || key == QLatin1String("own_mic")) && session && told_session)
	{
		session->CoPlaySetLiveAudio(map.value("voices").toBool(), map.value("own_mic").toBool());
	}
	else if(key == QLatin1String("leave_me_out") && session)
	{
		session->CoPlaySetLeaveMeOut(map.value("leave_me_out").toBool());
	}
	else if(key == QLatin1String("service"))
	{
		emit keyChanged();
	}
}

QString LiveController::KeyName() const
{
	return map.value("service").toString() == QLatin1String("custom")
		? QStringLiteral("live-stream-key-custom") : QStringLiteral("live-stream-key-twitch");
}

bool LiveController::keySaved() const
{
	// Only a note that there is one: asking the store itself could make it ask the player
	return settings->GetValue(QStringLiteral("live/%1_saved").arg(KeyName()), false).toBool()
		|| keys_in_memory.contains(KeyName());
}

QString LiveController::keyStore() const
{
	return keystore::StoreName();
}

QString LiveController::saveKey(const QString &key)
{
	const QString trimmed = key.trimmed();
	QString note;
	keys_in_memory.remove(KeyName());
	if(trimmed.isEmpty())
	{
		keystore::Remove(KeyName());
		settings->SetValue(QStringLiteral("live/%1_saved").arg(KeyName()), false);
	}
	else
	{
		QString error;
		const bool stored = keystore::Save(KeyName(), trimmed, &error);
		if(!stored)
		{
			note = error.isEmpty() || keystore::StoreName().isEmpty()
				? tr("This computer has no password store the app can use, so the key is kept only until the app quits.")
				: tr("The key could not be saved (%1), so it is kept only until the app quits.").arg(error);
			qCWarning(chiakiGui).noquote() << "Live: stream key kept in memory only:" << error;
			keys_in_memory.insert(KeyName());
		}
		settings->SetValue(QStringLiteral("live/%1_saved").arg(KeyName()), stored);
	}
	emit keyChanged();
	return note;
}

void LiveController::clearKey()
{
	saveKey(QString());
}

QString LiveController::server() const
{
	if(map.value("service").toString() == QLatin1String("custom"))
		return map.value("server").toString().trimmed();
	return QString::fromLatin1(kTwitchServer);
}

QSize LiveController::streamSize() const
{
	const Quality &q = FindQuality(map.value("quality").toString());
	return QSize(q.width, q.height);
}

int LiveController::defaultKbps() const
{
	return FindQuality(map.value("quality").toString()).kbps;
}

bool LiveController::live() const
{
	return current_state == QLatin1String("connecting") || current_state == QLatin1String("live")
		|| current_state == QLatin1String("reconnecting");
}

QString LiveController::CaFile()
{
	const QByteArray forced = qgetenv("PSRPT_LIVE_CA_FILE");
	if(!forced.isEmpty())
		return QString::fromLocal8Bit(forced);
#if defined(Q_OS_MACOS)
	// Every Mac has it; the OpenSSL that FFmpeg uses would otherwise look in Homebrew's folder
	return QStringLiteral("/etc/ssl/cert.pem");
#elif defined(Q_OS_WIN)
	// Windows checks the server against its own certificate store
	return QString();
#else
	const char *candidates[] = {
		"/etc/ssl/certs/ca-certificates.crt", // Debian, Ubuntu, Arch, SteamOS
		"/etc/pki/tls/certs/ca-bundle.crt",   // Fedora, RHEL
		"/etc/ssl/ca-bundle.pem",             // openSUSE
		"/etc/ssl/cert.pem",                  // Alpine, others
	};
	for(const char *c : candidates)
	{
		if(QFile::exists(QString::fromLatin1(c)))
			return QString::fromLatin1(c);
	}
	return QString();
#endif
}

QString LiveController::start()
{
	if(live())
		return QString();
	if(!session)
		return tr("Going live works while you play.");

	const bool custom = map.value("service").toString() == QLatin1String("custom");
	QString base = server();
	while(base.endsWith(QLatin1Char('/')))
		base.chop(1);
	if(custom && !base.startsWith(QLatin1String("rtmp://")) && !base.startsWith(QLatin1String("rtmps://")))
		return tr("Add the address of your streaming service (rtmp:// or rtmps://) under Settings → Live streaming.");
	const QString key = keystore::Load(KeyName());
	if(!custom && key.isEmpty())
		return tr("Add your Twitch stream key under Settings → Live streaming.");

	const Quality &q = FindQuality(map.value("quality").toString());
	live::Config config;
	config.url = (key.isEmpty() ? base : base + QLatin1Char('/') + key).toStdString();
	// Only where it goes: some services take the key as part of the address
	{
		const QUrl url(base);
		config.shown_url = QStringLiteral("%1://%2%3").arg(url.scheme(), url.host(),
			url.port() > 0 ? QStringLiteral(":%1").arg(url.port()) : QString()).toStdString();
	}
	config.width = q.width;
	config.height = q.height;
	config.fps = q.fps;
	const int kbps = map.value("video_kbps").toInt();
	config.video_kbps = kbps > 0 ? std::clamp(kbps, 500, 20000) : q.kbps;
	config.audio_kbps = 160;
	config.ca_file = CaFile().toStdString();

	live::Engine &engine = live::Engine::Get();
	engine.SetLevels(map.value("game_level").toInt(), map.value("voices_level").toInt(), map.value("mic_level").toInt());
	if(!engine.Start(config))
		return tr("The last stream is still ending. Try again in a moment.");
	// The voices go to the stream from now; the others see us as live once we are on air (Poll)
	session->CoPlaySetLiveAudio(map.value("voices").toBool(), map.value("own_mic").toBool());
	told_session = true;
	told_live = false;
	current_state = QStringLiteral("connecting");
	current_stats = QVariantMap();
	emit statsChanged();
	poll_timer.start();
	return QString();
}

void LiveController::stop()
{
	live::Engine::Get().RequestStop();
	ForgetSession();
	Poll();
}

void LiveController::ForgetSession()
{
	if(session && told_session)
	{
		session->CoPlaySetLive(false);
		session->CoPlaySetLiveAudio(false, false);
	}
	told_session = false;
	told_live = false;
}

void LiveController::SetSession(StreamSession *new_session)
{
	if(session == new_session)
		return;
	// The picture and the sound of this session are what goes out: no session, no stream
	if(!new_session)
	{
		if(live())
			stop();
		session = nullptr;
		return;
	}
	// Another session in its place: the one that gets the console back after a lost
	// connection. The stream goes on with it.
	session = new_session;
	told_session = false;
	told_live = false;
	if(live())
	{
		session->CoPlaySetLiveAudio(map.value("voices").toBool(), map.value("own_mic").toBool());
		told_session = true;
		Poll();
	}

	// For trying it out without clicking (like COPLAY_TEST_CAMERA): PSRPT_LIVE_START=N goes
	// live a few seconds into a session and stops after N seconds (0: stays live)
	if(session && qEnvironmentVariableIsSet("PSRPT_LIVE_START"))
	{
		const int stay = qEnvironmentVariableIntValue("PSRPT_LIVE_START");
		qCInfo(chiakiGui) << "Live: PSRPT_LIVE_START is set, going live in 4 s";
		QTimer::singleShot(4000, this, [this, stay] {
			if(!session || live())
				return;
			const QString problem = start();
			if(!problem.isEmpty())
				qCWarning(chiakiGui).noquote() << "Live:" << problem;
			else if(stay > 0)
				QTimer::singleShot(stay * 1000, this, [this] { stop(); });
		});
	}
}

void LiveController::Poll()
{
	const live::Stats s = live::Engine::Get().GetStats();
	QVariantMap m;
	m["kbps"] = s.kbps;
	m["frames"] = static_cast<qulonglong>(s.frames);
	m["dropped"] = static_cast<qulonglong>(s.dropped);
	m["seconds"] = static_cast<qlonglong>(s.live_seconds);
	m["reconnects"] = s.reconnects;
	m["encoder"] = QString::fromStdString(s.encoder);
	m["error"] = QString::fromStdString(s.error);
	const QString state = QString::fromLatin1(live::StateName(s.state));
	const bool changed = state != current_state || m != current_stats;
	current_state = state;
	current_stats = m;
	if(s.state == live::State::Off || s.state == live::State::Failed)
	{
		poll_timer.stop();
		// It gave up by itself: the others should not see us as live any more
		ForgetSession();
	}
	else if(s.state == live::State::Live && session && told_session && !told_live)
	{
		// On air: now everybody sees it (and keeps seeing it while a lost connection is made again)
		session->CoPlaySetLive(true);
		told_live = true;
	}
	if(changed)
		emit statsChanged();
}
