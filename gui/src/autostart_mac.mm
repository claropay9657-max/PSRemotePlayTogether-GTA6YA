// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Starting with the computer on a Mac (autostart.h): the app as a login item through
// SMAppService (macOS 13 and later), how it was opened, and its Dock icon while it sits in the
// menu bar only.

#include "autostart.h"

#include <QCoreApplication>
#include <QPointer>
#include <QTimer>

#import <AppKit/AppKit.h>
#import <ServiceManagement/ServiceManagement.h>
#include <CoreServices/CoreServices.h>

#include <memory>

namespace autostart
{

static QString Text(const char *text)
{
	return QCoreApplication::translate("Autostart", text);
}

bool Possible(QString *why)
{
	if(@available(macOS 13.0, *))
	{
		NSBundle *bundle = NSBundle.mainBundle;
		if(!bundle.bundleIdentifier)
		{
			if(why)
				*why = Text("Only for the app itself (PSRemotePlayTogether.app).");
			return false;
		}
		// Opened from Downloads or a disk image, the app runs from a hidden copy the system makes
		// for this time only (App Translocation): a login item would point at nothing tomorrow
		if([bundle.bundlePath containsString:@"/AppTranslocation/"])
		{
			if(why)
				*why = Text("Move PSRemotePlayTogether to the Applications folder first.");
			return false;
		}
		if(why)
			why->clear();
		return true;
	}
	if(why)
		*why = Text("Needs macOS 13 or later.");
	return false;
}

bool IsOn()
{
	if(@available(macOS 13.0, *))
	{
		const SMAppServiceStatus status = SMAppService.mainAppService.status;
		return status == SMAppServiceStatusEnabled || status == SMAppServiceStatusRequiresApproval;
	}
	return false;
}

bool Set(bool on, bool tray, QString *error)
{
	// Starting in the tray is not part of the login item: the app looks how it was opened
	Q_UNUSED(tray);
	if(@available(macOS 13.0, *))
	{
		QString why;
		if(on && !Possible(&why))
		{
			if(error)
				*error = why;
			return false;
		}
		SMAppService *service = SMAppService.mainAppService;
		NSError *ns_error = nil;
		BOOL ok = YES;
		if(on)
		{
			if(service.status != SMAppServiceStatusEnabled)
				ok = [service registerAndReturnError:&ns_error];
			// Where the system wants the player's word first, show them the place to give it
			if(ok && service.status == SMAppServiceStatusRequiresApproval)
				[SMAppService openSystemSettingsLoginItems];
		}
		else if(service.status != SMAppServiceStatusNotRegistered && service.status != SMAppServiceStatusNotFound)
			ok = [service unregisterAndReturnError:&ns_error];
		if(!ok)
		{
			if(error)
				*error = ns_error ? QString::fromNSString(ns_error.localizedDescription) : Text("macOS did not take the login item.");
			return false;
		}
		return true;
	}
	if(error)
		*error = Text("Needs macOS 13 or later.");
	return !on;
}

void Refresh(bool tray)
{
	// The system finds the app by itself, wherever it is
	Q_UNUSED(tray);
}

QString Note()
{
	if(@available(macOS 13.0, *))
	{
		if(SMAppService.mainAppService.status == SMAppServiceStatusRequiresApproval)
			return Text("Waiting for your OK in System Settings, General, Login Items.");
	}
	return QString();
}

void WhenLaunched(QObject *context, std::function<void(bool at_login)> done)
{
	auto pending = std::make_shared<std::function<void(bool)>>(std::move(done));
	QPointer<QObject> guard(context);
	// Always from Qt's event loop, and only once
	auto finish = [pending, guard](bool at_login) {
		if(!guard)
			return;
		QTimer::singleShot(0, guard.data(), [pending, at_login]() {
			if(!*pending)
				return;
			auto call = std::move(*pending);
			*pending = nullptr;
			call(at_login);
		});
	};
	if(NSRunningApplication.currentApplication.finishedLaunching)
	{
		finish(false);
		return;
	}
	// The system opens a login item with an "open application" event that says so; it is the
	// current event while the app's launch is being finished
	__block id observer = [NSNotificationCenter.defaultCenter addObserverForName:NSApplicationDidFinishLaunchingNotification
		object:nil queue:nil usingBlock:^(NSNotification *) {
			NSAppleEventDescriptor *event = NSAppleEventManager.sharedAppleEventManager.currentAppleEvent;
			const bool at_login = event && event.eventID == kAEOpenApplication
				&& [event paramDescriptorForKeyword:keyAEPropData].enumCodeValue == keyAELaunchedAsLogInItem;
			[NSNotificationCenter.defaultCenter removeObserver:observer];
			observer = nil;
			finish(at_login);
		}];
	// Never left waiting: without a word, the window opens
	QTimer::singleShot(3000, context, [finish]() { finish(false); });
}

void SetDockIcon(bool shown)
{
	if(!NSApp)
		return;
	if(!shown)
	{
		[NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
		// Without a window, menu bar or Dock icon the app steps back: the next app comes to front
		[NSApp hide:nil];
		return;
	}
	if(NSApp.activationPolicy != NSApplicationActivationPolicyRegular)
		[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
	[NSApp unhide:nil];
#if defined(MAC_OS_VERSION_14_0) && MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_VERSION_14_0
	[NSApp activate];
#else
	if(@available(macOS 14.0, *))
		[NSApp activate];
	else
		[NSApp activateIgnoringOtherApps:YES];
#endif
}

}
