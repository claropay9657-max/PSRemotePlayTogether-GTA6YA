// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef PSRPT_VIEWERACTIONS_H
#define PSRPT_VIEWERACTIONS_H

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>

class Settings;
class TwitchClient;
class StreamSession;

/**
 * Viewer actions: a channel point reward of the app's own, bits, a sub, a follow or a raid
 * presses a button on this player's controller in the game, for a moment. Off until the
 * player switches it on; at most so many a minute, one after the other; Stop all ends it.
 * A channel point redemption that cannot run gets its points back (Twitch: CANCELED).
 */
class ViewerActions : public QObject
{
	Q_OBJECT
	Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
	Q_PROPERTY(int perMinute READ perMinute WRITE setPerMinute NOTIFY changed)
	Q_PROPERTY(QVariantList actions READ actions NOTIFY changed)
	Q_PROPERTY(QString status READ status NOTIFY changed)
	Q_PROPERTY(QString last READ last NOTIFY changed)
	Q_PROPERTY(int waiting READ waiting NOTIFY changed)

	public:
		ViewerActions(Settings *settings, TwitchClient *twitch, QObject *parent = nullptr);

		bool enabled() const { return on; }
		void setEnabled(bool enabled);
		int perMinute() const { return per_minute; }
		void setPerMinute(int n);
		QVariantList actions() const { return list; }
		QString status() const { return status_text; }
		QString last() const { return last_text; }
		int waiting() const { return static_cast<int>(queue.size()); }

		Q_INVOKABLE void addAction();
		/** Changes an action: title, trigger, cost, min, cooldown, input, hold, on. */
		Q_INVOKABLE void setAction(int index, const QVariantMap &values);
		Q_INVOKABLE void removeAction(int index);
		/** Makes the app's channel point rewards on Twitch match the list (also done by itself). */
		Q_INVOKABLE void syncRewards();
		/** Empties the queue (points back), lets every button go and switches viewer actions off. */
		Q_INVOKABLE void stopAll();
		/** Presses an action's button once now, to try it in the game. */
		Q_INVOKABLE void tryAction(int index);

	public slots:
		void SetSession(StreamSession *session);
		void OnTwitchEvent(const QString &type, const QJsonObject &event);

	signals:
		void changed();

	private:
		struct Job
		{
			QVariantMap action;
			QString who;
			QString reward_id;
			QString redemption_id;
		};

		Settings *settings;
		TwitchClient *twitch;
		QPointer<StreamSession> session;
		bool on = false;
		int per_minute = 6;
		QVariantList list;
		QString status_text;
		QString last_text;
		QList<Job> queue;
		QList<qint64> accepted_ms;
		QHash<QString, qint64> last_run_ms;
		QTimer sync_soon;
		bool pressing = false;

		void Save();
		void Enqueue(const QVariantMap &action, const QString &who, const QString &reward_id, const QString &redemption_id, bool test);
		void Next();
		void Finish(const Job &job, bool ran);
		void Release();
		void SetStatus(const QString &text);
		int IndexOfId(const QString &id) const;
};

#endif // PSRPT_VIEWERACTIONS_H
