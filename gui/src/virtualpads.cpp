// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "virtualpads.h"

#include <QCoreApplication>

#include <algorithm>

#if PSRPT_VIGEM
#include <windows.h>
#include <ViGEm/Client.h>
#endif

#if defined(__linux__)
// Linux: uinput, the kernel's own way to add input devices (docs/pc-source.md)
#define PSRPT_UINPUT 1
#include <linux/uinput.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
#else
#define PSRPT_UINPUT 0
#endif

using namespace coplay;

bool VirtualPads::Built()
{
#if PSRPT_VIGEM || PSRPT_UINPUT
	return true;
#else
	return false;
#endif
}

QString VirtualPads::DriverUrl()
{
#if defined(_WIN32)
	return QStringLiteral("https://github.com/nefarius/ViGEmBus/releases/latest");
#else
	return QString();
#endif
}

QString VirtualPads::FixCommand()
{
#if PSRPT_UINPUT
	// The rule Steam's controller support installs too: the person at the computer may add input devices
	return QStringLiteral("echo 'KERNEL==\"uinput\", SUBSYSTEM==\"misc\", OPTIONS+=\"static_node=uinput\", TAG+=\"uaccess\"' "
		"| sudo tee /etc/udev/rules.d/60-psremoteplaytogether-uinput.rules && sudo modprobe uinput "
		"&& sudo udevadm control --reload-rules && sudo udevadm trigger");
#else
	return QString();
#endif
}

#if PSRPT_UINPUT
/** A controller as the kernel's xpad driver shows an Xbox 360 controller, so every game knows it. */
class UinputPad
{
	public:
		~UinputPad() { Close(); }

		bool Open(int number, std::function<void(uint8_t, uint8_t)> rumble, std::string *error)
		{
			on_rumble = std::move(rumble);
			fd = open("/dev/uinput", O_RDWR | O_NONBLOCK | O_CLOEXEC);
			if(fd < 0)
			{
				*error = std::string("/dev/uinput: ") + strerror(errno);
				return false;
			}
			bool ok = ioctl(fd, UI_SET_EVBIT, EV_KEY) == 0 && ioctl(fd, UI_SET_EVBIT, EV_ABS) == 0
				&& ioctl(fd, UI_SET_EVBIT, EV_FF) == 0 && ioctl(fd, UI_SET_FFBIT, FF_RUMBLE) == 0;
			for(int key : { BTN_A, BTN_B, BTN_X, BTN_Y, BTN_TL, BTN_TR, BTN_SELECT, BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR })
				ok = ok && ioctl(fd, UI_SET_KEYBIT, key) == 0;
			struct { int code, min, max, fuzz, flat; } const axes[] = {
				{ ABS_X, -32768, 32767, 16, 128 }, { ABS_Y, -32768, 32767, 16, 128 },
				{ ABS_RX, -32768, 32767, 16, 128 }, { ABS_RY, -32768, 32767, 16, 128 },
				{ ABS_Z, 0, 255, 0, 0 }, { ABS_RZ, 0, 255, 0, 0 },
				{ ABS_HAT0X, -1, 1, 0, 0 }, { ABS_HAT0Y, -1, 1, 0, 0 },
			};
			for(const auto &a : axes)
			{
				uinput_abs_setup abs;
				memset(&abs, 0, sizeof(abs));
				abs.code = static_cast<__u16>(a.code);
				abs.absinfo.minimum = a.min;
				abs.absinfo.maximum = a.max;
				abs.absinfo.fuzz = a.fuzz;
				abs.absinfo.flat = a.flat;
				ok = ok && ioctl(fd, UI_SET_EVBIT, EV_ABS) == 0 && ioctl(fd, UI_SET_ABSBIT, a.code) == 0
					&& ioctl(fd, UI_ABS_SETUP, &abs) == 0;
			}
			uinput_setup setup;
			memset(&setup, 0, sizeof(setup));
			setup.id.bustype = BUS_USB;
			setup.id.vendor = 0x045e;  // Microsoft
			setup.id.product = 0x028e; // Xbox 360 controller
			setup.id.version = 0x0114;
			// The app's own controller list leaves these out by this name (controllermanager.cpp)
			snprintf(setup.name, sizeof(setup.name), "Xbox 360 Controller (PSRemotePlayTogether %d)", number);
			setup.ff_effects_max = kEffects;
			ok = ok && ioctl(fd, UI_DEV_SETUP, &setup) == 0 && ioctl(fd, UI_DEV_CREATE) == 0;
			if(!ok)
			{
				*error = std::string("the controller could not be added: ") + strerror(errno);
				close(fd);
				fd = -1;
				return false;
			}
			running = true;
			reader = std::thread([this] { Read(); });
			return true;
		}

		void Close()
		{
			running = false;
			if(reader.joinable())
				reader.join();
			if(fd >= 0)
			{
				ioctl(fd, UI_DEV_DESTROY);
				close(fd);
				fd = -1;
			}
		}

		void Set(const coplay::PadState &p)
		{
			if(fd < 0)
				return;
			input_event events[24];
			int n = 0;
			auto add = [&](int type, int code, int value) {
				memset(&events[n], 0, sizeof(events[n]));
				events[n].type = static_cast<__u16>(type);
				events[n].code = static_cast<__u16>(code);
				events[n].value = value;
				n++;
			};
			using namespace coplay;
			// By where the buttons are: Cross is A, Square is X
			const struct { uint32_t from; int to; } map[] = {
				{ kButtonCross, BTN_A }, { kButtonCircle, BTN_B }, { kButtonSquare, BTN_X }, { kButtonTriangle, BTN_Y },
				{ kButtonL1, BTN_TL }, { kButtonR1, BTN_TR }, { kButtonOptions, BTN_START }, { kButtonPs, BTN_MODE },
				{ kButtonL3, BTN_THUMBL }, { kButtonR3, BTN_THUMBR },
			};
			for(const auto &m : map)
				add(EV_KEY, m.to, (p.buttons & m.from) ? 1 : 0);
			// The touchpad's click is the closest a DualSense has to "Back"
			add(EV_KEY, BTN_SELECT, (p.buttons & (kButtonShare | kButtonTouchpad)) ? 1 : 0);
			add(EV_ABS, ABS_X, p.left_x);
			// Up is negative here too, as on a PlayStation controller
			add(EV_ABS, ABS_Y, p.left_y);
			add(EV_ABS, ABS_RX, p.right_x);
			add(EV_ABS, ABS_RY, p.right_y);
			add(EV_ABS, ABS_Z, p.l2);
			add(EV_ABS, ABS_RZ, p.r2);
			add(EV_ABS, ABS_HAT0X, (p.buttons & kButtonDpadRight) ? 1 : (p.buttons & kButtonDpadLeft) ? -1 : 0);
			add(EV_ABS, ABS_HAT0Y, (p.buttons & kButtonDpadDown) ? 1 : (p.buttons & kButtonDpadUp) ? -1 : 0);
			add(EV_SYN, SYN_REPORT, 0);
			const ssize_t written = write(fd, events, sizeof(events[0]) * static_cast<size_t>(n));
			(void)written;
		}

	private:
		static constexpr int kEffects = 16;

		/** The game's rumble: effects it uploads, then plays and stops. */
		void Read()
		{
			struct Effect { uint16_t strong = 0, weak = 0, length = 0; };
			Effect effects[kEffects];
			std::chrono::steady_clock::time_point stop_at;
			bool rumbling = false;
			while(running)
			{
				pollfd pfd = { fd, POLLIN, 0 };
				poll(&pfd, 1, 50);
				if(rumbling && std::chrono::steady_clock::now() >= stop_at)
				{
					rumbling = false;
					if(on_rumble)
						on_rumble(0, 0);
				}
				input_event ev;
				while(read(fd, &ev, sizeof(ev)) == static_cast<ssize_t>(sizeof(ev)))
				{
					if(ev.type == EV_UINPUT && ev.code == UI_FF_UPLOAD)
					{
						uinput_ff_upload up;
						memset(&up, 0, sizeof(up));
						up.request_id = static_cast<__u32>(ev.value);
						if(ioctl(fd, UI_BEGIN_FF_UPLOAD, &up) == 0)
						{
							if(up.effect.id >= 0 && up.effect.id < kEffects && up.effect.type == FF_RUMBLE)
							{
								effects[up.effect.id].strong = up.effect.u.rumble.strong_magnitude;
								effects[up.effect.id].weak = up.effect.u.rumble.weak_magnitude;
								effects[up.effect.id].length = up.effect.replay.length;
							}
							up.retval = 0;
							ioctl(fd, UI_END_FF_UPLOAD, &up);
						}
					}
					else if(ev.type == EV_UINPUT && ev.code == UI_FF_ERASE)
					{
						uinput_ff_erase erase;
						memset(&erase, 0, sizeof(erase));
						erase.request_id = static_cast<__u32>(ev.value);
						if(ioctl(fd, UI_BEGIN_FF_ERASE, &erase) == 0)
						{
							if(erase.effect_id < static_cast<__u32>(kEffects))
								effects[erase.effect_id] = Effect();
							erase.retval = 0;
							ioctl(fd, UI_END_FF_ERASE, &erase);
						}
					}
					else if(ev.type == EV_FF && ev.code < kEffects)
					{
						const Effect &e = effects[ev.code];
						const bool on = ev.value > 0 && (e.strong || e.weak);
						if(on_rumble)
							on_rumble(on ? static_cast<uint8_t>(e.strong >> 8) : 0, on ? static_cast<uint8_t>(e.weak >> 8) : 0);
						rumbling = on && e.length > 0 && e.length < 0xffff;
						if(rumbling)
							stop_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(e.length);
					}
				}
			}
		}

		int fd = -1;
		std::atomic<bool> running{false};
		std::thread reader;
		std::function<void(uint8_t, uint8_t)> on_rumble;
};
#endif

#if PSRPT_VIGEM
/** XInput's buttons from a PlayStation controller's, by where they are on the controller. */
static XUSB_REPORT ToXusb(const PadState &p)
{
	XUSB_REPORT r;
	XUSB_REPORT_INIT(&r);
	struct { uint32_t from; USHORT to; } const map[] = {
		{ kButtonCross, XUSB_GAMEPAD_A },
		{ kButtonCircle, XUSB_GAMEPAD_B },
		{ kButtonSquare, XUSB_GAMEPAD_X },
		{ kButtonTriangle, XUSB_GAMEPAD_Y },
		{ kButtonDpadUp, XUSB_GAMEPAD_DPAD_UP },
		{ kButtonDpadDown, XUSB_GAMEPAD_DPAD_DOWN },
		{ kButtonDpadLeft, XUSB_GAMEPAD_DPAD_LEFT },
		{ kButtonDpadRight, XUSB_GAMEPAD_DPAD_RIGHT },
		{ kButtonL1, XUSB_GAMEPAD_LEFT_SHOULDER },
		{ kButtonR1, XUSB_GAMEPAD_RIGHT_SHOULDER },
		{ kButtonL3, XUSB_GAMEPAD_LEFT_THUMB },
		{ kButtonR3, XUSB_GAMEPAD_RIGHT_THUMB },
		{ kButtonOptions, XUSB_GAMEPAD_START },
		{ kButtonShare, XUSB_GAMEPAD_BACK },
		// The touchpad's click is the closest a DualSense has to "View" on an Xbox controller
		{ kButtonTouchpad, XUSB_GAMEPAD_BACK },
		{ kButtonPs, XUSB_GAMEPAD_GUIDE },
	};
	for(const auto &m : map)
	{
		if(p.buttons & m.from)
			r.wButtons |= m.to;
	}
	r.bLeftTrigger = p.l2;
	r.bRightTrigger = p.r2;
	// Up is negative on a PlayStation controller, positive for XInput
	auto up = [](int16_t v) { return static_cast<SHORT>(std::clamp(-static_cast<int>(v), -32768, 32767)); };
	r.sThumbLX = p.left_x;
	r.sThumbLY = up(p.left_y);
	r.sThumbRX = p.right_x;
	r.sThumbRY = up(p.right_y);
	return r;
}
#endif

struct VirtualPads::Private
{
	mutable std::mutex mutex;
#if PSRPT_UINPUT
	bool open = false;
	std::vector<std::unique_ptr<UinputPad>> pads;
#endif
#if PSRPT_VIGEM
	PVIGEM_CLIENT client = nullptr;
	std::vector<PVIGEM_TARGET> pads;
	VirtualPads *owner = nullptr;

	static void CALLBACK OnNotification(PVIGEM_CLIENT, PVIGEM_TARGET target, UCHAR large, UCHAR small, UCHAR, LPVOID user)
	{
		auto self = static_cast<Private *>(user);
		int index = -1;
		{
			std::lock_guard<std::mutex> lock(self->mutex);
			auto it = std::find(self->pads.begin(), self->pads.end(), target);
			if(it != self->pads.end())
				index = static_cast<int>(it - self->pads.begin());
		}
		if(index >= 0 && self->owner->on_rumble)
			self->owner->on_rumble(index, large, small);
	}
#endif
};

QString VirtualPads::Problem()
{
#if PSRPT_VIGEM
	PVIGEM_CLIENT client = vigem_alloc();
	if(!client)
		return QCoreApplication::translate("VirtualPads", "Out of memory.");
	const VIGEM_ERROR err = vigem_connect(client);
	vigem_free(client);
	if(VIGEM_SUCCESS(err))
		return QString();
	if(err == VIGEM_ERROR_BUS_NOT_FOUND)
		return QCoreApplication::translate("VirtualPads", "Your friends play through game controllers on this computer, which need the free ViGEmBus driver. Install it once (it asks for administrator rights), then share again.");
	if(err == VIGEM_ERROR_BUS_VERSION_MISMATCH)
		return QCoreApplication::translate("VirtualPads", "The ViGEmBus driver on this computer is too old. Install the newest one, then share again.");
	return QCoreApplication::translate("VirtualPads", "The ViGEmBus driver does not answer (error 0x%1). Restarting the computer may help.")
		.arg(static_cast<unsigned>(err), 8, 16, QLatin1Char('0'));
#elif PSRPT_UINPUT
	const int fd = open("/dev/uinput", O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if(fd >= 0)
	{
		close(fd);
		return QString();
	}
	if(errno == ENOENT || errno == ENODEV)
		return QCoreApplication::translate("VirtualPads", "Your friends play through game controllers on this computer, which need the kernel's uinput. Run the command below once (it asks for your password), then share again.");
	return QCoreApplication::translate("VirtualPads", "Your friends play through game controllers on this computer, which this app may not add yet. Run the command below once (it asks for your password; Steam's controller support sets up the same), then share again.");
#elif defined(__APPLE__)
	return QCoreApplication::translate("VirtualPads", "On a Mac your friends watch: macOS lets apps add game controllers only with a permission Apple gives out itself. You play on your own controller or keyboard.");
#else
	return QCoreApplication::translate("VirtualPads", "On this system your friends cannot play a game on this computer yet: they can watch.");
#endif
}

VirtualPads::VirtualPads() : d(std::make_unique<Private>())
{
#if PSRPT_VIGEM
	d->owner = this;
#endif
}

VirtualPads::~VirtualPads()
{
	Close();
}

bool VirtualPads::Open(std::string *error)
{
#if PSRPT_VIGEM
	std::lock_guard<std::mutex> lock(d->mutex);
	if(d->client)
		return true;
	d->client = vigem_alloc();
	if(!d->client)
	{
		*error = "out of memory";
		return false;
	}
	const VIGEM_ERROR err = vigem_connect(d->client);
	if(!VIGEM_SUCCESS(err))
	{
		vigem_free(d->client);
		d->client = nullptr;
		*error = Problem().toStdString();
		return false;
	}
	return true;
#elif PSRPT_UINPUT
	const QString problem = Problem();
	if(!problem.isEmpty())
	{
		*error = problem.toStdString();
		return false;
	}
	std::lock_guard<std::mutex> lock(d->mutex);
	d->open = true;
	return true;
#else
	*error = Problem().toStdString();
	return false;
#endif
}

void VirtualPads::Close()
{
#if PSRPT_VIGEM
	SetCount(0);
	std::lock_guard<std::mutex> lock(d->mutex);
	if(d->client)
	{
		vigem_disconnect(d->client);
		vigem_free(d->client);
		d->client = nullptr;
	}
#elif PSRPT_UINPUT
	SetCount(0);
	std::lock_guard<std::mutex> lock(d->mutex);
	d->open = false;
#endif
}

bool VirtualPads::IsOpen() const
{
#if PSRPT_VIGEM
	std::lock_guard<std::mutex> lock(d->mutex);
	return d->client != nullptr;
#elif PSRPT_UINPUT
	std::lock_guard<std::mutex> lock(d->mutex);
	return d->open;
#else
	return false;
#endif
}

void VirtualPads::SetCount(int count)
{
#if PSRPT_VIGEM
	count = std::clamp(count, 0, kMaxPads);
	std::vector<PVIGEM_TARGET> added, gone;
	PVIGEM_CLIENT client = nullptr;
	{
		std::lock_guard<std::mutex> lock(d->mutex);
		client = d->client;
		if(!client)
			return;
		while(static_cast<int>(d->pads.size()) < count)
		{
			PVIGEM_TARGET pad = vigem_target_x360_alloc();
			if(!pad)
				break;
			if(!VIGEM_SUCCESS(vigem_target_add(client, pad)))
			{
				vigem_target_free(pad);
				break;
			}
			d->pads.push_back(pad);
			added.push_back(pad);
		}
		while(static_cast<int>(d->pads.size()) > count)
		{
			gone.push_back(d->pads.back());
			d->pads.pop_back();
		}
	}
	// Outside the lock: the driver may call back right away, and the callback takes it
	for(PVIGEM_TARGET pad : added)
		vigem_target_x360_register_notification(client, pad, &Private::OnNotification, d.get());
	for(PVIGEM_TARGET pad : gone)
	{
		vigem_target_x360_unregister_notification(pad);
		vigem_target_remove(client, pad);
		vigem_target_free(pad);
	}
#elif PSRPT_UINPUT
	count = std::clamp(count, 0, kMaxPads);
	std::vector<std::unique_ptr<UinputPad>> gone;
	{
		std::lock_guard<std::mutex> lock(d->mutex);
		if(!d->open)
			return;
		while(static_cast<int>(d->pads.size()) < count)
		{
			const int index = static_cast<int>(d->pads.size());
			auto pad = std::make_unique<UinputPad>();
			std::string error;
			// On the pad's own thread; the owner forwards it
			auto rumble = [this, index](uint8_t left, uint8_t right) {
				if(on_rumble)
					on_rumble(index, left, right);
			};
			if(!pad->Open(index + 1, rumble, &error))
				break;
			d->pads.push_back(std::move(pad));
		}
		while(static_cast<int>(d->pads.size()) > count)
		{
			gone.push_back(std::move(d->pads.back()));
			d->pads.pop_back();
		}
	}
	// Outside the lock: their threads end first
	gone.clear();
#else
	(void)count;
#endif
}

int VirtualPads::Count() const
{
#if PSRPT_VIGEM || PSRPT_UINPUT
	std::lock_guard<std::mutex> lock(d->mutex);
	return static_cast<int>(d->pads.size());
#else
	return 0;
#endif
}

void VirtualPads::Set(int index, const PadState &state)
{
#if PSRPT_VIGEM
	std::lock_guard<std::mutex> lock(d->mutex);
	if(!d->client || index < 0 || index >= static_cast<int>(d->pads.size()))
		return;
	vigem_target_x360_update(d->client, d->pads[static_cast<size_t>(index)], ToXusb(state));
#elif PSRPT_UINPUT
	std::lock_guard<std::mutex> lock(d->mutex);
	if(index < 0 || index >= static_cast<int>(d->pads.size()))
		return;
	d->pads[static_cast<size_t>(index)]->Set(state);
#else
	(void)index;
	(void)state;
#endif
}
