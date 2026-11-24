// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_FRIENDS_H
#define CHIAKI_FRIENDS_H

#include <coplay/friends_crypto.h>

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

class QNetworkAccessManager;

/*
 * Friends, chats and status (docs/friends.md, the protocol in docs/friends-protocol.md).
 *
 * One identity per player, this computer one of its devices. What it knows (keys, friends,
 * conversations, chats, groups) is kept in one file in the app's data folder, sealed with a
 * key from the system's password store (keystore.h). It talks to the friends server
 * (server/friends/psrpt_friends.php) with signed requests; the server only ever sees sealed
 * notes. Everything sealed and opened here goes through coplay/friends_crypto.h.
 *
 * Nothing happens before the player switches friends on: no identity is made, no server asked.
 */
class FriendsClient : public QObject
{
	Q_OBJECT

	/** Friends are on: an identity exists and the server is used. */
	Q_PROPERTY(bool enabled READ Enabled NOTIFY changed)
	/** Talking to the server works right now. */
	Q_PROPERTY(bool online READ Online NOTIFY changed)
	Q_PROPERTY(QString problem READ Problem NOTIFY changed)
	/** The friends server; empty for the default one. */
	Q_PROPERTY(QString server READ ServerAddress WRITE SetServer NOTIFY changed)
	Q_PROPERTY(QString defaultServer READ DefaultServer CONSTANT)
	/** The own friend link, for the app (psrpt://friend/...) and for a browser. */
	Q_PROPERTY(QString link READ Link NOTIFY changed)
	Q_PROPERTY(QString webLink READ WebLink NOTIFY changed)
	/** Linking another device: "" (not), "waiting" (for the other device), "code" (both show linkCode),
	 *  "sending", "receiving", "done", "failed". */
	Q_PROPERTY(QString linkState READ LinkState NOTIFY changed)
	/** The six digits both devices show, to compare. */
	Q_PROPERTY(QString linkCode READ LinkCodeText NOTIFY changed)
	/** On the device that has the friends: what the new device needs (psrpt://link/...). */
	Q_PROPERTY(QString linkText READ LinkText NOTIFY changed)
	/** The devices of this identity: id, kind (1 the app, 2 a web browser), me (this one). */
	Q_PROPERTY(QVariantList devices READ Devices NOTIFY changed)
	/** This identity's fingerprint (base64url), empty while friends are off. */
	Q_PROPERTY(QString fingerprint READ Fingerprint NOTIFY changed)
	/** Each: fp, name, avatar (data URL), state ("online", "away", "busy", "web", "offline"), activity, room (map or null), free, checked, safety. */
	Q_PROPERTY(QVariantList friends READ Friends NOTIFY changed)
	/** Each: fp, name, avatar, text, incoming (bool), at. */
	Q_PROPERTY(QVariantList requests READ Requests NOTIFY changed)
	/** Each: fp, name. */
	Q_PROPERTY(QVariantList blocked READ Blocked NOTIFY changed)
	/** Each: id, kind ("friend" or "group"), title, avatar, last (text), at, unread. */
	Q_PROPERTY(QVariantList chats READ Chats NOTIFY changed)
	Q_PROPERTY(int unread READ Unread NOTIFY changed)
	/** online, away, busy, invisible */
	Q_PROPERTY(QString myState READ MyState WRITE SetMyState NOTIFY changed)
	Q_PROPERTY(QString myFree READ MyFree WRITE SetMyFree NOTIFY changed)
	/** Settings: who sees the status ("all", "nobody"), whether the game shows, whether friends may join the sessions ("always", "never"). */
	Q_PROPERTY(QString showStatus READ ShowStatus WRITE SetShowStatus NOTIFY changed)
	Q_PROPERTY(bool showGame READ ShowGame WRITE SetShowGame NOTIFY changed)
	Q_PROPERTY(QString friendsJoin READ FriendsJoin WRITE SetFriendsJoin NOTIFY changed)
	/** Read receipts, both ways: off sends none and shows none. */
	Q_PROPERTY(bool readReceipts READ ReadReceipts WRITE SetReadReceipts NOTIFY changed)

	public:
		explicit FriendsClient(QObject *parent = nullptr);
		~FriendsClient() override;

		/** Where the state file lives (tests use a folder of their own) and which server to ask. */
		void SetStorage(const QString &folder, const QString &secret_name);
		void SetServer(const QString &url);
		/** The name and picture (a JPEG) friends see, from the player's profile. */
		void SetProfile(const QString &name, const QByteArray &avatar_jpeg);
		/** What this computer is doing, for the status: "Playing X on PS5", a room to join. */
		void SetActivity(const QString &activity, const QVariantMap &room);
		/** How often to ask the server: the app's window open, or only in the tray. */
		void SetBackground(bool in_tray);

		bool Enabled() const { return enabled; }
		QString ServerAddress() const;
		static QString DefaultServer();
		/** Requests to the server and link previews still under way (tests wait for none). */
		int Pending() const { return pending_calls; }
		/** This identity's fingerprint, base64url. */
		QString Fingerprint() const;
		bool Online() const { return online; }
		QString Problem() const { return problem; }
		QString Link() const;
		QString WebLink() const;
		QVariantList Friends() const;
		QVariantList Requests() const;
		QVariantList Blocked() const;
		QVariantList Devices() const;
		/** Takes another device of this identity off the card: friends no longer write to it. */
		Q_INVOKABLE void removeDevice(const QString &id);
		QVariantList Chats() const;
		int Unread() const;
		QString MyState() const { return my_state; }
		void SetMyState(const QString &state);
		QString MyFree() const { return my_free; }
		void SetMyFree(const QString &text);
		QString ShowStatus() const;
		void SetShowStatus(const QString &who);
		bool ShowGame() const;
		void SetShowGame(bool on);
		QString FriendsJoin() const;
		bool ReadReceipts() const;
		void SetReadReceipts(bool on);
		/** Links in own messages get a preview (the app's setting, shared with a session's chat). */
		void SetLinkPreviews(bool on) { link_previews = on; }
		void SetFriendsJoin(const QString &who);

		/** Switches friends on: makes the identity and this device's keys, and puts the card on the server. */
		Q_INVOKABLE void enable();
		/** Switches them off on this device: forgets everything here (the friends keep their copy of the card). */
		Q_INVOKABLE void disable();
		/** Asks the server now: requests, messages, statuses. */
		Q_INVOKABLE void refresh();

		/** A friend link: looks it up and answers with linkLooked(ok, fp, name, avatar, error). */
		Q_INVOKABLE void lookUp(const QString &link);
		Q_INVOKABLE void sendRequest(const QString &link, const QString &text);
		/** A request that did not go out (or got no answer), once more. */
		Q_INVOKABLE void resendRequest(const QString &fp);
		Q_INVOKABLE void accept(const QString &fp);
		Q_INVOKABLE void decline(const QString &fp);
		Q_INVOKABLE void unfriend(const QString &fp);
		Q_INVOKABLE void block(const QString &fp);
		/** They may send requests again (they become no friend by it). */
		Q_INVOKABLE void unblock(const QString &fp);
		Q_INVOKABLE void setChecked(const QString &fp, bool checked);
		/** A new link: the old one stops working, the friends stay. */
		Q_INVOKABLE void newLink();

		/**
		 * The messages of a chat, oldest first: id, from (fp), name, mine, text, html, at, state
		 * ("sending", "sent", "failed"), join (map), plan, embed, reactions; deleted (a stand-in for
		 * a message its writer deleted), replyTo ({id, name, text, deleted, found}), mentionsMe.
		 */
		Q_INVOKABLE QVariantList messages(const QString &chat) const;
		/** A text message; reply is the id of the message it answers (empty: none). */
		Q_INVOKABLE void send(const QString &chat, const QString &text, const QString &reply = QString());
		/** Deletes one of this player's own messages for everybody in the chat. */
		Q_INVOKABLE void deleteMessage(const QString &chat, const QString &message);
		/** Mutes a chat on this device: minutes, or -1 until turned back on; 0 turns it back on. */
		Q_INVOKABLE void mute(const QString &chat, int minutes);
		/** Until when a chat is muted (ms since 1970; far ahead: until turned back on), 0 if it is not. */
		Q_INVOKABLE double mutedUntil(const QString &chat) const;
		/** The names that can be mentioned in a chat (the members of a group, the friend), without this player. */
		Q_INVOKABLE QVariantList mentionable(const QString &chat) const;
		/** A join card for a room: invite, title, game, console. */
		Q_INVOKABLE void sendJoin(const QString &chat, const QVariantMap &room);
		Q_INVOKABLE void markRead(const QString &chat);

		// ---- groups (a chat whose id is the group's id)
		/** A new group with these friends in it, this player its admin; its id. */
		Q_INVOKABLE QString createGroup(const QString &name, const QStringList &friends);
		/** A group as the panel shows it: id, name, admin (this player is one), add ("admins" or "all"),
		 *  left, members (each: fp, name, admin, friend, me), banned (each: fp, name). Empty if there is none. */
		Q_INVOKABLE QVariantMap group(const QString &chat) const;
		Q_INVOKABLE bool isGroup(const QString &chat) const;
		Q_INVOKABLE void addToGroup(const QString &chat, const QStringList &friends);
		/** Admins only; a ban keeps them from being added again. */
		Q_INVOKABLE void removeFromGroup(const QString &chat, const QString &fp, bool ban);
		Q_INVOKABLE void setGroupAdmin(const QString &chat, const QString &fp, bool admin);
		Q_INVOKABLE void renameGroup(const QString &chat, const QString &name);
		/** "admins" or "all": who may add people. */
		Q_INVOKABLE void setGroupAdd(const QString &chat, const QString &who);
		Q_INVOKABLE void leaveGroup(const QString &chat);

		// ---- agreeing on a time, in any chat
		/** A plan: a title and times to choose from (ms since 1970). */
		Q_INVOKABLE void sendPlan(const QString &chat, const QString &title, const QVariantList &times);
		/** This player's answers to a plan: one per time, 0 no, 1 maybe, 2 yes. */
		Q_INVOKABLE void vote(const QString &chat, const QString &plan, const QVariantList &answers);

		// ---- reactions and who read what
		/** This player's reaction to a message: given, or taken back if it was given already. */
		Q_INVOKABLE void react(const QString &chat, const QString &message, const QString &emoji);
		/** Who has read the chat how far, for their pictures under the last message they saw:
		 *  each fp, name, avatar, at (the newest message they read, by its writer's clock). Empty with read receipts off. */
		Q_INVOKABLE QVariantList readers(const QString &chat) const;

		// ---- reports
		/** Sends these messages of a chat, opened, to the server's operator, with a line of why. */
		Q_INVOKABLE void report(const QString &chat, const QStringList &message_ids, const QString &why);

		// ---- linking another device (docs/friends-protocol.md, "Linking a device")
		QString LinkState() const { return link_state; }
		QString LinkCodeText() const { return link_code; }
		QString LinkText() const { return link_text; }
		/** On the device that has the friends: a link for the new one, then waits for it. */
		Q_INVOKABLE void startLinking();
		/** The codes are the same on both: the keys and friends go to the new device. */
		Q_INVOKABLE void confirmLinking();
		Q_INVOKABLE void cancelLinking();
		/** On the new device (friends off here): the link the other device shows. */
		Q_INVOKABLE void joinLink(const QString &text);

		/** The backup file: everything of the identity, sealed with the passphrase. */
		Q_INVOKABLE bool saveBackup(const QString &path, const QString &passphrase);
		Q_INVOKABLE bool restoreBackup(const QString &path, const QString &passphrase);

	signals:
		void changed();
		void linkLooked(bool ok, const QString &fp, const QString &name, const QString &avatar, const QString &error);
		/** For notifications: a request came, a message came, a friend started hosting. */
		void requestArrived(const QString &fp, const QString &name);
		void messageArrived(const QString &chat, const QString &name, const QString &text);
		void friendHosting(const QString &fp, const QString &name, const QString &title);

	private:
		/** Where an envelope goes: a device's inbox (with a write token), a request box, or a group's box (with its token). */
		enum class Route { Inbox, RequestBox, GroupBox };

		// ---- state, kept in one sealed file
		void Load();
		void Save();
		/** Tells the own other devices about a changed friend, the friends key or the link key. */
		void SyncOwn(const QString &friend_fp = QString(), bool fk = false, bool lk = false);
		/** The own friends key, for one friend. */
		void SendFriendsKey(const QString &fp);

		// ---- talking to the server
		using Reply = std::function<void(bool ok, const QJsonObject &answer)>;
		/** A request to the server; signed ones are made again once when the server says this computer's clock is off. */
		void Call(const QString &op, const QMap<QString, QString> &fields, bool sign, Reply done, bool again = false);
		void Poll();
		/** The card with this device on it (and the other own devices), or without the device `without`. */
		void PublishCard(const QString &without = QString(), std::function<void()> after = nullptr);
		void TopUpPrekeys();
		void PutStatus();

		// ---- conversations with friends' devices
		bool Identity(coplay::friends::Identity &out) const;
		/** Seals a payload for every device of a friend (and of the own identity), and sends it. */
		void SendPayload(const QString &fp, QJsonObject payload, bool to_own_devices = true, std::function<void(bool)> done = nullptr);
		/** A sealed payload for every device of one identity (but this one), the way the route says. */
		void SendSealed(const QString &fp, const QByteArray &plain, Route route, const QString &target, const QString &token,
			std::function<void(bool)> done);
		/** A card, from the cache if it was fetched a short while ago, else from the server. */
		void WithCard(const QString &fp, std::function<void(bool ok, const coplay::friends::Card &card)> done);
		void SendToDevice(const QString &fp, const coplay::friends::Card &card, const coplay::friends::CardDevice &device,
			const QJsonObject &opk, const QByteArray &plain, Route route, const QString &target, const QString &token, std::function<void(bool)> done);
		/** Notes from an inbox or the request box: unknown senders' cards first, then each one, then the ack. */
		void TakeNotes(const QJsonArray &notes, bool from_request_box, const QString &ack_field, const QString &ack_value);
		/** False if it has to wait (the sender's card is missing); true once it is done with, opened or not. */
		bool HandleEnvelope(const QByteArray &note, bool from_request_box, const QString &group = QString());
		void HandlePayload(const QString &from_fp, const QJsonObject &payload, bool own_device, const QString &from_device = QString(),
			const QString &group = QString());

		// ---- groups
		/** A payload sealed for every device of one member, through the group's box (members need not be friends). */
		void SendInGroup(const QString &gid, const QString &fp, const QJsonObject &payload);
		/** A group message: sealed once with this device's sender key, into the group's box. */
		void SendGroup(const QString &gid, QJsonObject payload, std::function<void(bool)> done = nullptr);
		/** This device's sender key, new, to every member's devices. */
		void NewSenderKey(const QString &gid);
		/** The group's newest version, signed by this player, to every member (and to whoever was removed). */
		void PublishGroup(const QString &gid, const QStringList &also_to = QStringList());
		bool ApplyGroup(const QString &from, const QJsonObject &payload);
		void PollGroups();
		void TakeGroupNotes(const QString &gid, const QJsonArray &notes);
		/** False if the writer's sender key has not come yet. */
		bool HandleGroupMessage(const QString &gid, const QByteArray &note);
		/** Sends to a chat, whichever it is: a friend's devices, or a group. */
		void SendToChat(const QString &chat, const QJsonObject &payload, std::function<void(bool)> done = nullptr);
		/** A message of one's own: sent, and marked sent or not sent. */
		void SendOwnMessage(const QString &chat, const QJsonObject &payload);
		/** The link preview of a message of one's own, once it is made. */
		void SetOwnEmbed(const QString &chat, const QString &id, const QJsonObject &embed);
		/** The identity from a backup or another device: this computer becomes one more device of it. */
		bool RestoreFrom(QJsonObject restored);
		/** What a backup or a linked device gets: everything but this device's own keys and conversations. */
		QJsonObject Bundle() const;
		void PollLink();
		/** The digits from the other side's key; false if it is no key. */
		bool LinkKeyFrom(const coplay::friends::Bytes &data);
		void LinkFailed(const QString &why);
		/** Somebody read a chat up to the message written at `at` (the writer's clock). */
		void ApplyRead(const QString &chat, const QString &fp, double at);
		/** Another own device read it: here too. */
		void MarkSeenUpTo(const QString &chat, double at);
		/** A plan's answers from one player, kept with the plan. */
		void ApplyVote(const QString &chat, const QString &plan, const QString &fp, const QJsonValue &answers);
		/** A reaction from one player to a message, given or taken back. */
		/** False if it changed nothing (given already, over the limits, no such message). */
		bool ApplyReaction(const QString &chat, const QString &message, const QString &fp, const QString &emoji, bool on);
		/** A message deleted by its writer (by): a stand-in in its place; one not here yet is kept out when it comes. */
		bool ApplyDelete(const QString &chat, const QString &message, const QString &by);
		/** A message of a chat by id, or empty. */
		QJsonObject FindMessage(const QString &chat, const QString &id) const;
		bool Muted(const QString &chat) const;
		/** A message arrived: a notification, unless the chat is muted and it does not mention this player. */
		void Notify(const QString &chat, const QString &name, const QString &text);
		/** A new owner key for a group (when an admin goes): signed with the one before. */
		void RotateOwner(const QString &gid, std::function<void(bool)> then);
		/** Of this identity's devices, the one that acts where only one may (the lowest id). */
		bool ActingDevice() const;
		/** A request signed with a group's owner key (group_token). */
		void CallOwner(const QString &op, QMap<QString, QString> fields, const coplay::friends::KeyPair &owner, Reply done);
		void AddMessage(const QString &chat, const QJsonObject &message);

		QNetworkAccessManager *network = nullptr;
		QTimer poll_timer;
		QTimer status_timer;
		QString folder;
		QString secret_name;
		QString server;
		bool enabled = false;
		bool online = false;
		bool background = false;
		QString problem;
		QString my_state = QStringLiteral("online");
		QString my_free;
		QString activity;
		QVariantMap room;
		QString profile_name;
		QByteArray profile_avatar;
		QJsonObject state;
		/** Friends' statuses, as last read (fp -> status object); not kept. */
		QHash<QString, QJsonObject> statuses;
		/** When each card was last fetched (fp -> ms); not kept. */
		QHash<QString, qint64> card_fetched;
		// ---- linking, while it runs
		QTimer link_timer;
		QString link_state;
		QString link_code;
		QString link_text;
		coplay::friends::Id16 link_secret{};
		coplay::friends::KeyPair link_ek;
		coplay::friends::Key32 link_key{};
		/** On the new device: the hash the other side committed to before seeing this side's key. */
		coplay::friends::Key32 link_commit{};
		/** 1 the device that has the friends, 2 the new one. */
		int link_side = 0;
		bool link_busy = false;
		/** When the new device joined a link: a box that stays empty after that has run out. */
		qint64 link_joined_ms = 0;
		qint64 link_started_ms = 0;
		/** Sender keys taken, counted: a batch of group notes tries again what waited if one came in it. */
		int sender_keys_taken = 0;
		/** Counts up with every switch on or off: a late clean-up never touches a newer identity. */
		int generation = 0;
		bool link_previews = true;
		int pending_calls = 0;
		/** Seconds this computer's clock is behind the server's, once the server said so. */
		qint64 clock_offset = 0;
		/** One status note on its way at a time: two could arrive the wrong way round. */
		bool status_in_flight = false;
		bool status_again = false;
		/** The status note was taken away (invisible, or shown to nobody). */
		bool status_hidden = false;
		qint64 last_status_put = 0;
};

#endif // CHIAKI_FRIENDS_H
