// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace autostart
{

// ------------------------------------------------------------------ text only (test/autostart.cpp)

QCommandLineOption TrayOption()
{
	return QCommandLineOption(QStringLiteral("tray"),
		QStringLiteral("Start in the tray, without the window (how the app starts with the computer)."));
}

QStringList Arguments(bool tray)
{
	if(!tray)
		return {};
	return { QStringLiteral("--tray") };
}

QString DesktopExecArgument(const QString &argument)
{
	// The Desktop Entry Specification, "The Exec key": an argument with a reserved character
	// goes in double quotes, and in there ", `, $ and \ get a backslash. A % is doubled, or it
	// would be a field code.
	static const QString reserved = QStringLiteral(" \t\n\"'\\><~|&;$*?#()`");
	QString arg = argument;
	arg.replace(QLatin1Char('%'), QStringLiteral("%%"));
	bool quote = arg.isEmpty();
	for(const QChar c : std::as_const(arg))
	{
		if(reserved.contains(c))
		{
			quote = true;
			break;
		}
	}
	if(quote)
	{
		QString quoted;
		for(const QChar c : std::as_const(arg))
		{
			if(c == QLatin1Char('"') || c == QLatin1Char('`') || c == QLatin1Char('$') || c == QLatin1Char('\\'))
				quoted += QLatin1Char('\\');
			quoted += c;
		}
		arg = QLatin1Char('"') + quoted + QLatin1Char('"');
	}
	// Then the escapes of every string value of the file, which are taken off first when it is
	// read: so a backslash in a quoted argument ends up as four
	QString escaped;
	for(const QChar c : std::as_const(arg))
	{
		if(c == QLatin1Char('\\'))
			escaped += QStringLiteral("\\\\");
		else if(c == QLatin1Char('\n'))
			escaped += QStringLiteral("\\n");
		else if(c == QLatin1Char('\t'))
			escaped += QStringLiteral("\\t");
		else if(c == QLatin1Char('\r'))
			escaped += QStringLiteral("\\r");
		else
			escaped += c;
	}
	return escaped;
}

QString DesktopEntry(const QString &program, bool tray)
{
	QStringList exec = { DesktopExecArgument(program) };
	for(const QString &arg : Arguments(tray))
		exec.append(DesktopExecArgument(arg));
	return QStringLiteral(
		"[Desktop Entry]\n"
		"Type=Application\n"
		"Name=PSRemotePlayTogether\n"
		"Comment=Starts with the computer (Settings, General)\n"
		"Exec=%1\n"
		"Icon=chiaking\n"
		"Terminal=false\n"
		"X-GNOME-Autostart-enabled=true\n").arg(exec.join(QLatin1Char(' ')));
}

QString RunCommand(const QString &program, bool tray)
{
	// Windows splits a command line at spaces outside quotes; a path cannot hold a quote
	QString path = program;
	path.replace(QLatin1Char('/'), QLatin1Char('\\'));
	QStringList parts = { QLatin1Char('"') + path + QLatin1Char('"') };
	parts.append(Arguments(tray));
	return parts.join(QLatin1Char(' '));
}

// ------------------------------------------------------------------ this computer

#if defined(Q_OS_WIN)

static const char run_key[] = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const char run_value[] = "PSRemotePlayTogether";

bool Possible(QString *why)
{
	if(why)
		why->clear();
	return true;
}

bool IsOn()
{
	QSettings run(QString::fromLatin1(run_key), QSettings::NativeFormat);
	return run.contains(QString::fromLatin1(run_value));
}

bool Set(bool on, bool tray, QString *error)
{
	QSettings run(QString::fromLatin1(run_key), QSettings::NativeFormat);
	if(on)
		run.setValue(QString::fromLatin1(run_value), RunCommand(QCoreApplication::applicationFilePath(), tray));
	else
		run.remove(QString::fromLatin1(run_value));
	run.sync();
	if(run.status() != QSettings::NoError)
	{
		if(error)
			*error = QCoreApplication::translate("Autostart", "Windows did not let the app change its start with the computer.");
		return false;
	}
	return true;
}

QString Note()
{
	return QString();
}

#elif defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)

static QString EntryPath()
{
	return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
		+ QStringLiteral("/autostart/psremoteplaytogether.desktop");
}

static QString Program()
{
	// The AppImage, not the program unpacked from it for this run only
	const QString appimage = qEnvironmentVariable("APPIMAGE");
	if(!appimage.isEmpty() && QFileInfo(appimage).isFile())
		return appimage;
	return QCoreApplication::applicationFilePath();
}

bool Possible(QString *why)
{
	// A Flatpak sees a folder of its own as ~/.config: the desktop would never read the entry
	if(qEnvironmentVariableIsSet("FLATPAK_ID"))
	{
		if(why)
			*why = QCoreApplication::translate("Autostart", "Not from a Flatpak: add PSRemotePlayTogether in your desktop's own startup settings.");
		return false;
	}
	if(why)
		why->clear();
	return true;
}

bool IsOn()
{
	return QFileInfo(EntryPath()).isFile();
}

bool Set(bool on, bool tray, QString *error)
{
	const QString path = EntryPath();
	if(!on)
	{
		if(QFile::exists(path) && !QFile::remove(path))
		{
			if(error)
				*error = QCoreApplication::translate("Autostart", "Could not remove %1.").arg(path);
			return false;
		}
		return true;
	}
	QString why;
	if(!Possible(&why))
	{
		if(error)
			*error = why;
		return false;
	}
	QDir().mkpath(QFileInfo(path).absolutePath());
	QSaveFile file(path);
	if(!file.open(QIODevice::WriteOnly) || file.write(DesktopEntry(Program(), tray).toUtf8()) < 0 || !file.commit())
	{
		if(error)
			*error = QCoreApplication::translate("Autostart", "Could not write %1.").arg(path);
		return false;
	}
	return true;
}

QString Note()
{
	return QString();
}

#elif !defined(Q_OS_MACOS)

bool Possible(QString *why)
{
	if(why)
		*why = QCoreApplication::translate("Autostart", "Not on this system.");
	return false;
}

bool IsOn()
{
	return false;
}

bool Set(bool on, bool, QString *error)
{
	if(!on)
		return true;
	Possible(error);
	return false;
}

QString Note()
{
	return QString();
}

#endif

#if !defined(Q_OS_MACOS)
void Refresh(bool tray)
{
	if(IsOn())
		Set(true, tray);
}
#endif

}
