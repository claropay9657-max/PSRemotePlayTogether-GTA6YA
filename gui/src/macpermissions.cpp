// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Away from a Mac nothing has to be allowed: the page that lists it is not shown. See
// macpermissions.h; the Mac's is macpermissions.mm.

#include "macpermissions.h"

MacPermissions::MacPermissions(DiscoveryManager *discovery, QObject *parent) : QObject(parent), discovery(discovery)
{
	Q_UNUSED(this->discovery);
	Q_UNUSED(screen_at_start);
}

bool MacPermissions::supported()
{
	return false;
}

bool MacPermissions::allAllowed() const
{
	return true;
}

void MacPermissions::refresh()
{
}

void MacPermissions::allow(const QString &)
{
}

void MacPermissions::allowAll()
{
}

void MacPermissions::openSettings(const QString &)
{
}

QString MacPermissions::StateOf(const QString &) const
{
	return QStringLiteral("allowed");
}
