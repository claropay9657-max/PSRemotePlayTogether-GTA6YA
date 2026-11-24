// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "tray.h"

#include "autostart.h"
#include "friends.h"
#include "qmlmainwindow.h"
#include "qmlsettings.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QImage>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QPainter>
#include <QQmlEngine>
#include <QSvgRenderer>
#include <QTimer>

#include <algorithm>

namespace
{

/** The friends list's colours of a status (controls/Theme.qml: good, wait, bad, textDim). */
QColor StateColor(const QString &state)
{
	if(state == QLatin1String("online"))
		return QColor(0x34, 0xd3, 0x99);
	if(state == QLatin1String("away"))
		return QColor(0xf5, 0xb9, 0x42);
	if(state == QLatin1String("busy"))
		return QColor(0xf0, 0x46, 0x6e);
	return QColor(0x8f, 0x8a, 0xad);
}

/** Where a second start finds the app in the tray: one for each user of the computer. */
QString ServerName()
{
	const QByteArray who = (QDir::homePath() + QLatin1Char('|') + QCoreApplication::organizationName()
		+ QLatin1Char('|') + QCoreApplication::applicationName()).toUtf8();
	return QStringLiteral("psrpt-tray-") + QString::fromLatin1(QCryptographicHash::hash(who, QCryptographicHash::Sha256).toHex().left(16));
}

/** A name for a menu: an & would underline the letter after it. */
QString MenuText(QString text)
{
	return text.replace(QLatin1Char('&'), QStringLiteral("&&"));
}

}

TrayIcon::TrayIcon(FriendsClient *friends_client, QmlSettings *qml_settings, QmlMainWindow *main_window, QObject *parent)
	: QObject(parent),
	friends(friends_client),
	settings(qml_settings),
	window(main_window)
{
	// A menu takes a widget as its parent, not an object: it is deleted with this one
	menu = new QMenu();
	open_action = menu->addAction(tr("Open PSRemotePlayTogether"));
	connect(open_action, &QAction::triggered, this, [this]() {
		showWindow();
		emit showWindowRequested();
	});

	// The friends status, while friends are on
	status_separator = menu->addSeparator();
	status_group = new QActionGroup(this);
	status_group->setExclusive(true);
	const QList<QPair<QString, QString>> states = {
		{ QStringLiteral("online"), tr("Online") },
		{ QStringLiteral("away"), tr("Away") },
		{ QStringLiteral("busy"), tr("Busy") },
		{ QStringLiteral("invisible"), tr("Invisible") },
	};
	for(const auto &state : states)
	{
		QAction *action = menu->addAction(state.second);
		action->setCheckable(true);
		status_group->addAction(action);
		const QString name = state.first;
		connect(action, &QAction::triggered, this, [this, name]() {
			if(this->friends)
				this->friends->SetMyState(name);
		});
		status_actions.insert(name, action);
	}

	// The friends who host now, each with a click to join
	menu->addSeparator();
	hosting_menu = new QMenu(tr("Hosting now"), menu);
	hosting_action = menu->addMenu(hosting_menu);
	hosting_action->setVisible(false);

	menu->addSeparator();
	QAction *quit_action = menu->addAction(tr("Quit"));
	connect(quit_action, &QAction::triggered, this, &TrayIcon::quit);
	// Not moved into the application menu on a Mac
	for(QAction *action : menu->actions())
		action->setMenuRole(QAction::NoRole);

	tray = new QSystemTrayIcon(this);
	tray->setContextMenu(menu);
#ifndef Q_OS_MACOS
	// On a Mac, Qt tells of the menu being opened as a click as well: there the menu's Open does it
	connect(tray, &QSystemTrayIcon::activated, this, &TrayIcon::Activated);
#endif
	connect(tray, &QSystemTrayIcon::messageClicked, this, [this]() {
		showWindow();
		if(!last_chat.isEmpty())
			emit openChatRequested(last_chat);
	});

	if(friends)
	{
		connect(friends, &FriendsClient::changed, this, &TrayIcon::Update);
		connect(friends, &FriendsClient::requestArrived, this, [this](const QString &fp, const QString &name) {
			Notify(tr("Friend request"), tr("%1 wants to be your friend.").arg(name.isEmpty() ? tr("Somebody") : name), fp);
		});
		connect(friends, &FriendsClient::messageArrived, this, [this](const QString &chat, const QString &name, const QString &text) {
			QString title = name.isEmpty() ? tr("A friend") : name;
			if(this->friends)
			{
				for(const QVariant &entry : this->friends->Chats())
				{
					const QVariantMap c = entry.toMap();
					if(c.value(QStringLiteral("id")).toString() != chat)
						continue;
					if(c.value(QStringLiteral("kind")).toString() == QLatin1String("group"))
						title = tr("%1 in %2").arg(title, c.value(QStringLiteral("title")).toString());
					break;
				}
			}
			// Who wrote, not what, unless the player wants the words
			QString body = tr("New message");
			if(this->settings && this->settings->notificationWords() && !text.trimmed().isEmpty())
			{
				body = text.simplified();
				if(body.size() > 200)
					body = body.left(199) + QChar(0x2026);
			}
			Notify(title, body, chat);
		});
		connect(friends, &FriendsClient::friendHosting, this, [this](const QString &fp, const QString &name, const QString &title) {
			Notify(tr("%1 is hosting").arg(name.isEmpty() ? tr("A friend") : name),
				title.isEmpty() ? tr("Join from the friends list.") : title, fp);
		});
	}
	if(settings)
		connect(settings, &QmlSettings::trayChanged, this, &TrayIcon::Update);

	if(window)
	{
		window->installEventFilter(this);
		// Qt.quit() in the QML closes the window: that close has to quit, not go to the tray
		if(QQmlEngine *engine = window->findChild<QQmlEngine *>(QString(), Qt::FindDirectChildrenOnly))
		{
			connect(engine, &QQmlEngine::quit, this, [this]() {
				quit_asked = true;
				QTimer::singleShot(0, this, [this]() { quit_asked = false; });
			});
		}
	}
	// To see the app being quit (QEvent::Quit) before its windows are closed
	qApp->installEventFilter(this);

#ifdef Q_OS_MACOS
	// Opening the app again (Finder, Spotlight, the Dock) while it sits in the menu bar
	app_state = QGuiApplication::applicationState();
	connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
		// Asked to open while active already, the app is told "active" once more. Becoming
		// active at all only counts a while after the window went: a start at login may be
		// made active by the system as well.
		const bool again = state == Qt::ApplicationActive
			&& (app_state == Qt::ApplicationActive || (hidden_since.isValid() && hidden_since.elapsed() > 3000));
		app_state = state;
		if(again && hidden)
		{
			showWindow();
			emit showWindowRequested();
		}
	});
#endif

	Update();
}

TrayIcon::~TrayIcon()
{
	tray->setContextMenu(nullptr);
	delete menu;
}

bool TrayIcon::Available()
{
	return QSystemTrayIcon::isSystemTrayAvailable();
}

bool TrayIcon::NotificationsPossible()
{
	return QSystemTrayIcon::supportsMessages();
}

void TrayIcon::StartHidden()
{
	if(shown_once || hidden)
		return;
	EnterTray();
	WaitForTray();
}

void TrayIcon::WaitForTray()
{
	if(!hidden)
		return;
	if(Available())
	{
		Update();
		return;
	}
	// At login the desktop's tray may come a little after the app; without one, the window opens
	if(++tray_waits <= 20)
	{
		QTimer::singleShot(1000, this, &TrayIcon::WaitForTray);
		return;
	}
	showWindow();
}

void TrayIcon::SetPlaying(bool on)
{
	playing = on;
	// A session needs its window
	if(on && hidden)
		showWindow();
}

void TrayIcon::EnterTray()
{
	hidden = true;
	hidden_since.start();
	// Hidden is not closed, but another window closing must not end the app now
	QGuiApplication::setQuitOnLastWindowClosed(false);
	if(friends)
		friends->SetBackground(true);
#ifdef Q_OS_MACOS
	autostart::SetDockIcon(false);
#endif
	Listen(true);
	Update();
	emit changed();
}

void TrayIcon::LeaveTray()
{
	hidden = false;
	hidden_since.invalidate();
	Listen(false);
	QGuiApplication::setQuitOnLastWindowClosed(true);
	if(friends)
		friends->SetBackground(false);
	Update();
	emit changed();
}

void TrayIcon::showWindow()
{
	if(!window)
		return;
	if(hidden)
		LeaveTray();
#ifdef Q_OS_MACOS
	// Back in the Dock, and the app in front
	autostart::SetDockIcon(true);
#endif
	if(!shown_once)
	{
		// Started in the tray: QmlMainWindow::show makes the window's QML, then shows it
		window->show();
	}
	else
	{
		window->setWindowStates(window->windowStates() & ~Qt::WindowMinimized);
		window->setVisible(true);
	}
	window->raise();
	window->requestActivate();
}

void TrayIcon::hideToTray()
{
	if(!window || hidden || playing || !Available())
		return;
	window->hide();
	EnterTray();
}

void TrayIcon::quit()
{
	quitting = true;
	if(window && shown_once)
	{
		// A running session: its window shows the question whether to end it
		if(playing && hidden)
			showWindow();
		if(!window->close())
		{
			quitting = false;
			return;
		}
	}
	QCoreApplication::quit();
}

bool TrayIcon::HandOver(bool tray)
{
	QLocalSocket socket;
	socket.connectToServer(ServerName());
	if(!socket.waitForConnected(500))
		return false;
	socket.write(tray ? "tray\n" : "show\n");
	socket.flush();
	socket.waitForBytesWritten(500);
	socket.disconnectFromServer();
	if(socket.state() != QLocalSocket::UnconnectedState)
		socket.waitForDisconnected(500);
	return true;
}

void TrayIcon::Listen(bool on)
{
	if(!on)
	{
		if(server)
		{
			server->close();
			server->deleteLater();
			server = nullptr;
		}
		return;
	}
	if(server)
		return;
	server = new QLocalServer(this);
	server->setSocketOptions(QLocalServer::UserAccessOption);
	const QString name = ServerName();
	if(!server->listen(name))
	{
		// Left behind by an app that did not end properly
		QLocalServer::removeServer(name);
		if(!server->listen(name))
		{
			delete server;
			server = nullptr;
			return;
		}
	}
	connect(server, &QLocalServer::newConnection, this, [this]() {
		while(server && server->hasPendingConnections())
		{
			QLocalSocket *socket = server->nextPendingConnection();
			connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
			connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
				if(!socket->canReadLine())
					return;
				const QByteArray line = socket->readLine(64).trimmed();
				socket->disconnectFromServer();
				if(line == "show")
				{
					showWindow();
					emit showWindowRequested();
				}
			});
		}
	});
}

void TrayIcon::Activated(QSystemTrayIcon::ActivationReason reason)
{
	// A click on the icon opens the window; the menu is on the other button
	if(reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
	{
		showWindow();
		emit showWindowRequested();
	}
}

void TrayIcon::Notify(const QString &title, const QString &text, const QString &chat)
{
	if(!settings || !settings->notifications() || playing)
		return;
	if(friends && friends->MyState() == QLatin1String("busy"))
		return;
	if(!tray->isVisible() || !QSystemTrayIcon::supportsMessages())
		return;
	// The window in front shows it itself
	if(window && !hidden && window->isActive())
		return;
	last_chat = chat;
#ifdef Q_OS_MACOS
	// The notification centre shows the app's icon by itself
	tray->showMessage(title, text, QSystemTrayIcon::NoIcon, 10000);
#elif defined(Q_OS_WIN)
	tray->showMessage(title, text, StatusIcon(QString()), 10000);
#else
	// Notification servers on Linux may read the body as markup: a friend's name or words stay text
	tray->showMessage(title, text.toHtmlEscaped(), StatusIcon(QString()), 10000);
#endif
}

QIcon TrayIcon::StatusIcon(const QString &state)
{
	const auto known = icons.constFind(state);
	if(known != icons.constEnd())
		return known.value();
	// The app's own icon (the window's), with the status as a dot in its corner
	QSvgRenderer svg(QStringLiteral(":/icons/chiaking.svg"));
	QIcon icon;
	for(const int size : { 16, 20, 22, 24, 32, 40, 44, 48, 64, 128 })
	{
		QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::transparent);
		QPainter painter(&image);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setRenderHint(QPainter::SmoothPixmapTransform);
		svg.render(&painter, QRectF(0, 0, size, size));
		if(!state.isEmpty())
		{
			const qreal d = size * 0.46;
			const qreal edge = std::max<qreal>(1.0, size * 0.08);
			const QRectF dot(size - d, size - d, d, d);
			painter.setPen(Qt::NoPen);
			// A dark edge (Theme.ground), so the dot stands out on light and dark trays
			painter.setBrush(QColor(0x0c, 0x0a, 0x1d));
			painter.drawEllipse(dot);
			painter.setBrush(StateColor(state));
			painter.drawEllipse(dot.adjusted(edge, edge, -edge, -edge));
			// Invisible: a ring, as nobody sees it
			if(state == QLatin1String("invisible"))
			{
				const qreal hole = d * 0.26;
				painter.setBrush(QColor(0x0c, 0x0a, 0x1d));
				painter.drawEllipse(dot.adjusted(hole, hole, -hole, -hole));
			}
		}
		painter.end();
		icon.addPixmap(QPixmap::fromImage(image));
	}
	icons.insert(state, icon);
	return icon;
}

void TrayIcon::Update()
{
	const bool on = friends && friends->Enabled();
	const QString state = on ? friends->MyState() : QString();
	if(state != icon_state || tray->icon().isNull())
	{
		icon_state = state;
		tray->setIcon(StatusIcon(state));
	}

	QString tip = QStringLiteral("PSRemotePlayTogether");
	if(on)
	{
		const QAction *action = status_actions.value(state);
		if(action)
			tip += QStringLiteral(" · ") + action->text();
	}
	tray->setToolTip(tip);

	const bool shown = Available() && (hidden || on || (settings && settings->closeToTray()));
	if(tray->isVisible() != shown)
		tray->setVisible(shown);
	UpdateMenu();
}

void TrayIcon::UpdateMenu()
{
	const bool on = friends && friends->Enabled();
	status_separator->setVisible(on);
	for(auto it = status_actions.cbegin(); it != status_actions.cend(); ++it)
	{
		it.value()->setVisible(on);
		it.value()->setChecked(on && it.key() == friends->MyState());
	}

	// Built again only when somebody started or stopped hosting, or a name or title changed
	QList<QVariantMap> hosts;
	QStringList key;
	if(on)
	{
		for(const QVariant &entry : friends->Friends())
		{
			const QVariantMap f = entry.toMap();
			const QVariantMap room = f.value(QStringLiteral("room")).toMap();
			if(room.isEmpty() || f.value(QStringLiteral("state")).toString() == QLatin1String("offline"))
				continue;
			hosts.append(f);
			key.append(f.value(QStringLiteral("fp")).toString() + QLatin1Char('\x1f') + f.value(QStringLiteral("name")).toString()
				+ QLatin1Char('\x1f') + room.value(QStringLiteral("title")).toString());
		}
	}
	const QString menu_now = key.join(QLatin1Char('\x1e'));
	if(menu_now != menu_key)
	{
		menu_key = menu_now;
		hosting_menu->clear();
		for(const QVariantMap &f : std::as_const(hosts))
		{
			const QString fp = f.value(QStringLiteral("fp")).toString();
			const QString name = f.value(QStringLiteral("name")).toString();
			const QString title = f.value(QStringLiteral("room")).toMap().value(QStringLiteral("title")).toString();
			QAction *action = hosting_menu->addAction(MenuText(title.isEmpty() ? name : tr("%1 — %2").arg(name, title)));
			action->setMenuRole(QAction::NoRole);
			connect(action, &QAction::triggered, this, [this, fp, name]() {
				// The room as it is now, not as it was when the menu was made
				QVariantMap room;
				if(friends)
				{
					for(const QVariant &entry : friends->Friends())
					{
						const QVariantMap now = entry.toMap();
						if(now.value(QStringLiteral("fp")).toString() == fp)
						{
							room = now.value(QStringLiteral("room")).toMap();
							break;
						}
					}
				}
				showWindow();
				if(room.isEmpty())
					return;
				if(!room.contains(QStringLiteral("fp")))
					room.insert(QStringLiteral("fp"), fp);
				if(!room.contains(QStringLiteral("name")))
					room.insert(QStringLiteral("name"), name);
				emit joinRequested(room);
			});
		}
	}
	hosting_action->setVisible(!hosts.isEmpty());
}

bool TrayIcon::eventFilter(QObject *watched, QEvent *event)
{
	if(watched == qApp)
	{
		if(event->type() == QEvent::Quit)
		{
			// The app is being quit (Quit in a Mac's menu or Dock, logging out, the code): its
			// window goes for good. Should something keep it open, closing is closing again.
			quitting = true;
			QTimer::singleShot(0, this, [this]() { quitting = false; });
		}
		return false;
	}
	if(watched == window)
	{
		switch(event->type())
		{
			case QEvent::Show:
				shown_once = true;
				// Something else brought the window back
				if(hidden)
					LeaveTray();
				break;
			case QEvent::Close:
				// A session decides on its own whether the window closes (QmlBackend::closeRequested)
				if(quitting || playing || !settings || !settings->closeToTray() || !Available())
					break;
				event->ignore();
				// Decided a moment later: a close that Qt.quit() made is followed by quit_asked
				if(!close_pending)
				{
					close_pending = true;
					QTimer::singleShot(0, this, [this]() {
						close_pending = false;
						if(quit_asked)
							quit();
						else
							hideToTray();
					});
				}
				return true;
			default:
				break;
		}
	}
	return QObject::eventFilter(watched, event);
}
