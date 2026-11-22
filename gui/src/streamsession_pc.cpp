// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// This computer as the game source (docs/pc-source.md). Instead of a console session the
// session captures a window or a screen of this computer (PcCapture) and passes the picture
// and the sound on to the guests the way it passes a console's; the guests' controllers go to
// virtual controllers on this computer (VirtualPads). The host plays on their own controller,
// which the game sees directly.

#include "streamsession.h"
#include "pcsource.h"
#include "pcinput.h"
#include "virtualpads.h"

#include <coplay/host.h>
#include <chiaki/time.h>

#include <QTimer>

#include <algorithm>

#ifndef _WIN32
#include <unistd.h>
#endif

/** How often the game in the shared window or on the shared screen is looked for again. */
static const int kGameCheckMs = 15000;

bool StreamSession::HasConsole() const
{
	return !IsCoPlayGuest() && !pc_source;
}

QString StreamSession::GetPcSourceName() const
{
	return pc_target.value(QStringLiteral("title")).toString();
}

void StreamSession::PcStart()
{
	const PcSource::Target chosen = PcSource::Target::FromMap(pc_target);
	const PcSource::Target target = PcSource::Find(chosen);
	if(!target.Valid())
	{
		PcFinish(CHIAKI_QUIT_REASON_STREAM_CONNECTION_UNKNOWN,
			tr("\"%1\" is not open any more. Choose what to share again on the Host page.").arg(chosen.title));
		return;
	}
	pc_target = target.ToMap();

	// The guests' controllers on this computer. Without the driver they can still watch.
	pc_pads = std::make_unique<VirtualPads>();
	std::string pads_error;
	if(pc_pads->Open(&pads_error))
	{
		pc_pads_problem.clear();
		pc_pads->on_rumble = [this](int index, uint8_t left, uint8_t right) { PcOnRumble(index, left, right); };
	}
	else
	{
		pc_pads_problem = QString::fromStdString(pads_error);
		pc_pads.reset();
		CHIAKI_LOGW(GetChiakiLog(), "This computer: no controllers for the guests: %s", pads_error.c_str());
	}

	// Open for guests before the first picture, so that it already reaches them
	ChiakiEvent connected = {};
	connected.type = CHIAKI_EVENT_CONNECTED;
	Event(&connected);
	PcPlugPads();

	// Friends' mouse and keyboard, where the host lets them (Host page)
	if(coplay::Host *host = IsCoPlayHosting() ? CoPlayHost() : nullptr)
		host->SetKeyMouseForNewcomers(pc_key_mouse);
	if(pc_key_mouse)
	{
		pc_input = std::make_unique<PcInput>(target);
		std::string input_error;
		if(!pc_input->Open(&input_error))
		{
			CHIAKI_LOGW(GetChiakiLog(), "This computer: no mouse and keyboard for the guests: %s", input_error.c_str());
			pc_input.reset();
		}
	}

	PcCapture::Config config;
	config.target = target;
	config.width = pc_width;
	config.height = pc_height;
	config.fps = pc_fps;
	config.video_kbps = pc_video_kbps;
	config.portal_fd = pc_portal_fd;
	config.portal_node = pc_portal_node;
	PcCapture::Callbacks callbacks;
	// On the capture's threads
	callbacks.on_video = [this](const uint8_t *data, size_t size) {
		// To the guests, and to this computer's own picture of what they see
		CoPlayVideoSampleCb(const_cast<uint8_t *>(data), size, 0, false, this);
	};
	callbacks.on_audio = [this](const uint8_t *data, size_t size) {
		if(coplay::Host *host = IsCoPlayHosting() ? CoPlayHost() : nullptr)
			host->PushAudio(data, size);
	};
	callbacks.on_error = [this](const std::string &why) {
		const QString text = QString::fromStdString(why);
		QMetaObject::invokeMethod(this, [this, text]() {
			PcFinish(CHIAKI_QUIT_REASON_STREAM_CONNECTION_UNKNOWN, tr("Sharing this computer stopped: %1").arg(text));
		}, Qt::QueuedConnection);
	};
	ChiakiLog *session_log = GetChiakiLog();
	callbacks.on_log = [session_log](const std::string &line) {
		CHIAKI_LOGI(session_log, "This computer: %s", line.c_str());
	};
	pc_capture = std::make_unique<PcCapture>(config, callbacks);
	std::string error;
	if(!pc_capture->Start(&error))
	{
		pc_capture.reset();
		PcFinish(CHIAKI_QUIT_REASON_STREAM_CONNECTION_UNKNOWN,
			tr("The picture of this computer cannot be shared: %1").arg(QString::fromStdString(error)));
		return;
	}

	// Which game runs, for the room and the others: now, and again now and then
	auto check_game = [this]() {
		if(!pc_capture)
			return;
		const PcSource::Game game = PcSource::DetectGame(PcSource::Target::FromMap(pc_target));
		if(game.title != pc_game)
		{
			pc_game = game.title;
			if(!pc_game.isEmpty())
				CHIAKI_LOGI(GetChiakiLog(), "This computer: playing %s", qPrintable(pc_game));
			SetCoPlayRoomGame(pc_game, QString());
			emit PcGameChanged(pc_game);
		}
	};
	check_game();
	auto game_timer = new QTimer(this);
	game_timer->setInterval(kGameCheckMs);
	connect(game_timer, &QTimer::timeout, this, check_game);
	game_timer->start();
}

void StreamSession::PcStop()
{
	// Nothing a guest held stays held
	pc_input.reset();
	pc_key_mouse_guests.clear();
	pc_mouse_owner = 0;
	if(pc_capture)
	{
		pc_capture->Stop();
		pc_capture.reset();
	}
	if(pc_pads)
	{
		pc_pads->Close();
		pc_pads.reset();
	}
	// Linux: the system's screen sharing ends with the session
#ifndef _WIN32
	if(pc_portal_fd >= 0)
	{
		close(pc_portal_fd);
		pc_portal_fd = -1;
	}
#endif
	pc_portal.reset();
}

void StreamSession::PcFinish(ChiakiQuitReason reason, const QString &why)
{
	if(pc_finished)
		return;
	pc_finished = true;
	if(reason == CHIAKI_QUIT_REASON_STOPPED)
		stop_requested = true;
	PcStop();
	// Ends like a console session: the guests are told, the app closes the stream
	const QByteArray text = why.toUtf8();
	ChiakiEvent quit = {};
	quit.type = CHIAKI_EVENT_QUIT;
	quit.quit.reason = reason;
	quit.quit.reason_str = text.isEmpty() ? nullptr : text.constData();
	Event(&quit);
}

void StreamSession::PcPlugPads()
{
	if(!pc_source || !pc_pads)
		return;
	coplay::Host *host = CoPlayHost();
	const bool each = host && host->GetMixMode() == coplay::MixMode::EachOwn;
	// Each their own: players 2, 3 and 4 (the host is 1), given out as guests come (PcAssignPlayers).
	// Otherwise the one controller the guests share. They are plugged in that order, so that the
	// game numbers them the same way.
	const int count = each ? VirtualPads::kMaxPads - 1 : 1;
	pc_pads->SetCount(count);
	PcAssignPlayers();
	PcUpdatePads();
}

void StreamSession::PcAssignPlayers()
{
	coplay::Host *host = pc_source && IsCoPlayHosting() ? CoPlayHost() : nullptr;
	if(!host || host->GetMixMode() != coplay::MixMode::EachOwn)
		return;
	const int pads = VirtualPads::kMaxPads - 1;
	std::vector<uint8_t> places;
	for(const coplay::GuestInfo &g : host->Guests())
		places.push_back(g.slot);
	std::map<uint8_t, int> players;
	{
		std::lock_guard<std::mutex> lock(pc_players_mutex);
		for(auto it = pc_pad_of.begin(); it != pc_pad_of.end();)
		{
			if(std::find(places.begin(), places.end(), it->first) == places.end())
				it = pc_pad_of.erase(it);
			else
				++it;
		}
		// Newcomers, in the order they came: the lowest player nobody is
		for(uint8_t slot : places)
		{
			if(pc_pad_of.count(slot))
				continue;
			int free_pad = -1;
			for(int pad = 0; pad < pads && free_pad < 0; pad++)
			{
				bool taken = false;
				for(const auto &e : pc_pad_of)
					taken = taken || e.second == pad;
				if(!taken)
					free_pad = pad;
			}
			pc_pad_of[slot] = free_pad;
		}
		players = pc_pad_of;
	}
	// Their controller's lights and everybody's list say which player they are (0 is player 1)
	for(const auto &e : players)
		host->SetGuestPlayerIndex(e.first, e.second >= 0 ? static_cast<uint8_t>(e.second + 1) : coplay::Host::kNoPlayer);
}

int StreamSession::PcPlayerOf(uint8_t slot)
{
	coplay::Host *host = pc_source ? CoPlayHost() : nullptr;
	if(!host || host->GetMixMode() != coplay::MixMode::EachOwn)
		return 0;
	if(slot == 0)
		return 1;
	std::lock_guard<std::mutex> lock(pc_players_mutex);
	auto it = pc_pad_of.find(slot);
	return it != pc_pad_of.end() && it->second >= 0 ? it->second + 2 : 0;
}

void StreamSession::coplaySetPlayerNumber(int slot, int number)
{
	const int pads = VirtualPads::kMaxPads - 1;
	if(!pc_source || slot < 1 || slot > 255 || number < 2 || number > pads + 1)
		return;
	const int pad = number - 2;
	{
		std::lock_guard<std::mutex> lock(pc_players_mutex);
		auto mine = pc_pad_of.find(static_cast<uint8_t>(slot));
		if(mine == pc_pad_of.end())
			return;
		const int old = mine->second;
		for(auto &e : pc_pad_of)
		{
			if(e.first != slot && e.second == pad)
				e.second = old;
		}
		mine->second = pad;
	}
	PcAssignPlayers();
	PcUpdatePads();
	CoPlayUpdatePlayers();
}

void StreamSession::PcUpdatePads()
{
	if(!pc_pads)
		return;
	coplay::Host *host = IsCoPlayHosting() ? CoPlayHost() : nullptr;
	if(!host)
	{
		for(int i = 0; i < pc_pads->Count(); i++)
			pc_pads->Set(i, coplay::PadState());
		return;
	}
	if(host->GetMixMode() == coplay::MixMode::EachOwn)
	{
		std::map<uint8_t, int> players;
		{
			std::lock_guard<std::mutex> lock(pc_players_mutex);
			players = pc_pad_of;
		}
		for(int i = 0; i < pc_pads->Count(); i++)
		{
			coplay::PadState state;
			for(const auto &e : players)
			{
				if(e.second == i)
					state = host->GuestInput(e.first);
			}
			pc_pads->Set(i, state);
		}
		return;
	}
	// Together, one at a time, or only the host: what the guests play, the host's own
	// controller is already in the game
	pc_pads->Set(0, host->GuestsInput());
}

void StreamSession::coplaySetKeyMouse(int slot, bool allowed)
{
	coplay::Host *host = pc_source && IsCoPlayHosting() ? CoPlayHost() : nullptr;
	if(!host || slot < 1 || slot > 255)
		return;
	host->SetGuestKeyMouse(static_cast<uint8_t>(slot), allowed);
	CoPlayUpdatePlayers();
}

void StreamSession::PcKeyMouse(const coplay::KeyMouseUpdate &u)
{
	if(!pc_input)
		return;
	const uint64_t now = chiaki_time_now_monotonic_ms();
	int dx = 0, dy = 0, wheel_y = 0, wheel_x = 0;
	if(u.gone)
	{
		pc_key_mouse_guests.erase(u.slot);
		if(pc_mouse_owner == u.slot)
			pc_mouse_owner = 0;
	}
	else
	{
		PcKeyMouseGuest &g = pc_key_mouse_guests[u.slot];
		const bool uses_mouse = u.dx || u.dy || u.wheel_y || u.wheel_x || u.state.buttons != g.buttons;
		g.keys = u.state.keys;
		g.buttons = u.state.buttons;
		if(uses_mouse)
		{
			// One mouse: whoever has it keeps it while they use it; somebody else gets it once it rests
			auto owner = pc_key_mouse_guests.find(pc_mouse_owner);
			const bool free = owner == pc_key_mouse_guests.end()
				|| (owner->second.buttons == 0 && now - pc_mouse_owner_ms > 500);
			if(pc_mouse_owner == u.slot || free)
			{
				pc_mouse_owner = u.slot;
				pc_mouse_owner_ms = now;
				dx = u.dx;
				dy = u.dy;
				wheel_y = u.wheel_y;
				wheel_x = u.wheel_x;
			}
		}
	}
	// Keys from everybody together, the buttons of whoever has the mouse
	std::vector<uint8_t> keys;
	for(const auto &e : pc_key_mouse_guests)
		keys.insert(keys.end(), e.second.keys.begin(), e.second.keys.end());
	auto owner = pc_key_mouse_guests.find(pc_mouse_owner);
	const uint8_t buttons = owner != pc_key_mouse_guests.end() ? owner->second.buttons : 0;
	pc_input->Apply(dx, dy, wheel_y, wheel_x, buttons, keys);
}

void StreamSession::PcOnRumble(int index, uint8_t left, uint8_t right)
{
	// On the driver's thread; the host is safe to call from any
	coplay::Host *host = CoPlayHost();
	if(!host || !IsCoPlayHosting())
		return;
	if(host->GetMixMode() == coplay::MixMode::EachOwn)
	{
		// To whoever drives that controller
		std::lock_guard<std::mutex> lock(pc_players_mutex);
		for(const auto &e : pc_pad_of)
		{
			if(e.second == index)
				host->SetGuestRumble(e.first, left, right);
		}
	}
	else if(index == 0)
		host->SetRumble(left, right);
}
