// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "websocketclient.h"

#include <QCryptographicHash>
#include <QRandomGenerator>

namespace {

constexpr int kOpContinuation = 0x0;
constexpr int kOpText = 0x1;
constexpr int kOpBinary = 0x2;
constexpr int kOpClose = 0x8;
constexpr int kOpPing = 0x9;
constexpr int kOpPong = 0xa;
/** EventSub messages are small; anything this large is not one. */
constexpr qint64 kMaxMessage = 4 * 1024 * 1024;

} // namespace

WebSocketClient::WebSocketClient(QObject *parent)
	: QObject(parent)
{
	connect_timer.setSingleShot(true);
	connect_timer.setInterval(15000);
	connect(&connect_timer, &QTimer::timeout, this, [this] { OnGone(QStringLiteral("timed out")); });
}

WebSocketClient::~WebSocketClient()
{
	if(socket)
		socket->abort();
}

void WebSocketClient::Open(const QUrl &new_url)
{
	Close();
	url = new_url;
	buffer.clear();
	message.clear();
	socket = new QSslSocket(this);
	connect(socket, &QSslSocket::readyRead, this, &WebSocketClient::OnReadyRead);
	connect(socket, &QAbstractSocket::disconnected, this, [this] { OnGone(QStringLiteral("closed by the server")); });
	connect(socket, &QAbstractSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
		OnGone(socket ? socket->errorString() : QString());
	});
	const bool tls = url.scheme() == QLatin1String("wss");
	state = Connecting;
	connect_timer.start();
	if(tls)
	{
		connect(socket, &QSslSocket::encrypted, this, &WebSocketClient::OnConnected);
		socket->connectToHostEncrypted(url.host(), static_cast<quint16>(url.port(443)));
	}
	else
	{
		connect(socket, &QAbstractSocket::connected, this, &WebSocketClient::OnConnected);
		socket->connectToHost(url.host(), static_cast<quint16>(url.port(80)));
	}
}

void WebSocketClient::Close()
{
	connect_timer.stop();
	if(!socket)
		return;
	QSslSocket *s = socket;
	socket = nullptr;
	s->disconnect(this);
	if(state == Open_)
	{
		// A polite goodbye; the server closes its side
		QByteArray frame;
		frame.append(char(0x80 | kOpClose));
		frame.append(char(0x80));
		frame.append(4, char(0));
		s->write(frame);
		s->flush();
	}
	s->abort();
	s->deleteLater();
	state = Closed;
}

void WebSocketClient::OnConnected()
{
	if(!socket)
		return;
	state = Handshake;
	QByteArray nonce(16, 0);
	for(char &c : nonce)
		c = static_cast<char>(QRandomGenerator::global()->bounded(256));
	key = nonce.toBase64();
	QString path = url.path(QUrl::FullyEncoded);
	if(path.isEmpty())
		path = QStringLiteral("/");
	if(url.hasQuery())
		path += QLatin1Char('?') + url.query(QUrl::FullyEncoded);
	const bool default_port = url.port() < 0;
	const QByteArray host = url.host().toUtf8() + (default_port ? QByteArray() : ":" + QByteArray::number(url.port()));
	QByteArray request = "GET " + path.toUtf8() + " HTTP/1.1\r\n"
		"Host: " + host + "\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Key: " + key + "\r\n"
		"Sec-WebSocket-Version: 13\r\n"
		"User-Agent: PSRemotePlayTogether\r\n\r\n";
	socket->write(request);
}

void WebSocketClient::OnReadyRead()
{
	if(!socket)
		return;
	buffer.append(socket->readAll());
	if(state == Handshake)
	{
		const int end = buffer.indexOf("\r\n\r\n");
		if(end < 0)
		{
			if(buffer.size() > 16 * 1024)
				OnGone(QStringLiteral("bad handshake"));
			return;
		}
		const QByteArray head = buffer.left(end);
		buffer.remove(0, end + 4);
		const QByteArray expected = QCryptographicHash::hash(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", QCryptographicHash::Sha1).toBase64();
		if(!head.startsWith("HTTP/1.1 101") || !head.contains(expected))
		{
			OnGone(QStringLiteral("the server did not switch to WebSocket: %1").arg(QString::fromUtf8(head.left(head.indexOf('\r')))));
			return;
		}
		connect_timer.stop();
		state = Open_;
		emit Connected();
	}
	if(state == Open_)
	{
		while(ParseFrames())
		{
		}
	}
}

bool WebSocketClient::ParseFrames()
{
	if(buffer.size() < 2)
		return false;
	const auto b0 = static_cast<uint8_t>(buffer[0]);
	const auto b1 = static_cast<uint8_t>(buffer[1]);
	const bool fin = b0 & 0x80;
	const int opcode = b0 & 0x0f;
	const bool masked = b1 & 0x80;
	qint64 length = b1 & 0x7f;
	int at = 2;
	if(length == 126)
	{
		if(buffer.size() < 4)
			return false;
		length = (static_cast<uint8_t>(buffer[2]) << 8) | static_cast<uint8_t>(buffer[3]);
		at = 4;
	}
	else if(length == 127)
	{
		if(buffer.size() < 10)
			return false;
		length = 0;
		for(int i = 2; i < 10; i++)
			length = (length << 8) | static_cast<uint8_t>(buffer[i]);
		at = 10;
	}
	if(length > kMaxMessage)
	{
		OnGone(QStringLiteral("message too large"));
		return false;
	}
	QByteArray mask;
	if(masked)
	{
		if(buffer.size() < at + 4)
			return false;
		mask = buffer.mid(at, 4);
		at += 4;
	}
	if(buffer.size() < at + length)
		return false;
	QByteArray payload = buffer.mid(at, static_cast<int>(length));
	buffer.remove(0, at + static_cast<int>(length));
	if(masked)
	{
		for(int i = 0; i < payload.size(); i++)
			payload[i] = static_cast<char>(payload[i] ^ mask[i % 4]);
	}

	switch(opcode)
	{
		case kOpText:
		case kOpBinary:
			message = payload;
			message_opcode = opcode;
			if(fin)
			{
				if(message_opcode == kOpText)
					emit TextReceived(message);
				message.clear();
			}
			break;
		case kOpContinuation:
			message.append(payload);
			if(message.size() > kMaxMessage)
			{
				OnGone(QStringLiteral("message too large"));
				return false;
			}
			if(fin)
			{
				if(message_opcode == kOpText)
					emit TextReceived(message);
				message.clear();
			}
			break;
		case kOpPing:
			SendFrame(kOpPong, payload);
			break;
		case kOpPong:
			break;
		case kOpClose:
			OnGone(QStringLiteral("closed by the server (%1)").arg(payload.size() >= 2
				? (static_cast<uint8_t>(payload[0]) << 8) | static_cast<uint8_t>(payload[1]) : 0));
			return false;
		default:
			break;
	}
	return state == Open_;
}

void WebSocketClient::SendText(const QByteArray &utf8)
{
	SendFrame(kOpText, utf8);
}

void WebSocketClient::SendFrame(int opcode, const QByteArray &payload)
{
	if(!socket || state != Open_)
		return;
	// A client masks everything it sends
	QByteArray frame;
	frame.append(static_cast<char>(0x80 | opcode));
	const qint64 n = payload.size();
	if(n < 126)
		frame.append(static_cast<char>(0x80 | n));
	else if(n < 65536)
	{
		frame.append(static_cast<char>(0x80 | 126));
		frame.append(static_cast<char>((n >> 8) & 0xff));
		frame.append(static_cast<char>(n & 0xff));
	}
	else
	{
		frame.append(static_cast<char>(0x80 | 127));
		for(int i = 7; i >= 0; i--)
			frame.append(static_cast<char>((n >> (8 * i)) & 0xff));
	}
	QByteArray mask(4, 0);
	for(char &c : mask)
		c = static_cast<char>(QRandomGenerator::global()->bounded(256));
	frame.append(mask);
	QByteArray body = payload;
	for(int i = 0; i < body.size(); i++)
		body[i] = static_cast<char>(body[i] ^ mask[i % 4]);
	frame.append(body);
	socket->write(frame);
}

void WebSocketClient::OnGone(const QString &reason)
{
	if(state == Closed)
		return;
	connect_timer.stop();
	if(socket)
	{
		QSslSocket *s = socket;
		socket = nullptr;
		s->disconnect(this);
		s->abort();
		s->deleteLater();
	}
	state = Closed;
	emit Disconnected(reason);
}
