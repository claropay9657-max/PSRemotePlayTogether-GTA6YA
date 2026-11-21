// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_KEYSTORE_H
#define CHIAKI_KEYSTORE_H

#include <QString>

/**
 * Secrets of the player (the stream key for going live) in the system's own
 * password store, never in the settings file:
 *
 *  - macOS: the login keychain
 *  - Windows: the Credential Manager
 *  - Linux: the Secret Service (GNOME Keyring, KWallet), through libsecret if the
 *    system has it; it is looked for when needed, the app does not depend on it
 *
 * Where there is no such store a secret is kept in memory until the app quits.
 * The functions may block for a moment (the store may ask the player to unlock it),
 * so they are meant for the moments the player saves or uses a secret.
 */
namespace keystore {

/** Saves `secret` under `name`; false if it is kept in memory only (see `error`). */
bool Save(const QString &name, const QString &secret, QString *error = nullptr);
/** The secret saved under `name`, empty if there is none. */
QString Load(const QString &name);
void Remove(const QString &name);
/** Where secrets go on this computer, for the player ("the macOS keychain"). Empty: nowhere, memory only. */
QString StoreName();

} // namespace keystore

#endif // CHIAKI_KEYSTORE_H
