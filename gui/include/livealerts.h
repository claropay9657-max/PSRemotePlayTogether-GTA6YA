// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_LIVEALERTS_H
#define CHIAKI_LIVEALERTS_H

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QQmlPropertyMap>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <deque>
#include <memory>
#include <vector>

class Settings;
class LiveScenes;
class QObject;

/**
 * Alerts for the live stream (Chiaki.alerts): whatever Twitch tells about the channel
 * (twitch.h) as a card over the game, one after the other, never two at once, with
 * "N more coming up". Each kind (follow, sub, resub, gift subs, bits, raid, channel
 * points, hype train, poll, prediction, goal, charity, shoutout, ad break) can be
 * switched off, has its own sound, volume, time on screen and a minimum (bits from 100,
 * say), and a Test.
 *
 * Shown in the app's own live stream (StreamView's live overlay), on the streamer's own
 * screen if wanted, and for OBS as /alerts of the overlay server; the sound goes into
 * the live stream and is played here as well. Names and messages come from viewers:
 * they are plain text, cut short, and blocked words are hidden.
 */
class LiveAlerts : public QObject
{
	Q_OBJECT
	Q_PROPERTY(QObject *values READ valuesObject CONSTANT)
	/** Each kind: {id, name, what, hasMin, minName}. */
	Q_PROPERTY(QVariantList kinds READ kinds CONSTANT)
	/** The alert on screen now, empty when there is none; see Make(). */
	Q_PROPERTY(QVariantMap current READ current NOTIFY currentChanged)
	/** 0..1: how much of its time the current alert has left. */
	Q_PROPERTY(qreal left READ left NOTIFY leftChanged)
	Q_PROPERTY(int waiting READ waiting NOTIFY currentChanged)
	/** The next few waiting: {initial, color, icon, face}. */
	Q_PROPERTY(QVariantList next READ next NOTIFY currentChanged)
	Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY currentChanged)

	public:
		LiveAlerts(Settings *settings, LiveScenes *scenes, QObject *parent = nullptr);
		~LiveAlerts() override;

		QObject *valuesObject() { return &map; }
		QVariantList kinds() const;
		QVariantMap current() const { return shown; }
		qreal left() const;
		int waiting() const { return static_cast<int>(queue.size()); }
		QVariantList next() const;
		bool paused() const { return is_paused; }
		void setPaused(bool paused);

		Q_INVOKABLE void set(const QString &key, const QVariant &value);
		Q_INVOKABLE void test(const QString &kind);
		Q_INVOKABLE void skip();
		Q_INVOKABLE void clear();
		/** Asks for an own sound file for a kind (WAV, FLAC, MP3 or Ogg). */
		Q_INVOKABLE void chooseSoundFile(const QString &kind);
		/** Plays a kind's sound here once, at its level now (after moving its slider). "chat": the chat sound. */
		Q_INVOKABLE void preview(const QString &kind);
		/** "app", "none" or the own file: what a kind sounds like. */
		Q_INVOKABLE void setSound(const QString &kind, const QString &sound);

		/** An EventSub notification (TwitchClient::Event). */
		void OnTwitchEvent(const QString &type, const QJsonObject &event);
		/** The alert as data for the OBS page (/alerts). */
		QByteArray PageJson() const { return page_json; }
		/** The sound of a kind ("chat": the chat sound), for the OBS page; type: its media type. */
		QByteArray SoundFile(const QString &kind, QByteArray *type = nullptr) const;
		/**
		 * A chat message came in (the room's or Twitch's), not one of the streamer's own: the
		 * chat sound, here and, if wanted, on the stream; at most one every 1.5 s.
		 */
		void OnChatMessage();
		/** A message of the room's chat (StreamSession::coplayChatMessage): others' only. */
		void OnRoomChat(const QVariantMap &message);
		/** A viewer's text made safe to show: one line, no control characters, blocked words hidden, at most max. */
		QString Clean(const QString &text, int max) const;

	signals:
		void currentChanged();
		void leftChanged();
		void valuesChanged();

	private:
		struct Alert
		{
			QVariantMap data;
			QString kind;
		};

		void Store(const QString &key, const QVariant &value);
		void Push(const QString &kind, QVariantMap data);
		void Next();
		void Tick();
		void UpdatePage();
		/** here: also on this computer; stream: into the stream (and the OBS page). */
		void PlaySound(const QString &kind, bool here, bool stream);
		/** The kind's level now, 0..100, with the master level and mute. */
		int Level(const QString &kind) const;
		bool Held() const;

		Settings *settings;
		QPointer<LiveScenes> scenes;
		QQmlPropertyMap map;
		std::deque<Alert> queue;
		QVariantMap shown;
		qint64 shown_since_ms = 0;
		qint64 shown_ms = 0;
		bool is_paused = false;
		QTimer timer;
		quint64 serial = 0;
		QByteArray page_json;
		/** Decoded sounds, 48 kHz stereo. */
		QHash<QString, std::shared_ptr<const std::vector<int16_t>>> sound_cache;
		/** Plays on this computer (Qt Multimedia); null without it. */
		QObject *local = nullptr;
		qint64 chat_sound_ms = 0;
		/** Chat sounds that went to the stream, for the OBS page to play each once. */
		int chat_count = 0;
		/** The next alert shown was a Test: its sound plays here too. */
		bool test_next = false;
		/** The last level a hype train told about: progress only alerts at a new level. */
		int hype_level = 0;
};

#endif // CHIAKI_LIVEALERTS_H
