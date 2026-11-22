// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef PSRPT_MACPERMISSIONS_H
#define PSRPT_MACPERMISSIONS_H

#include <QObject>
#include <QString>
#include <QVariantList>

class DiscoveryManager;

/**
 * What macOS has to allow the app, in one place (Settings → Permissions): the devices on this
 * network (finding and waking consoles), the microphone (voice chat), the camera (webcam),
 * screen and system audio recording (sharing a game on this Mac) and accessibility (friends'
 * mouse and keyboard there).
 *
 * Each is "allowed", "ask" (one click makes macOS ask, in its own window) or "settings" (macOS
 * asks only once per app: after a no, only System Settings changes it, and the click opens the
 * right place there). Elsewhere than on a Mac there is nothing to allow: `supported` is false.
 */
class MacPermissions : public QObject
{
	Q_OBJECT
	Q_PROPERTY(bool supported READ supported CONSTANT)
	/** [{key, state}] in the order shown: localNetwork, microphone, camera, screen, accessibility. */
	Q_PROPERTY(QVariantList items READ items NOTIFY changed)
	Q_PROPERTY(bool allAllowed READ allAllowed NOTIFY changed)
	/** Screen recording was allowed while the app ran: macOS lets it record after a restart. */
	Q_PROPERTY(bool restartNeeded READ restartNeeded NOTIFY changed)

	public:
		MacPermissions(DiscoveryManager *discovery, QObject *parent = nullptr);

		static bool supported();
		QVariantList items() const { return list; }
		bool allAllowed() const;
		bool restartNeeded() const { return restart_needed; }

		/** Looks again (the user may have changed something in System Settings meanwhile). */
		Q_INVOKABLE void refresh();
		/** Asks macOS for it where it still asks, otherwise opens its place in System Settings. */
		Q_INVOKABLE void allow(const QString &key);
		/** Every one that macOS still asks about, one after the other. */
		Q_INVOKABLE void allowAll();
		Q_INVOKABLE void openSettings(const QString &key);

	signals:
		void changed();

	private:
		QString StateOf(const QString &key) const;

		DiscoveryManager *discovery;
		QVariantList list;
		bool restart_needed = false;
		bool screen_at_start = false;
};

#endif // PSRPT_MACPERMISSIONS_H
