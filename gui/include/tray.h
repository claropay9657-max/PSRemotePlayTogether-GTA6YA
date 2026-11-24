// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_TRAY_H
#define CHIAKI_TRAY_H

#include <QElapsedTimer>
#include <QHash>
#include <QIcon>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QSystemTrayIcon>
#include <QVariantMap>

class FriendsClient;
class QmlSettings;
class QmlMainWindow;
class QAction;
class QActionGroup;
class QLocalServer;
class QMenu;

/*
 * The icon in the tray (the menu bar on a Mac), docs/friends.md "Tray, start with the computer,
 * notifications": the app's own icon with the player's friends status as a dot, a menu (open the
 * app, the status, the friends who host now, quit), the system's notifications for friends, and
 * the app kept in the tray when its window is closed (Settings, General).
 *
 * Shown while friends are on, while closing the window keeps the app in the tray, and whenever
 * the window is hidden in it. Notifications only come while the icon is shown, never while the
 * player is Busy or plays, and not while the window is in front.
 *
 * Exposed to QML as Chiaki.tray; what the app does with a chat to open or a room to join is the
 * QML's (openChatRequested, joinRequested).
 */
class TrayIcon : public QObject
{
	Q_OBJECT

	/** This desktop has a tray to keep the app in. */
	Q_PROPERTY(bool available READ Available NOTIFY changed)
	/** The window is hidden: the app is in the tray only. */
	Q_PROPERTY(bool hidden READ Hidden NOTIFY changed)
	/** The system shows notifications from the tray here. */
	Q_PROPERTY(bool notificationsPossible READ NotificationsPossible CONSTANT)

	public:
		TrayIcon(FriendsClient *friends, QmlSettings *settings, QmlMainWindow *window, QObject *parent = nullptr);
		~TrayIcon() override;

		static bool Available();
		bool Hidden() const { return hidden; }
		static bool NotificationsPossible();

		/**
		 * A start in the tray: the window is not even made until it is opened. Where the desktop has
		 * no tray (yet: at login it may come a little later), the window opens instead.
		 */
		void StartHidden();
		/** A game session runs: no notifications, and the window comes out of the tray. */
		void SetPlaying(bool playing);

		/**
		 * Another start of the app while this one sits in the tray: it is told to show its window
		 * (or, for a start in the tray, nothing), and true means the new start should end.
		 */
		static bool HandOver(bool tray);

		Q_INVOKABLE void showWindow();
		Q_INVOKABLE void hideToTray();
		/** Quits the app; a running session is asked to end first, as with closing the window. */
		Q_INVOKABLE void quit();

	signals:
		void changed();
		/** A click on a notification: the chat to open (for a request or a friend who hosts: the friend's fp). */
		void openChatRequested(const QString &chat);
		/** "Hosting now" in the menu: the friend's room to join (with the friend's fp and name added). */
		void joinRequested(const QVariantMap &room);
		/** The window was opened from the tray. */
		void showWindowRequested();

	protected:
		bool eventFilter(QObject *watched, QEvent *event) override;

	private:
		/** The window hidden: the icon shown, friends asked less often, no Dock icon on a Mac. */
		void EnterTray();
		void LeaveTray();
		void WaitForTray();
		void Update();
		void UpdateMenu();
		QIcon StatusIcon(const QString &state);
		void Notify(const QString &title, const QString &text, const QString &chat);
		void Listen(bool on);
		void Activated(QSystemTrayIcon::ActivationReason reason);

		QPointer<FriendsClient> friends;
		QPointer<QmlSettings> settings;
		QPointer<QmlMainWindow> window;
		QSystemTrayIcon *tray = nullptr;
		QMenu *menu = nullptr;
		QAction *open_action = nullptr;
		QAction *status_separator = nullptr;
		QActionGroup *status_group = nullptr;
		QHash<QString, QAction *> status_actions;
		QMenu *hosting_menu = nullptr;
		QAction *hosting_action = nullptr;
		QLocalServer *server = nullptr;

		QHash<QString, QIcon> icons;
		QString icon_state;
		/** What the menu shows of the friends, to build it again only when that changed. */
		QString menu_key;
		/** The chat of the last notification, for a click on it. */
		QString last_chat;

		/** The window was shown once: its QML is made (QmlMainWindow::show). */
		bool shown_once = false;
		bool hidden = false;
		bool playing = false;
		/** The app quits: a close lets the window go. */
		bool quitting = false;
		/** The QML asked to quit (Qt.quit()), which arrives as a close of the window. */
		bool quit_asked = false;
		bool close_pending = false;
		QElapsedTimer hidden_since;
		Qt::ApplicationState app_state = Qt::ApplicationInactive;
		int tray_waits = 0;
};

#endif // CHIAKI_TRAY_H
