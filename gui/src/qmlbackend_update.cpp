// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Tells the user of an older build that a newer release exists. Once per start
// the app asks GitHub for the project's latest release and compares version
// numbers. It downloads and installs nothing by itself: it says so, and hands
// the release to AppUpdater (appupdater.h), which updates the app when the user
// says so, or offers the release page in the browser where the app cannot
// replace itself. Settings > Updates & logs has the switch for the check.

#include "qmlbackend.h"
#include "appupdater.h"
#include "qmlmainwindow.h"
#include "settings.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QVersionNumber>

static const QString kReleasesPage = QStringLiteral("https://github.com/Crypto90/PSRemotePlayTogether/releases");
static const QString kLatestRelease = QStringLiteral("https://api.github.com/repos/Crypto90/PSRemotePlayTogether/releases/latest");

void QmlBackend::checkForUpdate()
{
    if (!settings->GetUpdateCheckEnabled())
        return;

    // PSRPT_UPDATE_CHECK_URL: another address that answers like GitHub's "latest release", for trying this out
    const QString address = qEnvironmentVariable("PSRPT_UPDATE_CHECK_URL", kLatestRelease);
    QNetworkRequest request{QUrl(address)};
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("PSRemotePlayTogether/%1").arg(QCoreApplication::applicationVersion()));
    request.setTransferTimeout(10000);

    auto network = new QNetworkAccessManager(this);
    QNetworkReply *reply = network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, network, address]() {
        reply->deleteLater();
        network->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            // No connection, or no release yet: nothing to tell the user
            qCInfo(chiakiGui) << "Update check: no answer," << reply->errorString();
            return;
        }
        const QJsonObject release = QJsonDocument::fromJson(reply->readAll()).object();
        if (release.value("draft").toBool() || release.value("prerelease").toBool())
            return;
        QString tag = release.value("tag_name").toString(); // "v1.2.3"
        if (tag.startsWith('v'))
            tag.remove(0, 1);
        qsizetype parsed = 0;
        const QVersionNumber latest = QVersionNumber::fromString(tag, &parsed);
        // "1.2.3-rc1" and anything else that is not a plain version number is not offered
        if (latest.isNull() || parsed != tag.size())
            return;
        const QVersionNumber mine = QVersionNumber::fromString(QCoreApplication::applicationVersion());
        if (QVersionNumber::compare(latest, mine) <= 0) {
            qCInfo(chiakiGui) << "Update check: this is the latest version," << mine.toString();
            return;
        }

        update_version = latest.toString();
        // Only ever a page of the project's own releases: the answer is not trusted with more
        const QString page = release.value("html_url").toString();
        update_url = page.startsWith(kReleasesPage + '/') ? page : kReleasesPage;
        // Its files and its notes, for updating from inside the app. Asked of a server on this
        // computer for trying it out, files from that server are followed too, and from no other:
        // the variable cannot point the app at files somewhere else
        QString also;
        if (qEnvironmentVariableIsSet("PSRPT_UPDATE_CHECK_URL")) {
            const QUrl asked(address);
            const QString host = asked.host();
            if (host == QLatin1String("127.0.0.1") || host == QLatin1String("localhost") || host == QLatin1String("::1"))
                also = asked.scheme() + QStringLiteral("://") + asked.authority() + QLatin1Char('/');
        }
        app_updater->SetRelease(update_version, release.value("assets").toArray(), release.value("body").toString(), also);
        qCInfo(chiakiGui) << "Update check: version" << update_version << "is available, this is" << mine.toString();
        emit updateChanged();
        // "Later" was said to this version already: the start page still shows it, nothing asks again
        if (settings->GetUpdateSkippedVersion() != update_version)
            emit updateFound(update_version);
    });
}

void QmlBackend::openUpdatePage()
{
    QDesktopServices::openUrl(QUrl(update_url.isEmpty() ? kReleasesPage : update_url));
}

void QmlBackend::openLocalNetworkSettings()
{
#ifdef __APPLE__
    QDesktopServices::openUrl(QUrl(QStringLiteral("x-apple.systempreferences:com.apple.preference.security?Privacy_LocalNetwork")));
#endif
}

void QmlBackend::skipUpdate()
{
    if (!update_version.isEmpty())
        settings->SetUpdateSkippedVersion(update_version);
}
