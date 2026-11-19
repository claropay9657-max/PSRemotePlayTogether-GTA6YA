// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "coplayaddress.h"

#include <coplay/stun.h>

#include <QCoreApplication>
#include <QFutureWatcher>
#include <QLoggingCategory>
#include <QMutexLocker>
#include <QThread>
#include <QtConcurrentRun>

#include <string>
#include <vector>

Q_DECLARE_LOGGING_CATEGORY(chiakiGui)

// How long one server is given to answer
static const uint32_t kTimeoutMs = 1500;
// An address found longer ago than this is checked again before it goes into another invite:
// many connections get a new one every night
static const qint64 kFreshMs = 10 * 60 * 1000;

namespace {

struct Answer
{
	bool ok = false;
	QString address;
	QString error;
};

} // namespace

CoPlayPublicAddress::CoPlayPublicAddress(QObject *parent)
	: QObject(parent)
{
}

CoPlayPublicAddress *CoPlayPublicAddress::Instance()
{
	// Never deleted: a lookup may still be on its way when the app closes
	static CoPlayPublicAddress *instance = [] {
		auto created = new CoPlayPublicAddress;
		QCoreApplication *app = QCoreApplication::instance();
		if(app && created->thread() != app->thread())
			created->moveToThread(app->thread());
		return created;
	}();
	return instance;
}

CoPlayPublicAddress::State CoPlayPublicAddress::GetState() const
{
	QMutexLocker locker(&mutex);
	return state;
}

QString CoPlayPublicAddress::Address() const
{
	QMutexLocker locker(&mutex);
	return address;
}

QString CoPlayPublicAddress::Error() const
{
	QMutexLocker locker(&mutex);
	return error;
}

bool CoPlayPublicAddress::IsOff(const QString &stun_setting)
{
	return stun_setting.trimmed().compare(QLatin1String("off"), Qt::CaseInsensitive) == 0;
}

void CoPlayPublicAddress::LookupIfStale(const QString &stun_setting)
{
	bool known = GetState() == Found;
	if(known && found_at.isValid() && found_at.elapsed() < kFreshMs)
		return;
	Start(stun_setting, known);
}

void CoPlayPublicAddress::Lookup(const QString &stun_setting)
{
	Start(stun_setting, false);
}

void CoPlayPublicAddress::Reset()
{
	// Whatever is still being asked belongs to the old setting; its answer is dropped
	generation++;
	asking = false;
	found_at.invalidate();
	{
		QMutexLocker locker(&mutex);
		if(state == Idle)
			return;
		state = Idle;
		address.clear();
		error.clear();
	}
	emit Changed();
}

void CoPlayPublicAddress::Start(const QString &stun_setting, bool keep_known)
{
	if(asking || IsOff(stun_setting))
		return;

	std::vector<coplay::StunServer> servers;
	if(stun_setting.trimmed().isEmpty())
		servers = coplay::DefaultStunServers();
	else
	{
		coplay::StunServer server;
		if(!coplay::ParseStunServer(stun_setting.toStdString(), server))
		{
			{
				QMutexLocker locker(&mutex);
				state = Failed;
				address.clear();
				error = QStringLiteral("\"%1\" is not a server address").arg(stun_setting.trimmed());
			}
			emit Changed();
			return;
		}
		servers.push_back(server);
	}

	asking = true;
	if(!keep_known)
	{
		{
			QMutexLocker locker(&mutex);
			state = Looking;
			error.clear();
		}
		emit Changed();
	}

	const int asked_in = generation;
	auto watcher = new QFutureWatcher<Answer>(this);
	connect(watcher, &QFutureWatcher<Answer>::finished, this, [this, watcher, asked_in, keep_known]() {
		watcher->deleteLater();
		if(asked_in != generation)
			return;
		asking = false;
		const Answer answer = watcher->result();
		bool changed = true;
		{
			QMutexLocker locker(&mutex);
			if(answer.ok)
			{
				changed = state != Found || address != answer.address;
				state = Found;
				address = answer.address;
				error.clear();
			}
			else if(keep_known && state == Found)
			{
				// Only a check of an address that was right a while ago: one failed
				// question is no reason to take a working invite away
				changed = false;
				error = answer.error;
			}
			else
			{
				state = Failed;
				address.clear();
				error = answer.error;
			}
		}
		// The address itself stays out of the log: logs get passed around
		if(answer.ok)
		{
			found_at.start();
			qCInfo(chiakiGui) << "Play Together: found this computer's address on the internet";
		}
		else
			qCWarning(chiakiGui) << "Play Together: could not find this computer's address on the internet:" << answer.error;
		if(changed)
			emit Changed();
	});
	watcher->setFuture(QtConcurrent::run([servers]() {
		Answer answer;
		std::string address, error;
		// IPv4: that is the address a router hides. The IPv6 one the computer knows itself.
		answer.ok = coplay::StunQueryFirst(servers, kTimeoutMs, address, nullptr, &error, coplay::StunFamily::IPv4);
		if(answer.ok)
			answer.address = QString::fromStdString(address);
		else
			answer.error = QString::fromStdString(error);
		return answer;
	}));
}
