// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// The guests' mouse and keyboard into a game on this Mac; see pcinput.h. Needs the
// Accessibility permission. Keys go to the shared program itself, the mouse moves the pointer
// while that program is in front. Built with ARC (gui/CMakeLists.txt).

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

#include "pcinput.h"
#include "keymap.h"

#include <coplay/protocol.h>

#include <QCoreApplication>

#include <algorithm>

#include <unistd.h>

using namespace coplay;

struct PcInput::Private
{
	CGEventSourceRef source = nullptr;
	CGEventFlags flags = 0;
	int wheel_rest_y = 0;
	int wheel_rest_x = 0;
};

QString PcInput::Problem()
{
	if(AXIsProcessTrusted())
		return QString();
	return QCoreApplication::translate("PcInput", "Friends' mouse and keyboard need macOS's permission: allow PSRemotePlayTogether under System Settings → Privacy & Security → Accessibility.");
}

void PcInput::AskPermission()
{
	NSDictionary *options = @{ (__bridge NSString *)kAXTrustedCheckOptionPrompt: @YES };
	if(!AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options))
		[[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility"]];
}

PcInput::PcInput(const PcSource::Target &t) : d(std::make_unique<Private>()), target(t)
{
}

PcInput::~PcInput()
{
	ReleaseAll();
	if(d->source)
		CFRelease(d->source);
}

bool PcInput::Open(std::string *error)
{
	if(!AXIsProcessTrusted())
	{
		*error = "macOS's Accessibility permission is missing";
		return false;
	}
	d->source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
	return d->source != nullptr;
}

bool PcInput::GameInFront()
{
	const pid_t front = NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier;
	if(target.kind == QLatin1String("window"))
		return target.pid && front == static_cast<pid_t>(target.pid);
	return front != getpid();
}

static CGEventFlags FlagOf(uint8_t hid)
{
	switch(hid)
	{
		case KeyMap::kLeftCtrl: case KeyMap::kRightCtrl: return kCGEventFlagMaskControl;
		case KeyMap::kLeftShift: case KeyMap::kRightShift: return kCGEventFlagMaskShift;
		case KeyMap::kLeftAlt: case KeyMap::kRightAlt: return kCGEventFlagMaskAlternate;
		case KeyMap::kLeftGui: case KeyMap::kRightGui: return kCGEventFlagMaskCommand;
		default: return 0;
	}
}

void PcInput::Key(uint8_t hid, bool down)
{
	const uint16_t vk = KeyMap::HidToMac(hid);
	if(vk == 0xffff)
		return;
	// The modifiers held go with every key, as a keyboard's would
	const CGEventFlags flag = FlagOf(hid);
	if(flag)
		d->flags = down ? (d->flags | flag) : (d->flags & ~flag);
	CGEventRef e = CGEventCreateKeyboardEvent(d->source, static_cast<CGKeyCode>(vk), down);
	if(!e)
		return;
	CGEventSetFlags(e, d->flags);
	if(target.kind == QLatin1String("window") && target.pid)
		CGEventPostToPid(static_cast<pid_t>(target.pid), e);
	else
		CGEventPost(kCGHIDEventTap, e);
	CFRelease(e);
}

static CGPoint PointerNow()
{
	CGEventRef here = CGEventCreate(nullptr);
	const CGPoint p = here ? CGEventGetLocation(here) : CGPointZero;
	if(here)
		CFRelease(here);
	return p;
}

void PcInput::Button(uint8_t button, bool down)
{
	CGEventType type;
	CGMouseButton which;
	switch(button)
	{
		case kMouseLeft: type = down ? kCGEventLeftMouseDown : kCGEventLeftMouseUp; which = kCGMouseButtonLeft; break;
		case kMouseRight: type = down ? kCGEventRightMouseDown : kCGEventRightMouseUp; which = kCGMouseButtonRight; break;
		case kMouseMiddle: type = down ? kCGEventOtherMouseDown : kCGEventOtherMouseUp; which = kCGMouseButtonCenter; break;
		case kMouseBack: type = down ? kCGEventOtherMouseDown : kCGEventOtherMouseUp; which = static_cast<CGMouseButton>(3); break;
		case kMouseForward: type = down ? kCGEventOtherMouseDown : kCGEventOtherMouseUp; which = static_cast<CGMouseButton>(4); break;
		default: return;
	}
	CGEventRef e = CGEventCreateMouseEvent(d->source, type, PointerNow(), which);
	if(!e)
		return;
	CGEventSetFlags(e, d->flags);
	CGEventPost(kCGHIDEventTap, e);
	CFRelease(e);
}

void PcInput::Move(int dx, int dy)
{
	CGPoint p = PointerNow();
	p.x += dx;
	p.y += dy;
	// Never off the screens: the main one's bounds unless the pointer is on another
	CGDirectDisplayID display = CGMainDisplayID();
	uint32_t count = 0;
	CGGetDisplaysWithPoint(PointerNow(), 1, &display, &count);
	const CGRect bounds = CGDisplayBounds(display);
	p.x = std::clamp(p.x, CGRectGetMinX(bounds), CGRectGetMaxX(bounds) - 1);
	p.y = std::clamp(p.y, CGRectGetMinY(bounds), CGRectGetMaxY(bounds) - 1);
	CGEventType type = kCGEventMouseMoved;
	CGMouseButton which = kCGMouseButtonLeft;
	if(held_buttons & kMouseLeft)
		type = kCGEventLeftMouseDragged;
	else if(held_buttons & kMouseRight)
	{
		type = kCGEventRightMouseDragged;
		which = kCGMouseButtonRight;
	}
	else if(held_buttons)
	{
		type = kCGEventOtherMouseDragged;
		which = kCGMouseButtonCenter;
	}
	CGEventRef e = CGEventCreateMouseEvent(d->source, type, p, which);
	if(!e)
		return;
	// Games that turn the camera by the mouse read the movement itself
	CGEventSetIntegerValueField(e, kCGMouseEventDeltaX, dx);
	CGEventSetIntegerValueField(e, kCGMouseEventDeltaY, dy);
	CGEventSetFlags(e, d->flags);
	CGEventPost(kCGHIDEventTap, e);
	CFRelease(e);
}

void PcInput::Wheel(int wheel_y, int wheel_x)
{
	d->wheel_rest_y += wheel_y;
	d->wheel_rest_x += wheel_x;
	const int lines_y = d->wheel_rest_y / 120, lines_x = d->wheel_rest_x / 120;
	if(!lines_y && !lines_x)
		return;
	d->wheel_rest_y %= 120;
	d->wheel_rest_x %= 120;
	CGEventRef e = CGEventCreateScrollWheelEvent2(d->source, kCGScrollEventUnitLine, 2, lines_y, lines_x, 0);
	if(!e)
		return;
	CGEventPost(kCGHIDEventTap, e);
	CFRelease(e);
}

void PcInput::Flush()
{
}
