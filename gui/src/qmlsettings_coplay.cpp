// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Play Together settings as seen by the QML side.

#include "qmlsettings.h"

#include <coplay/invite.h>
#include <coplay/socket.h>

#include <QClipboard>
#include <QGuiApplication>

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
