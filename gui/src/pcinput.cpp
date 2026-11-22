// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// The guests' mouse and keyboard into a game on this computer; see pcinput.h. Windows and
// Linux here, the Mac in pcinput_mac.mm.

#include "pcinput.h"
#include "keymap.h"

#include <coplay/protocol.h>

#include <QCoreApplication>
#include <QGuiApplication>

#include <algorithm>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <linux/uinput.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#ifndef REL_WHEEL_HI_RES
#define REL_WHEEL_HI_RES 0x0b
#define REL_HWHEEL_HI_RES 0x0c
#endif
#endif

using namespace coplay;

// ------------------------------------------------------------------ what every system shares

std::vector<uint8_t> PcInput::Allowed(const std::vector<uint8_t> &keys)
{
	auto has = [&keys](uint8_t k) { return std::find(keys.begin(), keys.end(), k) != keys.end(); };
	const bool ctrl = has(KeyMap::kLeftCtrl) || has(KeyMap::kRightCtrl);
	const bool alt = has(KeyMap::kLeftAlt) || has(KeyMap::kRightAlt);
	std::vector<uint8_t> out;
	for(uint8_t k : keys)
	{
		// What would leave the game, or the computer's own keys: never from a guest
		if(k == KeyMap::kLeftGui || k == KeyMap::kRightGui)
			continue;
		if(alt && (k == KeyMap::kTab || k == KeyMap::kF4 || k == KeyMap::kEscape))
			continue;
		if(ctrl && k == KeyMap::kEscape)
			continue;
		if(ctrl && alt && k == KeyMap::kDelete)
			continue;
		if(std::find(out.begin(), out.end(), k) == out.end())
			out.push_back(k);
	}
	return out;
}

void PcInput::Apply(int dx, int dy, int wheel_y, int wheel_x, uint8_t buttons, const std::vector<uint8_t> &keys)
{
	if(!GameInFront())
	{
		ReleaseAll();
		return;
	}
	const std::vector<uint8_t> wanted = Allowed(keys);
	// Let go first, so that a changed modifier never meets the next key
	for(uint8_t k : std::vector<uint8_t>(held_keys))
	{
		if(std::find(wanted.begin(), wanted.end(), k) == wanted.end())
		{
			Key(k, false);
			held_keys.erase(std::find(held_keys.begin(), held_keys.end(), k));
		}
	}
	// Modifiers before the keys they change
	std::vector<uint8_t> order(wanted);
	std::stable_sort(order.begin(), order.end(), [](uint8_t a, uint8_t b) { return KeyMap::IsModifier(a) && !KeyMap::IsModifier(b); });
	for(uint8_t k : order)
	{
		if(std::find(held_keys.begin(), held_keys.end(), k) == held_keys.end())
		{
			Key(k, true);
			held_keys.push_back(k);
		}
	}
	if(dx || dy)
		Move(dx, dy);
	for(uint8_t b = 1; b && b <= kMouseForward; b = static_cast<uint8_t>(b << 1))
	{
		const bool now = (buttons & b) != 0, before = (held_buttons & b) != 0;
		if(now != before)
			Button(b, now);
	}
	held_buttons = buttons;
	if(wheel_y || wheel_x)
		Wheel(wheel_y, wheel_x);
	Flush();
}

void PcInput::ReleaseAll()
{
	if(held_keys.empty() && !held_buttons)
		return;
	for(uint8_t b = 1; b && b <= kMouseForward; b = static_cast<uint8_t>(b << 1))
	{
		if(held_buttons & b)
			Button(b, false);
	}
	held_buttons = 0;
	// Modifiers last
	std::vector<uint8_t> order(held_keys);
	std::stable_sort(order.begin(), order.end(), [](uint8_t a, uint8_t b) { return !KeyMap::IsModifier(a) && KeyMap::IsModifier(b); });
	for(uint8_t k : order)
		Key(k, false);
	held_keys.clear();
	Flush();
}

// ------------------------------------------------------------------ Windows

#if defined(_WIN32)

struct PcInput::Private
{
	std::vector<INPUT> queue;
};

QString PcInput::Problem()
{
	return QString();
}

void PcInput::AskPermission()
{
}

PcInput::PcInput(const PcSource::Target &t) : d(std::make_unique<Private>()), target(t)
{
}

PcInput::~PcInput()
{
	ReleaseAll();
}

bool PcInput::Open(std::string *error)
{
	(void)error;
	return true;
}

bool PcInput::GameInFront()
{
	HWND front = GetForegroundWindow();
	if(!front)
		return false;
	if(target.kind == QLatin1String("window"))
	{
		HWND shared = reinterpret_cast<HWND>(static_cast<uintptr_t>(target.handle));
		return front == shared || GetAncestor(front, GA_ROOTOWNER) == GetAncestor(shared, GA_ROOTOWNER);
	}
	// A whole screen: anything but this app's own windows
	DWORD pid = 0;
	GetWindowThreadProcessId(front, &pid);
	return pid != GetCurrentProcessId();
}

void PcInput::Key(uint8_t hid, bool down)
{
	const uint16_t scan = KeyMap::HidToWindows(hid);
	if(!scan)
		return;
	INPUT in = {};
	in.type = INPUT_KEYBOARD;
	in.ki.wScan = static_cast<WORD>(scan & 0xff);
	in.ki.dwFlags = KEYEVENTF_SCANCODE | ((scan & 0xff00) == 0xe000 ? KEYEVENTF_EXTENDEDKEY : 0) | (down ? 0 : KEYEVENTF_KEYUP);
	d->queue.push_back(in);
}

void PcInput::Button(uint8_t button, bool down)
{
	INPUT in = {};
	in.type = INPUT_MOUSE;
	switch(button)
	{
		case kMouseLeft: in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP; break;
		case kMouseRight: in.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP; break;
		case kMouseMiddle: in.mi.dwFlags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP; break;
		case kMouseBack:
		case kMouseForward:
			in.mi.dwFlags = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
			in.mi.mouseData = button == kMouseBack ? XBUTTON1 : XBUTTON2;
			break;
		default: return;
	}
	d->queue.push_back(in);
}

void PcInput::Move(int dx, int dy)
{
	INPUT in = {};
	in.type = INPUT_MOUSE;
	in.mi.dx = dx;
	in.mi.dy = dy;
	in.mi.dwFlags = MOUSEEVENTF_MOVE;
	d->queue.push_back(in);
}

void PcInput::Wheel(int wheel_y, int wheel_x)
{
	if(wheel_y)
	{
		INPUT in = {};
		in.type = INPUT_MOUSE;
		in.mi.dwFlags = MOUSEEVENTF_WHEEL;
		in.mi.mouseData = static_cast<DWORD>(wheel_y);
		d->queue.push_back(in);
	}
	if(wheel_x)
	{
		INPUT in = {};
		in.type = INPUT_MOUSE;
		in.mi.dwFlags = MOUSEEVENTF_HWHEEL;
		in.mi.mouseData = static_cast<DWORD>(wheel_x);
		d->queue.push_back(in);
	}
}

void PcInput::Flush()
{
	if(d->queue.empty())
		return;
	SendInput(static_cast<UINT>(d->queue.size()), d->queue.data(), sizeof(INPUT));
	d->queue.clear();
}

// ------------------------------------------------------------------ Linux

#elif defined(__linux__)

struct PcInput::Private
{
	int fd = -1;
	std::vector<input_event> queue;
	int wheel_rest_y = 0;
	int wheel_rest_x = 0;

	void Add(int type, int code, int value)
	{
		input_event e;
		memset(&e, 0, sizeof(e));
		e.type = static_cast<__u16>(type);
		e.code = static_cast<__u16>(code);
		e.value = value;
		queue.push_back(e);
	}
};

QString PcInput::Problem()
{
	const int fd = open("/dev/uinput", O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if(fd >= 0)
	{
		close(fd);
		return QString();
	}
	return QCoreApplication::translate("PcInput", "Friends' mouse and keyboard need the same as their controllers: the command below, once.");
}

void PcInput::AskPermission()
{
}

PcInput::PcInput(const PcSource::Target &t) : d(std::make_unique<Private>()), target(t)
{
}

PcInput::~PcInput()
{
	ReleaseAll();
	if(d->fd >= 0)
	{
		ioctl(d->fd, UI_DEV_DESTROY);
		close(d->fd);
	}
}

bool PcInput::Open(std::string *error)
{
	d->fd = open("/dev/uinput", O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if(d->fd < 0)
	{
		*error = std::string("/dev/uinput: ") + strerror(errno);
		return false;
	}
	bool ok = ioctl(d->fd, UI_SET_EVBIT, EV_KEY) == 0 && ioctl(d->fd, UI_SET_EVBIT, EV_REL) == 0;
	for(int hid = 0x04; hid <= 0xe7; hid++)
	{
		const uint16_t code = KeyMap::HidToEvdev(static_cast<uint8_t>(hid));
		if(code)
			ok = ok && ioctl(d->fd, UI_SET_KEYBIT, code) == 0;
	}
	for(int button : { BTN_LEFT, BTN_RIGHT, BTN_MIDDLE, BTN_SIDE, BTN_EXTRA })
		ok = ok && ioctl(d->fd, UI_SET_KEYBIT, button) == 0;
	for(int rel : { REL_X, REL_Y, REL_WHEEL, REL_HWHEEL, REL_WHEEL_HI_RES, REL_HWHEEL_HI_RES })
		ok = ok && ioctl(d->fd, UI_SET_RELBIT, rel) == 0;
	uinput_setup setup;
	memset(&setup, 0, sizeof(setup));
	setup.id.bustype = BUS_VIRTUAL;
	setup.id.vendor = 0x1209;
	setup.id.product = 0x5053;
	snprintf(setup.name, sizeof(setup.name), "PSRemotePlayTogether keyboard and mouse");
	ok = ok && ioctl(d->fd, UI_DEV_SETUP, &setup) == 0 && ioctl(d->fd, UI_DEV_CREATE) == 0;
	if(!ok)
	{
		*error = std::string("the keyboard and mouse could not be added: ") + strerror(errno);
		close(d->fd);
		d->fd = -1;
		return false;
	}
	return true;
}

bool PcInput::GameInFront()
{
	// Which window is in front a Wayland program cannot tell; at least never this app's own
	return d->fd >= 0 && QGuiApplication::applicationState() != Qt::ApplicationActive;
}

void PcInput::Key(uint8_t hid, bool down)
{
	const uint16_t code = KeyMap::HidToEvdev(hid);
	if(code)
		d->Add(EV_KEY, code, down ? 1 : 0);
}

void PcInput::Button(uint8_t button, bool down)
{
	int code = 0;
	switch(button)
	{
		case kMouseLeft: code = BTN_LEFT; break;
		case kMouseRight: code = BTN_RIGHT; break;
		case kMouseMiddle: code = BTN_MIDDLE; break;
		case kMouseBack: code = BTN_SIDE; break;
		case kMouseForward: code = BTN_EXTRA; break;
		default: return;
	}
	d->Add(EV_KEY, code, down ? 1 : 0);
}

void PcInput::Move(int dx, int dy)
{
	if(dx)
		d->Add(EV_REL, REL_X, dx);
	if(dy)
		d->Add(EV_REL, REL_Y, dy);
}

void PcInput::Wheel(int wheel_y, int wheel_x)
{
	// The fine steps, and a whole notch for programs that only count those
	if(wheel_y)
	{
		d->Add(EV_REL, REL_WHEEL_HI_RES, wheel_y);
		d->wheel_rest_y += wheel_y;
		if(d->wheel_rest_y / 120)
		{
			d->Add(EV_REL, REL_WHEEL, d->wheel_rest_y / 120);
			d->wheel_rest_y %= 120;
		}
	}
	if(wheel_x)
	{
		d->Add(EV_REL, REL_HWHEEL_HI_RES, wheel_x);
		d->wheel_rest_x += wheel_x;
		if(d->wheel_rest_x / 120)
		{
			d->Add(EV_REL, REL_HWHEEL, d->wheel_rest_x / 120);
			d->wheel_rest_x %= 120;
		}
	}
}

void PcInput::Flush()
{
	if(d->queue.empty() || d->fd < 0)
	{
		d->queue.clear();
		return;
	}
	d->Add(EV_SYN, SYN_REPORT, 0);
	const ssize_t written = write(d->fd, d->queue.data(), d->queue.size() * sizeof(input_event));
	(void)written;
	d->queue.clear();
}

#endif
