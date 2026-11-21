// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_TWITCHELEMENTS_H
#define CHIAKI_TWITCHELEMENTS_H

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QQmlPropertyMap>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class Settings;
class TwitchClient;
class LiveController;
class LiveAlerts;

/**
 * The Twitch elements on the game picture (Chiaki.twitchElements): latest follower, sub,
 * gift subs, bits, raid and channel point redemption; top cheerer, this stream's counts,
 * viewers and uptime; follower and sub goals, hype train, poll, prediction, charity and ad
 * break (only while they run); the recent events in one line, and the Twitch chat.
 *
 * Each one is an overlay element of its own, like the players and their controllers: a
 * switch, a place and a size (values: "<id>_show", "<id>_x", "<id>_y", "<id>_scale"), one
 * look for all of them ("look": transparent, glass or solid). The places are fractions of a
 * 1920 x 1080 picture and saved once: the game picture here, the app's own stream and the
 * OBS overlay page all use them.
 *
 * The data comes from Twitch's EventSub (TwitchClient::Event) and a few calls to its API:
 * viewers and the stream's start, the next ad, running goals, polls, predictions and charity
 * campaigns, the bits leaderboard. "Latest" ones are remembered across streams. Names and
 * messages come from viewers: plain text, cut short, blocked words hidden (LiveAlerts::Clean).
 */
class TwitchElements : public QObject
{
	Q_OBJECT
	Q_PROPERTY(QObject *values READ valuesObject CONSTANT)
	/** Each element: {id, group, name, what, icon}. */
	Q_PROPERTY(QVariantList elements READ elements CONSTANT)
	/** What the elements show; see the top of twitchelements.cpp. */
	Q_PROPERTY(QVariantMap data READ data NOTIFY dataChanged)
	/** Made-up data with the same keys, to see and place the elements while arranging. */
	Q_PROPERTY(QVariantMap sample READ sample CONSTANT)
	/** Logged in to Twitch and live, there or with the app's own stream: the elements show. */
	Q_PROPERTY(bool active READ active NOTIFY activeChanged)

	public:
		TwitchElements(Settings *settings, TwitchClient *twitch, LiveController *live, LiveAlerts *alerts, QObject *parent = nullptr);
		~TwitchElements() override;

		QObject *valuesObject() { return &map; }
		QVariantList elements() const;
		QVariantMap data() const { return current; }
		QVariantMap sample() const;
		bool active() const;

		Q_INVOKABLE void set(const QString &key, const QVariant &value);
		/** Every element back to its first place and size. */
		Q_INVOKABLE void resetPlaces();

		/** An EventSub notification (TwitchClient::Event). */
		void OnTwitchEvent(const QString &type, const QJsonObject &event);
		/** Settings, data and made-up data for the OBS overlay page. */
		QByteArray PageJson() const { return page_json; }
		/** A place dragged on the OBS page: "<id>_x" or "<id>_y" with 0..1. */
		bool SetPlace(const QString &key, double value);
		/** The overlay's look changed: the OBS page hears of it. */
		void Refresh() { UpdatePage(); }

	signals:
		void dataChanged();
		void activeChanged();
		void valuesChanged();

	private:
		void Store(const QString &key, const QVariant &value);
		void Load();
		void Remember(const QString &id);
		void Changed();
		void UpdatePage();
		void UpdateActive();
		void OnTwitchChanged();
		void Poll();
		void FetchStart();
		void FetchLeaderboard();
		void FetchAvatar(const QString &user_id);
		void SetStreamStart(qint64 ms);
		void AddRecent(const QString &kind, const QString &name, const QString &text);
		void ClearLater(const QString &key, int ms);
		QString Clean(const QString &text, int max) const;
		QString Name(const QJsonObject &e, const char *key = "user_name") const;

		void OnPoll(const QJsonObject &e, bool ended);
		void OnPrediction(const QJsonObject &e, const QString &stage);
		void OnGoal(const QJsonObject &e, bool ended);
		void OnHype(const QJsonObject &e, bool ended);
		void OnCharity(const QJsonObject &e, bool ended);
		void OnChat(const QJsonObject &e);
		void OnAds(const QJsonObject &reply);

		Settings *settings;
		QPointer<TwitchClient> twitch;
		QPointer<LiveController> live;
		QPointer<LiveAlerts> alerts;
		QQmlPropertyMap map;
		QVariantMap current;
		QByteArray page_json;
		QTimer poll_timer;
		bool was_active = false;
		bool logged_in = false;
		bool polled_once = false;
		/** Twitch says the channel is live (its API or stream.online). */
		bool twitch_live = false;
		/** Bits per viewer this stream, for the top cheerer of the stream. */
		QHash<QString, int> stream_bits;
		QHash<QString, QString> stream_bits_names;
		QString avatar_for;
		/** Changes to data that was cleared later on (a poll that ended): newer data wins. */
		QHash<QString, quint64> serials;
		quint64 serial = 0;
};

#endif // CHIAKI_TWITCHELEMENTS_H
