// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "virtualpads.h"

#include <QCoreApplication>

#include <algorithm>

#if PSRPT_VIGEM
#include <windows.h>
#include <ViGEm/Client.h>
#endif

using namespace coplay;

bool VirtualPads::Built()
{
#if PSRPT_VIGEM
	return true;
#else
	return false;
#endif
}

QString VirtualPads::DriverUrl()
{
	return QStringLiteral("https://github.com/nefarius/ViGEmBus/releases/latest");
}

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
#endif
}

bool VirtualPads::IsOpen() const
{
#if PSRPT_VIGEM
	std::lock_guard<std::mutex> lock(d->mutex);
	return d->client != nullptr;
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
#else
	(void)count;
#endif
}

int VirtualPads::Count() const
{
#if PSRPT_VIGEM
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
#else
	(void)index;
	(void)state;
#endif
}
