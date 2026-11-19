// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "overlayserver.h"
#include "settings.h"
#include "streamsession.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
	for(QTcpSocket *socket : std::as_const(event_clients))
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
		for(QTcpSocket *socket : std::as_const(event_clients))
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
	emit StateChanged();
}

void OverlayServer::SetSession(StreamSession *new_session)
{
	session = new_session;
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
	o["skins"] = QJsonArray::fromStringList(Skins());
	return QJsonDocument(o).toJson(QJsonDocument::Compact);
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
			if(event_clients.isEmpty())
				timer.stop();
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
	int query = target.indexOf('?');
	if(query >= 0)
		target.truncate(query);
	Handle(socket, parts[0], QUrl::fromPercentEncoding(target.toUtf8()));
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

void OverlayServer::Handle(QTcpSocket *socket, const QByteArray &method, const QString &path)
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
	else if(path == "/overlay.css" || path == "/overlay.js" || path == "/pad.js")
		ServeResource(socket, path, false);
	else if(path == "/favicon.ico")
		Respond(socket, 204, "image/x-icon", QByteArray(), true); // browsers ask; there is none
	else if(path == "/config")
		Respond(socket, 200, "application/json", ConfigJson());
	else if(path == "/state")
		Respond(socket, 200, "application/json", StateJson());
	else if(path == "/events")
		StartEvents(socket);
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

void OverlayServer::StartEvents(QTcpSocket *socket)
{
	socket->write("HTTP/1.1 200 OK\r\n"
		"Content-Type: text/event-stream\r\n"
		"Cache-Control: no-cache\r\n"
		"Connection: keep-alive\r\n\r\n");
	socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
	event_clients.append(socket);
	SendEvent(socket, "config", ConfigJson());
	SendEvent(socket, "state", StateJson());
	if(!timer.isActive())
		timer.start();
}

void OverlayServer::SendEvent(QTcpSocket *socket, const QByteArray &event, const QByteArray &json)
{
	socket->write("event: " + event + "\ndata: " + json + "\n\n");
}

void OverlayServer::Tick()
{
	if(event_clients.isEmpty())
		return;
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
