// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_APPUPDATER_H
#define CHIAKI_APPUPDATER_H

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariant>

#include <memory>

class QNetworkReply;

/**
 * The app updating itself. When the update check (qmlbackend_update.cpp) finds a newer
 * release, this knows which of its files is this computer's, and on the user's word downloads
 * it, checks it against the sum the release publishes next to it, puts it in the app's place
 * and starts it:
 *
 *  - on a Mac the app is taken out of the .dmg and takes the place of the running app's bundle;
 *  - on Windows the .zip is unpacked next to the app's folder, and a small script swaps the two
 *    folders once the app has closed (a running program's folder cannot be replaced);
 *  - on Linux the AppImage the app was started from is replaced by the new one.
 *
 * Nothing happens without the user's click, nothing is downloaded from anywhere but the
 * project's own releases over https, and nothing is installed that does not match its sum.
 * Where the app may not write to its own place (run straight from the disk image, a read-only
 * folder, a build that is no AppImage), `possible` is false, `reason` says why, and the
 * download page stays the way. The parts that are only working out are in appupdate.h.
 *
 * It also has the changelog for showing: what is new in the version on offer (from the
 * release's notes) and in every version so far (CHANGELOG.md, which the app carries).
 */
class AppUpdater : public QObject
{
	Q_OBJECT
	/** The newer version that can be had; "" if none is known. */
	Q_PROPERTY(QString version READ version NOTIFY releaseChanged)
	/** Whether the app can put it in its own place. */
	Q_PROPERTY(bool possible READ possible NOTIFY releaseChanged)
	/** Why not, in a sentence; "" if it can. */
	Q_PROPERTY(QString reason READ reason NOTIFY releaseChanged)
	/** "" nothing going on; "download", "check", "install" at work; "restart" done, the app is closing; "failed": `error` says what went wrong. */
	Q_PROPERTY(QString state READ state NOTIFY stateChanged)
	Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
	/** 0 to 1, of the download. */
	Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
	Q_PROPERTY(QString error READ error NOTIFY stateChanged)
	/** What is new in the version on offer: [{ html, isNew, isNote }, …]. */
	Q_PROPERTY(QVariantList news READ news NOTIFY releaseChanged)
	/** Every version so far, newest first: [{ version, changes: [{ html, isNew, isNote }, …] }, …]. */
	Q_PROPERTY(QVariantList history READ history CONSTANT)

	public:
		explicit AppUpdater(QObject *parent = nullptr);
		~AppUpdater();

		/**
		 * A newer release was found: its version, its files as GitHub lists them, and its notes.
		 * `also` is the beginning of addresses that are followed besides the project's releases,
		 * for trying this against a server of one's own ("" otherwise).
		 */
		void SetRelease(const QString &version, const QJsonArray &assets, const QString &notes, const QString &also = QString());

		QString version() const { return release_version; }
		bool possible() const { return cannot.isEmpty() && !release_version.isEmpty(); }
		QString reason() const { return release_version.isEmpty() ? QString() : cannot; }
		QString state() const { return current_state; }
		bool busy() const;
		double progress() const { return download_progress; }
		QString error() const { return last_error; }
		QVariantList news() const { return release_news; }
		QVariantList history() const;

		/** Downloads the new version, checks it, puts it in place and starts it. */
		Q_INVOKABLE void start();
		/** Stops the download. What is being put in place is not stopped. */
		Q_INVOKABLE void cancel();

		// ---- the three ways of putting a version in place; each starts the new app once `pid` has gone

#ifdef Q_OS_MACOS
		/** `dmg` holds the app; it takes the place of the bundle at `bundle`, which must say `version` afterwards. */
		static bool InstallMac(const QString &dmg, const QString &bundle, const QString &version, qint64 pid, QString *error);
#endif
#ifdef Q_OS_WIN
		/** `zip` holds a folder with the app; it takes the place of `app_dir`, whose program is `exe`. */
		static bool InstallWindows(const QString &zip, const QString &app_dir, const QString &exe, qint64 pid, QString *error);
#else
		/**
		 * The AppImage `file` takes the place of the one at `current` (or stands next to it under
		 * `asset_name`, where the old one's name says `old_version`; the old one is removed then).
		 */
		static bool InstallAppImage(const QString &file, const QString &current, const QString &old_version, const QString &asset_name, qint64 pid, QString *error);
#endif

	signals:
		void releaseChanged();
		void stateChanged();
		void progressChanged();
		/** The new version is in place and starts as soon as this app has closed. */
		void restarting();

	private:
		void SetState(const QString &state, const QString &error = QString());
		void Fail(const QString &error);
		void Downloaded();
		void Verify(const QByteArray &published);
		void Install();
		/** Why this app cannot replace itself; "" if it can. */
		static QString Cannot();

		QNetworkAccessManager network;
		QString release_version;
		QString asset_name;
		QUrl asset_url;
		qint64 asset_size = 0;
		/** The file's sum as the release's list gives it, and where the .sha256 next to the file is. */
		QString asset_digest;
		QUrl sums_url;
		QString cannot;
		QVariantList release_news;

		QString current_state;
		QString last_error;
		double download_progress = 0;
		QPointer<QNetworkReply> reply;
		std::unique_ptr<QTemporaryDir> work_dir;
		QFile file;
		QCryptographicHash hash{QCryptographicHash::Sha256};
		QString file_sum;
		mutable QVariantList loaded_history;
		mutable bool history_loaded = false;
};

#endif // CHIAKI_APPUPDATER_H
