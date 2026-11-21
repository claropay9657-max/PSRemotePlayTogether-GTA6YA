// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_DISCOVERYMANAGER_H
#define CHIAKI_DISCOVERYMANAGER_H

#include <chiaki/discoveryservice.h>

#include "host.h"

#include <QObject>
#include <QList>
#include <QHash>

struct DiscoveryHost
{
	bool ps5;
	ChiakiDiscoveryHostState state;
	ChiakiTarget target;
	uint16_t host_request_port;
#define STRING_MEMBER(name) QString name;
	CHIAKI_DISCOVERY_HOST_STRING_FOREACH(STRING_MEMBER)
#undef STRING_MEMBER

	HostMAC GetHostMAC() const;
};

Q_DECLARE_METATYPE(DiscoveryHost)

struct ManualService
{
	~ManualService() { chiaki_discovery_service_fini(&service); }

	class DiscoveryManager *manager;
	bool discovered = false;
	DiscoveryHost discovery_host;
	ChiakiDiscoveryService service;
};

class Settings;

class DiscoveryManager : public QObject
{
	Q_OBJECT

	friend class DiscoveryManagerPrivate;

	private:
		ChiakiLog log;
		QList<ChiakiDiscoveryService> services;
		ChiakiDiscoveryService service;
		ChiakiDiscoveryService service_ipv6;
		bool service_active;
		bool service_active_ipv6;
		QList<DiscoveryHost> hosts;
		Settings *settings = {};
		QHash<QString, ManualService*> manual_services;
		/**
		 * macOS: a console that the broadcast search finds does not answer a search sent straight
		 * to it: refused by macOS ("No route to host", Local Network privacy) or dropped by a
		 * firewall such as LuLu. Waking and playing need that direct way.
		 */
		bool local_network_blocked = false;
		bool local_network_probed = false;
		int local_network_misses = 0;
		/** Even the search to this computer's subnets is refused: a firewall blocks the app. */
		bool local_network_all_refused = false;
		int broadcast_probe_fd = -1;
		int broadcast_probe_ok = 0;
		int probe_fd = -1;
		QString probe_host;
		class QTimer *local_network_timer = nullptr;
		class QTimer *probe_timeout = nullptr;
		class QSocketNotifier *probe_notifier = nullptr;
		void ProbeLocalNetwork();
		void ProbeAnswered();
		void ProbeTimedOut();
		void CloseProbe();
		void CloseBroadcastProbe();
		void SetLocalNetworkBlocked(bool blocked);

	private slots:
		void DiscoveryServiceHosts(QList<DiscoveryHost> hosts);
		void UpdateManualServices();

	public:
		explicit DiscoveryManager(QObject *parent = nullptr);
		~DiscoveryManager();

		void SetActive(bool active);
		void SetSettings(Settings *settings);

		void SendWakeup(const QString &host, const QByteArray &regist_key, bool ps5);

		bool GetActive() const { return service_active; }
		const QList<DiscoveryHost> GetHosts() const;
		/** macOS only: the system (Local Network privacy) or a firewall keeps this app from its network. */
		bool GetLocalNetworkBlocked() const { return local_network_blocked; }
		/** What to do about it, for the user. */
		static QString LocalNetworkBlockedText();

	signals:
		void HostsUpdated();
		void LocalNetworkBlockedChanged();
};

#endif //CHIAKI_DISCOVERYMANAGER_H
