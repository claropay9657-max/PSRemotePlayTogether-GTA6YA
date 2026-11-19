// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Play Together: joining a session that somebody else shares.

#include "qmlbackend.h"
#include "qmlmainwindow.h"
#include "overlayserver.h"

#include <coplay/client.h>
#include <coplay/invite.h>

#include <QClipboard>
#include <QFile>
#include <QFutureWatcher>
#include <QGuiApplication>
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
    // The overlay shows what everybody presses
    if (settings->GetOverlayEnabled())
        config.want |= coplay::kWantInputs;
    // Talking to and seeing the others works with and without the console's picture
    if (settings->GetCoPlayVoiceEnabled())
        config.want |= coplay::kWantVoice;
    if (settings->GetCoPlayShowCams())
        config.want |= coplay::kWantCams;
    // The session corrects this as soon as it has opened the controllers
    config.controller = coplay::ControllerKind::None;
    for (QmlController *controller : qmlControllers()) {
        if (controller->isDualSense() || controller->isDualSenseEdge()) {
            config.controller = coplay::ControllerKind::DualSense;
            break;
        }
        config.controller = coplay::ControllerKind::Other;
    }
    QFile avatar_file(settings->GetCoPlayAvatar());
    if (!settings->GetCoPlayAvatar().isEmpty() && avatar_file.open(QIODevice::ReadOnly)) {
        QByteArray avatar = avatar_file.read(static_cast<qint64>(coplay::kMaxAvatarSize) + 1);
        if (avatar.size() <= static_cast<int>(coplay::kMaxAvatarSize))
            config.avatar.assign(avatar.constBegin(), avatar.constEnd());
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

// ------------------------------------------------------------------ stream overlay

void QmlBackend::initOverlay()
{
    overlay = new OverlayServer(settings, this);
    connect(overlay, &OverlayServer::StateChanged, this, &QmlBackend::overlayStateChanged);
    connect(settings_qml, &QmlSettings::overlayChanged, overlay, &OverlayServer::ApplySettings);
    connect(this, &QmlBackend::sessionChanged, overlay, [this](StreamSession *s) {
        overlay->SetSession(s);
    });
    overlay->ApplySettings();
    initDisplays();
}

QString QmlBackend::overlayUrl() const
{
    return overlay ? overlay->Url() : QString();
}

QString QmlBackend::overlayError() const
{
    return overlay ? overlay->LastError() : QString();
}

void QmlBackend::copyOverlayUrl()
{
    if (!overlayUrl().isEmpty())
        QGuiApplication::clipboard()->setText(overlayUrl());
}
