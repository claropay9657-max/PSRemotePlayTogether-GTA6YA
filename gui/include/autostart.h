// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_AUTOSTART_H
#define CHIAKI_AUTOSTART_H

#include <QCommandLineOption>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

/*
 * Starting with the computer (docs/friends.md, "Tray, start with the computer, notifications").
 *
 * Windows: a value in the user's Run key (HKCU\Software\Microsoft\Windows\CurrentVersion\Run)
 * that names this program. Written again at each start while it is on, so the program is still
 * found after the installer or the in-app update put it somewhere else.
 * macOS 13 and later: the app as a login item (SMAppService), listed in System Settings under
 * General, Login Items. The system keeps it; what it says is what the settings show.
 * Linux: ~/.config/autostart/psremoteplaytogether.desktop, pointing at the AppImage ($APPIMAGE),
 * or else at the running program; written again at each start as well.
 *
 * Starting in the tray travels as --tray on the entry. A Mac login item cannot carry arguments:
 * there the app asks the system how it was opened (WhenLaunched).
 *
 * The parts that only put text together are tried on their own by test/autostart.cpp.
 */
namespace autostart
{
	/** The option of a start in the tray: --tray. */
	QCommandLineOption TrayOption();
	/** The arguments an entry starts the program with. */
	QStringList Arguments(bool tray);
	/** One argument of the Exec line of a .desktop file, quoted and escaped where it needs it. */
	QString DesktopExecArgument(const QString &argument);
	/** The whole .desktop file for ~/.config/autostart. */
	QString DesktopEntry(const QString &program, bool tray);
	/** The Run value on Windows: the program in quotes, then the arguments. */
	QString RunCommand(const QString &program, bool tray);

	// ---- this computer

	/** Whether this system can start the app with the computer; if not, why (for the settings). */
	bool Possible(QString *why = nullptr);
	/** Whether the app starts with the computer, as the system has it now. */
	bool IsOn();
	/** Switches it on or off; false and why if that did not work. */
	bool Set(bool on, bool tray, QString *error = nullptr);
	/** At a start: an entry that is there is written again for this program (Windows, Linux). */
	void Refresh(bool tray);
	/** What the settings should say beside it: an approval the system waits for, say. */
	QString Note();

#ifdef Q_OS_MACOS
	/**
	 * Once the app has finished launching: whether it was opened as a login item. Called once,
	 * with false where that cannot be told (already launched, or no word within a few seconds).
	 */
	void WhenLaunched(QObject *context, std::function<void(bool at_login)> done);
	/** The Dock icon: gone while the app sits in the menu bar only, back with the window. */
	void SetDockIcon(bool shown);
#endif
}

#endif // CHIAKI_AUTOSTART_H
