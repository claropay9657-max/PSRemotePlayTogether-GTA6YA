// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// GTA6YA v0.1: localhost control bridge (disabled unless token configured).
// IMPORTANT: not connected to Cloudflare yet. Never expose this port to the network.

#include "gta6yabridge.h"
#include "qmlbackend.h"
#include "streamsession.h"

#include <coplay/protocol.h>

#include <QByteArray>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QVariantMap>
#include <QDebug>
#include <memory>

namespace {
constexpr quint16 kLocalPort = 38461;
constexpr qsizetype kMaxRequestBytes = 8192;

// Constant-time for equal-length tokens; the bridge is also bound to loopback.
bool equalToken(const QByteArray &a, const QByteArray &b)
{
    if (a.size() != b.size())
        return false;
    unsigned char difference = 0;
    for (qsizetype i = 0; i < a.size(); ++i)
        difference |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    return difference == 0;
}

class Gta6yaBridge final : public QObject
{
public:
    explicit Gta6yaBridge(QmlBackend *backend) : QObject(backend), backend_(backend)
    {
        token_ = qgetenv("GTA6YA_BRIDGE_TOKEN").trimmed();
        // Tokens must be >= 32 chars and have no header controls or spaces.
        if (token_.size() < 32 || token_.size() > 256 || token_.contains(' ') ||
            token_.contains('\r') || token_.contains('\n')) {
            qInfo().noquote() << "GTA6YA bridge disabled. Configure GTA6YA_BRIDGE_TOKEN (>=32 chars).";
            token_.clear();
            return;
        }
        connect(&server_, &QTcpServer::newConnection, this, [this] { onIncoming(); });
        if (!server_.listen(QHostAddress::LocalHost, kLocalPort)) {
            qWarning().noquote() << "GTA6YA bridge not started:" << server_.errorString();
            return;
        }
        qInfo().noquote() << "GTA6YA bridge listening on 127.0.0.1:" << kLocalPort;
    }

private:
    QPointer<QmlBackend> backend_;
    QTcpServer server_;
    QByteArray token_;

    static void reply(QTcpSocket *socket, int code, const QJsonObject &json)
    {
        const QByteArray payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
        const char *reason = code == 200 ? "OK" : code == 400 ? "Bad Request" :
                             code == 401 ? "Unauthorized" : code == 403 ? "Forbidden" :
                             code == 404 ? "Not Found" : code == 409 ? "Conflict" :
                             "Internal Server Error";
        QByteArray head = "HTTP/1.1 " + QByteArray::number(code) + " " + reason + "\r\n";
        head += "Content-Type: application/json; charset=utf-8\r\n";
        head += "Cache-Control: no-store\r\n";
        head += "X-Content-Type-Options: nosniff\r\n";
        head += "Connection: close\r\n";
        head += "Content-Length: " + QByteArray::number(payload.size()) + "\r\n\r\n";
        socket->write(head);
        socket->write(payload);
        socket->disconnectFromHost();
    }

    void onIncoming()
    {
        while (QTcpSocket *socket = server_.nextPendingConnection()) {
            socket->setParent(this);
            auto bytes = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, socket, [this, socket, bytes] {
                *bytes += socket->readAll();
                if (bytes->size() > kMaxRequestBytes) {
                    reply(socket, 400, {{"ok", false}, {"error", "request_too_large"}});
                    return;
                }
                if (!bytes->contains("\r\n\r\n"))
                    return;
                const QByteArray request = *bytes;
                // Do not process a second request on the same socket.
                socket->disconnect(SIGNAL(readyRead()));
                process(socket, request);
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    }

    void process(QTcpSocket *socket, const QByteArray &request)
    {
        const int end = request.indexOf("\r\n\r\n");
        if (end < 0) {
            reply(socket, 400, {{"ok", false}});
            return;
        }
        const QList<QByteArray> lines = request.left(end).split('\n');
        if (lines.isEmpty()) {
            reply(socket, 400, {{"ok", false}});
            return;
        }
        const QList<QByteArray> first = lines.first().trimmed().split(' ');
        if (first.size() != 3 || first[2] != "HTTP/1.1") {
            reply(socket, 400, {{"ok", false}, {"error", "bad_http_request"}});
            return;
        }
        const QByteArray method = first[0];
        const QByteArray path = first[1];
        QByteArray authorization;
        bool hasOrigin = false;
        bool hasBody = false;
        for (qsizetype i = 1; i < lines.size(); ++i) {
            const QByteArray line = lines[i].trimmed();
            const int colon = line.indexOf(':');
            if (colon < 0)
                continue;
            const QByteArray key = line.left(colon).trimmed().toLower();
            const QByteArray val = line.mid(colon + 1).trimmed();
            if (key == "authorization") authorization = val;
            if (key == "origin") hasOrigin = true;
            if (key == "content-length" && val != "0") hasBody = true;
            if (key == "transfer-encoding") hasBody = true;
        }
        // Browser-origin requests are never authorized (no CORS / CSRF).
        if (hasOrigin) {
            reply(socket, 403, {{"ok", false}, {"error", "browser_origin_disallowed"}});
            return;
        }
        if (!authorization.startsWith("Bearer ") ||
            !equalToken(authorization.mid(7), token_)) {
            reply(socket, 401, {{"ok", false}, {"error", "unauthorized"}});
            return;
        }
        if (hasBody) {
            reply(socket, 400, {{"ok", false}, {"error", "body_not_allowed"}});
            return;
        }
        StreamSession *session = backend_ ? backend_->qmlSession() : nullptr;
        const bool hosting = session && session->IsCoPlayHosting();
        if (method == "GET" && path == "/v1/status") {
            QJsonObject data{{"ok", true}, {"hosting", hosting}};
            if (hosting) {
                data.insert("mixMode", session->GetCoPlayMixMode());
                QJsonArray guests;
                for (const QVariant &v : session->GetCoPlayPlayers()) {
                    const QVariantMap p = v.toMap();
                    if (!p.value("host").toBool()) {
                        QJsonObject item;
                        item.insert("slot", p.value("slot").toInt());
                        item.insert("canControl", p.value("canControl").toBool());
                        guests.append(item);
                    }
                }
                data.insert("guests", guests);
            }
            reply(socket, 200, data);
            return;
        }
        if (method != "POST" ||
            (path != "/v1/lock" && path != "/v1/unlock" && path != "/v1/end")) {
            reply(socket, 404, {{"ok", false}, {"error", "not_found"}});
            return;
        }
        if (!hosting) {
            reply(socket, 409, {{"ok", false}, {"error", "no_active_host_session"}});
            return;
        }
        if (path == "/v1/unlock") {
            // Only open control explicitly, after a new invitation has been issued.
            session->SetCoPlayMixMode(static_cast<int>(coplay::MixMode::LastActive));
            reply(socket, 200, {{"ok", true}, {"control", "one_at_a_time"}});
            return;
        }
        // First revoke gameplay control, without stopping or sleeping the PS5.
        session->SetCoPlayMixMode(static_cast<int>(coplay::MixMode::HostOnly));
        if (path == "/v1/lock") {
            reply(socket, 200, {{"ok", true}, {"control", "host_only"}});
            return;
        }
        // /v1/end: remove all guests and rotate the key. Existing links cease to join.
        int kicked = 0;
        for (const QVariant &v : session->GetCoPlayPlayers()) {
            const QVariantMap p = v.toMap();
            if (p.value("host").toBool()) continue;
            const int slot = p.value("slot").toInt();
            if (slot > 0 && slot <= 255) {
                session->coplayKick(slot);
                ++kicked;
            }
        }
        session->coplayNewInvite();
        reply(socket, 200, {{"ok", true}, {"control", "host_only"},
                            {"kicked", kicked}, {"invitesRotated", true}});
    }
};
}  // namespace

void Gta6yaStartBridge(QmlBackend *backend)
{
    if (backend)
        new Gta6yaBridge(backend);
}
