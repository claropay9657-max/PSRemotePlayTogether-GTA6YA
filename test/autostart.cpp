// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// Starting with the computer, the parts that only put text together (gui/src/autostart.cpp):
// the .desktop file of Linux, the Run value of Windows, and the --tray flag both carry, read
// back the way the desktop, Windows and the app's own command line read them.

#include "autostart.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QProcess>

#include <cstdio>

using namespace autostart;

static int failures = 0;
static int checks = 0;

#define CHECK(what) \
	do { \
		checks++; \
		if(!(what)) \
		{ \
			failures++; \
			std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #what); \
		} \
	} while(0)

/** The app's own reading of its command line (main.cpp): is --tray set? */
static bool TraySet(const QStringList &command)
{
	QCommandLineParser parser;
	parser.addOption(TrayOption());
	if(!parser.parse(command))
		return false;
	return parser.isSet(TrayOption());
}

/**
 * An Exec value as a desktop reads it (Desktop Entry Specification): first the escapes of a
 * string value, then the arguments split at spaces outside double quotes, in which a backslash
 * keeps ", `, $ and \ as they are; then %% is a %. An empty list for something it would refuse.
 */
static QStringList ReadExec(const QString &value)
{
	QString text;
	for(int i = 0; i < value.size(); i++)
	{
		const QChar c = value[i];
		if(c != QLatin1Char('\\'))
		{
			text += c;
			continue;
		}
		if(++i >= value.size())
			return {};
		const QChar e = value[i];
		if(e == QLatin1Char('s'))
			text += QLatin1Char(' ');
		else if(e == QLatin1Char('n'))
			text += QLatin1Char('\n');
		else if(e == QLatin1Char('t'))
			text += QLatin1Char('\t');
		else if(e == QLatin1Char('r'))
			text += QLatin1Char('\r');
		else if(e == QLatin1Char('\\'))
			text += QLatin1Char('\\');
		else
			return {};
	}
	QStringList args;
	QString arg;
	bool in_arg = false;
	bool quoted = false;
	for(int i = 0; i < text.size(); i++)
	{
		const QChar c = text[i];
		if(quoted)
		{
			if(c == QLatin1Char('\\'))
			{
				if(++i >= text.size())
					return {};
				const QChar e = text[i];
				if(e != QLatin1Char('"') && e != QLatin1Char('`') && e != QLatin1Char('$') && e != QLatin1Char('\\'))
					return {};
				arg += e;
			}
			else if(c == QLatin1Char('"'))
				quoted = false;
			else
				arg += c;
			continue;
		}
		if(c == QLatin1Char(' '))
		{
			if(in_arg)
				args.append(arg);
			arg.clear();
			in_arg = false;
			continue;
		}
		// Reserved characters only inside quotes
		if(QStringLiteral("\t\n'\\><~|&;$*?#()`").contains(c))
			return {};
		in_arg = true;
		if(c == QLatin1Char('"'))
			quoted = true;
		else
			arg += c;
	}
	if(quoted)
		return {};
	if(in_arg)
		args.append(arg);
	for(QString &a : args)
	{
		// %% is a %; any other field code would be expanded, which an entry of ours never has
		QString out;
		for(int i = 0; i < a.size(); i++)
		{
			if(a[i] == QLatin1Char('%'))
			{
				if(i + 1 >= a.size() || a[i + 1] != QLatin1Char('%'))
					return {};
				i++;
			}
			out += a[i];
		}
		a = out;
	}
	return args;
}

static QString ExecLine(const QString &entry)
{
	for(const QString &line : entry.split(QLatin1Char('\n')))
	{
		if(line.startsWith(QLatin1String("Exec=")))
			return line.mid(5);
	}
	return QString();
}

static void Flag()
{
	CHECK(Arguments(true) == QStringList{ QStringLiteral("--tray") });
	CHECK(Arguments(false).isEmpty());
	CHECK(TraySet(QStringList{ QStringLiteral("PSRemotePlayTogether") } + Arguments(true)));
	CHECK(!TraySet(QStringList{ QStringLiteral("PSRemotePlayTogether") } + Arguments(false)));
	// As typed by hand
	CHECK(TraySet({ QStringLiteral("PSRemotePlayTogether"), QStringLiteral("--tray") }));
}

static void Run()
{
	const QString program = QStringLiteral("C:/Users/Ann Lee/AppData/Local/Programs/PSRemotePlayTogether/PSRemotePlayTogether.exe");
	const QString with_tray = RunCommand(program, true);
	CHECK(with_tray == QStringLiteral("\"C:\\Users\\Ann Lee\\AppData\\Local\\Programs\\PSRemotePlayTogether\\PSRemotePlayTogether.exe\" --tray"));
	CHECK(RunCommand(program, false) == QStringLiteral("\"C:\\Users\\Ann Lee\\AppData\\Local\\Programs\\PSRemotePlayTogether\\PSRemotePlayTogether.exe\""));
	// Read back as Windows splits it: the program, then the flag the app understands
	const QStringList split = QProcess::splitCommand(with_tray);
	CHECK(split.size() == 2);
	CHECK(split.value(0) == QStringLiteral("C:\\Users\\Ann Lee\\AppData\\Local\\Programs\\PSRemotePlayTogether\\PSRemotePlayTogether.exe"));
	CHECK(TraySet(split));
	CHECK(!TraySet(QProcess::splitCommand(RunCommand(program, false))));
	// Already with backslashes, and with characters a user name may have
	const QString odd = QStringLiteral("C:\\Users\\Zoë & Jo (2)\\PSRemotePlayTogether.exe");
	const QStringList odd_split = QProcess::splitCommand(RunCommand(odd, true));
	CHECK(odd_split.value(0) == odd);
	CHECK(TraySet(odd_split));
}

static void Desktop()
{
	// A plain path needs nothing
	CHECK(DesktopExecArgument(QStringLiteral("/home/ann/Apps/PSRemotePlayTogether.AppImage")) == QStringLiteral("/home/ann/Apps/PSRemotePlayTogether.AppImage"));
	CHECK(DesktopExecArgument(QStringLiteral("--tray")) == QStringLiteral("--tray"));
	// Spaces and the like: in quotes
	CHECK(DesktopExecArgument(QStringLiteral("/home/ann/My Apps/x.AppImage")) == QStringLiteral("\"/home/ann/My Apps/x.AppImage\""));
	// A % is doubled, quoted or not
	CHECK(DesktopExecArgument(QStringLiteral("/opt/100%/x")) == QStringLiteral("/opt/100%%/x"));
	// In quotes $ gets a backslash, and every backslash is doubled once more for the file
	CHECK(DesktopExecArgument(QStringLiteral("/home/x$y")) == QStringLiteral("\"/home/x\\\\$y\""));
	CHECK(DesktopExecArgument(QStringLiteral("/a\\b")) == QStringLiteral("\"/a\\\\\\\\b\""));

	// Whatever the path, the desktop gets back the same program and the flag
	const QStringList paths = {
		QStringLiteral("/home/ann/Apps/PSRemotePlayTogether-0.5.4-linux-x86_64.AppImage"),
		QStringLiteral("/home/ann/My Apps/PSRemotePlayTogether.AppImage"),
		QStringLiteral("/home/ann/odd \"quotes\" and 'single' ones/x"),
		QStringLiteral("/home/ann/$HOME `uname` \\ back/x"),
		QStringLiteral("/home/ann/100% sure; (really) & more ~ | < > * ? #/x"),
		QStringLiteral("/home/ann/tab\there/new\nline/x"),
		QStringLiteral("/home/zoë/日本語/x"),
	};
	for(const QString &path : paths)
	{
		const QString entry = DesktopEntry(path, true);
		CHECK(entry.startsWith(QLatin1String("[Desktop Entry]\n")));
		CHECK(entry.contains(QLatin1String("\nType=Application\n")));
		// One line per key: nothing in the path breaks the file
		CHECK(entry.count(QLatin1Char('\n')) == 8);
		const QStringList exec = ReadExec(ExecLine(entry));
		CHECK(exec.size() == 2);
		CHECK(exec.value(0) == path);
		CHECK(exec.value(1) == QLatin1String("--tray"));
		CHECK(TraySet(exec));

		const QStringList plain = ReadExec(ExecLine(DesktopEntry(path, false)));
		CHECK(plain == QStringList{ path });
		CHECK(!TraySet(plain));
	}
}

int main(int argc, char *argv[])
{
	QCoreApplication app(argc, argv);
	Flag();
	Run();
	Desktop();
	if(failures)
	{
		std::printf("%d of %d checks failed\n", failures, checks);
		return 1;
	}
	std::printf("All %d checks passed\n", checks);
	return 0;
}
