// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// The pointer held in place on a Mac; see macmouse.h. Built with ARC.

#include "macmouse.h"

#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QCoreApplication>

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

namespace
{

/** Sees every event the app gets before Qt does, and takes the mouse's way from those that carry one. */
class MoveFilter : public QAbstractNativeEventFilter
{
	public:
		std::function<void(double, double)> moved;

		bool nativeEventFilter(const QByteArray &type, void *message, qintptr *) override
		{
			if(type != "mac_generic_NSEvent" || !moved)
				return false;
			NSEvent *event = (__bridge NSEvent *)message;
			switch(event.type)
			{
				// With a button held, the mouse "drags" instead of moving
				case NSEventTypeMouseMoved:
				case NSEventTypeLeftMouseDragged:
				case NSEventTypeRightMouseDragged:
				case NSEventTypeOtherMouseDragged:
					if(event.deltaX != 0.0 || event.deltaY != 0.0)
						moved(event.deltaX, event.deltaY);
					break;
				default:
					break;
			}
			// The event goes on as it would have
			return false;
		}
};

MoveFilter *filter = nullptr;

} // namespace

void macmouse::Begin(double screen_x, double screen_y, std::function<void(double dx, double dy)> moved)
{
	if(!filter)
	{
		filter = new MoveFilter;
		QCoreApplication::instance()->installNativeEventFilter(filter);
	}
	filter->moved = std::move(moved);
	CGWarpMouseCursorPosition(CGPointMake(screen_x, screen_y));
	CGAssociateMouseAndMouseCursorPosition(false);
}

void macmouse::End()
{
	if(filter)
	{
		QCoreApplication::instance()->removeNativeEventFilter(filter);
		delete filter;
		filter = nullptr;
	}
	CGAssociateMouseAndMouseCursorPosition(true);
}
