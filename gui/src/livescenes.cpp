// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "livescenes.h"
#include "scenemusic.h"
#include "settings.h"
#include "streamsession.h"

#include "qrcodegen.hpp"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>

namespace {

struct Key
{
	const char *name;
	QVariant default_value;
};

const char kDefaultPage[] = "https://crypto90.github.io/PSRemotePlayTogether/";
const char kAppPage[] = "https://github.com/Crypto90/PSRemotePlayTogether";

qint64 NowMs()
{
	return QDateTime::currentMSecsSinceEpoch();
}

/** "https://example.org/page/" -> "example.org/page", for showing. */
QString ShownUrl(const QString &url)
{
	QString shown = url;
	shown.remove(QRegularExpression(QStringLiteral("^https?://")));
	while(shown.endsWith(QLatin1Char('/')))
		shown.chop(1);
	return shown;
}

QString Initial(const QString &name)
{
	const QString trimmed = name.trimmed();
	return trimmed.isEmpty() ? QStringLiteral("?") : trimmed.left(1).toUpper();
}

} // namespace

LiveScenes::LiveScenes(Settings *settings, QObject *parent)
	: QObject(parent), settings(settings)
{
	const Key keys[] = {
		// What the player fills in once
		{ "channel", QString() },
		{ "next_stream", QString() },
		{ "countdown_minutes", 5 },
		{ "break_minutes", 5 },
		// While the console's connection is made again, the stream shows Technical difficulties
		{ "auto_difficulties", true },
		// The texts, with {game}, {host}, {me}, {players}, {channel}
		{ "starting_title", tr("Starting soon") },
		{ "starting_line", tr("Tonight: {game}") },
		{ "lobby_title", tr("Just chatting") },
		{ "lobby_line", tr("Hosted by {host}") },
		{ "brb_title", tr("Be right back") },
		{ "brb_line", tr("{me} is away for a moment. The others are still in the room.") },
		{ "play_title", tr("Want to play with us?") },
		{ "play_line", tr("No download, no PlayStation, no account. Your own controller plays along.") },
		{ "difficulties_title", tr("Hang tight") },
		{ "difficulties_line", tr("We lost the picture for a moment. The app is reconnecting by itself.") },
		{ "difficulties_footer", tr("Chat is still open – say hi while we wait.") },
		{ "ending_title", tr("Thanks for watching!") },
		// Music while Starting soon, Be right back and Thanks for watching are on the stream:
		// the app's own loop ("builtin") or the player's own files ("own")
		{ "music_starting_on", true },
		{ "music_starting_volume", 70 },
		{ "music_starting_source", QStringLiteral("builtin") },
		{ "music_starting_files", QStringList() },
		{ "music_starting_shuffle", false },
		{ "music_brb_on", true },
		{ "music_brb_volume", 70 },
		{ "music_brb_source", QStringLiteral("builtin") },
		{ "music_brb_files", QStringList() },
		{ "music_brb_shuffle", false },
		{ "music_ending_on", true },
		{ "music_ending_volume", 70 },
		{ "music_ending_source", QStringLiteral("builtin") },
		{ "music_ending_files", QStringList() },
		{ "music_ending_shuffle", false },
	};
	for(const Key &k : keys)
	{
		QVariant v = settings->GetValue(QStringLiteral("scenes/%1").arg(QLatin1String(k.name)), k.default_value);
		if(k.default_value.typeId() == QMetaType::Bool)
			v = v.toBool();
		else if(k.default_value.typeId() == QMetaType::Int)
			v = v.toInt();
		else if(k.default_value.typeId() == QMetaType::QStringList)
			v = v.toStringList();
		else
			v = v.toString();
		map.insert(QLatin1String(k.name), v);
	}
	connect(&map, &QQmlPropertyMap::valueChanged, this, [this](const QString &key, const QVariant &value) {
		Store(key, value);
	});
	music = new SceneMusic(this, this);
	timer.setInterval(250);
	connect(&timer, &QTimer::timeout, this, &LiveScenes::Refresh);
	Refresh();
}

QVariantList LiveScenes::scenes() const
{
	QVariantList list;
	auto add = [&list](const char *id, const QString &name) {
		QVariantMap m;
		m["id"] = QString::fromLatin1(id);
		m["name"] = name;
		list.append(m);
	};
	add("game", tr("The game"));
	add("starting", tr("Starting soon"));
	add("lobby", tr("Lobby"));
	add("brb", tr("Be right back"));
	add("play", tr("Play with us"));
	add("difficulties", tr("Technical difficulties"));
	add("ending", tr("Thanks for watching"));
	return list;
}

QString LiveScenes::shown() const
{
	if(reconnecting && map.value("auto_difficulties").toBool() && scene == QLatin1String("game"))
		return QStringLiteral("difficulties");
	return scene;
}

void LiveScenes::show(const QString &id)
{
	bool known = false;
	for(const QVariant &s : scenes())
		known = known || s.toMap().value("id").toString() == id;
	if(!known || id == scene)
		return;
	const qint64 now = NowMs();
	if(id == QLatin1String("starting") && countdown_end_ms <= now)
		restartCountdown();
	if(id == QLatin1String("brb"))
		break_end_ms = now + static_cast<qint64>(std::max(1, map.value("break_minutes").toInt())) * 60000;
	scene = id;
	emit sceneChanged();
	Refresh();
}

void LiveScenes::restartCountdown()
{
	countdown_end_ms = NowMs() + static_cast<qint64>(std::max(1, map.value("countdown_minutes").toInt())) * 60000;
	Refresh();
}

void LiveScenes::set(const QString &key, const QVariant &value)
{
	if(map.value(key) == value)
		return;
	map.insert(key, value);
	Store(key, value);
}

void LiveScenes::Store(const QString &key, const QVariant &value)
{
	settings->SetValue(QStringLiteral("scenes/%1").arg(key), value);
	emit valuesChanged();
	if(key == QLatin1String("auto_difficulties"))
		emit sceneChanged();
	Refresh();
}

void LiveScenes::SetSession(StreamSession *new_session)
{
	if(session == new_session)
		return;
	// The session that gets the console back after a lost connection carries on the old
	// one: the old object is still there when its successor comes
	const bool successor = session && new_session;
	session = new_session;
	if(session && !successor)
	{
		// A new session: Thanks for watching counts from here
		session_start_ms = NowMs();
		session_end_ms = 0;
		seen_names.clear();
		seen_slots.clear();
		game_picture.clear();
	}
	else
	{
		session_end_ms = NowMs();
		reconnecting = false;
		// No game behind the stream any more
		if(scene != QLatin1String("game"))
		{
			scene = QStringLiteral("game");
		}
		emit sceneChanged();
	}
	UpdateTimer();
	Refresh();
}

void LiveScenes::SetGame(const QString &name)
{
	if(game == name)
		return;
	game = name;
	Refresh();
}

void LiveScenes::SetReconnecting(bool now)
{
	if(reconnecting == now)
		return;
	reconnecting = now;
	emit sceneChanged();
	Refresh();
}

void LiveScenes::SetOverlayUrl(const QString &url)
{
	overlay_url = url;
}

QVariantMap LiveScenes::obsUrls() const
{
	QVariantMap urls;
	if(overlay_url.isEmpty())
		return urls;
	QString base = overlay_url;
	if(!base.endsWith(QLatin1Char('/')))
		base += QLatin1Char('/');
	for(const QVariant &s : scenes())
	{
		const QString id = s.toMap().value("id").toString();
		urls[id] = base + QStringLiteral("scene/") + (id == QLatin1String("game") ? QStringLiteral("current") : id);
	}
	return urls;
}

void LiveScenes::copy(const QString &text) const
{
	QGuiApplication::clipboard()->setText(text);
}

namespace {

/** A source of an OBS scene collection, with what OBS writes for every source. */
QJsonObject ObsSource(const QString &name, const QString &id, const QJsonObject &settings, bool audio)
{
	QJsonObject o;
	o["balance"] = 0.5;
	o["deinterlace_field_order"] = 0;
	o["deinterlace_mode"] = 0;
	o["enabled"] = true;
	o["flags"] = 0;
	o["hotkeys"] = QJsonObject();
	o["id"] = id;
	o["versioned_id"] = id;
	o["mixers"] = audio ? 255 : 0;
	o["monitoring_type"] = 0;
	o["muted"] = false;
	o["name"] = name;
	o["private_settings"] = QJsonObject();
	o["push-to-mute"] = false;
	o["push-to-mute-delay"] = 0;
	o["push-to-talk"] = false;
	o["push-to-talk-delay"] = 0;
	o["settings"] = settings;
	o["sync"] = 0;
	o["volume"] = 1.0;
	return o;
}

/** A source placed in a scene: at the top left, its own size, or fitted into 1920 x 1080. */
QJsonObject ObsItem(const QString &source, int id, bool fit)
{
	auto vec = [](double x, double y) {
		QJsonObject v;
		v["x"] = x;
		v["y"] = y;
		return v;
	};
	QJsonObject o;
	o["align"] = 5;
	o["blend_method"] = QStringLiteral("default");
	o["blend_type"] = QStringLiteral("normal");
	o["bounds"] = vec(1920.0, 1080.0);
	o["bounds_align"] = 0;
	// 2: scaled into the bounds, keeping its shape
	o["bounds_type"] = fit ? 2 : 0;
	o["crop_bottom"] = 0;
	o["crop_left"] = 0;
	o["crop_right"] = 0;
	o["crop_top"] = 0;
	o["group_item_backup"] = false;
	o["id"] = id;
	o["locked"] = false;
	o["name"] = source;
	o["pos"] = vec(0.0, 0.0);
	o["private_settings"] = QJsonObject();
	o["rot"] = 0.0;
	o["scale"] = vec(1.0, 1.0);
	o["scale_filter"] = QStringLiteral("disable");
	o["visible"] = true;
	return o;
}

QJsonObject ObsBrowser(const QString &url, bool sound, bool only_while_shown = false)
{
	QJsonObject s;
	s["url"] = url;
	s["width"] = 1920;
	s["height"] = 1080;
	// "Control audio via OBS": the voices, the alert sounds and the scenes' music go into the stream
	s["reroute_audio"] = sound;
	// A scene's page runs only while its scene is shown: its music starts with it, faded in
	s["shutdown"] = only_while_shown;
	s["restart_when_active"] = only_while_shown;
	return s;
}

} // namespace

QString LiveScenes::exportObs()
{
	QString base = overlay_url;
	if(base.isEmpty())
	{
		int port = settings->GetOverlayPort();
		if(port < 1 || port > 65535)
			port = 9412;
		base = QStringLiteral("http://localhost:%1/").arg(port);
	}
	if(!base.endsWith(QLatin1Char('/')))
		base += QLatin1Char('/');

	QJsonArray sources;
	QJsonArray order;
	const QString overlay_name = tr("Overlay and Twitch elements");
	const QString alerts_name = tr("Twitch alerts");
	const QString game_name = tr("PSRemotePlayTogether window");
	const QString sound_name = tr("PSRemotePlayTogether sound");
	sources.append(ObsSource(overlay_name, QStringLiteral("browser_source"), ObsBrowser(base, true), true));
	sources.append(ObsSource(alerts_name, QStringLiteral("browser_source"), ObsBrowser(base + QStringLiteral("alerts"), true), true));

	// The game: this app's window (and on macOS its sound), as OBS captures it on each system.
	// OBS may still ask once which window it is.
	bool with_sound_source = false;
#if defined(Q_OS_MACOS)
	QSettings plist(QCoreApplication::applicationDirPath() + QStringLiteral("/../Info.plist"), QSettings::NativeFormat);
	const QString bundle = plist.value(QStringLiteral("CFBundleIdentifier"), QStringLiteral("net.gamernova.psremoteplaytogether")).toString();
	QJsonObject capture;
	capture["type"] = 2; // the application, whichever window it shows
	capture["application"] = bundle;
	capture["show_cursor"] = false;
	sources.append(ObsSource(game_name, QStringLiteral("screen_capture"), capture, false));
	QJsonObject sound;
	sound["type"] = 1; // the application's sound
	sound["application"] = bundle;
	sources.append(ObsSource(sound_name, QStringLiteral("sck_audio_capture"), sound, true));
	with_sound_source = true;
#elif defined(Q_OS_WIN)
	QJsonObject capture;
	// "title:class:program"; 2: found by its program, whatever the title says
	capture["window"] = QStringLiteral("PSRemotePlayTogether::") + QFileInfo(QCoreApplication::applicationFilePath()).fileName();
	capture["priority"] = 2;
	capture["method"] = 2; // Windows Graphics Capture
	capture["capture_audio"] = true;
	capture["cursor"] = false;
	sources.append(ObsSource(game_name, QStringLiteral("window_capture"), capture, true));
#else
	sources.append(ObsSource(game_name, QStringLiteral("pipewire-screen-capture-source"), QJsonObject(), false));
#endif

	auto scene = [&](const QString &name, const QList<QPair<QString, bool>> &items) {
		QJsonArray list;
		int id = 0;
		for(const auto &item : items)
			list.append(ObsItem(item.first, ++id, item.second));
		QJsonObject s;
		s["custom_size"] = false;
		s["id_counter"] = id;
		s["items"] = list;
		sources.append(ObsSource(name, QStringLiteral("scene"), s, false));
		QJsonObject o;
		o["name"] = name;
		order.append(o);
	};

	const QString game_scene = tr("Game with overlay");
	QList<QPair<QString, bool>> game_items = { { game_name, true } };
	if(with_sound_source)
		game_items.append({ sound_name, false });
	game_items.append({ overlay_name, false });
	game_items.append({ alerts_name, false });
	scene(game_scene, game_items);

	for(const QVariant &v : scenes())
	{
		const QVariantMap m = v.toMap();
		const QString id = m.value("id").toString();
		if(id == QLatin1String("game"))
			continue;
		const QString page = m.value("name").toString() + QStringLiteral(" – ") + tr("page");
		sources.append(ObsSource(page, QStringLiteral("browser_source"), ObsBrowser(base + QStringLiteral("scene/") + id, true, true), true));
		scene(m.value("name").toString(), { { page, false }, { alerts_name, false } });
	}

	QJsonObject collection;
	collection["name"] = QStringLiteral("PSRemotePlayTogether");
	collection["current_scene"] = game_scene;
	collection["current_program_scene"] = game_scene;
	collection["current_transition"] = QStringLiteral("Fade");
	collection["transition_duration"] = 300;
	collection["transitions"] = QJsonArray();
	collection["groups"] = QJsonArray();
	collection["modules"] = QJsonObject();
	collection["preview_locked"] = false;
	collection["quick_transitions"] = QJsonArray();
	collection["saved_projectors"] = QJsonArray();
	collection["scaling_enabled"] = false;
	collection["scene_order"] = order;
	collection["sources"] = sources;

	const QString start = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(QStringLiteral("PSRemotePlayTogether OBS.json"));
	const QString file = QFileDialog::getSaveFileName(QApplication::focusWidget(), tr("Save the OBS layout"), start,
		tr("OBS scene collection (*.json)"), nullptr, QFileDialog::DontUseNativeDialog);
	if(file.isEmpty())
		return QString();
	QSaveFile out(file);
	if(!out.open(QIODevice::WriteOnly))
		return QString();
	out.write(QJsonDocument(collection).toJson(QJsonDocument::Indented));
	return out.commit() ? file : QString();
}

void LiveScenes::SetPageClients(int count)
{
	page_clients = count;
	UpdateTimer();
}

void LiveScenes::UpdateTimer()
{
	const bool needed = session || page_clients > 0;
	if(needed && !timer.isActive())
		timer.start();
	else if(!needed && timer.isActive())
		timer.stop();
}

void LiveScenes::SetGamePicture(const QByteArray &jpeg)
{
	if(jpeg.isEmpty())
		return;
	game_picture = jpeg;
	picture_serial++;
	Refresh();
}

QString LiveScenes::fill(const QString &text) const
{
	QString me;
	QString host;
	int players = 0;
	for(const QVariant &p : qml_data.value("players").toList())
	{
		const QVariantMap m = p.toMap();
		players++;
		if(m.value("you").toBool())
			me = m.value("name").toString();
		if(m.value("host").toBool())
			host = m.value("name").toString();
	}
	if(me.isEmpty() && session)
		me = settings->GetCoPlayName();
	const QString channel = map.value("channel").toString().trimmed();
	const struct
	{
		const char *key;
		QString value;
	} values[] = {
		{ "{game}", game.isEmpty() && session ? session->GetCoPlayRoomGame() : game },
		{ "{host}", host },
		{ "{me}", me },
		{ "{players}", players > 0 ? QString::number(players) : QString() },
		{ "{channel}", channel },
	};
	QString out = text;
	for(const auto &v : values)
	{
		if(!out.contains(QLatin1String(v.key)))
			continue;
		// A line whose placeholder has nothing to say is left out altogether
		if(v.value.trimmed().isEmpty())
			return QString();
		out.replace(QLatin1String(v.key), v.value);
	}
	return out.trimmed();
}

/** The QR code of `url` as SVG, and its rows of modules ("0101...") for QML. */
static QByteArray QrCodeSvg(const QString &url, QVariantList *rows)
{
	using qrcodegen::QrCode;
	const QByteArray utf8 = url.toUtf8();
	const QrCode qr = QrCode::encodeText(utf8.constData(), QrCode::Ecc::MEDIUM);
	const int size = qr.getSize();
	const int border = 2;
	QByteArray path;
	for(int y = 0; y < size; y++)
	{
		QString row;
		for(int x = 0; x < size; x++)
		{
			const bool dark = qr.getModule(x, y);
			row += dark ? QLatin1Char('1') : QLatin1Char('0');
			if(dark)
				path += "M" + QByteArray::number(x + border) + "," + QByteArray::number(y + border) + "h1v1h-1z";
		}
		if(rows)
			rows->append(row);
	}
	const QByteArray side = QByteArray::number(size + 2 * border);
	return "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " + side + " " + side
		+ "\" shape-rendering=\"crispEdges\"><rect width=\"100%\" height=\"100%\" fill=\"#f3f1ff\"/><path fill=\"#15122b\" d=\""
		+ path + "\"/></svg>";
}

QByteArray LiveScenes::DemoQrSvg()
{
	static const QByteArray svg = QrCodeSvg(QString::fromLatin1(kDefaultPage), nullptr);
	return svg;
}

void LiveScenes::BuildQr(const QString &url)
{
	if(url == qr_url)
		return;
	qr_url = url;
	qr_svg.clear();
	qr_rows.clear();
	if(url.isEmpty())
		return;
	qr_svg = QrCodeSvg(url, &qr_rows);
}

void LiveScenes::Refresh()
{
	const qint64 now = NowMs();
	QVariantMap d;
	d["scene"] = shown();
	d["chosen"] = scene;
	d["live"] = !!session;

	// ---- the players, as streams may show them
	QVariantList players;
	QVariantList page_players;
	int in_voice = 0;
	QString host_name;
	QString own_name;
	if(session)
	{
		QVariantList list = session->GetCoPlayPlayers();
		if(list.isEmpty())
			list.append(session->coplayOwnPlayer());
		const QVariantMap talking = session->GetCoPlayTalking();
		const int cam_serial = session->GetCoPlayCamSerial();
		for(const QVariant &entry : std::as_const(list))
		{
			QVariantMap p = entry.toMap();
			const int slot = p.value("slot").toInt();
			const bool you = p.value("you").toBool();
			const bool left_out = p.value("noStreams").toBool() && !you;
			QVariantMap m;
			m["slot"] = slot;
			m["name"] = left_out ? tr("Player %1").arg(slot + 1) : p.value("name").toString();
			if(m["name"].toString().trimmed().isEmpty())
				m["name"] = tr("Player %1").arg(slot + 1);
			m["initial"] = Initial(m["name"].toString());
			m["host"] = p.value("host").toBool();
			m["you"] = you;
			m["live"] = p.value("live").toBool();
			m["voice"] = p.value("voice").toBool();
			m["muted"] = p.value("muted").toBool() || p.value("silenced").toBool();
			m["talking"] = !left_out && talking.value(QString::number(slot)).toBool();
			m["camera"] = !left_out && p.value("camera").toBool();
			const QString avatar = left_out ? QString() : p.value("avatar").toString();
			if(m["voice"].toBool())
				in_voice++;
			if(m["host"].toBool())
				host_name = m["name"].toString();
			if(you)
				own_name = m["name"].toString();

			QVariantMap qml = m;
			qml["avatarUrl"] = left_out ? QString() : p.value("avatarUrl").toString();
			qml["camUrl"] = m["camera"].toBool() ? QStringLiteral("image://coplaycam/%1/%2").arg(slot).arg(cam_serial) : QString();
			players.append(qml);

			QVariantMap page = m;
			page["avatarUrl"] = avatar.isEmpty() ? QString() : QStringLiteral("/avatar/%1").arg(avatar);
			page["camUrl"] = m["camera"].toBool() ? QStringLiteral("/cam/%1.mjpg").arg(slot) : QString();
			page_players.append(page);

			if(!seen_slots.contains(slot) || !seen_names.contains(m["name"].toString()))
			{
				seen_slots.insert(slot);
				if(!seen_names.contains(m["name"].toString()))
					seen_names.append(m["name"].toString());
			}
		}
	}
	d["players"] = players;
	d["playerCount"] = players.size();
	d["inVoice"] = in_voice;
	d["hostName"] = host_name;
	d["ownName"] = own_name;

	// ---- the room and how to get in
	const bool hosting = session && session->IsCoPlayHosting();
	const bool is_public = session && session->GetCoPlayPublic();
	const int max_players = hosting ? settings->GetCoPlayMaxGuests() + 1 : 0;
	d["maxPlayers"] = max_players;
	d["freePlaces"] = max_players > 0 ? std::max(0, max_players - static_cast<int>(players.size())) : -1;
	const QString shown_game = game.isEmpty() && session ? session->GetCoPlayRoomGame() : game;
	d["game"] = shown_game;
	d["roomTitle"] = session ? session->GetCoPlayRoomTitle() : QString();
	d["public"] = is_public;
	const int mix = session ? session->GetCoPlayMixMode() : 0;
	d["mixText"] = mix == 2 ? tr("Only the host plays") : (mix == 1 ? tr("One at a time") : tr("Everybody plays together"));
	const int policy = session ? session->GetCoPlayJoinPolicy() : 0;
	d["policyText"] = policy == 2 ? tr("Join and play") : (policy == 1 ? tr("Join as viewers") : tr("Ask to join"));
	const bool web = hosting && session->GetCoPlayWebGuestsOn();
	d["browsers"] = web;

	QString join_url;
	QVariantList steps;
	const QString room = d["roomTitle"].toString();
	const QString host_for_steps = host_name.isEmpty() ? tr("the host") : host_name;
	const QString last_step = policy == 2 ? tr("Plug in a controller and join – you play right away.")
		: policy == 1 ? tr("Plug in a controller and join – %1 lets you play.").arg(host_for_steps)
		: tr("Plug in a controller and knock – %1 lets you in.").arg(host_for_steps);
	if(is_public && web)
	{
		QString page = session->GetCoPlayWebPage();
		join_url = page.isEmpty() ? QString::fromLatin1(kDefaultPage) : page;
		steps << tr("Open the link below in Chrome, Edge, Firefox or Safari.")
		      << tr("Pick “%1” under Public rooms.").arg(room)
		      << last_step;
		d["joinMode"] = QStringLiteral("web");
	}
	else if(is_public)
	{
		join_url = QString::fromLatin1(kAppPage);
		steps << tr("Get PSRemotePlayTogether from the link below.")
		      << tr("Join → Public rooms → “%1”.").arg(room)
		      << last_step;
		d["joinMode"] = QStringLiteral("app");
	}
	else
	{
		steps << tr("Ask in the chat for an invite.")
		      << tr("%1 sends it to you.").arg(host_for_steps)
		      << tr("Open it with PSRemotePlayTogether or in your browser.");
		d["joinMode"] = QStringLiteral("invite");
	}
	d["steps"] = steps;
	d["joinUrl"] = join_url;
	d["joinShown"] = ShownUrl(join_url);
	BuildQr(join_url);
	d["qrRows"] = qr_rows;

	// ---- the clocks
	const int countdown_minutes = std::max(1, map.value("countdown_minutes").toInt());
	const qint64 countdown_left = countdown_end_ms > now ? countdown_end_ms - now : 0;
	const qint64 countdown_secs = (countdown_left + 999) / 1000;
	d["countdown"] = countdown_end_ms > 0
		? QStringLiteral("%1:%2").arg(countdown_secs / 60, 2, 10, QLatin1Char('0')).arg(countdown_secs % 60, 2, 10, QLatin1Char('0'))
		: QStringLiteral("%1:00").arg(countdown_minutes, 2, 10, QLatin1Char('0'));
	d["countdownDone"] = countdown_end_ms > 0 && countdown_left == 0;
	d["progress"] = countdown_end_ms > 0
		? std::clamp(1.0 - static_cast<double>(countdown_left) / (countdown_minutes * 60000.0), 0.0, 1.0) : 0.0;
	const qint64 break_left = break_end_ms > now ? break_end_ms - now : 0;
	d["breakMinutes"] = static_cast<int>(std::max<qint64>(1, (break_left + 59999) / 60000));
	const qint64 together_ms = session_start_ms > 0 ? (session_end_ms > 0 ? session_end_ms : now) - session_start_ms : 0;
	const int together_min = static_cast<int>(together_ms / 60000);
	d["togetherText"] = together_min >= 60 ? tr("%1 h %2 min").arg(together_min / 60).arg(together_min % 60)
		: tr("%n min", "", std::max(1, together_min));
	d["seenNames"] = seen_names;
	d["seenCount"] = seen_names.size();

	// ---- the connection, for Technical difficulties
	QVariantList rows;
	auto row = [&rows](const QString &what, const QString &state, const QString &tone) {
		QVariantMap r;
		r["what"] = what;
		r["state"] = state;
		r["tone"] = tone;
		rows.append(r);
	};
	if(session && session->IsCoPlayGuest())
		row(tr("Host"), session->IsCoPlayReconnecting() ? tr("Reconnecting…") : tr("Connected"),
			session->IsCoPlayReconnecting() ? QStringLiteral("wait") : QStringLiteral("good"));
	else
		row(tr("Console"), reconnecting ? tr("Reconnecting…") : (session ? tr("Connected") : tr("Not connected")),
			reconnecting ? QStringLiteral("wait") : (session ? QStringLiteral("good") : QStringLiteral("bad")));
	row(tr("Voice chat"), session && session->GetCoPlayVoiceOn() ? tr("Connected") : tr("Off"),
		session && session->GetCoPlayVoiceOn() ? QStringLiteral("good") : QStringLiteral("neutral"));
	row(tr("Players"), tr("%n in the room", "", static_cast<int>(players.size())), QStringLiteral("good"));
	d["connection"] = rows;

	// ---- what the player filled in
	const QString channel = map.value("channel").toString().trimmed();
	d["channelLine"] = channel.isEmpty() ? QString() : QStringLiteral("twitch.tv/%1").arg(channel);
	const QString next = map.value("next_stream").toString().trimmed();
	d["nextLine"] = next.isEmpty() ? QString() : tr("Next stream: %1").arg(next);
	qml_data = d; // fill() reads the players from here
	QVariantMap texts;
	for(const char *key : { "starting_title", "starting_line", "lobby_title", "lobby_line", "brb_title", "brb_line",
		"play_title", "play_line", "difficulties_title", "difficulties_line", "difficulties_footer", "ending_title" })
		texts[QLatin1String(key)] = fill(map.value(key).toString());
	d["texts"] = texts;

	// ---- the game's picture: a fresh one now and then, while a scene shows it
	const QString shown_scene = shown();
	const bool picture_shown = session && (page_clients > 0 || shown_scene == QLatin1String("play")
		|| shown_scene == QLatin1String("brb") || shown_scene == QLatin1String("lobby"));
	if(picture_shown && now - picture_asked_ms > 8000)
	{
		picture_asked_ms = now;
		picture_wanted.storeRelaxed(1);
	}
	const bool has_picture = !game_picture.isEmpty();
	d["hasGamePicture"] = has_picture;

	QVariantMap qml = d;
	qml["gamePicture"] = has_picture
		? QStringLiteral("data:image/jpeg;base64,") + QString::fromLatin1(game_picture.toBase64()) : QString();
	QVariantMap page = d;
	page["players"] = page_players;
	page.remove("qrRows");
	page["qr"] = qr_svg.isEmpty() ? QString() : QStringLiteral("/scene/qr.svg?v=%1").arg(qHash(qr_url));
	page["gamePicture"] = has_picture ? QStringLiteral("/scene/game.jpg?v=%1").arg(picture_serial) : QString();
	// The music, for the pages to play: how loud, and how many of the player's own files
	// (their names stay here: the page asks for them by number)
	QVariantMap music;
	for(const char *id : { "starting", "brb", "ending" })
	{
		const QString s = QString::fromLatin1(id);
		QVariantMap m;
		m["on"] = map.value(QStringLiteral("music_%1_on").arg(s)).toBool();
		m["volume"] = map.value(QStringLiteral("music_%1_volume").arg(s)).toInt();
		const QStringList files = map.value(QStringLiteral("music_%1_files").arg(s)).toStringList();
		const bool own = map.value(QStringLiteral("music_%1_source").arg(s)).toString() == QLatin1String("own") && !files.isEmpty();
		m["own"] = own ? files.size() : 0;
		m["shuffle"] = map.value(QStringLiteral("music_%1_shuffle").arg(s)).toBool();
		// A new list of files: the pages load them anew
		m["serial"] = static_cast<int>(qHash(files.join(QLatin1Char('\n'))) & 0x7fffffff);
		music[s] = m;
	}
	page["music"] = music;

	const QByteArray json = QJsonDocument(QJsonObject::fromVariantMap(page)).toJson(QJsonDocument::Compact);
	// The page's data names the picture by its number, so this covers a new picture too
	const bool changed = json != page_json;
	page_json = json;
	qml_data = qml;
	if(changed)
		emit dataChanged();
}

// ------------------------------------------------------------------ music

void LiveScenes::chooseMusicFiles(const QString &scene)
{
	if(!SceneMusic::HasMusic(scene))
		return;
	const QStringList files = QFileDialog::getOpenFileNames(QApplication::focusWidget(), tr("Music for this scene"), QString(),
		tr("Music (*.mp3 *.ogg *.opus *.flac *.wav)"), nullptr, QFileDialog::DontUseNativeDialog);
	if(files.isEmpty())
		return;
	const QString key = QStringLiteral("music_%1_files").arg(scene);
	QStringList list = map.value(key).toStringList();
	for(const QString &f : files)
	{
		if(!list.contains(f))
			list.append(f);
	}
	set(key, list);
	set(QStringLiteral("music_%1_source").arg(scene), QStringLiteral("own"));
	music_notes.remove(scene);
	emit musicNotesChanged();
}

void LiveScenes::removeMusicFile(const QString &scene, int index)
{
	const QString key = QStringLiteral("music_%1_files").arg(scene);
	QStringList list = map.value(key).toStringList();
	if(index < 0 || index >= list.size())
		return;
	list.removeAt(index);
	set(key, list);
	music_notes.remove(scene);
	emit musicNotesChanged();
}

void LiveScenes::SetMusicNote(const QString &scene, const QString &note)
{
	if(music_notes.value(scene).toString() == note)
		return;
	music_notes[scene] = note;
	emit musicNotesChanged();
}

QByteArray LiveScenes::MusicFile(const QString &scene, const QString &which, QByteArray *type) const
{
	if(!SceneMusic::HasMusic(scene))
		return QByteArray();
	if(which == QLatin1String("builtin"))
	{
		QFile file(QStringLiteral(":/sounds/music-%1.flac").arg(scene));
		*type = "audio/flac";
		return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
	}
	bool ok = false;
	const int index = which.toInt(&ok);
	const QStringList files = map.value(QStringLiteral("music_%1_files").arg(scene)).toStringList();
	if(!ok || index < 0 || index >= files.size())
		return QByteArray();
	// Only the files the player chose for this scene, by their number
	QFile file(files.at(index));
	if(!file.open(QIODevice::ReadOnly) || file.size() > 128 * 1024 * 1024)
		return QByteArray();
	const QString ext = QFileInfo(files.at(index)).suffix().toLower();
	*type = ext == QLatin1String("mp3") ? "audio/mpeg" : ext == QLatin1String("flac") ? "audio/flac"
		: ext == QLatin1String("wav") ? "audio/wav" : "audio/ogg";
	return file.readAll();
}
