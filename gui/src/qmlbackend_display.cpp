// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// What is shown around the game: the display on top of the game picture
// ("hud") and the OBS overlay. Both are groups of live settings with presets.

#include "qmlbackend.h"
#include "friends.h"
#include <QApplication>
#include <QBuffer>
#include <QDate>
#include <QFileDialog>
#include <QImage>
#include <QStandardPaths>
#include "livecontroller.h"
#include "livescenes.h"
#include "livealerts.h"
#include "devicecheck.h"
#include "keymouse.h"
#include "appupdater.h"
#include "macpermissions.h"
#include "introsound.h"
#include "twitch.h"
#include "vieweractions.h"
#include "twitchelements.h"
#include "overlayserver.h"
#include "presetsettings.h"

#include <QJsonArray>
#include <QJsonObject>

static QJsonObject Preset(const QString &name, std::initializer_list<std::pair<QString, QJsonValue>> values)
{
    QJsonObject o;
    o["name"] = name;
    for (const auto &v : values)
        o[v.first] = v.second;
    return o;
}

void QmlBackend::initDisplays()
{
    // ---- on top of the game picture ----
    // Every element has a switch, a place and a size. Places are fractions of the
    // space that is left over: 0 is the left/top edge, 1 the right/bottom edge.
    QList<PresetSettings::Key> hud_keys = {
        { "players_show", true },
        { "players_x", 0.0, true },
        { "players_y", 0.0, true },
        { "players_scale", 100, true },
        // Carried over from the switch that existed before the other elements did
        { "cams_show", settings->GetCoPlayCamsOnStream() },
        { "cams_x", 1.0, true },
        { "cams_y", 0.0, true },
        { "cams_scale", 100, true },
        { "pads_show", false },
        { "pads_x", 0.0, true },
        { "pads_y", 1.0, true },
        { "pads_scale", 100, true },
        { "combined_show", false },
        { "combined_x", 1.0, true },
        { "combined_y", 1.0, true },
        { "combined_scale", 100, true },
        // What a player's entry shows
        { "avatars", true },
        { "names", true },
        { "talking", true },
        { "battery", true },
        { "vertical", true },
        // "auto": the colour of a white DualSense
        { "skin", QString("auto") },
        // "transparent": nothing behind names and pictures; "glass": see-through boxes; "solid": dark boxes
        { "style", QString("transparent") },
        { "opacity", 100 },
        // Also when playing alone, without anybody else in the session
        { "solo", false },
        // "Pixel joined", "Pixel left"
        { "notices", true },
        // New chat messages for a few seconds, bottom left
        { "chat", true },
        // Everybody's controller as one row, or each one placed by itself
        { "pads_separate", false },
    };
    // Where each player's controller goes when they are placed one by one: along the bottom
    for (int slot = 0; slot <= 8; slot++) {
        hud_keys.append({ QString("pad%1_x").arg(slot), qMin(1.0, slot * 0.2), true });
        hud_keys.append({ QString("pad%1_y").arg(slot), 1.0, true });
        hud_keys.append({ QString("pad%1_scale").arg(slot), 100, true });
    }
    QJsonArray hud_builtin = {
        Preset(tr("Players and webcams"), {}),
        Preset(tr("Everything"), { { "pads_show", true }, { "combined_show", true } }),
        Preset(tr("Everything, in boxes"), { { "pads_show", true }, { "combined_show", true }, { "style", "glass" } }),
        Preset(tr("Players in a row along the top"), { { "vertical", false }, { "cams_show", false }, { "players_x", 0.5 } }),
        Preset(tr("Each controller by itself"), { { "pads_show", true }, { "pads_separate", true }, { "combined_show", true } }),
        Preset(tr("Console controller only"), { { "players_show", false }, { "cams_show", false }, { "combined_show", true }, { "solo", true } }),
        Preset(tr("Nothing"), { { "players_show", false }, { "cams_show", false }, { "notices", false } }),
    };
    hud_settings = new PresetSettings(settings, "hud", hud_keys, hud_builtin, this);

    // ---- the OBS overlay ----
    // The names are the ones these settings always had in the configuration file
    QList<PresetSettings::Key> overlay_keys = {
        { "players", true },
        { "pads", true },
        { "combined", true },
        { "webcams", true },
        { "battery", true },
        { "voice", true },
        { "own_voice", false },
        { "skin", QString("auto") },
        // "transparent": no boxes, the stream shows through; "glass": see-through boxes; "solid": dark boxes
        { "style", QString("transparent") },
        { "scale", 30 },
        { "layout", QString("row") },
        { "notices", true },
        // The latest chat messages, for the viewers
        { "chat", true },
        // Free placement: every player's card and the console's controller where they were dragged to
        { "free", false },
        { "combined_x", 0.0, true },
        { "combined_y", 1.0, true },
    };
    // -1: not placed yet, the page lines such cards up along the top
    for (int slot = 0; slot <= 8; slot++) {
        overlay_keys.append({ QString("p%1_x").arg(slot), -1.0, true });
        overlay_keys.append({ QString("p%1_y").arg(slot), -1.0, true });
    }
    QJsonArray overlay_builtin = {
        Preset(tr("Everything"), {}),
        Preset(tr("Players only"), { { "pads", false }, { "combined", false }, { "webcams", false } }),
        Preset(tr("Players below each other"), { { "pads", false }, { "combined", false }, { "webcams", false }, { "layout", "column" } }),
        Preset(tr("Controllers only"), { { "players", false }, { "webcams", false }, { "voice", false } }),
        Preset(tr("Console controller only"), { { "players", false }, { "pads", false }, { "webcams", false }, { "voice", false } }),
        Preset(tr("Webcams and voices"), { { "pads", false }, { "combined", false } }),
        Preset(tr("Everything, in glass boxes"), { { "style", "glass" } }),
        Preset(tr("Everything, in dark boxes"), { { "style", "solid" } }),
    };
    overlay_settings = new PresetSettings(settings, "overlay", overlay_keys, overlay_builtin, this);

    // The overlay's switches are also ordinary settings that other parts write directly;
    // keep the two views of them in step, in both directions
    connect(settings_qml, &QmlSettings::overlayChanged, overlay_settings, &PresetSettings::Reload);
    connect(overlay_settings, &PresetSettings::changed, this, [this]() {
        overlay->ApplySettings();
        // The Twitch elements are drawn in the overlay's look
        twitch_elements->Refresh();
        emit settings_qml->overlayChanged();
    });
    connect(overlay, &OverlayServer::LayoutEdited, overlay_settings, [this](const QVariantMap &values) {
        for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
            // The Twitch elements keep their places themselves, once for everywhere
            if (it.key().startsWith(QLatin1String("tw_")))
                twitch_elements->SetPlace(it.key().mid(3), it.value().toDouble());
            else
                overlay_settings->set(it.key(), it.value());
        }
    });
}

QObject *QmlBackend::hudSettings() const
{
    return hud_settings;
}

QObject *QmlBackend::liveController() const
{
    return live_controller;
}

QObject *QmlBackend::liveScenes() const
{
    return live_scenes;
}

QObject *QmlBackend::twitchClient() const
{
    return twitch;
}

QObject *QmlBackend::liveAlerts() const
{
    return live_alerts;
}

QObject *QmlBackend::twitchElementsObject() const
{
    return twitch_elements;
}

QObject *QmlBackend::viewerActionsObject() const
{
    return viewer_actions;
}

QObject *QmlBackend::deviceCheckObject() const
{
    return device_check;
}

QObject *QmlBackend::permissionsObject() const
{
    return permissions;
}

QObject *QmlBackend::keyMouseObject() const
{
    return key_mouse;
}

QObject *QmlBackend::updaterObject() const
{
    return app_updater;
}

QObject *QmlBackend::introSoundObject() const
{
    return intro_sound;
}

QObject *QmlBackend::friendsObject() const
{
    return friends_client;
}

QString QmlBackend::friendsSaveBackup(const QString &passphrase)
{
    if (!friends_client || !friends_client->Enabled())
        return tr("Friends are switched off.");
    if (passphrase.size() < 8)
        return tr("The passphrase needs at least 8 characters.");
    const QString start = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        + QStringLiteral("/PSRemotePlayTogether friends %1.psrpt-backup").arg(QDate::currentDate().toString(Qt::ISODate));
    const QString path = QFileDialog::getSaveFileName(QApplication::focusWidget(), tr("Save the friends backup"), start,
        tr("Friends backup (*.psrpt-backup)"), nullptr, QFileDialog::DontUseNativeDialog);
    if (path.isEmpty())
        return QStringLiteral("cancel");
    return friends_client->saveBackup(path, passphrase) ? QString() : tr("The backup could not be saved there.");
}

QString QmlBackend::friendsRestoreBackup(const QString &passphrase)
{
    if (!friends_client)
        return tr("Friends are not available.");
    const QString path = QFileDialog::getOpenFileName(QApplication::focusWidget(), tr("Open a friends backup"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), tr("Friends backup (*.psrpt-backup);;All files (*)"),
        nullptr, QFileDialog::DontUseNativeDialog);
    if (path.isEmpty())
        return QStringLiteral("cancel");
    return friends_client->restoreBackup(path, passphrase) ? QString()
        : tr("This backup does not open with that passphrase, or it is no friends backup.");
}

void QmlBackend::updateFriendsActivity()
{
    if (!friends_client)
        return;
    if (!session) {
        friends_client->SetActivity(QString(), QVariantMap());
        return;
    }
    // The game: what the console of the session says it runs, or the room's, or this computer
    QString game;
    QString console = session_info.host.isEmpty() ? QString() : tr("PS5");
    for (const auto &host : discovery_manager.GetHosts()) {
        if (host.host_addr == session_info.host && host.state == CHIAKI_DISCOVERY_HOST_STATE_READY) {
            game = host.running_app_name;
            console = host.ps5 ? tr("PS5") : tr("PS4");
            break;
        }
    }
    if (game.isEmpty())
        game = session->GetCoPlayRoomGame();
    if (session->IsPcSource())
        console = tr("this computer");

    QString activity;
    QVariantMap room;
    if (session->IsCoPlayHosting()) {
        const int players = static_cast<int>(session->GetCoPlayPlayers().size());
        const int max = session->GetCoPlayMaxGuests() + 1;
        const QString title = session->GetCoPlayRoomTitle().isEmpty()
            ? tr("%1's session").arg(settings->GetCoPlayName().isEmpty() ? tr("A friend") : settings->GetCoPlayName())
            : session->GetCoPlayRoomTitle();
        if (session->GetCoPlayTalkOnly())
            activity = tr("Just talking (%1 / %2)").arg(players).arg(max);
        else if (!game.isEmpty())
            activity = tr("Playing %1 with friends (%2 / %3)").arg(game).arg(players).arg(max);
        else
            activity = tr("Hosting %1 (%2 / %3 players)").arg(title).arg(players).arg(max);
        // Friends may join while there is room and the session is not locked: the same card as
        // an invite in a chat
        if (!session->GetCoPlayLocked() && players < max && !session->GetCoPlayInvite().isEmpty()) {
            room = session->GetCoPlayInviteCard();
            if (room.value(QStringLiteral("game")).toString().isEmpty())
                room[QStringLiteral("game")] = game;
        }
    } else if (session->IsCoPlayGuest()) {
        activity = game.isEmpty() ? tr("In a session with friends") : tr("Playing %1 with friends").arg(game);
    } else {
        activity = game.isEmpty() ? tr("Playing on %1").arg(console) : tr("Playing %1 on %2").arg(game, console);
    }
    friends_client->SetActivity(activity, room);
}

void QmlBackend::updateFriendsProfile()
{
    if (!friends_client)
        return;
    // One switch for the previews of links in every chat (Settings, Voice & webcam)
    friends_client->SetLinkPreviews(settings->GetCoPlayChatPreviews());
    // A small picture: the card goes into every request and has to stay small
    QByteArray small;
    QImage image(settings->GetCoPlayAvatar());
    if (!settings->GetCoPlayAvatar().isEmpty() && !image.isNull()) {
        image = image.scaled(72, 72, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)
                    .convertToFormat(QImage::Format_RGB32);
        for (int quality = 85; quality >= 35; quality -= 10) {
            small.clear();
            QBuffer buffer(&small);
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, "JPEG", quality);
            if (small.size() <= 4 * 1024)
                break;
        }
        if (small.size() > 4 * 1024)
            small.clear();
    }
    friends_client->SetProfile(settings->GetCoPlayName(), small);
}

QObject *QmlBackend::overlayLayoutSettings() const
{
    return overlay_settings;
}

bool QmlBackend::hudInline() const
{
    // A see-through, click-through window over the game costs the stream nothing, but it only
    // shows reliably on macOS: on Windows such a window stays invisible. Everywhere else the
    // display is drawn into the stream scene, which is laid over the game like the stream menu
    // (only with Vulkan, see StreamView.hudInline).
    // PSRPT_HUD=inline or PSRPT_HUD=window picks one regardless, for trying the other way.
    const QByteArray forced = qgetenv("PSRPT_HUD").trimmed().toLower();
    if (forced == "inline")
        return true;
    if (forced == "window")
        return false;
#ifdef Q_OS_MACOS
    return false;
#else
    return true;
#endif
}
