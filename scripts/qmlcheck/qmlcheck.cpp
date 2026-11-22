// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Compiles every QML file of the app with the Qt this is built against, and says what does
// not compile. The Windows and Linux builds use an older Qt than a Mac with Homebrew's: a
// word, a type or a property that only the newer one knows makes the app's window fail to
// load there ("Component not ready"), which no build notices (0.4.0 to 0.4.2 shipped so).
//
//   qmlcheck <qml.rcc> [more.rcc ...]
//
// The .rcc files are the app's resources (gui/src/qml/qml.qrc, and gui/res for what the QML
// loads), made with the same Qt's rcc. Nothing is created, only compiled: the app's own
// module is a stand-in, so what the pages read from it is not checked. Exits with 1 if a
// file does not compile.
#include <QDirIterator>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QResource>

#include <cstdio>

class StandIn : public QObject
{
	Q_OBJECT
	public:
		using QObject::QObject;
};

class StandInWindow : public StandIn { Q_OBJECT public: using StandIn::StandIn; };
class StandInSettings : public StandIn { Q_OBJECT public: using StandIn::StandIn; };
class StandInSession : public StandIn { Q_OBJECT public: using StandIn::StandIn; };

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
	qputenv("QT_MAC_DISABLE_FOREGROUND_APPLICATION_TRANSFORM", "1");
	QGuiApplication app(argc, argv);
	const QStringList args = app.arguments();
	if(args.size() < 2)
	{
		fprintf(stderr, "usage: qmlcheck <qml.rcc> [more.rcc ...]\n");
		return 2;
	}
	for(int i = 1; i < args.size(); i++)
	{
		if(!QResource::registerResource(args[i]))
		{
			fprintf(stderr, "cannot read %s\n", qPrintable(args[i]));
			return 2;
		}
	}

	// The app's own module (QmlBackend), as names only
	const char *uri = "org.streetpea.chiaking";
	StandIn chiaki;
	qmlRegisterSingletonInstance(uri, 1, 0, "Chiaki", &chiaki);
	qmlRegisterUncreatableType<StandInWindow>(uri, 1, 0, "ChiakiWindow", {});
	qmlRegisterUncreatableType<StandInSettings>(uri, 1, 0, "ChiakiSettings", {});
	qmlRegisterUncreatableType<StandInSession>(uri, 1, 0, "ChiakiSession", {});

	QQmlEngine engine;
	QStringList files;
	QDirIterator it(QStringLiteral(":/"), { QStringLiteral("*.qml") }, QDir::Files, QDirIterator::Subdirectories);
	while(it.hasNext())
	{
		const QString path = it.next();
		// Qt's own files are in the resources too
		if(!path.startsWith(QLatin1String(":/qt-project.org")) && !path.startsWith(QLatin1String(":/qt/")))
			files.append(path);
	}
	files.sort();
	int bad = 0;
	for(const QString &path : files)
	{
		QQmlComponent component(&engine, QUrl(QStringLiteral("qrc") + path));
		if(component.status() == QQmlComponent::Ready)
			continue;
		bad++;
		const auto errors = component.errors();
		for(const auto &e : errors)
			printf("%s\n", qPrintable(e.toString()));
		if(errors.isEmpty())
			printf("%s: not ready\n", qPrintable(path));
	}
	printf("%lld QML files compiled with Qt %s, %d with errors\n", static_cast<long long>(files.size()), qVersion(), bad);
	return bad ? 1 : 0;
}

#include "qmlcheck.moc"
