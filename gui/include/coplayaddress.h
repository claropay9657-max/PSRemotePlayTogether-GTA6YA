// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_COPLAYADDRESS_H
#define CHIAKI_COPLAYADDRESS_H

#include <QElapsedTimer>
#include <QMutex>
#include <QObject>
#include <QString>

/**
 * The address this computer has on the internet, for the invite that friends
 * elsewhere need. A STUN server is asked for it. That is a question to somebody
 * else's server, so it is only asked when the user shares a session or wants to
 * see that invite, never just because the app runs. The answer is kept until
 * the app is closed.
 *
 * There is one for the whole app, so that the settings and a running session
 * show the same. It lives on the main thread; the asking happens on another.
 */
class CoPlayPublicAddress : public QObject
{
	Q_OBJECT

	public:
		enum State
		{
			Idle = 0,    // nobody has asked yet
			Looking = 1,
			Found = 2,
			Failed = 3
		};

		/** First called on the main thread. */
		static CoPlayPublicAddress *Instance();

		// These three may be read from any thread
		State GetState() const;
		/** For example "203.0.113.7"; empty unless found. */
		QString Address() const;
		/** Why the last lookup did not work, in the words of the network code. */
		QString Error() const;

		/**
		 * Asks unless the address is known and no older than a few minutes, or
		 * somebody is asking already. An address that was found stays in place
		 * while it is checked again. For the main thread, returns at once.
		 * @param stun_setting what Settings::GetCoPlayStunServer() says
		 */
		void LookupIfStale(const QString &stun_setting);
		/** Asks in any case, because the user wants to know now. */
		void Lookup(const QString &stun_setting);
		/** Forgets the address, for example after the setting has changed. */
		void Reset();

		/** Whether the setting forbids asking. */
		static bool IsOff(const QString &stun_setting);

	signals:
		void Changed();

	private:
		explicit CoPlayPublicAddress(QObject *parent = nullptr);
		void Start(const QString &stun_setting, bool keep_known);

		mutable QMutex mutex; // state, address, error
		State state = Idle;
		QString address;
		QString error;

		// main thread only
		bool asking = false;
		int generation = 0; // counts Reset(), so that an answer to an older question is dropped
		QElapsedTimer found_at;
};

#endif // CHIAKI_COPLAYADDRESS_H
