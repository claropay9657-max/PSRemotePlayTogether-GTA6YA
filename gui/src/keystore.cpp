// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "keystore.h"

#include <QHash>
#include <QMutex>
#include <QObject>

#include <cstdint>
#include <string>

#if defined(Q_OS_MACOS)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#include <wincred.h>
#else
#include <dlfcn.h>
#endif

namespace keystore {

static const char kService[] = "PSRemotePlayTogether";

// Kept here when the system has no store (or it would not take the secret)
static QMutex memory_mutex;
static QHash<QString, QString> memory;

static void Remember(const QString &name, const QString &secret)
{
	QMutexLocker locker(&memory_mutex);
	if(secret.isEmpty())
		memory.remove(name);
	else
		memory.insert(name, secret);
}

static QString Remembered(const QString &name)
{
	QMutexLocker locker(&memory_mutex);
	return memory.value(name);
}

#if defined(Q_OS_MACOS)

// ------------------------------------------------------------------ macOS keychain

static CFStringRef Cf(const QString &s)
{
	return s.toCFString();
}

static CFMutableDictionaryRef Query(const QString &name)
{
	CFMutableDictionaryRef q = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	CFStringRef service = Cf(QString::fromLatin1(kService));
	CFStringRef account = Cf(name);
	CFDictionarySetValue(q, kSecClass, kSecClassGenericPassword);
	CFDictionarySetValue(q, kSecAttrService, service);
	CFDictionarySetValue(q, kSecAttrAccount, account);
	CFRelease(service);
	CFRelease(account);
	return q;
}

static bool StoreSave(const QString &name, const QString &secret, QString *error)
{
	CFMutableDictionaryRef q = Query(name);
	SecItemDelete(q);
	QByteArray utf8 = secret.toUtf8();
	CFDataRef data = CFDataCreate(nullptr, reinterpret_cast<const UInt8 *>(utf8.constData()), utf8.size());
	CFStringRef label = Cf(QStringLiteral("PSRemotePlayTogether: %1").arg(name));
	CFDictionarySetValue(q, kSecValueData, data);
	CFDictionarySetValue(q, kSecAttrLabel, label);
	OSStatus status = SecItemAdd(q, nullptr);
	CFRelease(label);
	CFRelease(data);
	CFRelease(q);
	if(status != errSecSuccess)
	{
		if(error)
		{
			CFStringRef msg = SecCopyErrorMessageString(status, nullptr);
			*error = msg ? QString::fromCFString(msg) : QString::number(status);
			if(msg)
				CFRelease(msg);
		}
		return false;
	}
	return true;
}

static bool StoreLoad(const QString &name, QString *secret)
{
	CFMutableDictionaryRef q = Query(name);
	CFDictionarySetValue(q, kSecReturnData, kCFBooleanTrue);
	CFDictionarySetValue(q, kSecMatchLimit, kSecMatchLimitOne);
	CFTypeRef result = nullptr;
	OSStatus status = SecItemCopyMatching(q, &result);
	CFRelease(q);
	if(status != errSecSuccess || !result)
		return false;
	CFDataRef data = static_cast<CFDataRef>(result);
	*secret = QString::fromUtf8(reinterpret_cast<const char *>(CFDataGetBytePtr(data)), static_cast<int>(CFDataGetLength(data)));
	CFRelease(result);
	return true;
}

static void StoreRemove(const QString &name)
{
	CFMutableDictionaryRef q = Query(name);
	SecItemDelete(q);
	CFRelease(q);
}

QString StoreName()
{
	return QObject::tr("the macOS keychain");
}

#elif defined(Q_OS_WIN)

// ------------------------------------------------------------------ Windows Credential Manager

static std::wstring Target(const QString &name)
{
	return QStringLiteral("%1/%2").arg(QString::fromLatin1(kService), name).toStdWString();
}

static bool StoreSave(const QString &name, const QString &secret, QString *error)
{
	std::wstring target = Target(name);
	QByteArray utf8 = secret.toUtf8();
	CREDENTIALW cred = {};
	cred.Type = CRED_TYPE_GENERIC;
	cred.TargetName = const_cast<LPWSTR>(target.c_str());
	cred.CredentialBlobSize = static_cast<DWORD>(utf8.size());
	cred.CredentialBlob = reinterpret_cast<LPBYTE>(utf8.data());
	cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
	std::wstring user = QStringLiteral("PSRemotePlayTogether").toStdWString();
	cred.UserName = const_cast<LPWSTR>(user.c_str());
	if(!CredWriteW(&cred, 0))
	{
		if(error)
			*error = QStringLiteral("error %1").arg(GetLastError());
		return false;
	}
	return true;
}

static bool StoreLoad(const QString &name, QString *secret)
{
	std::wstring target = Target(name);
	PCREDENTIALW cred = nullptr;
	if(!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &cred) || !cred)
		return false;
	*secret = QString::fromUtf8(reinterpret_cast<const char *>(cred->CredentialBlob), static_cast<int>(cred->CredentialBlobSize));
	CredFree(cred);
	return true;
}

static void StoreRemove(const QString &name)
{
	std::wstring target = Target(name);
	CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0);
}

QString StoreName()
{
	return QObject::tr("the Windows Credential Manager");
}

#else

// ------------------------------------------------------------------ Linux: Secret Service through libsecret
//
// libsecret is opened when needed instead of linked, so the app also starts where it is
// missing. The few types it takes are plain C structures that libsecret keeps stable.

namespace {

typedef struct
{
	const char *name;
	int type; // SecretSchemaAttributeType, 0 = string
} SecretSchemaAttribute;

typedef struct
{
	const char *name;
	int flags; // SecretSchemaFlags
	SecretSchemaAttribute attributes[32];
	int reserved;
	void *reserved1;
	void *reserved2;
	void *reserved3;
	void *reserved4;
	void *reserved5;
	void *reserved6;
	void *reserved7;
} SecretSchema;

typedef struct
{
	uint32_t domain;
	int code;
	char *message;
} GError;

using StoreFn = int (*)(const SecretSchema *, const char *, const char *, const char *, void *, GError **, ...);
using LookupFn = char *(*)(const SecretSchema *, void *, GError **, ...);
using ClearFn = int (*)(const SecretSchema *, void *, GError **, ...);
using FreeFn = void (*)(char *);
using ErrorFreeFn = void (*)(GError *);

struct Libsecret
{
	bool loaded = false;
	StoreFn store = nullptr;
	LookupFn lookup = nullptr;
	ClearFn clear = nullptr;
	FreeFn free_password = nullptr;
	ErrorFreeFn error_free = nullptr;
};

const SecretSchema kSchema = {
	"net.gamernova.PSRemotePlayTogether.Secret", 0,
	{ { "service", 0 }, { "name", 0 }, { nullptr, 0 } },
	0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr
};

const Libsecret &Lib()
{
	static Libsecret lib;
	static bool tried = false;
	static QMutex mutex;
	QMutexLocker locker(&mutex);
	if(tried)
		return lib;
	tried = true;
	void *handle = dlopen("libsecret-1.so.0", RTLD_NOW | RTLD_GLOBAL);
	if(!handle)
		return lib;
	lib.store = reinterpret_cast<StoreFn>(dlsym(handle, "secret_password_store_sync"));
	lib.lookup = reinterpret_cast<LookupFn>(dlsym(handle, "secret_password_lookup_sync"));
	lib.clear = reinterpret_cast<ClearFn>(dlsym(handle, "secret_password_clear_sync"));
	lib.free_password = reinterpret_cast<FreeFn>(dlsym(handle, "secret_password_free"));
	lib.error_free = reinterpret_cast<ErrorFreeFn>(dlsym(RTLD_DEFAULT, "g_error_free"));
	lib.loaded = lib.store && lib.lookup && lib.clear && lib.free_password;
	return lib;
}

QString TakeError(GError *err)
{
	if(!err)
		return QString();
	QString msg = QString::fromUtf8(err->message ? err->message : "");
	if(Lib().error_free)
		Lib().error_free(err);
	return msg;
}

} // namespace

static bool StoreSave(const QString &name, const QString &secret, QString *error)
{
	const Libsecret &lib = Lib();
	if(!lib.loaded)
	{
		if(error)
			*error = QObject::tr("libsecret is not installed");
		return false;
	}
	GError *err = nullptr;
	QByteArray n = name.toUtf8();
	QByteArray label = QStringLiteral("PSRemotePlayTogether: %1").arg(name).toUtf8();
	QByteArray s = secret.toUtf8();
	int ok = lib.store(&kSchema, "default", label.constData(), s.constData(), nullptr, &err,
		"service", kService, "name", n.constData(), static_cast<char *>(nullptr));
	QString msg = TakeError(err);
	if(!ok)
	{
		if(error)
			*error = msg;
		return false;
	}
	return true;
}

static bool StoreLoad(const QString &name, QString *secret)
{
	const Libsecret &lib = Lib();
	if(!lib.loaded)
		return false;
	GError *err = nullptr;
	QByteArray n = name.toUtf8();
	char *value = lib.lookup(&kSchema, nullptr, &err, "service", kService, "name", n.constData(), static_cast<char *>(nullptr));
	TakeError(err);
	if(!value)
		return false;
	*secret = QString::fromUtf8(value);
	lib.free_password(value);
	return true;
}

static void StoreRemove(const QString &name)
{
	const Libsecret &lib = Lib();
	if(!lib.loaded)
		return;
	GError *err = nullptr;
	QByteArray n = name.toUtf8();
	lib.clear(&kSchema, nullptr, &err, "service", kService, "name", n.constData(), static_cast<char *>(nullptr));
	TakeError(err);
}

QString StoreName()
{
	return Lib().loaded ? QObject::tr("the system keyring") : QString();
}

#endif

// ------------------------------------------------------------------ the same everywhere

bool Save(const QString &name, const QString &secret, QString *error)
{
	if(secret.isEmpty())
	{
		Remove(name);
		return true;
	}
	if(StoreSave(name, secret, error))
	{
		Remember(name, QString());
		return true;
	}
	Remember(name, secret);
	return false;
}

QString Load(const QString &name)
{
	QString in_memory = Remembered(name);
	if(!in_memory.isEmpty())
		return in_memory;
	QString secret;
	if(StoreLoad(name, &secret))
		return secret;
	return QString();
}

void Remove(const QString &name)
{
	Remember(name, QString());
	StoreRemove(name);
}

} // namespace keystore
