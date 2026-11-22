// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// This computer as the game source (docs/pc-source.md). Instead of a console session the
// session captures a window or a screen of this computer (PcCapture) and passes the picture
// and the sound on to the guests the way it passes a console's; the guests' controllers go to
// virtual controllers on this computer (VirtualPads). The host plays on their own controller,
// which the game sees directly.

#include "streamsession.h"
#include "pcsource.h"
#include "virtualpads.h"

#include <coplay/host.h>

#include <QTimer>

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

	PcCapture::Config config;
	config.target = target;
	config.width = pc_width;
	config.height = pc_height;
	config.fps = pc_fps;
	config.video_kbps = pc_video_kbps;
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
	// Each their own: players 2, 3 and 4 (the host is 1), the guest in place n is player n + 1.
	// Otherwise the one controller the guests share. They are plugged in that order, so that the
	// game numbers them the same way.
	const int count = each ? VirtualPads::kMaxPads - 1 : 1;
	pc_pads->SetCount(count);
	if(host && each)
	{
		// Their controller's lights say which player they are (0 is player 1)
		for(int slot = 1; slot <= count; slot++)
			host->SetGuestPlayerIndex(static_cast<uint8_t>(slot), static_cast<uint8_t>(slot));
	}
	PcUpdatePads();
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
		for(int i = 0; i < pc_pads->Count(); i++)
			pc_pads->Set(i, host->GuestInput(static_cast<uint8_t>(i + 1)));
		return;
	}
	// Together, one at a time, or only the host: what the guests play, the host's own
	// controller is already in the game
	pc_pads->Set(0, host->GuestsInput());
}

void StreamSession::PcOnRumble(int index, uint8_t left, uint8_t right)
{
	// On the driver's thread; the host is safe to call from any
	coplay::Host *host = CoPlayHost();
	if(!host || !IsCoPlayHosting())
		return;
	if(host->GetMixMode() == coplay::MixMode::EachOwn)
		host->SetGuestRumble(static_cast<uint8_t>(index + 1), left, right);
	else if(index == 0)
		host->SetRumble(left, right);
}
