// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "overlayserver.h"
#include "livescenes.h"
#include "settings.h"
#include "streamsession.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QUrl>

static const int kMaxRequestBytes = 8192;
static const int kTickMs = 16; // one update per frame at 60 fps
static const qint64 kKeepaliveMs = 10000;
static const char kResourceRoot[] = ":/overlay";

OverlayServer::OverlayServer(Settings *settings, QObject *parent)
	: QObject(parent), settings(settings)
{
	connect(&server, &QTcpServer::newConnection, this, &OverlayServer::OnNewConnection);
	timer.setInterval(kTickMs);
	timer.setTimerType(Qt::PreciseTimer);
	connect(&timer, &QTimer::timeout, this, &OverlayServer::Tick);
}

OverlayServer::~OverlayServer()
{
	// abort() reports the disconnect right away, which edits the lists: walk copies
	const QList<QTcpSocket *> sockets = event_clients + voice_clients + cam_clients.keys();
	for(QTcpSocket *socket : sockets)
		socket->abort();
	server.close();
}

QStringList OverlayServer::Skins()
{
	return QDir(QString(kResourceRoot) + "/cryptopad/templates").entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
}

QString OverlayServer::Url() const
{
	if(!server.isListening())
		return QString();
	return QStringLiteral("http://localhost:%1/").arg(server.serverPort());
}

void OverlayServer::ApplySettings()
{
	bool enabled = settings->GetOverlayEnabled();
	int port = settings->GetOverlayPort();
	if(port < 1 || port > 65535)
		port = 9412;

	if(server.isListening() && (!enabled || server.serverPort() != port))
	{
		const QList<QTcpSocket *> sockets = event_clients + voice_clients + cam_clients.keys();
		for(QTcpSocket *socket : sockets)
			socket->disconnectFromHost();
		server.close();
		timer.stop();
	}
	last_error.clear();
	if(enabled && !server.isListening())
	{
		// This computer only: OBS runs here, and nobody else on the network needs to see it
		if(!server.listen(QHostAddress::LocalHost, static_cast<quint16>(port)))
			last_error = server.errorString();
	}

	// Options changed: tell the pages that are open
	QByteArray config = ConfigJson();
	if(config != last_config)
	{
		last_config = config;
		for(QTcpSocket *socket : std::as_const(event_clients))
			SendEvent(socket, "config", config);
	}
	UpdateVoiceTap();
	emit StateChanged();
}

void OverlayServer::SetSession(StreamSession *new_session)
{
	session = new_session;
	UpdateVoiceTap();
}

void OverlayServer::UpdateVoiceTap()
{
	// The session copies the conversation for us only while a page is listening
	bool on = !voice_clients.isEmpty() && settings->GetOverlayVoice();
	bool own = settings->GetOverlayOwnVoice();
	if(session == tap_session && on == tap_on && own == tap_own)
		return;
	if(tap_session && tap_session != session)
		tap_session->CoPlaySetOverlayAudio(false, false);
	tap_session = session;
	tap_on = on;
	tap_own = own;
	if(session)
		session->CoPlaySetOverlayAudio(on, own);
}

void OverlayServer::UpdateTimer()
{
	bool needed = !event_clients.isEmpty() || !cam_clients.isEmpty() || !voice_clients.isEmpty();
	if(needed && !timer.isActive())
		timer.start();
	else if(!needed && timer.isActive())
		timer.stop();
}

QByteArray OverlayServer::ConfigJson() const
{
	QJsonObject o;
	o["players"] = settings->GetOverlayPlayers();
	o["pads"] = settings->GetOverlayPads();
	o["combined"] = settings->GetOverlayCombined();
	o["skin"] = settings->GetOverlaySkin();
	o["scale"] = qBound(10, settings->GetOverlayScale(), 100);
	o["layout"] = settings->GetOverlayLayout() == "column" ? "column" : "row";
	o["cams"] = settings->GetOverlayWebcams();
	o["voice"] = settings->GetOverlayVoice();
	o["battery"] = settings->GetValue("overlay/battery", true).toBool();
	o["notices"] = settings->GetValue("overlay/notices", true).toBool();
	o["chat"] = settings->GetValue("overlay/chat", true).toBool();
	QString style = settings->GetValue("overlay/style", "transparent").toString();
	o["style"] = (style == "glass" || style == "solid") ? style : QString("transparent");
	// Free placement: every player's card and the console's controller sit where they were
	// dragged to, as fractions of the space that is left over (0 = left/top, 1 = right/bottom)
	o["free"] = settings->GetValue("overlay/free", false).toBool();
	o["combined_x"] = qBound(0.0, settings->GetValue("overlay/combined_x", 0.0).toDouble(), 1.0);
	o["combined_y"] = qBound(0.0, settings->GetValue("overlay/combined_y", 1.0).toDouble(), 1.0);
	// Negative: not placed yet, the page lines such cards up along the top
	for(int slot = 0; slot <= 8; slot++)
	{
		QString key = QString("p%1").arg(slot);
		double x = settings->GetValue("overlay/" + key + "_x", -1.0).toDouble();
		double y = settings->GetValue("overlay/" + key + "_y", -1.0).toDouble();
		o[key + "_x"] = x < 0.0 ? -1.0 : qMin(x, 1.0);
		o[key + "_y"] = y < 0.0 ? -1.0 : qMin(y, 1.0);
	}
	o["skins"] = QJsonArray::fromStringList(Skins());
	return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QByteArray OverlayServer::ChatJson() const
{
	QJsonArray chat = session ? session->OverlayChat() : QJsonArray();
	return QJsonDocument(chat).toJson(QJsonDocument::Compact);
}

QByteArray OverlayServer::StateJson() const
{
	QJsonObject state;
	if(session)
		state = session->OverlayState();
	else
		state["active"] = false;
	return QJsonDocument(state).toJson(QJsonDocument::Compact);
}

void OverlayServer::OnNewConnection()
{
	while(QTcpSocket *socket = server.nextPendingConnection())
	{
		pending.insert(socket, QByteArray());
		connect(socket, &QTcpSocket::readyRead, this, [this, socket] { OnReadyRead(socket); });
		connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
			pending.remove(socket);
			event_clients.removeAll(socket);
			if(scene_clients.removeAll(socket) && scenes)
				scenes->SetPageClients(static_cast<int>(scene_clients.size()));
			cam_clients.remove(socket);
			if(voice_clients.removeAll(socket))
				UpdateVoiceTap();
			UpdateTimer();
			socket->deleteLater();
		});
	}
}

void OverlayServer::OnReadyRead(QTcpSocket *socket)
{
	auto it = pending.find(socket);
	if(it == pending.end())
	{
		socket->readAll(); // an event stream: the page has nothing to say
		return;
	}
	it.value().append(socket->readAll());
	int end = it.value().indexOf("\r\n\r\n");
	if(end < 0)
	{
		if(it.value().size() > kMaxRequestBytes)
			socket->abort();
		return;
	}
	QByteArray request_line = it.value().left(it.value().indexOf("\r\n"));
	pending.erase(it);

	QList<QByteArray> parts = request_line.split(' ');
	if(parts.size() < 2)
	{
		Respond(socket, 400, "text/plain", "Bad request");
		return;
	}
	QString target = QString::fromUtf8(parts[1]);
	int query_at = target.indexOf('?');
	QString query;
	if(query_at >= 0)
	{
		query = target.mid(query_at + 1);
		target.truncate(query_at);
	}
	Handle(socket, parts[0], QUrl::fromPercentEncoding(target.toUtf8()), query);
}

static QByteArray MimeType(const QString &path)
{
	if(path.endsWith(".html"))
		return "text/html; charset=utf-8";
	if(path.endsWith(".css"))
		return "text/css; charset=utf-8";
	if(path.endsWith(".js"))
		return "text/javascript; charset=utf-8";
	if(path.endsWith(".svg"))
		return "image/svg+xml";
	if(path.endsWith(".png"))
		return "image/png";
	if(path.endsWith(".webp"))
		return "image/webp";
	if(path.endsWith(".jpg") || path.endsWith(".jpeg"))
		return "image/jpeg";
	return "application/octet-stream";
}

void OverlayServer::Handle(QTcpSocket *socket, const QByteArray &method, const QString &path, const QString &query)
{
	if(method != "GET" && method != "HEAD")
	{
		Respond(socket, 405, "text/plain", "Method not allowed");
		return;
	}
	// Nothing outside the overlay's own files
	if(path.contains("..") || path.contains('\\') || !path.startsWith('/'))
	{
		Respond(socket, 404, "text/plain", "Not found");
		return;
	}

	if(path == "/" || path == "/index.html")
		ServeResource(socket, "/index.html", false);
	// The stream scenes: /scene/starting, /scene/brb, ..., or /scene/current for the one
	// chosen in the stream menu. One page that reads which one from its address.
	else if(path == "/scene.css" || path == "/scene.js")
		ServeResource(socket, path, false);
	else if(path == "/scene/data")
		Respond(socket, 200, "application/json", scenes ? scenes->PageJson() : QByteArray("{}"));
	else if(path == "/scene/qr.svg")
	{
		QByteArray svg = scenes ? (query == QLatin1String("demo=1") ? LiveScenes::DemoQrSvg() : scenes->QrSvg()) : QByteArray();
		if(svg.isEmpty())
			Respond(socket, 404, "text/plain", "Not found");
		else
			Respond(socket, 200, "image/svg+xml", svg);
	}
	else if(path == "/scene/game.jpg")
	{
		QByteArray jpeg = scenes ? scenes->GamePicture() : QByteArray();
		if(jpeg.isEmpty())
			Respond(socket, 404, "text/plain", "Not found");
		else
			Respond(socket, 200, "image/jpeg", jpeg);
	}
	else if(path == "/scene/logo.svg")
		ServeFile(socket, ":/icons/ui/app-icon.svg", "image/svg+xml");
	else if(path == "/fonts/PlusJakartaSans.ttf")
		ServeFile(socket, ":/fonts/PlusJakartaSans.ttf", "font/ttf");
	else if(path.startsWith("/scene/"))
	{
		static const QStringList known = { "starting", "lobby", "brb", "play", "difficulties", "ending", "current" };
		if(known.contains(path.mid(7)))
			ServeResource(socket, "/scene.html", false);
		else
			Respond(socket, 404, "text/plain", "Not found");
	}
	else if(path == "/overlay.css" || path == "/overlay.js" || path == "/pad.js")
		ServeResource(socket, path, false);
	else if(path == "/favicon.ico")
		Respond(socket, 204, "image/x-icon", QByteArray(), true); // browsers ask; there is none
	else if(path == "/layout")
	{
		// The page in arrange mode (?edit=1) reports where things were dragged to.
		// Only placements are accepted, and only as numbers between 0 and 1.
		static const QRegularExpression allowed_key("^(combined|p[0-8])_(x|y)$");
		QVariantMap values;
		const QStringList pairs = query.split('&', Qt::SkipEmptyParts);
		for(const QString &pair : pairs)
		{
			int eq = pair.indexOf('=');
			if(eq <= 0)
				continue;
			QString key = pair.left(eq);
			bool ok = false;
			double value = pair.mid(eq + 1).toDouble(&ok);
			if(ok && allowed_key.match(key).hasMatch() && value >= 0.0 && value <= 1.0)
				values.insert(key, value);
		}
		if(!values.isEmpty())
			emit LayoutEdited(values);
		Respond(socket, 200, "application/json", "{}");
	}
	else if(path == "/config")
		Respond(socket, 200, "application/json", ConfigJson());
	else if(path == "/state")
		Respond(socket, 200, "application/json", StateJson());
	else if(path == "/events")
		StartEvents(socket, query.split('&').contains(QStringLiteral("scene=1")));
	else if(path == "/voice")
		StartVoice(socket);
	else if(path.startsWith("/cam/"))
	{
		// /cam/<slot>.mjpg: that player's webcam as it comes in; /cam/<slot>.jpg: the latest picture
		QString name = path.mid(5);
		bool stream = name.endsWith(".mjpg");
		bool ok = false;
		int slot = name.left(name.lastIndexOf('.')).toInt(&ok);
		if(!ok || slot < 0 || slot > 255 || (!stream && !name.endsWith(".jpg")))
			Respond(socket, 404, "text/plain", "Not found");
		else if(stream)
			StartCam(socket, slot);
		else
		{
			QByteArray jpeg = session ? session->CoPlayCamJpeg(slot) : QByteArray();
			if(jpeg.isEmpty())
				Respond(socket, 404, "text/plain", "Not found");
			else
				Respond(socket, 200, "image/jpeg", jpeg);
		}
	}
	else if(path.startsWith("/avatar/"))
	{
		QByteArray data = session ? session->CoPlayAvatarData(path.mid(8)) : QByteArray();
		if(data.isEmpty())
			Respond(socket, 404, "text/plain", "Not found");
		else
		{
			QByteArray type = "application/octet-stream";
			if(data.startsWith("\x89PNG"))
				type = "image/png";
			else if(data.startsWith("\xff\xd8\xff"))
				type = "image/jpeg";
			else if(data.startsWith("RIFF"))
				type = "image/webp";
			// The address contains the picture's hash, so it never changes
			Respond(socket, 200, type, data, true);
		}
	}
	else if(path.startsWith("/pad/"))
	{
		// /pad/<skin>/index.html and what that page refers to: /pad/<skin>/static/...
		QString rest = path.mid(5);
		int slash = rest.indexOf('/');
		QString skin = slash < 0 ? rest : rest.left(slash);
		QString file = slash < 0 ? QString() : rest.mid(slash);
		if(!Skins().contains(skin))
			Respond(socket, 404, "text/plain", "Unknown skin");
		else if(file.isEmpty() || file == "/" || file == "/index.html")
			ServePad(socket, skin);
		else if(file.startsWith("/static/"))
			ServeResource(socket, "/cryptopad" + file, true);
		else
			Respond(socket, 404, "text/plain", "Not found");
	}
	else
		Respond(socket, 404, "text/plain", "Not found");
}

void OverlayServer::ServeResource(QTcpSocket *socket, const QString &resource, bool cacheable)
{
	QFile file(QString(kResourceRoot) + resource);
	if(!file.open(QIODevice::ReadOnly))
	{
		Respond(socket, 404, "text/plain", "Not found");
		return;
	}
	Respond(socket, 200, MimeType(resource), file.readAll(), cacheable);
}

void OverlayServer::ServePad(QTcpSocket *socket, const QString &skin)
{
	QFile file(QString(kResourceRoot) + "/cryptopad/templates/" + skin + "/index.html");
	if(!file.open(QIODevice::ReadOnly))
	{
		Respond(socket, 404, "text/plain", "Not found");
		return;
	}
	// CryptoPad's own page gets its data from its own server through socket.io.
	// Here the overlay page hands the data in, so swap the scripts.
	QString html = QString::fromUtf8(file.readAll());
	int script = html.indexOf("<script");
	int body_end = html.lastIndexOf("</body>");
	if(script >= 0 && body_end > script)
		html = html.left(script) + "<script src=\"/pad.js\"></script>\n" + html.mid(body_end);
	Respond(socket, 200, "text/html; charset=utf-8", html.toUtf8());
}

void OverlayServer::Respond(QTcpSocket *socket, int status, const QByteArray &content_type, const QByteArray &body, bool cacheable)
{
	QByteArray reason = status == 200 ? "OK" : (status == 204 ? "No Content" : (status == 404 ? "Not Found"
		: (status == 405 ? "Method Not Allowed" : "Bad Request")));
	QByteArray head = "HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\n"
		"Content-Type: " + content_type + "\r\n"
		"Content-Length: " + QByteArray::number(body.size()) + "\r\n"
		"Cache-Control: " + (cacheable ? "max-age=86400" : "no-cache") + "\r\n"
		"Connection: close\r\n\r\n";
	socket->write(head);
	socket->write(body);
	socket->disconnectFromHost();
}

void OverlayServer::SetScenes(LiveScenes *new_scenes)
{
	scenes = new_scenes;
}

void OverlayServer::ServeFile(QTcpSocket *socket, const QString &path, const QByteArray &type)
{
	QFile file(path);
	if(!file.open(QIODevice::ReadOnly))
	{
		Respond(socket, 404, "text/plain", "Not found");
		return;
	}
	Respond(socket, 200, type, file.readAll(), true);
}

void OverlayServer::StartEvents(QTcpSocket *socket, bool scene_page)
{
	socket->write("HTTP/1.1 200 OK\r\n"
		"Content-Type: text/event-stream\r\n"
		"Cache-Control: no-cache\r\n"
		"Connection: keep-alive\r\n\r\n");
	socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
	event_clients.append(socket);
	SendEvent(socket, "config", ConfigJson());
	SendEvent(socket, "state", StateJson());
	SendEvent(socket, "chat", ChatJson());
	if(scene_page && scenes)
	{
		scene_clients.append(socket);
		scenes->SetPageClients(static_cast<int>(scene_clients.size()));
		SendEvent(socket, "scene", scenes->PageJson());
	}
	UpdateTimer();
}

void OverlayServer::StartCam(QTcpSocket *socket, int slot)
{
	// Motion JPEG: every picture replaces the one before, which an <img> shows as video
	socket->write("HTTP/1.1 200 OK\r\n"
		"Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
		"Cache-Control: no-cache\r\n"
		"Connection: close\r\n\r\n");
	socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
	CamClient client;
	client.slot = slot;
	cam_clients.insert(socket, client);
	UpdateTimer();
}

void OverlayServer::StartVoice(QTcpSocket *socket)
{
	// Raw sound without an end: 48 kHz, mono, signed 16 bit little endian
	socket->write("HTTP/1.1 200 OK\r\n"
		"Content-Type: application/octet-stream\r\n"
		"Cache-Control: no-cache\r\n"
		"Connection: close\r\n\r\n");
	socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
	voice_clients.append(socket);
	UpdateVoiceTap();
	UpdateTimer();
}

void OverlayServer::SendEvent(QTcpSocket *socket, const QByteArray &event, const QByteArray &json)
{
	socket->write("event: " + event + "\ndata: " + json + "\n\n");
}

void OverlayServer::Tick()
{
	if(!voice_clients.isEmpty() && session && tap_on)
	{
		QByteArray audio = session->CoPlayReadOverlayAudio();
		if(!audio.isEmpty())
		{
			for(QTcpSocket *socket : std::as_const(voice_clients))
			{
				// Late sound is worse than a gap
				if(socket->bytesToWrite() < 96 * 1024)
					socket->write(audio);
			}
		}
	}
	for(auto it = cam_clients.begin(); it != cam_clients.end(); ++it)
	{
		QTcpSocket *socket = it.key();
		quint64 serial = 0;
		QByteArray jpeg = session ? session->CoPlayCamJpeg(it->slot, &serial) : QByteArray();
		if(jpeg.isEmpty() || serial == it->serial || socket->bytesToWrite() > 512 * 1024)
			continue;
		it->serial = serial;
		socket->write("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: " + QByteArray::number(jpeg.size()) + "\r\n\r\n");
		socket->write(jpeg);
		socket->write("\r\n");
	}

	if(event_clients.isEmpty())
		return;
	// The scenes' data, a few times a second at most: countdowns, who talks
	if(!scene_clients.isEmpty() && scenes && ++scene_tick * kTickMs >= 250)
	{
		scene_tick = 0;
		QByteArray scene = scenes->PageJson();
		if(scene != last_scene)
		{
			last_scene = scene;
			for(QTcpSocket *socket : std::as_const(scene_clients))
			{
				if(socket->bytesToWrite() < 256 * 1024)
					SendEvent(socket, "scene", scene);
			}
		}
	}
	// The chat on its own: it changes seldom, the controllers all the time
	QByteArray chat = ChatJson();
	if(chat != last_chat)
	{
		last_chat = chat;
		for(QTcpSocket *socket : std::as_const(event_clients))
		{
			if(socket->bytesToWrite() < 256 * 1024)
				SendEvent(socket, "chat", chat);
		}
	}
	QByteArray state = StateJson();
	qint64 now = QDateTime::currentMSecsSinceEpoch();
	if(state != last_state)
	{
		last_state = state;
		last_keepalive_ms = now;
		for(QTcpSocket *socket : std::as_const(event_clients))
		{
			// A page that does not keep up gets the next state instead of a growing backlog
			if(socket->bytesToWrite() < 256 * 1024)
				SendEvent(socket, "state", state);
		}
	}
	else if(now - last_keepalive_ms > kKeepaliveMs)
	{
		last_keepalive_ms = now;
		for(QTcpSocket *socket : std::as_const(event_clients))
			socket->write(": keep-alive\n\n");
	}
}
