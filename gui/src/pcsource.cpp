// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "pcsource_p.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#if PSRPT_PC_CAPTURE_FILTERS
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#endif
}

#include <opus/opus.h>

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#include <tlhelp32.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#if __has_include(<audioclientactivationparams.h>)
#include <audioclientactivationparams.h>
#else
// The Windows SDK has these (Windows 10 2004); mingw-w64 not yet
typedef enum AUDIOCLIENT_ACTIVATION_TYPE
{
	AUDIOCLIENT_ACTIVATION_TYPE_DEFAULT = 0,
	AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK = 1
} AUDIOCLIENT_ACTIVATION_TYPE;
typedef enum PROCESS_LOOPBACK_MODE
{
	PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE = 0,
	PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE = 1
} PROCESS_LOOPBACK_MODE;
typedef struct AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS
{
	DWORD TargetProcessId;
	PROCESS_LOOPBACK_MODE ProcessLoopbackMode;
} AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS;
typedef struct AUDIOCLIENT_ACTIVATION_PARAMS
{
	AUDIOCLIENT_ACTIVATION_TYPE ActivationType;
	union
	{
		AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS ProcessLoopbackParams;
	};
} AUDIOCLIENT_ACTIVATION_PARAMS;
#endif
#endif

// ------------------------------------------------------------------ targets

QVariantMap PcSource::Target::ToMap() const
{
	QVariantMap m;
	m[QStringLiteral("kind")] = kind;
	// As text: QML's numbers cannot hold every 64-bit handle
	m[QStringLiteral("handle")] = QString::number(handle);
	m[QStringLiteral("title")] = title;
	m[QStringLiteral("app")] = app;
	m[QStringLiteral("pid")] = pid;
	m[QStringLiteral("width")] = width;
	m[QStringLiteral("height")] = height;
	m[QStringLiteral("xbox")] = xbox;
	m[QStringLiteral("game")] = game;
	if(!token.isEmpty())
		m[QStringLiteral("token")] = token;
	return m;
}

PcSource::Target PcSource::Target::FromMap(const QVariantMap &m)
{
	Target t;
	t.kind = m.value(QStringLiteral("kind")).toString();
	if(t.kind != QLatin1String("window") && t.kind != QLatin1String("screen") && t.kind != QLatin1String("portal")
		&& t.kind != QLatin1String("test") && t.kind != QLatin1String("device"))
		return Target();
	t.handle = m.value(QStringLiteral("handle")).toString().toULongLong();
	t.title = m.value(QStringLiteral("title")).toString();
	t.app = m.value(QStringLiteral("app")).toString();
	t.pid = m.value(QStringLiteral("pid")).toUInt();
	t.width = m.value(QStringLiteral("width")).toInt();
	t.height = m.value(QStringLiteral("height")).toInt();
	t.xbox = m.value(QStringLiteral("xbox")).toBool();
	t.game = m.value(QStringLiteral("game")).toBool();
	t.token = m.value(QStringLiteral("token")).toString();
	return t;
}

static bool TestSourceWanted()
{
	return !qEnvironmentVariableIsEmpty("PSRPT_PC_SOURCE_TEST");
}

static PcSource::Target TestTarget()
{
	PcSource::Target t;
	t.kind = QStringLiteral("test");
	t.title = QCoreApplication::translate("PcSource", "Test picture");
	t.width = 1920;
	t.height = 1080;
	return t;
}

// ------------------------------------------------------------------ which game (GamerNova client's rules)

// Where game stores install games (matched in the lower-case path, with forward slashes)
static const char *const kGameFolders[] = {
	"/steamapps/common/", "/epic games/", "/origin games/", "/ea games/", "/xboxgames/",
	"/riot games/", "/ubisoft game launcher/games/", "/gog games/", "/battle.net/",
	"/world of warcraft/", "/diablo", "/overwatch", "/starcraft", "/hearthstone", "/call of duty", "/blizzard",
#if defined(__linux__)
	// Heroic and Lutris install into ~/Games by default, Lutris also into its own folder
	"/games/", "/lutris/",
#endif
};
// Programs that run beside games: launchers, browsers, chat, tools (the name without .exe)
static const char *const kNotGames[] = {
	"steam", "steamwebhelper", "epicgameslauncher", "origin", "eadesktop", "riotclientux", "upc", "galaxyclient",
	"battle.net", "agent", "opera", "chrome", "firefox", "msedge", "discord", "brave", "explorer", "nvidia share",
	"nvcontainer", "spoolsv", "searchapp", "cmd", "conhost", "applicationframehost", "textinputhost",
	"startmenuexperiencehost", "systemsettings", "taskmgr", "powershell", "pwsh", "rundll32", "svchost", "services",
	"lsass", "csrss", "smss", "winlogon", "wininit", "spotify", "netflix", "whatsapp", "slack", "teams", "zoom",
	"winword", "excel", "powerpnt", "outlook", "onenote", "notepad", "code", "idea", "devenv", "wallpaper64",
	"wallpaper32", "wallpaper_engine", "eosoverlayrenderer-win64-shipping", "eosoverlayrenderer-win32-shipping",
	"eosoverlayrenderer", "epiconlineservicesuserhelper", "epicwebhelper", "steamoverlayui", "steamoverlayrenderer",
	"gameoverlayui", "crashreportclient",
	// What runs beside a game on Linux: Steam's container and Wine's own programs
	"reaper", "pressure-vessel-wrap", "pv-bwrap", "srt-bwrap", "steam-runtime-launcher-service", "wineserver",
	"wine", "wine64", "wine-preloader", "wine64-preloader", "winedevice", "plugplay", "rpcss", "tabtip", "start",
	"proton", "umu-run", "gamescope", "gamemoded", "mangohud", "heroic", "lutris", "legendary", "gogdl", "nile",
	"python", "python3", "bash", "sh", "steamerrorreporter", "crashpad_handler", "unitycrashhandler64",
	"unitycrashhandler32", "easyanticheat", "easyanticheat_eos", "start_protected_game",
	// and this app
	"psremoteplaytogether",
};
static const char *const kNotGameParts[] = {
	"windowsterminal", "widget", "overlay", "webhelper", "userhelper", "wallpaper", "crashreport", "errorreporter",
};

bool PcSource::LooksLikeGame(const QString &program)
{
	if(program.isEmpty())
		return false;
	// Backslashes too where this is no Windows: Wine's programs have Windows paths
	QString path = QDir::fromNativeSeparators(program).toLower();
	path.replace(QLatin1Char('\\'), QLatin1Char('/'));
	if(path.startsWith(QLatin1String("c:/windows/")) || path.contains(QLatin1String("/windows/system32/"))
		|| path.contains(QLatin1String("/windows/syswow64/")))
		return false;
	QString name = path.section(QLatin1Char('/'), -1);
	if(name.endsWith(QLatin1String(".exe")))
		name.chop(4);
	for(const char *n : kNotGames)
	{
		if(name == QLatin1String(n))
			return false;
	}
	for(const char *part : kNotGameParts)
	{
		if(name.contains(QLatin1String(part)))
			return false;
	}
	for(const char *folder : kGameFolders)
	{
		if(path.contains(QLatin1String(folder)))
			return true;
	}
	// Xbox and Game Pass games live among the store apps: the ones that look like a game
	if(path.contains(QLatin1String("/program files/windowsapps/")))
	{
		if(path.contains(QLatin1String("/microsoft.windows")) || path.contains(QLatin1String("/microsoft.zune"))
			|| path.contains(QLatin1String("/microsoft.yourphone")))
			return false;
		const QFileInfo exe(program);
		const QDir dir = exe.dir();
		return dir.exists(QStringLiteral("MicrosoftGame.config")) // Xbox GDK
			|| dir.exists(exe.completeBaseName() + QStringLiteral("_Data")) // Unity
			|| path.contains(QLatin1String("/binaries/win64/")) || path.contains(QLatin1String("/binaries/win32/")); // Unreal
	}
	return false;
}

static QString ManifestName(const QString &file_name)
{
	QFile file(file_name);
	if(!file.open(QIODevice::ReadOnly))
		return QString();
	static const QRegularExpression name(QStringLiteral("\"name\"\\s*\"([^\"]+)\""));
	const QRegularExpressionMatch found = name.match(QString::fromUtf8(file.read(256 * 1024)));
	return found.hasMatch() ? found.captured(1).trimmed() : QString();
}

QString PcSource::SteamNameById(const QString &steam_root, const QString &app_id)
{
	if(steam_root.isEmpty() || app_id.isEmpty())
		return QString();
	const QString manifest = QStringLiteral("/steamapps/appmanifest_%1.acf").arg(app_id);
	QString name = ManifestName(steam_root + manifest);
	if(!name.isEmpty())
		return name;
	// The other libraries, on other drives
	QFile folders(steam_root + QStringLiteral("/steamapps/libraryfolders.vdf"));
	if(!folders.open(QIODevice::ReadOnly))
		return QString();
	static const QRegularExpression path(QStringLiteral("\"path\"\\s*\"([^\"]+)\""));
	auto it = path.globalMatch(QString::fromUtf8(folders.read(256 * 1024)));
	while(it.hasNext())
	{
		QString library = it.next().captured(1);
		library.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
		name = ManifestName(QDir::fromNativeSeparators(library) + manifest);
		if(!name.isEmpty())
			return name;
	}
	return QString();
}

QString PcSource::SteamName(const QString &program)
{
	const QString path = QDir::fromNativeSeparators(program);
	static const QLatin1String common("/steamapps/common/");
	const int at = path.toLower().indexOf(common);
	if(at < 0)
		return QString();
	const QString steamapps = path.left(at + static_cast<int>(strlen("/steamapps")));
	const QString folder = path.mid(at + common.size()).section(QLatin1Char('/'), 0, 0);
	static const QRegularExpression installdir(QStringLiteral("\"installdir\"\\s*\"([^\"]+)\""));
	static const QRegularExpression name(QStringLiteral("\"name\"\\s*\"([^\"]+)\""));
	const QStringList manifests = QDir(steamapps).entryList({ QStringLiteral("appmanifest_*.acf") }, QDir::Files);
	for(const QString &manifest : manifests)
	{
		QFile file(steamapps + QLatin1Char('/') + manifest);
		if(!file.open(QIODevice::ReadOnly))
			continue;
		const QString text = QString::fromUtf8(file.read(256 * 1024));
		const QRegularExpressionMatch dir = installdir.match(text);
		if(!dir.hasMatch() || dir.captured(1).compare(folder, Qt::CaseInsensitive) != 0)
			continue;
		const QRegularExpressionMatch found = name.match(text);
		if(found.hasMatch())
			return found.captured(1).trimmed();
	}
	// No manifest: the install folder is named after the game
	return folder;
}

QString PcSource::TitleAsName(const QString &title)
{
	static const QRegularExpression brackets(QStringLiteral("\\s*\\(.*?\\)"));
	static const QRegularExpression control(QStringLiteral("[\\x00-\\x1f\\x7f]"));
	QString t = title;
	t.remove(control);
	t.remove(brackets);
	return t.simplified().left(80);
}

using PcSource::LooksLikeGame;
using PcSource::SteamName;
using PcSource::TitleAsName;

#ifdef _WIN32
static QString PathOf(DWORD pid)
{
	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if(!process)
		return QString();
	wchar_t path[MAX_PATH * 2];
	DWORD size = static_cast<DWORD>(sizeof(path) / sizeof(path[0]));
	QString name;
	if(QueryFullProcessImageNameW(process, 0, path, &size))
		name = QString::fromWCharArray(path, static_cast<int>(size));
	CloseHandle(process);
	return name;
}

/** What a program calls itself (its FileDescription), else its file name without .exe. */
static QString ProgramName(const QString &program)
{
	const std::wstring path = QDir::toNativeSeparators(program).toStdWString();
	DWORD ignored = 0;
	const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
	if(size > 0)
	{
		std::vector<char> data(size);
		if(GetFileVersionInfoW(path.c_str(), 0, size, data.data()))
		{
			struct Translation { WORD language; WORD codepage; } *translations = nullptr;
			UINT length = 0;
			if(VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void **>(&translations), &length)
				&& length >= sizeof(Translation))
			{
				wchar_t key[64];
				swprintf(key, 64, L"\\StringFileInfo\\%04x%04x\\FileDescription", translations[0].language, translations[0].codepage);
				wchar_t *text = nullptr;
				UINT text_length = 0;
				if(VerQueryValueW(data.data(), key, reinterpret_cast<void **>(&text), &text_length) && text && text_length > 1)
				{
					const QString description = QString::fromWCharArray(text).simplified();
					if(!description.isEmpty())
						return description.left(80);
				}
			}
		}
	}
	return QFileInfo(program).completeBaseName();
}

/** Every program's first visible window title, by process. */
static BOOL CALLBACK CollectTitle(HWND hwnd, LPARAM user)
{
	auto titles = reinterpret_cast<QHash<DWORD, QString> *>(user);
	if(!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER))
		return TRUE;
	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);
	if(titles->contains(pid))
		return TRUE;
	wchar_t text[256];
	const int length = GetWindowTextW(hwnd, text, 256);
	if(length > 0)
		titles->insert(pid, QString::fromWCharArray(text, length));
	return TRUE;
}

/** The name of a game: Steam's, else its window title, else what the program calls itself. */
static QString GameName(const QString &program, const QString &window_title)
{
	QString name = SteamName(program);
	if(name.isEmpty())
		name = TitleAsName(window_title);
	if(name.isEmpty())
		name = ProgramName(program);
	return name;
}

static bool IsXboxApp(const QString &app, const QString &title)
{
	const QString a = app.toLower();
	return a == QLatin1String("xboxpcapp.exe") || a == QLatin1String("xboxapp.exe")
		|| (a == QLatin1String("applicationframehost.exe") && title == QLatin1String("Xbox"));
}

static PcSource::Target WindowTarget(HWND hwnd)
{
	PcSource::Target t;
	if(!IsWindow(hwnd) || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER))
		return t;
	if(GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)
		return t;
	// Windows that are there but nobody sees: other virtual desktops, suspended store apps
	int cloaked = 0;
	if(SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
		return t;
	int length = GetWindowTextLengthW(hwnd);
	if(length <= 0)
		return t;
	std::vector<wchar_t> text(static_cast<size_t>(length) + 1);
	GetWindowTextW(hwnd, text.data(), length + 1);
	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);
	if(pid == GetCurrentProcessId())
		return t;
	t.title = QString::fromWCharArray(text.data());
	if(t.title == QLatin1String("Program Manager"))
		return PcSource::Target();
	RECT rect = {};
	GetClientRect(hwnd, &rect);
	t.kind = QStringLiteral("window");
	t.handle = static_cast<quint64>(reinterpret_cast<uintptr_t>(hwnd));
	t.pid = pid;
	const QString path = PathOf(pid);
	t.app = QFileInfo(path).fileName();
	t.width = rect.right - rect.left;
	t.height = rect.bottom - rect.top;
	t.xbox = IsXboxApp(t.app, t.title);
	t.game = t.xbox || LooksLikeGame(path);
	return t;
}

static BOOL CALLBACK CollectWindow(HWND hwnd, LPARAM user)
{
	auto list = reinterpret_cast<QList<PcSource::Target> *>(user);
	PcSource::Target t = WindowTarget(hwnd);
	if(t.Valid())
		list->append(t);
	return TRUE;
}

static BOOL CALLBACK CollectScreen(HMONITOR monitor, HDC, LPRECT, LPARAM user)
{
	auto list = reinterpret_cast<QList<PcSource::Target> *>(user);
	MONITORINFOEXW info = {};
	info.cbSize = sizeof(info);
	if(!GetMonitorInfoW(monitor, &info))
		return TRUE;
	PcSource::Target t;
	t.kind = QStringLiteral("screen");
	t.handle = static_cast<quint64>(reinterpret_cast<uintptr_t>(monitor));
	t.width = info.rcMonitor.right - info.rcMonitor.left;
	t.height = info.rcMonitor.bottom - info.rcMonitor.top;
	const bool primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
	const int number = static_cast<int>(std::count_if(list->begin(), list->end(),
		[](const PcSource::Target &o) { return o.kind == QLatin1String("screen"); })) + 1;
	t.title = primary ? QCoreApplication::translate("PcSource", "Main screen")
		: QCoreApplication::translate("PcSource", "Screen %1").arg(number);
	if(primary)
	{
		// The main screen first
		int first = 0;
		while(first < list->size() && list->at(first).kind != QLatin1String("screen"))
			first++;
		list->insert(first, t);
	}
	else
		list->append(t);
	return TRUE;
}

static bool ProcessLoopbackSupported()
{
	// Windows 10 2004 (build 19041) captures a single program's sound
	using RtlGetVersionFn = LONG (WINAPI *)(PRTL_OSVERSIONINFOW);
	auto get = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
	RTL_OSVERSIONINFOW v = {};
	v.dwOSVersionInfoSize = sizeof(v);
	return get && get(&v) == 0 && (v.dwMajorVersion > 10 || (v.dwMajorVersion == 10 && v.dwBuildNumber >= 19041));
}
#endif

QString PcSource::Unsupported()
{
	if(TestSourceWanted())
		return QString();
#if defined(_WIN32) && PSRPT_PC_CAPTURE_FILTERS
	// Windows Graphics Capture with a window or a screen of choice: Windows 10 2004 and later
	if(!ProcessLoopbackSupported())
		return QCoreApplication::translate("PcSource", "Sharing a game on this computer needs Windows 10 version 2004 or newer.");
	return QString();
#elif defined(__linux__) && PSRPT_PC_PIPEWIRE
	return PcLinux::Unsupported();
#elif defined(__APPLE__) && PSRPT_PC_SCREENCAPTUREKIT
	return PcMac::Unsupported();
#else
	return QCoreApplication::translate("PcSource", "This build of the app cannot share a game on this computer.");
#endif
}

QString PcSource::Permission()
{
#if defined(__APPLE__) && PSRPT_PC_SCREENCAPTUREKIT
	return PcMac::Permission();
#else
	return QString();
#endif
}

void PcSource::AskPermission()
{
#if defined(__APPLE__) && PSRPT_PC_SCREENCAPTUREKIT
	PcMac::AskPermission();
#endif
}

QString PcSource::Note()
{
#if defined(__APPLE__)
	return QCoreApplication::translate("PcSource", "Friends get the picture and only the game's sound, never your voice chat. You play on your own controller or keyboard; on a Mac your friends watch.");
#elif defined(__linux__)
	return QCoreApplication::translate("PcSource", "Friends get the picture and the game's sound (without a game found: every program's but this app's and voice chats'). You play on your own controller; theirs become controllers on this computer.");
#else
	return QCoreApplication::translate("PcSource", "Friends get the picture and only the game's sound, never your voice chat. You play on your own controller; theirs become controllers on this computer. A game in exclusive fullscreen, or protected video, can stay black: use borderless or windowed.");
#endif
}

QList<PcSource::Target> PcSource::Targets()
{
	QList<Target> list;
	if(TestSourceWanted())
		list.append(TestTarget());
#if defined(_WIN32) && PSRPT_PC_CAPTURE_FILTERS
	QList<Target> windows;
	EnumWindows(CollectWindow, reinterpret_cast<LPARAM>(&windows));
	// Games first, then the other windows as they are stacked, then the screens
	std::stable_sort(windows.begin(), windows.end(), [](const Target &a, const Target &b) { return a.game && !b.game; });
	list.append(windows);
	EnumDisplayMonitors(nullptr, nullptr, CollectScreen, reinterpret_cast<LPARAM>(&list));
#elif defined(__linux__) && PSRPT_PC_PIPEWIRE
	list.append(PcLinux::Targets());
#elif defined(__APPLE__) && PSRPT_PC_SCREENCAPTUREKIT
	list.append(PcMac::Targets());
#endif
	// Capture cards (and cameras: the system cannot tell them apart) last
	list.append(PcDevice::Targets());
	return list;
}

PcSource::Game PcSource::DetectGame(const Target &target)
{
	Game game;
	if(target.kind == QLatin1String("test"))
	{
		game.title = QCoreApplication::translate("PcSource", "Test picture");
		return game;
	}
	// What is plugged into a capture device, this computer cannot tell
	if(target.kind == QLatin1String("device"))
		return game;
#if defined(__linux__) && PSRPT_PC_PIPEWIRE
	return PcLinux::DetectGame(target);
#elif defined(__APPLE__) && PSRPT_PC_SCREENCAPTUREKIT
	return PcMac::DetectGame(target);
#elif defined(_WIN32)
	// The Xbox app streams a console: which game runs there, this computer cannot tell
	if(target.xbox)
		return game;
	QHash<DWORD, QString> titles;
	EnumWindows(CollectTitle, reinterpret_cast<LPARAM>(&titles));
	if(target.kind == QLatin1String("window"))
	{
		const QString program = PathOf(target.pid);
		game.path = program;
		if(LooksLikeGame(program))
			game.title = GameName(program, titles.value(target.pid, target.title));
		else if(!program.isEmpty())
			game.title = ProgramName(program); // an emulator, a cloud gaming app
		return game;
	}
	// A screen: the game started last, as the GamerNova client shows the newest
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if(snapshot == INVALID_HANDLE_VALUE)
		return game;
	PROCESSENTRY32W entry = {};
	entry.dwSize = sizeof(entry);
	ULONGLONG newest = 0;
	for(BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry))
	{
		const DWORD pid = entry.th32ProcessID;
		if(pid == GetCurrentProcessId())
			continue;
		const QString program = PathOf(pid);
		if(!LooksLikeGame(program))
			continue;
		ULONGLONG started = 0;
		if(HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid))
		{
			FILETIME created, exited, kernel, user;
			if(GetProcessTimes(process, &created, &exited, &kernel, &user))
				started = (static_cast<ULONGLONG>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
			CloseHandle(process);
		}
		if(!game.title.isEmpty() && started < newest)
			continue;
		newest = started;
		game.path = program;
		game.title = GameName(program, titles.value(pid));
	}
	CloseHandle(snapshot);
#endif
	return game;
}

void PcSource::PictureSize(const Target &target, int height, int *width_out, int *height_out)
{
	height = std::clamp(height, 360, 2160);
	double aspect = 16.0 / 9.0;
	if(target.width > 0 && target.height > 0)
	{
		aspect = std::clamp(static_cast<double>(target.width) / target.height, 4.0 / 3.0, 32.0 / 9.0);
		// Never larger than the window: more lines would only be scaled up
		height = std::min(height, std::max(360, target.height));
	}
	int width = static_cast<int>(std::lround(height * aspect));
	// Encoders want even sizes, and some a multiple of 16 in width
	width = std::min(7680, (width + 8) & ~15);
	*width_out = width;
	*height_out = height & ~1;
}

int PcSource::BitrateFor(int width, int height, int fps)
{
	const double pixels = static_cast<double>(width) * height * std::max(10, fps);
	const double base = 1920.0 * 1080.0 * 60.0;
	return std::clamp(static_cast<int>(std::lround(15000.0 * std::pow(pixels / base, 0.8))), 4000, 60000);
}

PcSource::Target PcSource::Find(const Target &chosen)
{
	if(chosen.kind == QLatin1String("test"))
		return TestSourceWanted() ? TestTarget() : Target();
	if(chosen.kind == QLatin1String("device"))
		return PcDevice::Find(chosen);
	// The system asks, or shares what it was told last time: nothing to look for here
	if(chosen.kind == QLatin1String("portal"))
		return chosen;
	const QList<Target> all = Targets();
	for(const Target &t : all)
	{
		if(t.kind == chosen.kind && t.handle == chosen.handle)
			return t;
	}
	// Started again since: the same program with the same title, or the same program
	for(const Target &t : all)
	{
		if(t.kind == QLatin1String("window") && chosen.kind == t.kind && !t.app.isEmpty()
			&& t.app.compare(chosen.app, Qt::CaseInsensitive) == 0 && t.title == chosen.title)
			return t;
	}
	for(const Target &t : all)
	{
		if(t.kind == QLatin1String("window") && chosen.kind == t.kind && !t.app.isEmpty()
			&& t.app.compare(chosen.app, Qt::CaseInsensitive) == 0)
			return t;
	}
	if(chosen.kind == QLatin1String("screen"))
	{
		for(const Target &t : all)
		{
			if(t.kind == chosen.kind && t.title == chosen.title)
				return t;
		}
	}
	return Target();
}

// ------------------------------------------------------------------ picture sources

static std::string AvError(int err)
{
	char text[AV_ERROR_MAX_STRING_SIZE] = {};
	av_strerror(err, text, sizeof(text));
	return text;
}

namespace {

using FrameSource = PcFrameSource;

/** A picture that moves and says which frame it is, for trying everything out without a game. */
class TestSource : public FrameSource
{
	public:
		TestSource(int width, int height, int fps, AVPixelFormat format)
			: width(width), height(height), fps(fps), format(format), start(std::chrono::steady_clock::now()) {}

		AVFrame *Next(bool *ended, std::string *error) override
		{
			(void)ended;
			(void)error;
			// Paced like a game at its frame rate
			auto due = start + std::chrono::microseconds(1000000LL * count / fps);
			std::this_thread::sleep_until(due);
			AVFrame *f = av_frame_alloc();
			f->format = format;
			f->width = width;
			f->height = height;
			if(av_frame_get_buffer(f, 0) < 0)
			{
				av_frame_free(&f);
				return nullptr;
			}
			Draw(f);
			count++;
			return f;
		}

	private:
		void Draw(AVFrame *f)
		{
			// Eight colour bars that move to the left, a white block that bounces, and
			// below them the frame count as a bar of 16 squares (black 0, white 1)
			static const uint8_t bars[8][3] = {
				{ 235, 128, 128 }, { 210, 16, 146 }, { 170, 166, 16 }, { 145, 54, 34 },
				{ 106, 202, 222 }, { 81, 90, 240 }, { 41, 240, 110 }, { 16, 128, 128 },
			};
			const int shift = static_cast<int>((count * 4) % width);
			const int block = height / 6;
			const int range = width - block;
			const int phase = static_cast<int>((count * 12) % (2 * range));
			const int bx = phase < range ? phase : 2 * range - phase;
			const int by = height / 2 - block / 2;
			const bool planar = format == AV_PIX_FMT_YUV420P;
			for(int y = 0; y < height; y++)
			{
				uint8_t *row = f->data[0] + y * f->linesize[0];
				for(int x = 0; x < width; x++)
				{
					const int bar = ((x + shift) % width) * 8 / width;
					uint8_t luma = bars[bar][0];
					if(x >= bx && x < bx + block && y >= by && y < by + block)
						luma = 235;
					if(y >= height - height / 12)
					{
						const int bit = x * 16 / width;
						luma = ((count >> (15 - bit)) & 1) ? 235 : 16;
					}
					row[x] = luma;
				}
			}
			for(int y = 0; y < height / 2; y++)
			{
				uint8_t *u = f->data[1] + y * f->linesize[1];
				uint8_t *v = planar ? f->data[2] + y * f->linesize[2] : nullptr;
				for(int x = 0; x < width / 2; x++)
				{
					const int bar = ((x * 2 + shift) % width) * 8 / width;
					uint8_t cb = bars[bar][1], cr = bars[bar][2];
					const int px = x * 2, py = y * 2;
					if((px >= bx && px < bx + block && py >= by && py < by + block) || py >= height - height / 12)
						cb = cr = 128;
					if(planar)
					{
						u[x] = cb;
						v[x] = cr;
					}
					else
					{
						u[x * 2] = cb;
						u[x * 2 + 1] = cr;
					}
				}
			}
		}

		int width, height, fps;
		AVPixelFormat format;
		std::chrono::steady_clock::time_point start;
		int64_t count = 0;
};

#if PSRPT_PC_CAPTURE_FILTERS
/**
 * A window or a screen through FFmpeg's capture filters: Windows Graphics Capture
 * (gfxcapture), scaled into the picture size keeping its shape and made NV12 on the
 * graphics card (scale_d3d11), and taken into memory (hwdownload) for an encoder that
 * cannot read the card's frames.
 */
class FilterSource : public FrameSource
{
	public:
		~FilterSource() override
		{
			avfilter_graph_free(&graph);
		}

		bool Open(const PcSource::Target &target, int width, int height, int fps, bool on_card, std::string *error)
		{
			char desc[512];
			const char *what = target.kind == QLatin1String("screen") ? "hmonitor" : "hwnd";
			snprintf(desc, sizeof(desc),
				"gfxcapture=%s=%llu:width=%d:height=%d:resize_mode=scale_aspect:max_framerate=%d:capture_cursor=1,"
				"scale_d3d11=format=nv12%s",
				what, static_cast<unsigned long long>(target.handle), width, height, fps,
				on_card ? "" : ",hwdownload,format=nv12");
			graph = avfilter_graph_alloc();
			if(!graph)
			{
				*error = "out of memory";
				return false;
			}
			int err = avfilter_graph_create_filter(&sink, avfilter_get_by_name("buffersink"), "out", nullptr, nullptr, graph);
			if(err < 0)
			{
				*error = "buffersink: " + AvError(err);
				return false;
			}
			AVFilterInOut *inputs = avfilter_inout_alloc();
			inputs->name = av_strdup("out");
			inputs->filter_ctx = sink;
			inputs->pad_idx = 0;
			inputs->next = nullptr;
			err = avfilter_graph_parse_ptr(graph, desc, &inputs, nullptr, nullptr);
			avfilter_inout_free(&inputs);
			if(err >= 0)
				err = avfilter_graph_config(graph, nullptr);
			if(err < 0)
			{
				*error = std::string("capture (") + desc + "): " + AvError(err);
				return false;
			}
			return true;
		}

		AVFrame *Next(bool *ended, std::string *error) override
		{
			AVFrame *f = av_frame_alloc();
			for(int round = 0; round < 2; round++)
			{
				int err = av_buffersink_get_frame_flags(sink, f, AV_BUFFERSINK_FLAG_NO_REQUEST);
				if(err >= 0)
					return f;
				if(err != AVERROR(EAGAIN))
				{
					*ended = true;
					*error = err == AVERROR_EOF ? "the window was closed" : "capture: " + AvError(err);
					break;
				}
				if(round == 1)
					break;
				// Waits up to a second for the window to change
				err = avfilter_graph_request_oldest(graph);
				if(err == AVERROR_EOF)
				{
					*ended = true;
					*error = "the window was closed";
					break;
				}
				if(err < 0 && err != AVERROR(EAGAIN))
				{
					*ended = true;
					*error = "capture: " + AvError(err);
					break;
				}
			}
			av_frame_free(&f);
			return nullptr;
		}

		AVBufferRef *HwFrames() const override
		{
			return av_buffersink_get_hw_frames_ctx(sink);
		}

	private:
		AVFilterGraph *graph = nullptr;
		AVFilterContext *sink = nullptr;
};
#endif

// ------------------------------------------------------------------ encoders

struct EncoderChoice
{
	const char *name;
	/** Takes the graphics card's frames as they come from the capture. */
	bool on_card;
	/** What it gets in memory. */
	AVPixelFormat input;
	/** For play: as little delay as the encoder can do, a steady bit rate. */
	const char *options;
	/** Takes the card's frames, which the pictures in memory are copied into (VAAPI). */
	AVHWDeviceType upload = AV_HWDEVICE_TYPE_NONE;
};

// Hardware first, then what the system or FFmpeg has in software
const EncoderChoice kEncoders[] = {
#if defined(_WIN32)
	{ "h264_nvenc", true, AV_PIX_FMT_NV12, "preset=p1:tune=ull:rc=cbr:zerolatency=1:delay=0:forced-idr=1" },
	{ "h264_amf", true, AV_PIX_FMT_NV12, "usage=ultralowlatency:rc=cbr:quality=speed" },
	{ "h264_nvenc", false, AV_PIX_FMT_NV12, "preset=p1:tune=ull:rc=cbr:zerolatency=1:delay=0:forced-idr=1" },
	{ "h264_amf", false, AV_PIX_FMT_NV12, "usage=ultralowlatency:rc=cbr:quality=speed" },
	{ "h264_mf", false, AV_PIX_FMT_NV12, "hw_encoding=1:rate_control=cbr:scenario=display_remoting" },
	// Windows' own encoder in software
	{ "h264_mf", false, AV_PIX_FMT_NV12, "rate_control=cbr:scenario=display_remoting" },
#elif defined(__APPLE__)
	{ "h264_videotoolbox", false, AV_PIX_FMT_NV12, "realtime=1:allow_sw=1:prio_speed=1" },
#else
	{ "h264_nvenc", false, AV_PIX_FMT_NV12, "preset=p1:tune=ull:rc=cbr:zerolatency=1:delay=0:forced-idr=1" },
	// Intel and AMD on Linux
	{ "h264_vaapi", false, AV_PIX_FMT_NV12, "rc_mode=CBR:async_depth=1", AV_HWDEVICE_TYPE_VAAPI },
#endif
	{ "libx264", false, AV_PIX_FMT_YUV420P, "preset=ultrafast:tune=zerolatency:forced-idr=1" },
	{ "libopenh264", false, AV_PIX_FMT_YUV420P, "rc_mode=bitrate:allow_skip_frames=0" },
};

/** The NAL units of an Annex B buffer: offset of the unit (after its start code) and its length. */
static std::vector<std::pair<size_t, size_t>> NalUnits(const uint8_t *data, size_t size)
{
	std::vector<std::pair<size_t, size_t>> units;
	size_t i = 0;
	size_t begin = SIZE_MAX;
	while(i + 2 < size)
	{
		if(data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1)
		{
			if(begin != SIZE_MAX)
			{
				size_t end = i;
				while(end > begin && data[end - 1] == 0)
					end--;
				units.emplace_back(begin, end - begin);
			}
			i += 3;
			begin = i;
			continue;
		}
		i++;
	}
	if(begin != SIZE_MAX && begin < size)
		units.emplace_back(begin, size - begin);
	return units;
}

/** SPS and PPS out of an encoder's extradata, which may be Annex B or avcC. */
static std::vector<uint8_t> ParameterSetsFrom(const uint8_t *data, size_t size)
{
	static const uint8_t start[] = { 0, 0, 0, 1 };
	std::vector<uint8_t> out;
	if(!data || size < 4)
		return out;
	if(data[0] == 1 && size >= 7)
	{
		// avcC: version, profile, compat, level, length size, then SPS and PPS with 16-bit lengths
		size_t p = 5;
		for(int kind = 0; kind < 2 && p < size; kind++)
		{
			unsigned count = data[p++] & (kind == 0 ? 0x1f : 0xff);
			for(unsigned n = 0; n < count && p + 2 <= size; n++)
			{
				size_t length = (static_cast<size_t>(data[p]) << 8) | data[p + 1];
				p += 2;
				if(p + length > size)
					return std::vector<uint8_t>();
				out.insert(out.end(), start, start + 4);
				out.insert(out.end(), data + p, data + p + length);
				p += length;
			}
		}
		return out;
	}
	for(const auto &unit : NalUnits(data, size))
	{
		const unsigned type = data[unit.first] & 0x1f;
		if(type == 7 || type == 8)
		{
			out.insert(out.end(), start, start + 4);
			out.insert(out.end(), data + unit.first, data + unit.first + unit.second);
		}
	}
	return out;
}

} // namespace

// ------------------------------------------------------------------ sound

namespace {

/** Gathers 16-bit stereo samples and turns them into Opus frames of 10 ms. */
class OpusPacker
{
	public:
		explicit OpusPacker(std::function<void(const uint8_t *, size_t)> out) : out(std::move(out))
		{
			int err = 0;
			encoder = opus_encoder_create(PcCapture::kAudioRate, PcCapture::kAudioChannels, OPUS_APPLICATION_RESTRICTED_LOWDELAY, &err);
			if(encoder)
			{
				opus_encoder_ctl(encoder, OPUS_SET_BITRATE(160000));
				opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(5));
			}
		}
		~OpusPacker()
		{
			if(encoder)
				opus_encoder_destroy(encoder);
		}
		bool Ok() const { return encoder != nullptr; }

		void Push(const int16_t *samples, size_t frames)
		{
			pending.insert(pending.end(), samples, samples + frames * PcCapture::kAudioChannels);
			const size_t per = PcCapture::kAudioFrameSamples * PcCapture::kAudioChannels;
			size_t used = 0;
			uint8_t packet[1500];
			while(pending.size() - used >= per)
			{
				int n = opus_encode(encoder, pending.data() + used, PcCapture::kAudioFrameSamples, packet, sizeof(packet));
				used += per;
				if(n > 0 && out)
					out(packet, static_cast<size_t>(n));
			}
			pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(used));
		}

	private:
		OpusEncoder *encoder = nullptr;
		std::vector<int16_t> pending;
		std::function<void(const uint8_t *, size_t)> out;
};

#ifdef _WIN32
#ifndef VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK
#define VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK L"VAD\\Process_Loopback"
#endif

/** Waits for ActivateAudioInterfaceAsync(); free-threaded, as the call wants it. */
class ActivationWaiter : public IActivateAudioInterfaceCompletionHandler, public IAgileObject
{
	public:
		ActivationWaiter() { done = CreateEventW(nullptr, TRUE, FALSE, nullptr); }
		~ActivationWaiter() { CloseHandle(done); }

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override
		{
			if(riid == __uuidof(IUnknown) || riid == __uuidof(IActivateAudioInterfaceCompletionHandler))
				*out = static_cast<IActivateAudioInterfaceCompletionHandler *>(this);
			else if(riid == __uuidof(IAgileObject))
				*out = static_cast<IAgileObject *>(this);
			else
			{
				*out = nullptr;
				return E_NOINTERFACE;
			}
			AddRef();
			return S_OK;
		}
		ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
		ULONG STDMETHODCALLTYPE Release() override
		{
			ULONG left = --refs;
			if(!left)
				delete this;
			return left;
		}
		HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation *operation) override
		{
			IUnknown *unknown = nullptr;
			HRESULT activated = E_FAIL;
			result = operation->GetActivateResult(&activated, &unknown);
			if(SUCCEEDED(result))
				result = activated;
			if(SUCCEEDED(result) && unknown)
				result = unknown->QueryInterface(__uuidof(IAudioClient), reinterpret_cast<void **>(&client));
			if(unknown)
				unknown->Release();
			SetEvent(done);
			return S_OK;
		}

		HANDLE done;
		HRESULT result = E_FAIL;
		IAudioClient *client = nullptr;

	private:
		std::atomic<ULONG> refs{1};
};

/**
 * The sound of one program and the programs it started (a window), or of everything but
 * this app (a screen): never the voice chat this app plays, or the guests would hear
 * themselves again. Windows 10 2004 and later.
 */
static IAudioClient *ActivateLoopback(DWORD pid, bool include, std::string *error)
{
	using ActivateFn = HRESULT (WINAPI *)(LPCWSTR, REFIID, PROPVARIANT *, IActivateAudioInterfaceCompletionHandler *, IActivateAudioInterfaceAsyncOperation **);
	HMODULE mmdevapi = LoadLibraryW(L"Mmdevapi.dll");
	auto activate = mmdevapi ? reinterpret_cast<ActivateFn>(GetProcAddress(mmdevapi, "ActivateAudioInterfaceAsync")) : nullptr;
	if(!activate)
	{
		*error = "this Windows cannot capture a program's sound";
		return nullptr;
	}
	AUDIOCLIENT_ACTIVATION_PARAMS params = {};
	params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
	params.ProcessLoopbackParams.TargetProcessId = pid;
	params.ProcessLoopbackParams.ProcessLoopbackMode = include
		? PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE : PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE;
	PROPVARIANT prop = {};
	prop.vt = VT_BLOB;
	prop.blob.cbSize = sizeof(params);
	prop.blob.pBlobData = reinterpret_cast<BYTE *>(&params);
	auto waiter = new ActivationWaiter;
	IActivateAudioInterfaceAsyncOperation *operation = nullptr;
	HRESULT hr = activate(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &prop, waiter, &operation);
	IAudioClient *client = nullptr;
	if(SUCCEEDED(hr))
	{
		WaitForSingleObject(waiter->done, 5000);
		hr = waiter->result;
		client = waiter->client;
		waiter->client = nullptr;
	}
	if(operation)
		operation->Release();
	waiter->Release();
	if(FAILED(hr) || !client)
	{
		char text[64];
		snprintf(text, sizeof(text), "cannot capture the sound (0x%08lx)", static_cast<unsigned long>(hr));
		*error = text;
		if(client)
			client->Release();
		return nullptr;
	}
	return client;
}
#endif

} // namespace

// ------------------------------------------------------------------ capture

struct PcCapture::Private
{
	Config config;
	Callbacks callbacks;

	std::unique_ptr<FrameSource> source;
	AVCodecContext *encoder = nullptr;
	/** The pictures go to the card before the encoder (EncoderChoice::upload). */
	bool upload = false;
	const EncoderChoice *choice = nullptr;
	std::vector<uint8_t> parameter_sets;

	std::thread capture_thread;
	std::thread encode_thread;
	std::thread sound_thread;
	std::atomic<bool> running{false};
	std::atomic<bool> keyframe_wanted{true};
	std::atomic<bool> failed{false};

	// Between the capture and the encoder thread
	std::mutex frame_mutex;
	std::condition_variable frame_changed;
	AVFrame *latest = nullptr;  // the newest picture, not yet encoded
	bool latest_new = false;

	mutable std::mutex info_mutex;
	std::string encoder_name;
	std::string sound_source;

	std::chrono::steady_clock::time_point started;
	int64_t last_pts = -1;

	void Log(const std::string &line)
	{
		if(callbacks.on_log)
			callbacks.on_log(line);
	}

	void Fail(const std::string &why)
	{
		if(failed.exchange(true))
			return;
		running = false;
		frame_changed.notify_all();
		if(callbacks.on_error)
			callbacks.on_error(why);
	}

	bool OpenSource(bool on_card, AVPixelFormat input, std::string *error)
	{
		source.reset();
		if(config.target.kind == QLatin1String("test"))
		{
			if(on_card)
			{
				*error = "the test picture is made in memory";
				return false;
			}
			source = std::make_unique<TestSource>(config.width, config.height, config.fps, input);
			return true;
		}
		if(config.target.kind == QLatin1String("device"))
		{
			if(on_card)
			{
				*error = "a capture device gives pictures in memory";
				return false;
			}
			std::string what;
			source = PcDevice::OpenVideo(config, input, error, &what);
			if(source && !what.empty())
				Log("the device sends " + what);
			return source != nullptr;
		}
#if PSRPT_PC_CAPTURE_FILTERS
		if(input != AV_PIX_FMT_NV12)
		{
			*error = "the capture gives NV12";
			return false;
		}
		auto filter = std::make_unique<FilterSource>();
		if(!filter->Open(config.target, config.width, config.height, config.fps, on_card, error))
			return false;
		source = std::move(filter);
		return true;
#elif (defined(__linux__) && PSRPT_PC_PIPEWIRE) || (defined(__APPLE__) && PSRPT_PC_SCREENCAPTUREKIT)
		if(on_card)
		{
			*error = "the capture gives pictures in memory";
			return false;
		}
#if defined(__linux__)
		source = PcLinux::OpenVideo(config, input, error);
#else
		source = PcMac::OpenVideo(config, input, error);
#endif
		return source != nullptr;
#else
		*error = "this build cannot capture windows or screens";
		return false;
#endif
	}

	bool OpenEncoder(const EncoderChoice &c, std::string *error)
	{
		const AVCodec *codec = avcodec_find_encoder_by_name(c.name);
		if(!codec)
		{
			*error = "not in this build";
			return false;
		}
		AVCodecContext *ctx = avcodec_alloc_context3(codec);
		if(!ctx)
		{
			*error = "out of memory";
			return false;
		}
		ctx->width = config.width;
		ctx->height = config.height;
		// Pictures come when the game draws one: their time in milliseconds
		ctx->time_base = AVRational{1, 1000};
		ctx->framerate = AVRational{config.fps, 1};
		ctx->sample_aspect_ratio = AVRational{1, 1};
		// A key frame when a guest needs one (RequestKeyframe), else rarely
		ctx->gop_size = config.fps * 30;
		ctx->max_b_frames = 0;
		ctx->bit_rate = static_cast<int64_t>(config.video_kbps) * 1000;
		ctx->rc_max_rate = ctx->bit_rate;
		// A quarter of a second: little delay, room enough for a key frame
		ctx->rc_buffer_size = static_cast<int>(ctx->bit_rate / 4);
		ctx->profile = strcmp(c.name, "libopenh264") == 0 ? AV_PROFILE_H264_CONSTRAINED_BASELINE : AV_PROFILE_H264_HIGH;
		ctx->color_range = AVCOL_RANGE_MPEG;
		ctx->colorspace = AVCOL_SPC_BT709;
		ctx->color_primaries = AVCOL_PRI_BT709;
		ctx->color_trc = AVCOL_TRC_BT709;
		ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
		ctx->pix_fmt = c.input;
		if(c.upload != AV_HWDEVICE_TYPE_NONE)
		{
			AVBufferRef *device = nullptr;
			int err = av_hwdevice_ctx_create(&device, c.upload, nullptr, nullptr, 0);
			AVBufferRef *frames = err >= 0 ? av_hwframe_ctx_alloc(device) : nullptr;
			if(frames)
			{
				auto fc = reinterpret_cast<AVHWFramesContext *>(frames->data);
				fc->format = c.upload == AV_HWDEVICE_TYPE_VAAPI ? AV_PIX_FMT_VAAPI : AV_PIX_FMT_NONE;
				fc->sw_format = c.input;
				fc->width = config.width;
				fc->height = config.height;
				fc->initial_pool_size = 8;
				err = fc->format == AV_PIX_FMT_NONE ? AVERROR(ENOSYS) : av_hwframe_ctx_init(frames);
			}
			av_buffer_unref(&device);
			if(!frames || err < 0)
			{
				av_buffer_unref(&frames);
				avcodec_free_context(&ctx);
				*error = "no graphics card to encode on" + (err < 0 ? " (" + AvError(err) + ")" : std::string());
				return false;
			}
			ctx->pix_fmt = reinterpret_cast<AVHWFramesContext *>(frames->data)->format;
			ctx->sw_pix_fmt = c.input;
			ctx->hw_frames_ctx = frames;
		}
		else if(c.on_card)
		{
			AVBufferRef *frames = source ? source->HwFrames() : nullptr;
			if(!frames)
			{
				avcodec_free_context(&ctx);
				*error = "no frames on the graphics card";
				return false;
			}
			ctx->pix_fmt = reinterpret_cast<AVHWFramesContext *>(frames->data)->format;
			ctx->sw_pix_fmt = c.input;
			ctx->hw_frames_ctx = av_buffer_ref(frames);
		}
		AVDictionary *opts = nullptr;
		av_dict_parse_string(&opts, c.options, "=", ":", 0);
		int err = avcodec_open2(ctx, codec, &opts);
		av_dict_free(&opts);
		if(err < 0)
		{
			avcodec_free_context(&ctx);
			*error = AvError(err);
			return false;
		}
		encoder = ctx;
		choice = &c;
		upload = c.upload != AV_HWDEVICE_TYPE_NONE;
		parameter_sets = ParameterSetsFrom(ctx->extradata, static_cast<size_t>(ctx->extradata_size));
		return true;
	}

	/** Capture and encoder that work together: the card's frames straight to the encoder where it can. */
	bool Open(std::string *error)
	{
		const char *forced = getenv("PSRPT_PC_ENCODER");
		std::string tried;
		for(const EncoderChoice &c : kEncoders)
		{
			if(forced && *forced && strcmp(forced, c.name) != 0)
				continue;
			if(!avcodec_find_encoder_by_name(c.name))
				continue;
			std::string why;
			// The source changes with what the encoder takes
			const bool same_source = source && last_on_card == c.on_card && last_input == c.input;
			if(!same_source)
			{
				if(!OpenSource(c.on_card, c.input, &why))
				{
					tried += std::string(tried.empty() ? "" : ", ") + c.name + " (" + why + ")";
					source.reset();
					continue;
				}
				last_on_card = c.on_card;
				last_input = c.input;
			}
			if(OpenEncoder(c, &why))
			{
				std::lock_guard<std::mutex> lock(info_mutex);
				encoder_name = c.name;
				return true;
			}
			tried += std::string(tried.empty() ? "" : ", ") + c.name + (c.on_card ? " from the graphics card" : "") + " (" + why + ")";
		}
		source.reset();
		*error = tried.empty() ? "no H.264 encoder in this build" : "no encoder could be opened: " + tried;
		return false;
	}
	bool last_on_card = false;
	AVPixelFormat last_input = AV_PIX_FMT_NONE;

	void CaptureLoop()
	{
		while(running)
		{
			bool ended = false;
			std::string why;
			AVFrame *f = source->Next(&ended, &why);
			if(ended)
			{
				Fail(why);
				return;
			}
			if(!f)
				continue;
			{
				std::lock_guard<std::mutex> lock(frame_mutex);
				if(latest)
					av_frame_free(&latest);
				latest = f;
				latest_new = true;
			}
			frame_changed.notify_one();
		}
	}

	void EncodeLoop()
	{
		AVFrame *last = nullptr; // what was encoded last, for showing it again
		auto last_sent = std::chrono::steady_clock::now();
		AVPacket *packet = av_packet_alloc();
		while(running)
		{
			AVFrame *f = nullptr;
			{
				std::unique_lock<std::mutex> lock(frame_mutex);
				// A still picture is sent again now and then, and at once when a guest needs a key frame
				frame_changed.wait_for(lock, std::chrono::milliseconds(100), [this] { return latest_new || !running; });
				if(!running)
					break;
				if(latest_new)
				{
					f = latest;
					latest = nullptr;
					latest_new = false;
				}
			}
			const auto now = std::chrono::steady_clock::now();
			if(f)
			{
				if(last)
					av_frame_free(&last);
				last = f;
			}
			else if(!last || (!keyframe_wanted && now - last_sent < std::chrono::milliseconds(500)))
				continue;
			AVFrame *send = av_frame_clone(last);
			if(!send)
				continue;
			int64_t pts = std::chrono::duration_cast<std::chrono::milliseconds>(now - started).count();
			if(pts <= last_pts)
				pts = last_pts + 1;
			last_pts = pts;
			send->pts = pts;
			send->pict_type = AV_PICTURE_TYPE_NONE;
			send->flags &= ~AV_FRAME_FLAG_KEY;
			if(keyframe_wanted.exchange(false))
			{
				send->pict_type = AV_PICTURE_TYPE_I;
				send->flags |= AV_FRAME_FLAG_KEY;
			}
			if(upload)
			{
				// Into the card's memory, for an encoder that reads only that
				AVFrame *on_card = av_frame_alloc();
				if(!on_card || av_hwframe_get_buffer(encoder->hw_frames_ctx, on_card, 0) < 0
					|| av_hwframe_transfer_data(on_card, send, 0) < 0 || av_frame_copy_props(on_card, send) < 0)
				{
					av_frame_free(&on_card);
					av_frame_free(&send);
					continue;
				}
				av_frame_free(&send);
				send = on_card;
			}
			int err = avcodec_send_frame(encoder, send);
			av_frame_free(&send);
			if(err < 0 && err != AVERROR(EAGAIN))
			{
				Fail("the encoder stopped: " + AvError(err));
				break;
			}
			last_sent = now;
			while(avcodec_receive_packet(encoder, packet) >= 0)
			{
				Deliver(packet);
				av_packet_unref(packet);
			}
		}
		av_packet_free(&packet);
		if(last)
			av_frame_free(&last);
	}

	/** The parameter sets last handed over on their own. */
	std::vector<uint8_t> config_sent;
	/** PSRPT_PC_DUMP=<file>: what goes to the guests, as an H.264 file, for finding faults. */
	FILE *dump = nullptr;

	void Deliver(const AVPacket *packet)
	{
		if(!callbacks.on_video || packet->size <= 0)
			return;
		const uint8_t *data = packet->data;
		const size_t size = static_cast<size_t>(packet->size);
		if(dump)
		{
			fwrite(data, 1, size, dump);
			fflush(dump);
		}
		bool key = false, has_sps = false;
		std::vector<uint8_t> sets;
		static const uint8_t start[] = { 0, 0, 0, 1 };
		for(const auto &unit : NalUnits(data, size))
		{
			const unsigned type = data[unit.first] & 0x1f;
			if(type == 5)
				key = true;
			else if(type == 7 || type == 8)
			{
				has_sps = has_sps || type == 7;
				sets.insert(sets.end(), start, start + 4);
				sets.insert(sets.end(), data + unit.first, data + unit.first + unit.second);
			}
		}
		if(has_sps)
			parameter_sets = sets;
		// The parameter sets on their own first, as a console sends them: the host keeps them
		// as the picture's setup and gives them to every guest before the frames (FrameKind::Config)
		if(!parameter_sets.empty() && parameter_sets != config_sent)
		{
			config_sent = parameter_sets;
			callbacks.on_video(config_sent.data(), config_sent.size());
		}
		// A guest that starts at a key frame needs SPS and PPS right in front of it
		if(key && !has_sps && !parameter_sets.empty())
		{
			std::vector<uint8_t> whole(parameter_sets);
			whole.insert(whole.end(), data, data + size);
			callbacks.on_video(whole.data(), whole.size());
			return;
		}
		callbacks.on_video(data, size);
	}

	void SoundLoop()
	{
		OpusPacker packer(callbacks.on_audio);
		if(!packer.Ok())
		{
			Log("no sound: the Opus encoder could not be opened");
			return;
		}
		if(config.target.kind == QLatin1String("test"))
		{
			// A short beep every second, the first frame of each second
			const size_t frames = kAudioFrameSamples;
			std::vector<int16_t> buf(frames * kAudioChannels);
			auto due = std::chrono::steady_clock::now();
			uint64_t n = 0;
			while(running)
			{
				for(size_t i = 0; i < frames; i++)
				{
					const uint64_t t = n * frames + i;
					const bool on = (t % kAudioRate) < kAudioRate / 8;
					const int16_t v = on ? static_cast<int16_t>(6000 * std::sin(2 * 3.14159265358979 * 660.0 * static_cast<double>(t) / kAudioRate)) : 0;
					buf[i * 2] = buf[i * 2 + 1] = v;
				}
				packer.Push(buf.data(), frames);
				n++;
				due += std::chrono::milliseconds(10);
				std::this_thread::sleep_until(due);
			}
			return;
		}
		if(config.target.kind == QLatin1String("device"))
		{
			// The device's own sound input; its system thread hands the samples over
			std::string why;
			PcSoundInfo info;
			auto push = [&packer](const int16_t *samples, size_t frames) { packer.Push(samples, frames); };
			std::unique_ptr<PcSoundSource> device_sound = PcDevice::OpenSound(config, push, &info, &why);
			if(!device_sound)
			{
				Log("no sound: " + why);
				return;
			}
			{
				std::lock_guard<std::mutex> lock(info_mutex);
				sound_source = info.what;
			}
			Log("sound: " + info.what);
			while(running)
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			device_sound.reset();
			return;
		}
#ifdef _WIN32
		CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		const bool own_program = config.target.kind == QLatin1String("window") && config.target.pid;
		std::string why;
		IAudioClient *client = ActivateLoopback(own_program ? config.target.pid : GetCurrentProcessId(), own_program, &why);
		if(!client)
		{
			Log("no sound: " + why);
			CoUninitialize();
			return;
		}
		{
			std::lock_guard<std::mutex> lock(info_mutex);
			sound_source = own_program ? "the program's own sound" : "everything but this app";
		}
		WAVEFORMATEX format = {};
		format.wFormatTag = WAVE_FORMAT_PCM;
		format.nChannels = kAudioChannels;
		format.nSamplesPerSec = kAudioRate;
		format.wBitsPerSample = 16;
		format.nBlockAlign = static_cast<WORD>(format.nChannels * format.wBitsPerSample / 8);
		format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
		HANDLE ready = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		IAudioCaptureClient *capture = nullptr;
		HRESULT hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
			AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
			200000 /* 20 ms */, 0, &format, nullptr);
		if(SUCCEEDED(hr))
			hr = client->SetEventHandle(ready);
		if(SUCCEEDED(hr))
			hr = client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void **>(&capture));
		if(SUCCEEDED(hr))
			hr = client->Start();
		if(FAILED(hr))
		{
			char text[64];
			snprintf(text, sizeof(text), "no sound: capture failed (0x%08lx)", static_cast<unsigned long>(hr));
			Log(text);
		}
		std::vector<int16_t> silence;
		while(SUCCEEDED(hr) && running)
		{
			if(WaitForSingleObject(ready, 200) != WAIT_OBJECT_0)
				continue;
			UINT32 frames = 0;
			while(SUCCEEDED(capture->GetNextPacketSize(&frames)) && frames > 0)
			{
				BYTE *data = nullptr;
				DWORD flags = 0;
				if(FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr)))
					break;
				if(flags & AUDCLNT_BUFFERFLAGS_SILENT)
				{
					silence.assign(static_cast<size_t>(frames) * kAudioChannels, 0);
					packer.Push(silence.data(), frames);
				}
				else
					packer.Push(reinterpret_cast<const int16_t *>(data), frames);
				capture->ReleaseBuffer(frames);
			}
		}
		if(SUCCEEDED(hr))
			client->Stop();
		if(capture)
			capture->Release();
		client->Release();
		CloseHandle(ready);
		CoUninitialize();
#elif (defined(__linux__) && PSRPT_PC_PIPEWIRE) || (defined(__APPLE__) && PSRPT_PC_SCREENCAPTUREKIT)
		// The system's own thread hands the samples over; this one only waits for the end
		std::string why;
		PcSoundInfo info;
		auto push = [&packer](const int16_t *samples, size_t frames) { packer.Push(samples, frames); };
#if defined(__linux__)
		std::unique_ptr<PcSoundSource> source_sound = PcLinux::OpenSound(config, push, &info, &why);
#else
		std::unique_ptr<PcSoundSource> source_sound = PcMac::OpenSound(config, push, &info, &why);
#endif
		if(!source_sound)
		{
			Log("no sound: " + why);
			return;
		}
		{
			std::lock_guard<std::mutex> lock(info_mutex);
			sound_source = info.what;
		}
		Log("sound: " + info.what);
		while(running)
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		// Stops the system's thread before the packer goes
		source_sound.reset();
#else
		Log("no sound: this build cannot capture sound");
#endif
	}
};

PcCapture::PcCapture(const Config &config, const Callbacks &callbacks) : d(std::make_unique<Private>())
{
	d->config = config;
	d->callbacks = callbacks;
	// Encoders take even sizes
	d->config.width = std::max(320, config.width & ~1);
	d->config.height = std::max(180, config.height & ~1);
	d->config.fps = std::clamp(config.fps, 10, 120);
	d->config.video_kbps = std::clamp(config.video_kbps, 1000, 100000);
}

PcCapture::~PcCapture()
{
	Stop();
}

bool PcCapture::Start(std::string *error)
{
	if(d->running)
		return true;
	if(!d->Open(error))
		return false;
	d->Log("sharing \"" + d->config.target.title.toStdString() + "\" with " + d->encoder_name + " at "
		+ std::to_string(d->config.width) + "x" + std::to_string(d->config.height) + ", "
		+ std::to_string(d->config.fps) + " fps, " + std::to_string(d->config.video_kbps) + " kbit/s");
	if(const char *dump = getenv("PSRPT_PC_DUMP"))
		d->dump = *dump ? fopen(dump, "wb") : nullptr;
	d->started = std::chrono::steady_clock::now();
	d->running = true;
	d->failed = false;
	d->keyframe_wanted = true;
	d->capture_thread = std::thread([this] { d->CaptureLoop(); });
	d->encode_thread = std::thread([this] { d->EncodeLoop(); });
	if(d->config.sound)
		d->sound_thread = std::thread([this] { d->SoundLoop(); });
	return true;
}

void PcCapture::Stop()
{
	d->running = false;
	d->frame_changed.notify_all();
	if(d->capture_thread.joinable())
		d->capture_thread.join();
	if(d->encode_thread.joinable())
		d->encode_thread.join();
	if(d->sound_thread.joinable())
		d->sound_thread.join();
	if(d->latest)
		av_frame_free(&d->latest);
	if(d->encoder)
		avcodec_free_context(&d->encoder);
	d->source.reset();
	if(d->dump)
	{
		fclose(d->dump);
		d->dump = nullptr;
	}
}

void PcCapture::RequestKeyframe()
{
	d->keyframe_wanted = true;
	d->frame_changed.notify_one();
}

std::string PcCapture::Encoder() const
{
	std::lock_guard<std::mutex> lock(d->info_mutex);
	return d->encoder_name;
}

std::string PcCapture::SoundSource() const
{
	std::lock_guard<std::mutex> lock(d->info_mutex);
	return d->sound_source;
}

#if !defined(__linux__) || !PSRPT_PC_PIPEWIRE
// Only Linux asks the system which window or screen (pcsource_linux.cpp)
struct PcPortal::Private
{
};

PcPortal::PcPortal() : d(std::make_unique<Private>())
{
}

PcPortal::~PcPortal() = default;

void PcPortal::Open(const QString &token, std::function<void(const Stream &stream, const QString &error)> done)
{
	(void)token;
	done(Stream(), QCoreApplication::translate("PcSource", "This system has no screen sharing to ask."));
}
#endif
