// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "appupdater.h"

#include "appupdate.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QtConcurrent>

#include <cstdio>
#include <utility>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace
{

/** A file this large is not a release of this app. */
const qint64 kLargest = 1024ll * 1024 * 1024;

#if defined(Q_OS_MACOS)
const char *const kSystem = "macos";
#elif defined(Q_OS_WIN)
const char *const kSystem = "windows";
#else
const char *const kSystem = "linux";
#endif

/** The program's own name in its folder or bundle. */
const QString kProgram = QStringLiteral("PSRemotePlayTogether");

/** Runs a program to its end, in the folder `in` if one is given. True if it ended well; what it said goes to `said`. */
bool Run(const QString &program, const QStringList &arguments, int timeout_ms, QString *said = nullptr, const QString &in = QString())
{
	QProcess process;
	process.setProcessChannelMode(QProcess::MergedChannels);
	if(!in.isEmpty())
		process.setWorkingDirectory(in);
	process.start(program, arguments);
	if(!process.waitForStarted(10000))
	{
		if(said)
			*said = process.errorString();
		return false;
	}
	if(!process.waitForFinished(timeout_ms))
	{
		process.kill();
		process.waitForFinished(3000);
		if(said)
			*said = QStringLiteral("no end after %1 s").arg(timeout_ms / 1000);
		return false;
	}
	if(said)
		*said = QString::fromLocal8Bit(process.readAll()).trimmed();
	return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

QVariantMap ChangeForPage(const appupdate::Change &change)
{
	QVariantMap m;
	QString html;
	if(!change.title.empty())
		html = QStringLiteral("<b>") + QString::fromStdString(appupdate::InlineHtml(change.title)) + QStringLiteral("</b>");
	html += QString::fromStdString(appupdate::InlineHtml(change.text));
	m[QStringLiteral("html")] = html;
	m[QStringLiteral("isNew")] = change.is_new;
	m[QStringLiteral("isNote")] = change.is_note;
	return m;
}

QVariantList ChangesForPage(const std::vector<appupdate::Change> &changes)
{
	QVariantList out;
	for(const appupdate::Change &change : changes)
		out.append(ChangeForPage(change));
	return out;
}

#ifdef Q_OS_WIN
/** Windows' own programs (tar, cmd, PowerShell): by their full path, whatever else of those names is around. */
QString SystemFolder()
{
	return QDir::toNativeSeparators(qEnvironmentVariable("SystemRoot", QStringLiteral("C:\\Windows"))) + QStringLiteral("\\System32\\");
}

/**
 * Whether folders can be made and taken away in `dir`, by trying it. What Windows says about
 * a folder (not read-only) does not tell: under "Program Files" it says yes and means no.
 */
bool CanChange(const QString &dir)
{
	QTemporaryDir probe(dir + QStringLiteral("/.psrpt-XXXXXX"));
	return probe.isValid();
}
#endif

#ifdef Q_OS_MACOS
/** The bundle of the running app ("…/PSRemotePlayTogether.app"), or "" if it does not run from one. */
QString RunningBundle()
{
	QDir dir(QCoreApplication::applicationDirPath()); // …/Contents/MacOS
	if(dir.dirName() != QLatin1String("MacOS") || !dir.cdUp() || dir.dirName() != QLatin1String("Contents") || !dir.cdUp())
		return QString();
	return dir.dirName().endsWith(QLatin1String(".app")) ? dir.absolutePath() : QString();
}
#endif

} // namespace

AppUpdater::AppUpdater(QObject *parent)
	: QObject(parent)
{
}

AppUpdater::~AppUpdater()
{
	if(reply)
		reply->abort();
}

bool AppUpdater::busy() const
{
	return current_state == QLatin1String("download") || current_state == QLatin1String("check")
		|| current_state == QLatin1String("install") || current_state == QLatin1String("restart");
}

QString AppUpdater::Cannot()
{
#if defined(Q_OS_MACOS)
	const QString bundle = RunningBundle();
	if(bundle.isEmpty())
		return tr("This copy does not run from an app bundle, so it is updated the way it was built.");
	// Started where it was downloaded to and never moved, macOS runs the app from a protected copy
	if(bundle.contains(QLatin1String("/AppTranslocation/")))
		return tr("macOS runs the app from a protected place because it has not been moved since it was downloaded. Move it to the Applications folder, start it from there, and it can update itself.");
	const QFileInfo parent(QFileInfo(bundle).absolutePath());
	if(!parent.isWritable() || !QFileInfo(bundle).isWritable())
		return tr("The app cannot be replaced where it is (%1). Move it to the Applications folder, or update it from the download page.").arg(parent.absoluteFilePath());
	return QString();
#elif defined(Q_OS_WIN)
	// The new version is unpacked next to the app's folder, and the two are swapped
	const QFileInfo dir(QCoreApplication::applicationDirPath());
	if(!CanChange(dir.absolutePath()))
		return tr("The folder the app is in cannot be changed (%1). Move the app to a folder of your own, or update it from the download page.").arg(QDir::toNativeSeparators(dir.absoluteFilePath()));
	return QString();
#else
	const QString appimage = qEnvironmentVariable("APPIMAGE");
	if(appimage.isEmpty() || !QFileInfo::exists(appimage))
		return tr("This copy was not started from an AppImage, so it is updated the way it was installed.");
	const QFileInfo file(appimage);
	if(!file.isWritable() || !QFileInfo(file.absolutePath()).isWritable())
		return tr("The AppImage cannot be replaced where it is (%1). Update it from the download page.").arg(file.absolutePath());
	return QString();
#endif
}

void AppUpdater::SetRelease(const QString &version, const QJsonArray &assets, const QString &notes, const QString &also)
{
	if(busy())
		return;
	release_version = version;
	asset_name = QString::fromStdString(appupdate::AssetName(version.toStdString(), kSystem, QSysInfo::buildCpuArchitecture().toStdString()));
	asset_url.clear();
	sums_url.clear();
	asset_digest.clear();
	asset_size = 0;
	for(const QJsonValue &value : assets)
	{
		const QJsonObject asset = value.toObject();
		const QString name = asset.value(QStringLiteral("name")).toString();
		const QString url = asset.value(QStringLiteral("browser_download_url")).toString();
		if(asset_name.isEmpty() || !appupdate::TrustedUrl(url.toStdString(), also.toStdString()))
			continue;
		if(name == asset_name)
		{
			asset_url = QUrl(url);
			asset_size = static_cast<qint64>(asset.value(QStringLiteral("size")).toDouble());
			asset_digest = QString::fromStdString(appupdate::Sha256FromDigest(asset.value(QStringLiteral("digest")).toString().toStdString()));
		}
		else if(name == asset_name + QLatin1String(".sha256"))
			sums_url = QUrl(url);
	}

	if(asset_url.isEmpty())
		cannot = tr("Version %1 has no download for this computer. The download page has what there is.").arg(version);
	else if(sums_url.isEmpty() && asset_digest.isEmpty())
		cannot = tr("Version %1 does not say what its download must be, so the app cannot check it. Update from the download page.").arg(version);
	else if(asset_size > kLargest)
		cannot = tr("The download of version %1 is larger than this app can be. Update from the download page.").arg(version);
	else
		cannot = Cannot();

	release_news = ChangesForPage(appupdate::ParseReleaseNotes(notes.toStdString()));
	if(current_state == QLatin1String("failed"))
		SetState(QString());
	emit releaseChanged();
}

QVariantList AppUpdater::history() const
{
	if(!history_loaded)
	{
		history_loaded = true;
		QFile changelog(QStringLiteral(":/changelog.md"));
		if(changelog.open(QIODevice::ReadOnly))
		{
			for(const appupdate::Version &version : appupdate::ParseChangelog(changelog.readAll().toStdString()))
			{
				// A heading that is not a version ("Unreleased") is not for showing
				if(version.changes.empty() || version.version.empty() || !QChar(QLatin1Char(version.version[0])).isDigit())
					continue;
				QVariantMap m;
				m[QStringLiteral("version")] = QString::fromStdString(version.version);
				m[QStringLiteral("changes")] = ChangesForPage(version.changes);
				loaded_history.append(m);
			}
		}
	}
	return loaded_history;
}

void AppUpdater::SetState(const QString &state, const QString &error)
{
	current_state = state;
	last_error = error;
	emit stateChanged();
}

void AppUpdater::Fail(const QString &error)
{
	if(file.isOpen())
		file.close();
	work_dir.reset();
	SetState(QStringLiteral("failed"), error);
}

void AppUpdater::start()
{
	if(busy() || !possible())
		return;
	// Things may have changed since the release was found
	cannot = Cannot();
	if(!cannot.isEmpty())
	{
		emit releaseChanged();
		return;
	}
	work_dir.reset(new QTemporaryDir(QDir::tempPath() + QStringLiteral("/psrpt-update-XXXXXX")));
	file.setFileName(work_dir->isValid() ? work_dir->filePath(asset_name) : QString());
	if(!work_dir->isValid() || !file.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		Fail(tr("There is no room to put the download: %1").arg(QDir::tempPath()));
		return;
	}
	hash.reset();
	file_sum.clear();
	download_progress = 0;
	emit progressChanged();
	SetState(QStringLiteral("download"));

	QNetworkRequest request(asset_url);
	// GitHub hands a release's file on to where it is kept
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setHeader(QNetworkRequest::UserAgentHeader,
		QStringLiteral("PSRemotePlayTogether/%1").arg(QCoreApplication::applicationVersion()));
	request.setTransferTimeout(30000);
	reply = network.get(request);
	QNetworkReply *download = reply;
	connect(download, &QNetworkReply::readyRead, this, [this, download]() {
		const QByteArray data = download->readAll();
		hash.addData(data);
		// More than the release said, or more than any release is: not this file
		if(file.write(data) != data.size() || file.size() > kLargest)
			download->abort();
	});
	connect(download, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
		const qint64 of = total > 0 ? total : asset_size;
		download_progress = of > 0 ? qBound(0.0, static_cast<double>(received) / static_cast<double>(of), 1.0) : 0.0;
		emit progressChanged();
	});
	connect(download, &QNetworkReply::finished, this, [this, download]() {
		download->deleteLater();
		if(current_state != QLatin1String("download"))
			return;
		if(download->error() != QNetworkReply::NoError)
		{
			Fail(tr("The download did not arrive: %1. Nothing was changed.").arg(download->errorString()));
			return;
		}
		Downloaded();
	});
}

void AppUpdater::cancel()
{
	if(current_state != QLatin1String("download"))
		return;
	SetState(QString());
	if(reply)
		reply->abort();
	if(file.isOpen())
		file.close();
	work_dir.reset();
}

void AppUpdater::Downloaded()
{
	file.close();
	file_sum = QString::fromLatin1(hash.result().toHex());
	SetState(QStringLiteral("check"));
	if(sums_url.isEmpty())
	{
		Verify(QByteArray());
		return;
	}
	// The sum the release publishes next to the file
	QNetworkRequest request(sums_url);
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setHeader(QNetworkRequest::UserAgentHeader,
		QStringLiteral("PSRemotePlayTogether/%1").arg(QCoreApplication::applicationVersion()));
	request.setTransferTimeout(20000);
	reply = network.get(request);
	QNetworkReply *sums = reply;
	connect(sums, &QNetworkReply::finished, this, [this, sums]() {
		sums->deleteLater();
		if(current_state != QLatin1String("check"))
			return;
		if(sums->error() != QNetworkReply::NoError)
		{
			Fail(tr("What the download must be could not be fetched: %1. Nothing was changed.").arg(sums->errorString()));
			return;
		}
		// A list of sums is a line or two
		Verify(sums->read(64 * 1024));
	});
}

void AppUpdater::Verify(const QByteArray &published)
{
	QStringList wanted;
	if(!sums_url.isEmpty())
		wanted.append(QString::fromStdString(appupdate::Sha256For(published.toStdString(), asset_name.toStdString())));
	if(!asset_digest.isEmpty())
		wanted.append(asset_digest);
	// Every sum there is has to be the file's; one that is missing where it should be is a no as well
	bool matches = !wanted.isEmpty();
	for(const QString &sum : std::as_const(wanted))
		matches = matches && !sum.isEmpty() && sum == file_sum;
	if(!matches)
	{
		Fail(tr("The download is not what version %1 says it must be, so it was thrown away. Nothing was changed. Try again, or use the download page.").arg(release_version));
		return;
	}
	Install();
}

void AppUpdater::Install()
{
	SetState(QStringLiteral("install"));
	const QString downloaded = file.fileName();
	const QString version = release_version;
	const QString name = asset_name;
	const qint64 pid = QCoreApplication::applicationPid();
#if defined(Q_OS_MACOS)
	const QString bundle = RunningBundle();
#elif defined(Q_OS_WIN)
	const QString app_dir = QCoreApplication::applicationDirPath();
	const QString exe = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
#else
	const QString appimage = qEnvironmentVariable("APPIMAGE");
	const QString mine = QCoreApplication::applicationVersion();
#endif
	// Unpacking and copying takes a moment: not on the thread that draws
	auto *watcher = new QFutureWatcher<QPair<bool, QString>>(this);
	connect(watcher, &QFutureWatcher<QPair<bool, QString>>::finished, this, [this, watcher]() {
		watcher->deleteLater();
		const QPair<bool, QString> result = watcher->result();
		work_dir.reset();
		if(!result.first)
		{
			SetState(QStringLiteral("failed"), tr("The new version could not be put in place: %1").arg(result.second));
			return;
		}
		SetState(QStringLiteral("restart"));
		emit restarting();
	});
	watcher->setFuture(QtConcurrent::run([=]() {
		QString error;
#if defined(Q_OS_MACOS)
		const bool ok = InstallMac(downloaded, bundle, version, pid, &error);
		Q_UNUSED(name);
#elif defined(Q_OS_WIN)
		const bool ok = InstallWindows(downloaded, app_dir, exe, pid, &error);
		Q_UNUSED(version);
		Q_UNUSED(name);
#else
		const bool ok = InstallAppImage(downloaded, appimage, mine, name, pid, &error);
		Q_UNUSED(version);
#endif
		return qMakePair(ok, error);
	}));
}

#ifdef Q_OS_MACOS
bool AppUpdater::InstallMac(const QString &dmg, const QString &bundle, const QString &version, qint64 pid, QString *error)
{
	const QFileInfo bundle_info(bundle);
	const QString parent = bundle_info.absolutePath();
	const QString mount = QFileInfo(dmg).absolutePath() + QStringLiteral("/mount");
	// Next to the app, on the same disk: putting it in place is then one step that cannot stop half way
	const QString staged = parent + QStringLiteral("/.") + bundle_info.fileName() + QStringLiteral(".update");
	const QString old = parent + QStringLiteral("/.") + bundle_info.fileName() + QStringLiteral(".old");
	QString said;

	QDir().mkpath(mount);
	if(!Run(QStringLiteral("/usr/bin/hdiutil"), { QStringLiteral("attach"), QStringLiteral("-nobrowse"), QStringLiteral("-readonly"),
		QStringLiteral("-noautoopen"), QStringLiteral("-mountpoint"), mount, dmg }, 120000, &said))
	{
		*error = tr("the disk image could not be opened (%1).").arg(said);
		return false;
	}
	// The app in it: the one of the same name, or the only one
	const QStringList apps = QDir(mount).entryList({ QStringLiteral("*.app") }, QDir::Dirs | QDir::NoDotAndDotDot);
	QString source;
	if(apps.contains(bundle_info.fileName()))
		source = mount + QLatin1Char('/') + bundle_info.fileName();
	else if(apps.size() == 1)
		source = mount + QLatin1Char('/') + apps.first();
	bool ok = !source.isEmpty();
	if(!ok)
		*error = tr("the disk image has no app in it.");
	if(ok)
	{
		QDir(staged).removeRecursively();
		// ditto copies an app as it is, with everything a plain copy leaves behind
		ok = Run(QStringLiteral("/usr/bin/ditto"), { source, staged }, 600000, &said);
		if(!ok)
			*error = tr("the app could not be copied out of the disk image (%1).").arg(said);
	}
	if(!Run(QStringLiteral("/usr/bin/hdiutil"), { QStringLiteral("detach"), mount }, 60000))
		Run(QStringLiteral("/usr/bin/hdiutil"), { QStringLiteral("detach"), QStringLiteral("-force"), mount }, 60000);
	if(!ok)
	{
		QDir(staged).removeRecursively();
		return false;
	}

	// What came out is the app, in the version that was promised
	const QString program = staged + QStringLiteral("/Contents/MacOS/") + kProgram;
	const QString says = QSettings(staged + QStringLiteral("/Contents/Info.plist"), QSettings::NativeFormat)
		.value(QStringLiteral("CFBundleShortVersionString")).toString();
	if(!QFileInfo(program).isExecutable() || (!says.isEmpty() && says != version))
	{
		QDir(staged).removeRecursively();
		*error = tr("what was downloaded is not version %1 of the app.").arg(version);
		return false;
	}

	// The running app's bundle aside (it goes on running from there), the new one in its place
	QDir(old).removeRecursively();
	if(!QDir().rename(bundle, old))
	{
		QDir(staged).removeRecursively();
		*error = tr("the app could not be moved out of the way (%1).").arg(parent);
		return false;
	}
	if(!QDir().rename(staged, bundle))
	{
		QDir().rename(old, bundle);
		QDir(staged).removeRecursively();
		*error = tr("the new app could not be moved into place (%1).").arg(parent);
		return false;
	}

	// Once this app has closed: the old bundle goes, the new app starts
	const QString script = QStringLiteral("while /bin/kill -0 \"$1\" 2>/dev/null; do /bin/sleep 0.2; done; /bin/rm -rf \"$2\"; /usr/bin/open -n \"$3\"");
	if(!QProcess::startDetached(QStringLiteral("/bin/sh"), { QStringLiteral("-c"), script, QStringLiteral("sh"), QString::number(pid), old, bundle }, parent))
	{
		// Nothing would start the new app: everything back as it was
		QDir().rename(bundle, staged);
		QDir().rename(old, bundle);
		QDir(staged).removeRecursively();
		*error = tr("the new app could not be set to start.");
		return false;
	}
	return true;
}
#endif

#ifdef Q_OS_WIN
bool AppUpdater::InstallWindows(const QString &zip, const QString &app_dir, const QString &exe, qint64 pid, QString *error)
{
	const QString app = QFileInfo(app_dir).absoluteFilePath();
	const QString stage = app + QStringLiteral(".update");
	QString said;
	QDir(stage).removeRecursively();
	if(!QDir().mkpath(stage))
	{
		*error = tr("there is no room next to the app's folder (%1).").arg(QDir::toNativeSeparators(stage));
		return false;
	}
	// Windows has tar, which reads .zip files, since 2018 (Windows 10 1803)
	const QString system = SystemFolder();
	const QString tar = system + QStringLiteral("tar.exe");
	if(!QFileInfo::exists(tar))
	{
		QDir(stage).removeRecursively();
		*error = tr("this Windows has no tar.exe to unpack the download with (%1).").arg(tar);
		return false;
	}
	// The download into that folder under a plain name, and tar started there: it is told no
	// folder's name then, which it might not read the way Windows wrote it
	const QString packed = stage + QStringLiteral("/update.zip");
	if(!QFile::rename(zip, packed))
	{
		QDir(stage).removeRecursively();
		*error = tr("there is no room next to the app's folder (%1).").arg(QDir::toNativeSeparators(stage));
		return false;
	}
	const bool unpacked = Run(tar, { QStringLiteral("-xf"), QStringLiteral("update.zip") }, 600000, &said, stage);
	QFile::remove(packed);
	if(!unpacked)
	{
		QDir(stage).removeRecursively();
		*error = tr("the download could not be unpacked (%1).").arg(said);
		return false;
	}
	// The zip holds one folder with the app in it
	QString fresh;
	if(QFileInfo::exists(stage + QLatin1Char('/') + exe))
		fresh = stage;
	else
	{
		const QStringList folders = QDir(stage).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
		for(const QString &folder : folders)
		{
			if(QFileInfo::exists(stage + QLatin1Char('/') + folder + QLatin1Char('/') + exe))
				fresh = stage + QLatin1Char('/') + folder;
		}
	}
	if(fresh.isEmpty() || fresh == stage)
	{
		// Straight in the folder that was made for unpacking: one folder further in, so that it can be moved
		if(fresh.isEmpty())
		{
			QDir(stage).removeRecursively();
			*error = tr("the download has no %1 in it.").arg(exe);
			return false;
		}
		const QString inner = stage + QStringLiteral(".app");
		QDir(inner).removeRecursively();
		if(!QDir().rename(stage, inner) || !QDir().mkpath(stage) || !QDir().rename(inner, stage + QLatin1Char('/') + kProgram))
		{
			*error = tr("the unpacked app could not be made ready (%1).").arg(QDir::toNativeSeparators(stage));
			return false;
		}
		fresh = stage + QLatin1Char('/') + kProgram;
	}

	// A running program's folder cannot be replaced: a script does it once this app has closed
	const QString script_path = QDir::temp().filePath(QStringLiteral("psrpt-update-%1.cmd").arg(pid));
	QFile script(script_path);
	const std::string text = appupdate::WindowsSwapScript();
	if(!script.open(QIODevice::WriteOnly | QIODevice::Truncate) || script.write(text.data(), static_cast<qint64>(text.size())) != static_cast<qint64>(text.size()))
	{
		QDir(stage).removeRecursively();
		*error = tr("the step that swaps the folders could not be written (%1).").arg(QDir::toNativeSeparators(script_path));
		return false;
	}
	script.close();
	QProcess swap;
	swap.setProgram(system + QStringLiteral("cmd.exe"));
	// /d: nothing else is run first; /v:off: a ! in a folder's name stays one. The script by its
	// plain name, in the folder it is started in: nothing on this line needs quotes.
	swap.setArguments({ QStringLiteral("/d"), QStringLiteral("/v:off"), QStringLiteral("/c"), QFileInfo(script_path).fileName() });
	// What it works on, in its environment: there a folder's name is taken as it is
	QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
	environment.insert(QString::fromLatin1(appupdate::kSwapPid), QString::number(pid));
	environment.insert(QString::fromLatin1(appupdate::kSwapApp), QDir::toNativeSeparators(app));
	environment.insert(QString::fromLatin1(appupdate::kSwapNew), QDir::toNativeSeparators(fresh));
	environment.insert(QString::fromLatin1(appupdate::kSwapStage), QDir::toNativeSeparators(stage));
	environment.insert(QString::fromLatin1(appupdate::kSwapExe), exe);
	swap.setProcessEnvironment(environment);
	// Not in the app's folder: a folder that is a running program's working folder cannot be moved
	swap.setWorkingDirectory(QDir::tempPath());
	// A console of its own that has no window: nothing black comes up while the script waits
	swap.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
		arguments->flags = (arguments->flags & ~static_cast<unsigned long>(CREATE_NEW_CONSOLE)) | CREATE_NO_WINDOW;
	});
	if(!swap.startDetached())
	{
		QDir(stage).removeRecursively();
		QFile::remove(script_path);
		*error = tr("the step that swaps the folders could not be started.");
		return false;
	}
	return true;
}
#else
bool AppUpdater::InstallAppImage(const QString &file, const QString &current, const QString &old_version, const QString &asset_name, qint64 pid, QString *error)
{
	const QFileInfo now(current);
	const QString target = QString::fromStdString(appupdate::AppImageTarget(now.absoluteFilePath().toStdString(), old_version.toStdString(), asset_name.toStdString()));
	// Next to where it goes, on the same disk, then into place in one step
	const QString part = QFileInfo(target).absolutePath() + QStringLiteral("/.") + QFileInfo(target).fileName() + QStringLiteral(".part");
	QFile::remove(part);
	if(!QFile::copy(file, part))
	{
		*error = tr("the new AppImage could not be written next to the old one (%1).").arg(now.absolutePath());
		return false;
	}
	// May be run by whoever could run the old one
	QFile::setPermissions(part, QFile::permissions(now.absoluteFilePath()) | QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
	if(std::rename(QFile::encodeName(part).constData(), QFile::encodeName(target).constData()) != 0)
	{
		QFile::remove(part);
		*error = tr("the new AppImage could not be moved into place (%1).").arg(target);
		return false;
	}
	// What the running AppImage put into the environment for itself points into the old one: not for the new one
	QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
	const QString appdir = environment.value(QStringLiteral("APPDIR"));
	if(!appdir.isEmpty())
	{
		const QStringList names = environment.keys();
		for(const QString &name : names)
		{
			const QString value = environment.value(name);
			if(!value.contains(appdir))
				continue;
			QStringList kept;
			const QStringList parts = value.split(QLatin1Char(':'));
			for(const QString &piece : parts)
			{
				if(!piece.contains(appdir))
					kept.append(piece);
			}
			if(kept.isEmpty())
				environment.remove(name);
			else
				environment.insert(name, kept.join(QLatin1Char(':')));
		}
	}
	for(const char *name : { "APPIMAGE", "APPDIR", "ARGV0", "OWD" })
		environment.remove(QLatin1String(name));

	// Once this app has closed: the old file goes if the new one has another name, and the new one starts
	const QString remove = target != now.absoluteFilePath() ? now.absoluteFilePath() : QString();
	QProcess start;
	start.setProgram(QStringLiteral("/bin/sh"));
	start.setArguments({ QStringLiteral("-c"),
		QStringLiteral("while kill -0 \"$1\" 2>/dev/null; do sleep 0.2; done; if [ -n \"$3\" ]; then rm -f \"$3\"; fi; exec \"$2\""),
		QStringLiteral("sh"), QString::number(pid), target, remove });
	start.setProcessEnvironment(environment);
	// Not wherever the running AppImage is unpacked to: that goes away with it
	start.setWorkingDirectory(QFileInfo(target).absolutePath());
	if(!start.startDetached())
	{
		*error = tr("the new AppImage is in place (%1) but could not be set to start. Start it yourself.").arg(target);
		return false;
	}
	return true;
}
#endif
