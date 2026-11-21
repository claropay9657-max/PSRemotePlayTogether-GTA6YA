// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_LIVECONTROLLER_H
#define CHIAKI_LIVECONTROLLER_H

#include <QObject>
#include <QPointer>
#include <QQmlPropertyMap>
#include <QSet>
#include <QSize>
#include <QTimer>
#include <QVariantMap>

class Settings;
class StreamSession;

/**
 * Going live, as the app shows it (Chiaki.live): the settings of the Live streaming
 * page, the stream key, "Go live" and "Stop" in the stream menu, and the numbers
 * while it runs. The work is done by live::Engine (livestream.h).
 *
 * Settings live in "live/<key>" of the configuration file, offered to QML as a
 * property map: read and bind with `values.quality`, change with `set()`. The stream
 * key never goes there; it is kept in the system's password store (keystore.h).
 */
class LiveController : public QObject
{
	Q_OBJECT
	Q_PROPERTY(QObject *values READ valuesObject CONSTANT)
	/** A stream key is saved for the chosen service. */
	Q_PROPERTY(bool keySaved READ keySaved NOTIFY keyChanged)
	/** Where the key is kept ("the macOS keychain"); empty: in memory until the app quits. */
	Q_PROPERTY(QString keyStore READ keyStore CONSTANT)
	/** "off", "connecting", "live", "reconnecting" or "failed". */
	Q_PROPERTY(QString state READ state NOTIFY statsChanged)
	/** On air or on its way there (connecting, reconnecting). */
	Q_PROPERTY(bool live READ live NOTIFY statsChanged)
	/** kbps, frames, dropped, seconds, reconnects, encoder, error. */
	Q_PROPERTY(QVariantMap stats READ stats NOTIFY statsChanged)
	/** The server the stream goes to, without the key. */
	Q_PROPERTY(QString server READ server NOTIFY valuesChanged)
	/** Size of the stream's picture for the chosen quality. */
	Q_PROPERTY(QSize streamSize READ streamSize NOTIFY valuesChanged)
	/** The bitrate the chosen quality calls for, kbit/s (values.video_kbps 0 means this). */
	Q_PROPERTY(int defaultKbps READ defaultKbps NOTIFY valuesChanged)

	public:
		LiveController(Settings *settings, QObject *parent = nullptr);
		~LiveController() override;

		QObject *valuesObject() { return &map; }
		bool keySaved() const;
		QString keyStore() const;
		QString state() const { return current_state; }
		bool live() const;
		QVariantMap stats() const { return current_stats; }
		QString server() const;
		QSize streamSize() const;
		int defaultKbps() const;

		Q_INVOKABLE void set(const QString &key, const QVariant &value);
		/** Saves the stream key of the chosen service; returns a note for the player, empty if all went well. */
		Q_INVOKABLE QString saveKey(const QString &key);
		Q_INVOKABLE void clearKey();
		/** Goes live; returns what is missing, empty if it started. */
		Q_INVOKABLE QString start();
		Q_INVOKABLE void stop();

		/** The session whose picture and sound go out; null when there is none (ends the stream). */
		void SetSession(StreamSession *session);
		/** Stops at once and waits, for quitting the app. */
		void Shutdown();

	signals:
		void valuesChanged();
		void keyChanged();
		void statsChanged();

	private:
		void Store(const QString &key, const QVariant &value);
		void Apply(const QString &key);
		void Poll();
		/** Tells the session we are not live any more, if we had told it we were. */
		void ForgetSession();
		QString KeyName() const;
		static QString CaFile();

		Settings *settings;
		QQmlPropertyMap map;
		QPointer<StreamSession> session;
		QTimer poll_timer;
		QString current_state = QStringLiteral("off");
		QVariantMap current_stats;
		bool told_session = false;
		bool told_live = false;
		/** Keys the system's store would not take: kept until the app quits. */
		QSet<QString> keys_in_memory;
};

#endif // CHIAKI_LIVECONTROLLER_H
