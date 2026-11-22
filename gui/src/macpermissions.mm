// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// What macOS has to allow the app, and asking for it; see macpermissions.h. Built with ARC
// (gui/CMakeLists.txt).

#import <AVFoundation/AVFoundation.h>
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <CoreGraphics/CoreGraphics.h>

#include "macpermissions.h"
#include "discoverymanager.h"

#include <QGuiApplication>
#include <QHash>
#include <QPointer>
#include <QSettings>
#include <QStringList>
#include <QVariantMap>

namespace {

const QStringList kKeys = {
	QStringLiteral("localNetwork"), QStringLiteral("microphone"), QStringLiteral("camera"),
	QStringLiteral("screen"), QStringLiteral("accessibility"),
};

/** macOS asks for screen recording and accessibility only once: whether the app has asked. */
QString AskedKey(const QString &key)
{
	return QStringLiteral("permissions/asked_") + key;
}

bool Asked(const QString &key)
{
	return QSettings().value(AskedKey(key), false).toBool();
}

void SetAsked(const QString &key)
{
	QSettings().setValue(AskedKey(key), true);
}

AVMediaType MediaOf(const QString &key)
{
	return key == QLatin1String("microphone") ? AVMediaTypeAudio : AVMediaTypeVideo;
}

} // namespace

MacPermissions::MacPermissions(DiscoveryManager *discovery, QObject *parent) : QObject(parent), discovery(discovery)
{
	screen_at_start = CGPreflightScreenCaptureAccess();
	if(discovery)
		connect(discovery, &DiscoveryManager::LocalNetworkBlockedChanged, this, &MacPermissions::refresh);
	// Back from System Settings: something may have changed there
	connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
		if(state == Qt::ApplicationActive)
			refresh();
	});
	refresh();
}

bool MacPermissions::supported()
{
	return true;
}

bool MacPermissions::allAllowed() const
{
	for(const QVariant &item : list)
	{
		if(item.toMap().value(QStringLiteral("state")).toString() != QLatin1String("allowed"))
			return false;
	}
	return true;
}

QString MacPermissions::StateOf(const QString &key) const
{
	const QString allowed = QStringLiteral("allowed"), ask = QStringLiteral("ask"), settings = QStringLiteral("settings");
	if(key == QLatin1String("localNetwork"))
	{
		// macOS asks by itself the first time the app looks for consoles; there is no asking again
		return discovery && discovery->GetLocalNetworkBlocked() ? settings : allowed;
	}
	if(key == QLatin1String("microphone") || key == QLatin1String("camera"))
	{
		switch([AVCaptureDevice authorizationStatusForMediaType:MediaOf(key)])
		{
			case AVAuthorizationStatusAuthorized: return allowed;
			case AVAuthorizationStatusNotDetermined: return ask;
			default: return settings;
		}
	}
	if(key == QLatin1String("screen"))
		return CGPreflightScreenCaptureAccess() ? allowed : (Asked(key) ? settings : ask);
	if(key == QLatin1String("accessibility"))
		return AXIsProcessTrusted() ? allowed : (Asked(key) ? settings : ask);
	return settings;
}

void MacPermissions::refresh()
{
	QVariantList now;
	for(const QString &key : kKeys)
	{
		QVariantMap item;
		item[QStringLiteral("key")] = key;
		item[QStringLiteral("state")] = StateOf(key);
		now.append(item);
	}
	const bool restart = !screen_at_start && CGPreflightScreenCaptureAccess();
	if(now == list && restart == restart_needed)
		return;
	list = now;
	restart_needed = restart;
	emit changed();
}

void MacPermissions::allow(const QString &key)
{
	const QString state = StateOf(key);
	if(state == QLatin1String("allowed"))
		return;
	if(state != QLatin1String("ask"))
	{
		openSettings(key);
		return;
	}
	if(key == QLatin1String("microphone") || key == QLatin1String("camera"))
	{
		QPointer<MacPermissions> self(this);
		[AVCaptureDevice requestAccessForMediaType:MediaOf(key) completionHandler:^(BOOL) {
			// On a queue of macOS: back to the main thread
			QMetaObject::invokeMethod(qGuiApp, [self]() {
				if(self)
					self->refresh();
			}, Qt::QueuedConnection);
		}];
		return;
	}
	if(key == QLatin1String("screen"))
	{
		SetAsked(key);
		// macOS shows its own window, which leads to System Settings
		CGRequestScreenCaptureAccess();
		refresh();
		return;
	}
	if(key == QLatin1String("accessibility"))
	{
		SetAsked(key);
		NSDictionary *options = @{ (__bridge NSString *)kAXTrustedCheckOptionPrompt: @YES };
		AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
		refresh();
	}
}

void MacPermissions::allowAll()
{
	// The microphone first, then the camera when its question is answered, then the two
	// that lead to System Settings: one window of macOS at a time where it can be helped
	QPointer<MacPermissions> self(this);
	auto rest = [self]() {
		if(!self)
			return;
		for(const QString &key : { QStringLiteral("screen"), QStringLiteral("accessibility") })
		{
			if(self->StateOf(key) == QLatin1String("ask"))
				self->allow(key);
		}
		self->refresh();
	};
	auto camera = [self, rest]() {
		if(!self)
			return;
		if(self->StateOf(QStringLiteral("camera")) != QLatin1String("ask"))
		{
			rest();
			return;
		}
		[AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo completionHandler:^(BOOL) {
			QMetaObject::invokeMethod(qGuiApp, rest, Qt::QueuedConnection);
		}];
	};
	if(StateOf(QStringLiteral("microphone")) != QLatin1String("ask"))
	{
		camera();
		return;
	}
	[AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL) {
		QMetaObject::invokeMethod(qGuiApp, camera, Qt::QueuedConnection);
	}];
}

void MacPermissions::openSettings(const QString &key)
{
	static const QHash<QString, QString> panes = {
		{ QStringLiteral("localNetwork"), QStringLiteral("Privacy_LocalNetwork") },
		{ QStringLiteral("microphone"), QStringLiteral("Privacy_Microphone") },
		{ QStringLiteral("camera"), QStringLiteral("Privacy_Camera") },
		{ QStringLiteral("screen"), QStringLiteral("Privacy_ScreenCapture") },
		{ QStringLiteral("accessibility"), QStringLiteral("Privacy_Accessibility") },
	};
	const QString pane = panes.value(key, QStringLiteral("Privacy"));
	NSString *url = [NSString stringWithFormat:@"x-apple.systempreferences:com.apple.preference.security?%@", pane.toNSString()];
	[[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:url]];
}
