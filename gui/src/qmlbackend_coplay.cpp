// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Play Together: joining a session that somebody else shares.

#include "qmlbackend.h"
#include "qmlmainwindow.h"

#include <coplay/client.h>
#include <coplay/invite.h>

#include <QFutureWatcher>
#include <QtConcurrentRun>

void QmlBackend::joinCoPlay(const QString &invite_text, const QString &name, bool controller_only)
{
    if (coplay_joining)
        return;
    if (session) {
        emit coplayJoinFinished(false, tr("Another session is already running."));
        return;
    }

    coplay::ClientConfig config;
    std::string error;
    if (!coplay::DecodeInvite(invite_text.toStdString(), config.invite, &error)) {
        emit coplayJoinFinished(false, QString::fromStdString(error));
        return;
    }
    QString player = name.trimmed().left(static_cast<int>(coplay::kMaxNameLength));
    settings->SetCoPlayLastInvite(invite_text.trimmed());
    settings->SetCoPlayName(player);
    settings->SetCoPlayControllerOnly(controller_only);

    config.name = player.toStdString();
    // Controller only: no picture and no sound, but rumble, haptics and triggers still work
    config.want = controller_only ? coplay::kWantHaptics : coplay::kWantAll;
    config.controller = coplay::ControllerKind::None;
    for (QmlController *controller : qmlControllers()) {
        if (controller->isDualSense() || controller->isDualSenseEdge()) {
            config.controller = coplay::ControllerKind::DualSense;
            break;
        }
        config.controller = coplay::ControllerKind::Other;
    }

    auto client = std::make_shared<coplay::Client>(config);
    QString host_address = QString::fromStdString(config.invite.host);

    coplay_joining = true;
    emit coplayJoiningChanged();

    // Looking up the host and waiting for its answer can take seconds: not on this thread
    auto watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, client, host_address]() {
        QString failure = watcher->result();
        watcher->deleteLater();
        coplay_joining = false;
        emit coplayJoiningChanged();
        if (!failure.isEmpty()) {
            emit coplayJoinFinished(false, failure);
            return;
        }
        if (session) {
            client->Stop();
            emit coplayJoinFinished(false, tr("Another session is already running."));
            return;
        }

        const coplay::WelcomePayload &welcome = client->Welcome();

        bool fullscreen = false, zoom = false, stretch = false;
        switch (settings->GetWindowType()) {
        case WindowType::Fullscreen:
            fullscreen = true;
            break;
        case WindowType::Zoom:
            zoom = true;
            break;
        case WindowType::Stretch:
            stretch = true;
            break;
        default:
            break;
        }
        emit windowTypeUpdated(settings->GetWindowType());

        window->setWindowAdjustable(false);
        wakeup_nickname.clear();
        resume_session = false;

        StreamSessionConnectInfo info(
                settings,
                welcome.stream.ps5 ? CHIAKI_TARGET_PS5_1 : CHIAKI_TARGET_PS4_10,
                host_address,
                QString::fromStdString(welcome.host_name),
                QByteArray(),
                QByteArray(),
                QString(),
                QString(),
                false,
                fullscreen,
                zoom,
                stretch);
        // What we decode is whatever the host's console session produces
        if (welcome.stream.width && welcome.stream.height) {
            info.video_profile.width = welcome.stream.width;
            info.video_profile.height = welcome.stream.height;
        }
        if (welcome.stream.fps)
            info.video_profile.max_fps = welcome.stream.fps;
        info.video_profile.codec = static_cast<ChiakiCodec>(welcome.stream.codec);
        info.enable_dualsense = welcome.stream.dualsense;
        // A guest is never a host at the same time
        info.coplay_host_enabled = false;
        info.coplay_client = client;

        emit coplayJoinFinished(true, QString());
        createSession(info);
    });
    watcher->setFuture(QtConcurrent::run([client]() {
        std::string failure;
        if (client->Connect(&failure))
            return QString();
        return QString::fromStdString(failure.empty() ? "Could not connect" : failure);
    }));
}
