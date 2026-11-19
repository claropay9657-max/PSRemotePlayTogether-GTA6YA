// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Play Together settings as seen by the QML side.

#include "qmlsettings.h"

#include <coplay/invite.h>
#include <coplay/socket.h>

#include "overlayserver.h"
#include "coplaycamera.h"

#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QStandardPaths>
#include <QUrl>

bool QmlSettings::coplayHostEnabled() const
{
    return settings->GetCoPlayHostEnabled();
}

void QmlSettings::setCoplayHostEnabled(bool enabled)
{
    settings->SetCoPlayHostEnabled(enabled);
    emit coplayChanged();
}

int QmlSettings::coplayPort() const
{
    return settings->GetCoPlayPort();
}

void QmlSettings::setCoplayPort(int port)
{
    if (port < 1 || port > 65535)
        return;
    settings->SetCoPlayPort(port);
    emit coplayChanged();
}

QString QmlSettings::coplayAddress() const
{
    return settings->GetCoPlayAddress();
}

void QmlSettings::setCoplayAddress(const QString &address)
{
    settings->SetCoPlayAddress(address.trimmed());
    emit coplayChanged();
}

QString QmlSettings::coplayName() const
{
    return settings->GetCoPlayName();
}

void QmlSettings::setCoplayName(const QString &name)
{
    settings->SetCoPlayName(name.trimmed().left(static_cast<int>(coplay::kMaxNameLength)));
    emit coplayChanged();
}

int QmlSettings::coplayMixMode() const
{
    return settings->GetCoPlayMixMode();
}

void QmlSettings::setCoplayMixMode(int mode)
{
    settings->SetCoPlayMixMode(mode);
    emit coplayChanged();
}

QString QmlSettings::coplayLastInvite() const
{
    return settings->GetCoPlayLastInvite();
}

void QmlSettings::setCoplayLastInvite(const QString &invite)
{
    settings->SetCoPlayLastInvite(invite.trimmed());
    emit coplayChanged();
}

bool QmlSettings::coplayControllerOnly() const
{
    return settings->GetCoPlayControllerOnly();
}

void QmlSettings::setCoplayControllerOnly(bool enabled)
{
    settings->SetCoPlayControllerOnly(enabled);
    emit coplayChanged();
}

QString QmlSettings::coplayLocalAddress() const
{
    return QString::fromStdString(coplay::GuessLocalAddress());
}

QString QmlSettings::coplayInvite() const
{
    coplay::Invite invite;
    if (!coplay::SecretFromString(settings->GetCoPlaySecret().toStdString(), invite.secret)) {
        // First use: there is no key yet
        invite.secret = coplay::GenerateSecret();
        settings->SetCoPlaySecret(QString::fromStdString(coplay::SecretToString(invite.secret)));
    }
    QString address = settings->GetCoPlayAddress();
    invite.host = address.isEmpty() ? coplay::GuessLocalAddress() : address.toStdString();
    if (invite.host.empty())
        invite.host = "127.0.0.1";
    int port = settings->GetCoPlayPort();
    invite.port = static_cast<uint16_t>(port > 0 && port < 65536 ? port : coplay::kDefaultPort);
    return QString::fromStdString(coplay::EncodeInvite(invite));
}

void QmlSettings::coplayNewKey()
{
    settings->SetCoPlaySecret(QString::fromStdString(coplay::SecretToString(coplay::GenerateSecret())));
    emit coplayChanged();
}

void QmlSettings::coplayCopyInvite()
{
    QGuiApplication::clipboard()->setText(coplayInvite());
}

// ------------------------------------------------------------------ profile picture

QString QmlSettings::coplayAvatarUrl() const
{
    QString path = settings->GetCoPlayAvatar();
    QFileInfo info(path);
    if (path.isEmpty() || !info.exists())
        return QString();
    // The time stamp makes the view load the new picture when the file is replaced
    return QUrl::fromLocalFile(path).toString() + "?t=" + QString::number(info.lastModified().toMSecsSinceEpoch());
}

void QmlSettings::coplayChooseAvatar()
{
    QString file = QFileDialog::getOpenFileName(QApplication::focusWidget(), tr("Choose Your Picture"),
                                                QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
                                                tr("Images (*.png *.jpg *.jpeg *.webp *.bmp *.gif)"),
                                                nullptr,
                                                QFileDialog::DontUseNativeDialog);
    if (file.isEmpty())
        return;
    QImageReader reader(file);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull())
        return;

    // A square from the middle, small enough to send to the other players in an instant
    int side = qMin(image.width(), image.height());
    image = image.copy((image.width() - side) / 2, (image.height() - side) / 2, side, side)
                .scaled(160, 160, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                .convertToFormat(QImage::Format_RGB32);
    QByteArray bytes;
    for (int quality = 88; quality >= 40; quality -= 12) {
        bytes.clear();
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "JPEG", quality);
        if (bytes.size() <= static_cast<int>(coplay::kMaxAvatarSize))
            break;
    }
    if (bytes.isEmpty() || bytes.size() > static_cast<int>(coplay::kMaxAvatarSize))
        return;

    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    QString path = dir + "/player-picture.jpg";
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    out.write(bytes);
    out.close();
    settings->SetCoPlayAvatar(path);
    emit coplayChanged();
}

void QmlSettings::coplayClearAvatar()
{
    settings->SetCoPlayAvatar(QString());
    emit coplayChanged();
}

// ------------------------------------------------------------------ stream overlay

bool QmlSettings::overlayEnabled() const
{
    return settings->GetOverlayEnabled();
}

void QmlSettings::setOverlayEnabled(bool enabled)
{
    settings->SetOverlayEnabled(enabled);
    emit overlayChanged();
}

int QmlSettings::overlayPort() const
{
    return settings->GetOverlayPort();
}

void QmlSettings::setOverlayPort(int port)
{
    if (port < 1 || port > 65535)
        return;
    settings->SetOverlayPort(port);
    emit overlayChanged();
}

bool QmlSettings::overlayPlayers() const
{
    return settings->GetOverlayPlayers();
}

void QmlSettings::setOverlayPlayers(bool on)
{
    settings->SetOverlayPlayers(on);
    emit overlayChanged();
}

bool QmlSettings::overlayPads() const
{
    return settings->GetOverlayPads();
}

void QmlSettings::setOverlayPads(bool on)
{
    settings->SetOverlayPads(on);
    emit overlayChanged();
}

bool QmlSettings::overlayCombined() const
{
    return settings->GetOverlayCombined();
}

void QmlSettings::setOverlayCombined(bool on)
{
    settings->SetOverlayCombined(on);
    emit overlayChanged();
}

QString QmlSettings::overlaySkin() const
{
    return settings->GetOverlaySkin();
}

void QmlSettings::setOverlaySkin(const QString &skin)
{
    settings->SetOverlaySkin(skin);
    emit overlayChanged();
}

int QmlSettings::overlayScale() const
{
    return settings->GetOverlayScale();
}

void QmlSettings::setOverlayScale(int percent)
{
    settings->SetOverlayScale(qBound(10, percent, 100));
    emit overlayChanged();
}

QString QmlSettings::overlayLayout() const
{
    return settings->GetOverlayLayout();
}

void QmlSettings::setOverlayLayout(const QString &layout)
{
    settings->SetOverlayLayout(layout == "column" ? "column" : "row");
    emit overlayChanged();
}

QStringList QmlSettings::overlaySkins() const
{
    return OverlayServer::Skins();
}

// ------------------------------------------------------------------ voice and webcam

bool QmlSettings::coplayVoiceEnabled() const
{
    return settings->GetCoPlayVoiceEnabled();
}

void QmlSettings::setCoplayVoiceEnabled(bool on)
{
    settings->SetCoPlayVoiceEnabled(on);
    emit coplayChanged();
}

bool QmlSettings::coplayVoiceStartMuted() const
{
    return settings->GetCoPlayVoiceStartMuted();
}

void QmlSettings::setCoplayVoiceStartMuted(bool on)
{
    settings->SetCoPlayVoiceStartMuted(on);
    emit coplayChanged();
}

int QmlSettings::coplayVoiceVolume() const
{
    return qBound(0, settings->GetCoPlayVoiceVolume(), 200);
}

void QmlSettings::setCoplayVoiceVolume(int percent)
{
    settings->SetCoPlayVoiceVolume(qBound(0, percent, 200));
    emit coplayChanged();
}

QString QmlSettings::coplayCameraDevice() const
{
    return settings->GetCoPlayCameraDevice();
}

void QmlSettings::setCoplayCameraDevice(const QString &device)
{
    settings->SetCoPlayCameraDevice(device);
    emit coplayChanged();
}

int QmlSettings::coplayCameraQuality() const
{
    return qBound(0, settings->GetCoPlayCameraQuality(), 2);
}

void QmlSettings::setCoplayCameraQuality(int quality)
{
    settings->SetCoPlayCameraQuality(qBound(0, quality, 2));
    emit coplayChanged();
}

bool QmlSettings::coplayCameraOnStart() const
{
    return settings->GetCoPlayCameraOnStart();
}

void QmlSettings::setCoplayCameraOnStart(bool on)
{
    settings->SetCoPlayCameraOnStart(on);
    emit coplayChanged();
}

bool QmlSettings::coplayShowCams() const
{
    return settings->GetCoPlayShowCams();
}

void QmlSettings::setCoplayShowCams(bool on)
{
    settings->SetCoPlayShowCams(on);
    emit coplayChanged();
}

bool QmlSettings::coplayCamsOnStream() const
{
    return settings->GetCoPlayCamsOnStream();
}

void QmlSettings::setCoplayCamsOnStream(bool on)
{
    settings->SetCoPlayCamsOnStream(on);
    emit coplayChanged();
}

bool QmlSettings::coplayCameraSupported() const
{
    return CoPlayCamera::Supported();
}

QStringList QmlSettings::coplayCameraDevices() const
{
    return CoPlayCamera::Devices();
}

bool QmlSettings::overlayWebcams() const
{
    return settings->GetOverlayWebcams();
}

void QmlSettings::setOverlayWebcams(bool on)
{
    settings->SetOverlayWebcams(on);
    emit overlayChanged();
}

bool QmlSettings::overlayVoice() const
{
    return settings->GetOverlayVoice();
}

void QmlSettings::setOverlayVoice(bool on)
{
    settings->SetOverlayVoice(on);
    emit overlayChanged();
}

bool QmlSettings::overlayOwnVoice() const
{
    return settings->GetOverlayOwnVoice();
}

void QmlSettings::setOverlayOwnVoice(bool on)
{
    settings->SetOverlayOwnVoice(on);
    emit overlayChanged();
}
