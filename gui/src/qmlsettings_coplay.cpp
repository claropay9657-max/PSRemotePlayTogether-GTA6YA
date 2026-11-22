// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Play Together settings as seen by the QML side.

#include "qmlsettings.h"
#include "coplayrooms.h"

#include <coplay/web.h>
#include <coplay/invite.h>
#include <coplay/socket.h>

#include "overlayserver.h"
#include "coplayaddress.h"
#include "coplaymeet.h"
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
    // Whoever switches sharing on will want the invite for friends elsewhere next
    if (enabled)
        coplayLookupPublicAddress();
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

bool QmlSettings::coplayPcKeyMouse() const
{
    return settings->GetCoPlayPcKeyMouse();
}

void QmlSettings::setCoplayPcKeyMouse(bool on)
{
    if (on == settings->GetCoPlayPcKeyMouse())
        return;
    settings->SetCoPlayPcKeyMouse(on);
    emit coplayChanged();
}

int QmlSettings::coplayPcHeight() const
{
    return settings->GetCoPlayPcHeight();
}

void QmlSettings::setCoplayPcHeight(int height)
{
    if (height == settings->GetCoPlayPcHeight())
        return;
    settings->SetCoPlayPcHeight(height);
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

bool QmlSettings::coplayAutoReconnect() const
{
    return settings->GetCoPlayAutoReconnect();
}

void QmlSettings::setCoplayAutoReconnect(bool enabled)
{
    settings->SetCoPlayAutoReconnect(enabled);
    emit coplayChanged();
}

QString QmlSettings::coplayLocalAddress() const
{
    return QString::fromStdString(coplay::GuessLocalAddress());
}

/**
 * The invite that leads to this computer under the given address. Without an
 * address there is none, unless a meeting point is asked: then the invite says so.
 */
QString QmlSettings::coplayInviteFor(const QString &host) const
{
    const bool meet = coplayMeetOn();
    if (host.isEmpty() && !meet)
        return QString();
    coplay::Invite invite;
    invite.secret = coplayHostSecret();
    invite.host = host.toStdString();
    int port = settings->GetCoPlayPort();
    invite.port = static_cast<uint16_t>(port > 0 && port < 65536 ? port : coplay::kDefaultPort);
    if (meet)
        invite.meet = CoPlayMeet::InviteValue(settings->GetCoPlayMeetServer());
    return QString::fromStdString(coplay::EncodeInvite(invite));
}

static QString LocalAddressOrLoopback()
{
    QString address = QString::fromStdString(coplay::GuessLocalAddress());
    return address.isEmpty() ? QStringLiteral("127.0.0.1") : address;
}

QString QmlSettings::coplayInvite() const
{
    QString address = settings->GetCoPlayAddress();
    return coplayInviteFor(address.isEmpty() ? LocalAddressOrLoopback() : address);
}

// ------------------------------------------------------------------ one invite per way in

QString QmlSettings::coplayInviteLan() const
{
    return coplayInviteFor(LocalAddressOrLoopback());
}

QString QmlSettings::coplayPublicAddress() const
{
    // An address the user wrote down, such as a name that follows the connection
    // around, is the one friends elsewhere reach: there is nothing to look up then
    QString address = settings->GetCoPlayAddress().trimmed();
    if (!address.isEmpty())
        return address;
    return CoPlayPublicAddress::Instance()->Address();
}

int QmlSettings::coplayPublicLookupState() const
{
    if (!settings->GetCoPlayAddress().trimmed().isEmpty())
        return CoPlayPublicAddress::Found;
    return CoPlayPublicAddress::Instance()->GetState();
}

QString QmlSettings::coplayPublicLookupError() const
{
    if (!settings->GetCoPlayAddress().trimmed().isEmpty())
        return QString();
    return CoPlayPublicAddress::Instance()->Error();
}

QString QmlSettings::coplayInvitePublic() const
{
    return coplayInviteFor(coplayPublicAddress());
}

QString QmlSettings::coplayIPv6Address() const
{
    return QString::fromStdString(coplay::GuessGlobalIPv6Address());
}

QString QmlSettings::coplayInviteIPv6() const
{
    // Without an IPv6 address there is no IPv6 invite, meeting point or not
    const QString address = coplayIPv6Address();
    return address.isEmpty() ? QString() : coplayInviteFor(address);
}

QString QmlSettings::coplayMeetServer() const
{
    return settings->GetCoPlayMeetServer();
}

/** "off" in a server field: that one is switched off. */
static bool IsOffSetting(const QString &s)
{
    return s.trimmed().compare(QLatin1String("off"), Qt::CaseInsensitive) == 0;
}

void QmlSettings::setCoplayMeetServer(const QString &server)
{
    const QString text = server.trimmed();
    const QString current = settings->GetCoPlayMeetServer();
    if (text == current)
        return;
    // Switched off: an own address waits for the switch to come on again
    if (IsOffSetting(text) && !IsOffSetting(current))
        settings->SetCoPlayMeetServerKept(current);
    settings->SetCoPlayMeetServer(text);
    emit coplayChanged();
}

QString QmlSettings::coplayMeetServerKept() const
{
    return settings->GetCoPlayMeetServerKept();
}

bool QmlSettings::coplayMeetOn() const
{
    return CoPlayMeet::ForHost(settings->GetCoPlayMeetServer(), QStringLiteral("off")).Enabled();
}

QString QmlSettings::coplayMeetDefault() const
{
    QString url = QString::fromStdString(CoPlayMeet::DefaultUrl());
    return url.startsWith(QLatin1String("https://")) ? url.mid(8) : url;
}

QString QmlSettings::coplayStunServer() const
{
    return settings->GetCoPlayStunServer();
}

void QmlSettings::setCoplayStunServer(const QString &server)
{
    if (server.trimmed() == settings->GetCoPlayStunServer())
        return;
    settings->SetCoPlayStunServer(server.trimmed());
    // What another server said does not count any more, and "off" means off
    CoPlayPublicAddress::Instance()->Reset();
    emit coplayChanged();
}

void QmlSettings::coplayLookupPublicAddress()
{
    if (!settings->GetCoPlayAddress().trimmed().isEmpty())
        return;
    CoPlayPublicAddress::Instance()->LookupIfStale(settings->GetCoPlayStunServer());
}

void QmlSettings::coplayRefreshPublicAddress()
{
    if (!settings->GetCoPlayAddress().trimmed().isEmpty())
        return;
    CoPlayPublicAddress::Instance()->Lookup(settings->GetCoPlayStunServer());
}

static void CopyUnlessEmpty(const QString &text)
{
    if (!text.isEmpty())
        QGuiApplication::clipboard()->setText(text);
}

void QmlSettings::coplayCopyInviteLan()
{
    CopyUnlessEmpty(coplayInviteLan());
}

void QmlSettings::coplayCopyInvitePublic()
{
    CopyUnlessEmpty(coplayInvitePublic());
}

void QmlSettings::coplayCopyInviteIPv6()
{
    CopyUnlessEmpty(coplayInviteIPv6());
}

// ------------------------------------------------------------------ friends in a web browser

bool QmlSettings::coplayWebSupported() const
{
    return coplay::WebGuestsSupported();
}

// ------------------------------------------------------------------ public rooms

coplay::Secret QmlSettings::coplayHostSecret() const
{
    // A public room has a key of its own: strangers who had it cannot come back to a private session
    const bool pub = settings->GetCoPlayPublic();
    coplay::Secret secret;
    const QString stored = pub ? settings->GetCoPlayPublicSecret() : settings->GetCoPlaySecret();
    if (!coplay::SecretFromString(stored.toStdString(), secret)) {
        // First use: there is no key yet
        secret = coplay::GenerateSecret();
        const QString text = QString::fromStdString(coplay::SecretToString(secret));
        if (pub)
            settings->SetCoPlayPublicSecret(text);
        else
            settings->SetCoPlaySecret(text);
    }
    return secret;
}

bool QmlSettings::coplayHostPublic() const
{
    return settings->GetCoPlayPublic();
}

void QmlSettings::setCoplayHostPublic(bool on)
{
    if (on == settings->GetCoPlayPublic())
        return;
    settings->SetCoPlayPublic(on);
    emit coplayChanged();
}

bool QmlSettings::coplayRoomPreview() const
{
    return settings->GetCoPlayRoomPreview();
}

void QmlSettings::setCoplayRoomPreview(bool on)
{
    if (on == settings->GetCoPlayRoomPreview())
        return;
    settings->SetCoPlayRoomPreview(on);
    emit coplayChanged();
}

QString QmlSettings::coplayRoomTitle() const
{
    return settings->GetCoPlayRoomTitle();
}

void QmlSettings::setCoplayRoomTitle(const QString &title)
{
    const QString clean = title.simplified().left(60);
    if (clean == settings->GetCoPlayRoomTitle())
        return;
    settings->SetCoPlayRoomTitle(clean);
    emit coplayChanged();
}

QString QmlSettings::coplayRoomLanguage() const
{
    return settings->GetCoPlayRoomLanguage();
}

void QmlSettings::setCoplayRoomLanguage(const QString &lang)
{
    if (lang == settings->GetCoPlayRoomLanguage())
        return;
    settings->SetCoPlayRoomLanguage(lang.left(2).toLower());
    emit coplayChanged();
}

int QmlSettings::coplayJoinPolicy() const
{
    return settings->GetCoPlayJoinPolicy();
}

void QmlSettings::setCoplayJoinPolicy(int policy)
{
    if (policy == settings->GetCoPlayJoinPolicy())
        return;
    settings->SetCoPlayJoinPolicy(policy);
    emit coplayChanged();
}

QString QmlSettings::coplayRoomsServer() const
{
    return settings->GetCoPlayRoomsServer();
}

void QmlSettings::setCoplayRoomsServer(const QString &server)
{
    const QString text = server.trimmed();
    const QString current = settings->GetCoPlayRoomsServer();
    if (text == current)
        return;
    // Switched off: an own address waits for the switch to come on again
    if (IsOffSetting(text) && !IsOffSetting(current))
        settings->SetCoPlayRoomsServerKept(current);
    settings->SetCoPlayRoomsServer(text);
    emit coplayChanged();
}

QString QmlSettings::coplayRoomsServerKept() const
{
    return settings->GetCoPlayRoomsServerKept();
}

QString QmlSettings::coplayRoomsDefault() const
{
    QString url = CoPlayRooms::DefaultUrl();
    return url.startsWith(QLatin1String("https://")) ? url.mid(8) : url;
}

bool QmlSettings::coplayRoomsOn() const
{
    return !CoPlayRooms::UrlFromSetting(settings->GetCoPlayRoomsServer()).isEmpty();
}

QVariantList QmlSettings::coplayLanguages() const
{
    return CoPlayRooms::Languages();
}

QVariantList QmlSettings::coplayRooms() const
{
    return CoPlayRoomList::Instance()->Rooms();
}

int QmlSettings::coplayRoomsState() const
{
    return CoPlayRoomList::Instance()->GetState();
}

QString QmlSettings::coplayRoomsError() const
{
    return CoPlayRoomList::Instance()->Error();
}

void QmlSettings::coplayRefreshRooms()
{
    CoPlayRoomList::Instance()->Refresh(settings->GetCoPlayRoomsServer());
}

int QmlSettings::coplayMaxGuests() const
{
    return settings->GetCoPlayMaxGuests();
}

void QmlSettings::setCoplayMaxGuests(int n)
{
    if (n == settings->GetCoPlayMaxGuests())
        return;
    settings->SetCoPlayMaxGuests(n);
    emit coplayChanged();
}

bool QmlSettings::coplayGuestsMaySleep() const
{
    return settings->GetCoPlayGuestsMaySleep();
}

void QmlSettings::setCoplayGuestsMaySleep(bool allowed)
{
    if (allowed == settings->GetCoPlayGuestsMaySleep())
        return;
    settings->SetCoPlayGuestsMaySleep(allowed);
    emit coplayChanged();
}

bool QmlSettings::coplayWebGuests() const
{
    return settings->GetCoPlayWebGuests();
}

void QmlSettings::setCoplayWebGuests(bool enabled)
{
    if (enabled == settings->GetCoPlayWebGuests())
        return;
    settings->SetCoPlayWebGuests(enabled);
    emit coplayChanged();
}

QString QmlSettings::coplayWebUrl() const
{
    return settings->GetCoPlayWebUrl();
}

void QmlSettings::setCoplayWebUrl(const QString &url)
{
    if (url.trimmed() == settings->GetCoPlayWebUrl())
        return;
    settings->SetCoPlayWebUrl(url.trimmed());
    emit coplayChanged();
}

int QmlSettings::coplayWebPort() const
{
    return settings->GetCoPlayWebPort();
}

void QmlSettings::setCoplayWebPort(int port)
{
    if (port < -1 || port > 65535)
        port = 0;
    if (port == settings->GetCoPlayWebPort())
        return;
    settings->SetCoPlayWebPort(port);
    emit coplayChanged();
}

QString QmlSettings::coplayWebDefault() const
{
    QString url = QString::fromLatin1(coplay::kDefaultWebUrl);
    return url.startsWith(QLatin1String("https://")) ? url.mid(8) : url;
}

QString QmlSettings::coplayInviteWeb() const
{
    if (!coplay::WebGuestsSupported() || !settings->GetCoPlayWebGuests() || !coplayMeetOn())
        return QString();
    const coplay::Secret secret = coplayHostSecret();
    return QString::fromStdString(coplay::WebInviteLink(coplay::WebUrlFromText(settings->GetCoPlayWebUrl().toStdString()),
        secret, CoPlayMeet::InviteValue(settings->GetCoPlayMeetServer()),
        coplay::WebStunForLink(settings->GetCoPlayStunServer().toStdString())));
}

void QmlSettings::coplayCopyInviteWeb()
{
    CopyUnlessEmpty(coplayInviteWeb());
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

bool QmlSettings::coplayChatPreviews() const
{
    return settings->GetCoPlayChatPreviews();
}

void QmlSettings::setCoplayChatPreviews(bool on)
{
    settings->SetCoPlayChatPreviews(on);
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
