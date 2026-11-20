// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "coplaymeet.h"

#include "coplayaddress.h"

#include <coplay/stun.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <memory>

// An answer of a meeting point is a few lines. Whatever is longer is something else.
static const qint64 kMaxAnswerSize = 64 * 1024;

std::string CoPlayMeet::DefaultUrl()
{
	const QString replaced = qEnvironmentVariable("PSRPT_MEET_URL").trimmed();
	if(!replaced.isEmpty())
	{
		std::string url = coplay::MeetUrlFromText(replaced.toStdString());
		if(!url.empty())
			return url;
	}
	return coplay::kDefaultMeetUrl;
}

bool CoPlayMeet::Allowed(const QString &meet_setting)
{
	return !coplay::MeetIsOff(meet_setting.toStdString());
}

/** The servers the setting names, as for the invite's address (coplayaddress.cpp). */
static std::vector<coplay::StunServer> StunServers(const QString &stun_setting)
{
	if(CoPlayPublicAddress::IsOff(stun_setting))
		return {};
	if(stun_setting.trimmed().isEmpty())
		return coplay::DefaultStunServers();
	coplay::StunServer server;
	if(coplay::ParseStunServer(stun_setting.toStdString(), server))
		return { server };
	return {};
}

coplay::MeetConfig CoPlayMeet::ForHost(const QString &meet_setting, const QString &stun_setting)
{
	coplay::MeetConfig config;
	config.url = coplay::MeetUrlFromText(meet_setting.toStdString(), DefaultUrl());
	if(config.url.empty())
		return config;
	config.post = &CoPlayMeet::Post;
	config.stun_servers = StunServers(stun_setting);
	return config;
}

coplay::MeetConfig CoPlayMeet::ForGuest(const QString &meet_setting, const QString &stun_setting, const coplay::Invite &invite)
{
	coplay::MeetConfig config;
	if(!Allowed(meet_setting))
		return config;
	// Where the host is waiting: the invite knows, not our own setting
	config.url = coplay::MeetUrlFromInvite(invite.meet, DefaultUrl());
	if(config.url.empty())
		return config;
	config.post = &CoPlayMeet::Post;
	config.stun_servers = StunServers(stun_setting);
	return config;
}

std::string CoPlayMeet::InviteValue(const QString &meet_setting)
{
	const std::string standard = DefaultUrl();
	return coplay::MeetUrlToInvite(coplay::MeetUrlFromText(meet_setting.toStdString(), standard), standard);
}

bool CoPlayMeet::Post(const coplay::HttpRequest &request, coplay::HttpResponse &response, std::string *error)
{
	auto fail = [error](const QString &why) {
		if(error)
			*error = why.toStdString();
		return false;
	};

	// One per thread: it belongs to the thread that made it, and it keeps the
	// connection open, so that a host's request every few seconds does not start
	// with a new TLS handshake each time
	static thread_local std::unique_ptr<QNetworkAccessManager> network;
	if(!network)
		network = std::make_unique<QNetworkAccessManager>();

	const QUrl url(QString::fromStdString(request.url));
	if(!url.isValid() || (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http")))
		return fail(QStringLiteral("\"%1\" is no address of a meeting point").arg(QString::fromStdString(request.url)));

	QNetworkRequest net_request(url);
	net_request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
	net_request.setHeader(QNetworkRequest::UserAgentHeader,
		QStringLiteral("PSRemotePlayTogether/%1").arg(QCoreApplication::applicationVersion()));
	net_request.setTransferTimeout(static_cast<int>(request.timeout_ms));
	// No cookies in either direction: a meeting point has no business recognising anybody
	net_request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
	net_request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);

	std::unique_ptr<QNetworkReply> reply(network->post(net_request, QByteArray::fromStdString(request.body)));
	QEventLoop loop;
	QObject::connect(reply.get(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
	// The session may end while we wait: look for that often
	QTimer watch;
	watch.setInterval(40);
	QObject::connect(&watch, &QTimer::timeout, &loop, [&request, &reply] {
		if(request.cancel && request.cancel->load())
			reply->abort();
	});
	watch.start();
	if(!reply->isFinished())
		loop.exec();
	watch.stop();

	const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
	if(!status)
	{
		if(request.cancel && request.cancel->load())
			return fail(QStringLiteral("given up"));
		return fail(reply->errorString());
	}
	response.status = status;
	response.body = reply->read(kMaxAnswerSize).toStdString();
	return true;
}
