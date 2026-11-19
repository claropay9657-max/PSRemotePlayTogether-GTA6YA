// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Play Together: the parts of StreamSession that share a console session with
// other people (host) or take part in somebody else's (guest).
//
// Host:  everything the console sends is passed on to the guests untouched, and
//        the guests' controllers are merged with ours before the state goes to
//        the console.
// Guest: there is no console connection. Video, audio, haptics and controller
//        feedback come from the host and are fed into the same decoders and
//        controller code a console session uses.

#include <streamsession.h>

#include <chiaki/video.h>

#include <coplay/client.h>
#include <coplay/host.h>
#include <coplay/invite.h>

#include <QClipboard>
#include <QGuiApplication>
#include <QJsonArray>
#include <QMutex>
#include <QMutexLocker>
#include <QVariantMap>

#include <atomic>
#include <cstring>

struct CoPlayState
{
	// host
	std::unique_ptr<coplay::Host> host;
	std::atomic<bool> hosting{false};
	QString host_name;

	// guest
	std::shared_ptr<coplay::Client> client;
	std::atomic<bool> guest_quit_sent{false};
	std::atomic<bool> audio_ready{false};
	coplay::Feedback applied_feedback;
	bool applied_any = false;
	coplay::AudioHeader applied_audio;
	std::vector<uint8_t> video_buf;
	uint64_t bitrate_window_start_ms = 0;
	uint64_t bitrate_window_bytes = 0;

	std::vector<coplay::Participant> participants; // receiving thread only

	// both
	ChiakiAudioSink opus_sink = {};
	QMutex mutex; // everything below
	QVariantList players;
	int mix_mode = 0;
	QString invite;
	coplay::PadState local_pad;       // our own controllers, before merging
	coplay::InputsSnapshot guest_inputs;
	bool have_guest_inputs = false;
	QHash<QString, QString> avatar_urls; // picture id -> data: URL for the QML side
	std::atomic<bool> video_enabled{true};

	// ourselves
	QString own_name;
	std::vector<uint8_t> own_avatar;
	coplay::BlobRef own_avatar_ref;
	std::atomic<int> own_controller{0};
};

static QString AvatarId(const coplay::BlobRef &ref)
{
	if(!ref.Valid())
		return QString();
	return QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(ref.hash.data()), static_cast<int>(ref.hash.size())).toHex());
}

static bool AvatarHash(const QString &id, coplay::BlobHash &hash)
{
	QByteArray raw = QByteArray::fromHex(id.toLatin1());
	if(raw.size() != static_cast<int>(hash.size()) || id.size() != static_cast<int>(hash.size()) * 2)
		return false;
	memcpy(hash.data(), raw.constData(), hash.size());
	return true;
}

static const char *ImageMime(const uint8_t *data, size_t size)
{
	if(size >= 8 && memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0)
		return "image/png";
	if(size >= 3 && data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff)
		return "image/jpeg";
	if(size >= 12 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WEBP", 4) == 0)
		return "image/webp";
	return nullptr;
}

/** Picture as a data: URL the QML side can show; empty if it is not an image we know. */
static QString AvatarUrl(const coplay::BlobData &data)
{
	if(!data || data->empty())
		return QString();
	const char *mime = ImageMime(data->data(), data->size());
	if(!mime)
		return QString();
	QByteArray raw(reinterpret_cast<const char *>(data->data()), static_cast<int>(data->size()));
	return QStringLiteral("data:%1;base64,%2").arg(QLatin1String(mime), QString::fromLatin1(raw.toBase64()));
}

static QJsonObject PadJson(uint32_t buttons, int l2, int r2, int lx, int ly, int rx, int ry)
{
	QJsonObject o;
	o["b"] = static_cast<qint64>(buttons);
	o["l2"] = l2;
	o["r2"] = r2;
	o["lx"] = lx;
	o["ly"] = ly;
	o["rx"] = rx;
	o["ry"] = ry;
	return o;
}

static QJsonObject PadJson(const coplay::PlayerInput &p)
{
	return PadJson(p.buttons, p.l2, p.r2, p.left_x, p.left_y, p.right_x, p.right_y);
}

static QJsonObject PadJson(const coplay::PadState &p)
{
	return PadJson(p.buttons, p.l2, p.r2, p.left_x, p.left_y, p.right_x, p.right_y);
}

static coplay::PadState ToPad(const ChiakiControllerState &s)
{
	coplay::PadState p;
	p.buttons = s.buttons;
	p.l2 = s.l2_state;
	p.r2 = s.r2_state;
	p.left_x = s.left_x;
	p.left_y = s.left_y;
	p.right_x = s.right_x;
	p.right_y = s.right_y;
	p.touch_id_next = s.touch_id_next;
	for(size_t i = 0; i < CHIAKI_CONTROLLER_TOUCHES_MAX; i++)
	{
		p.touches[i].x = s.touches[i].x;
		p.touches[i].y = s.touches[i].y;
		p.touches[i].id = s.touches[i].id;
	}
	p.gyro[0] = s.gyro_x;
	p.gyro[1] = s.gyro_y;
	p.gyro[2] = s.gyro_z;
	p.accel[0] = s.accel_x;
	p.accel[1] = s.accel_y;
	p.accel[2] = s.accel_z;
	p.orient[0] = s.orient_x;
	p.orient[1] = s.orient_y;
	p.orient[2] = s.orient_z;
	p.orient[3] = s.orient_w;
	return p;
}

static void FromPad(const coplay::PadState &p, ChiakiControllerState *s)
{
	s->buttons = p.buttons;
	s->l2_state = p.l2;
	s->r2_state = p.r2;
	s->left_x = p.left_x;
	s->left_y = p.left_y;
	s->right_x = p.right_x;
	s->right_y = p.right_y;
	s->touch_id_next = p.touch_id_next;
	for(size_t i = 0; i < CHIAKI_CONTROLLER_TOUCHES_MAX; i++)
	{
		s->touches[i].x = p.touches[i].x;
		s->touches[i].y = p.touches[i].y;
		s->touches[i].id = p.touches[i].id;
	}
	s->gyro_x = p.gyro[0];
	s->gyro_y = p.gyro[1];
	s->gyro_z = p.gyro[2];
	s->accel_x = p.accel[0];
	s->accel_y = p.accel[1];
	s->accel_z = p.accel[2];
	s->orient_x = p.orient[0];
	s->orient_y = p.orient[1];
	s->orient_z = p.orient[2];
	s->orient_w = p.orient[3];
}

void StreamSession::CoPlayInit(const StreamSessionConnectInfo &connect_info)
{
	coplay = new CoPlayState;
	chiaki_opus_decoder_get_sink(&opus_decoder, &coplay->opus_sink);
	coplay->mix_mode = connect_info.coplay_mix_mode;
	coplay->own_name = connect_info.coplay_name.trimmed();
	if(!connect_info.coplay_avatar.isEmpty() && connect_info.coplay_avatar.size() <= static_cast<int>(coplay::kMaxAvatarSize))
	{
		coplay->own_avatar.assign(connect_info.coplay_avatar.constBegin(), connect_info.coplay_avatar.constEnd());
		coplay->own_avatar_ref = coplay::MakeBlobRef(coplay->own_avatar);
	}

	coplay->client = connect_info.coplay_client;
	if(coplay->client)
	{
		coplay->video_enabled = (coplay->client->Want() & coplay::kWantVideo) != 0;
		return;
	}
	if(!connect_info.coplay_host_enabled)
		return;

	coplay::HostConfig config;
	if(!coplay::SecretFromString(connect_info.coplay_secret.toStdString(), config.secret))
	{
		CHIAKI_LOGE(GetChiakiLog(), "Play Together: no valid session key in the settings, not sharing");
		return;
	}
	config.port = static_cast<uint16_t>(connect_info.coplay_port > 0 && connect_info.coplay_port < 65536
		? connect_info.coplay_port : coplay::kDefaultPort);
	coplay->host_name = connect_info.coplay_name.trimmed().isEmpty() ? tr("Host") : connect_info.coplay_name.trimmed();
	config.name = coplay->host_name.toStdString();
	config.avatar = coplay->own_avatar;

	coplay::HostCallbacks callbacks;
	// These arrive on the sharing thread; the work happens on ours
	callbacks.on_input_changed = [this] {
		QMetaObject::invokeMethod(this, [this] { SendFeedbackState(); }, Qt::QueuedConnection);
	};
	callbacks.on_keyframe_needed = [this] {
		QMetaObject::invokeMethod(this, [this] { RequestIDR(); }, Qt::QueuedConnection);
	};
	callbacks.on_guests_changed = [this] {
		QMetaObject::invokeMethod(this, [this] { CoPlayUpdatePlayers(); }, Qt::QueuedConnection);
	};
	ChiakiLog *session_log = GetChiakiLog();
	callbacks.on_log = [session_log](const std::string &line) {
		CHIAKI_LOGI(session_log, "Play Together: %s", line.c_str());
	};
	coplay->host = std::make_unique<coplay::Host>(config, callbacks);

	if(connect_info.coplay_mix_mode >= 0 && connect_info.coplay_mix_mode <= static_cast<int>(coplay::MixMode::HostOnly))
		coplay->host->SetMixMode(static_cast<coplay::MixMode>(connect_info.coplay_mix_mode));

	bool ps5 = chiaki_target_is_ps5(connect_info.target);
	coplay::StreamInfo info;
	info.codec = ps5 ? static_cast<coplay::VideoCodec>(connect_info.video_profile.codec) : coplay::VideoCodec::H264;
	info.width = static_cast<uint16_t>(connect_info.video_profile.width);
	info.height = static_cast<uint16_t>(connect_info.video_profile.height);
	info.fps = static_cast<uint8_t>(connect_info.video_profile.max_fps);
	info.ps5 = ps5;
	info.dualsense = connect_info.enable_dualsense;
	coplay->host->SetStreamInfo(info);

	coplay::Invite invite;
	QString address = connect_info.coplay_address.trimmed();
	invite.host = address.isEmpty() ? coplay::GuessLocalAddress() : address.toStdString();
	if(invite.host.empty())
		invite.host = "127.0.0.1";
	invite.port = config.port;
	invite.secret = config.secret;
	coplay->invite = QString::fromStdString(coplay::EncodeInvite(invite));
}

void StreamSession::CoPlayStopThreads()
{
	if(!coplay)
		return;
	// Both calls return only after the respective thread has ended, so that none of
	// its callbacks runs while the rest of the session is taken apart
	coplay->hosting = false;
	if(coplay->host)
		coplay->host->Stop();
	if(coplay->client)
		coplay->client->Stop();
}

void StreamSession::CoPlayFini()
{
	CoPlayStopThreads();
	delete coplay;
	coplay = nullptr;
}

bool StreamSession::IsCoPlayGuest() const
{
	return coplay && coplay->client;
}

bool StreamSession::IsCoPlayHosting() const
{
	return coplay && coplay->host && coplay->hosting;
}

QString StreamSession::GetCoPlayInvite() const
{
	if(!IsCoPlayHosting())
		return QString();
	QMutexLocker locker(&coplay->mutex);
	return coplay->invite;
}

QVariantList StreamSession::GetCoPlayPlayers() const
{
	if(!coplay)
		return {};
	QMutexLocker locker(&coplay->mutex);
	return coplay->players;
}

int StreamSession::GetCoPlayMixMode() const
{
	if(!coplay)
		return 0;
	QMutexLocker locker(&coplay->mutex);
	return coplay->mix_mode;
}

void StreamSession::SetCoPlayMixMode(int mode)
{
	if(!coplay || !coplay->host || mode < 0 || mode > static_cast<int>(coplay::MixMode::HostOnly))
		return;
	{
		QMutexLocker locker(&coplay->mutex);
		coplay->mix_mode = mode;
	}
	coplay->host->SetMixMode(static_cast<coplay::MixMode>(mode));
	CoPlayUpdatePlayers();
	// The combined state may have changed with the mode
	SendFeedbackState();
}

bool StreamSession::GetCoPlayVideoEnabled() const
{
	return !coplay || coplay->video_enabled;
}

void StreamSession::SetCoPlayVideoEnabled(bool enabled)
{
	if(!IsCoPlayGuest() || coplay->video_enabled == enabled)
		return;
	coplay->video_enabled = enabled;
	// Controller only: no picture and no sound, but still rumble, haptics and triggers
	uint8_t inputs = coplay->client->Want() & coplay::kWantInputs;
	coplay->client->SetWant(static_cast<uint8_t>((enabled ? coplay::kWantAll : coplay::kWantHaptics) | inputs));
	emit CoPlayChanged();
}

void StreamSession::coplayKick(int slot)
{
	if(!coplay || !coplay->host || slot < 1 || slot > 255)
		return;
	coplay->host->KickGuest(static_cast<uint8_t>(slot));
}

void StreamSession::coplaySetCanControl(int slot, bool allowed)
{
	if(!coplay || !coplay->host || slot < 1 || slot > 255)
		return;
	coplay->host->SetGuestCanControl(static_cast<uint8_t>(slot), allowed);
}

void StreamSession::coplayCopyInvite()
{
	QString invite = GetCoPlayInvite();
	if(!invite.isEmpty())
		QGuiApplication::clipboard()->setText(invite);
}

void StreamSession::SendControllerState(ChiakiControllerState *state)
{
	if(coplay)
	{
		QMutexLocker locker(&coplay->mutex);
		coplay->local_pad = ToPad(*state);
	}
	if(IsCoPlayGuest())
	{
		coplay->client->SetInput(ToPad(*state));
		return;
	}
	if(IsCoPlayHosting())
	{
		coplay->host->SetLocalInput(ToPad(*state));
		FromPad(coplay->host->MixedInput(), state);
	}
	chiaki_session_set_controller_state(&session, state);
}

// ------------------------------------------------------------------ host

void StreamSession::CoPlayUpdatePlayers()
{
	if(!coplay || !coplay->host)
		return;
	QVariantList players;
	if(coplay->hosting)
	{
		QVariantMap me;
		me["slot"] = 0;
		me["name"] = coplay->host_name;
		me["host"] = true;
		me["you"] = true;
		me["canControl"] = true;
		me["inControl"] = coplay->host->HostInControl();
		me["video"] = true;
		me["controller"] = coplay->own_controller.load();
		me["avatar"] = AvatarId(coplay->own_avatar_ref);
		me["avatarUrl"] = AvatarUrl(coplay->host->Avatar(coplay->own_avatar_ref.hash));
		players.append(me);
		for(const coplay::GuestInfo &g : coplay->host->Guests())
		{
			QVariantMap p;
			p["slot"] = g.slot;
			p["name"] = QString::fromStdString(g.name);
			p["host"] = false;
			p["you"] = false;
			p["canControl"] = g.can_control;
			p["inControl"] = g.in_control;
			p["video"] = (g.want & coplay::kWantVideo) != 0;
			p["address"] = QString::fromStdString(g.address);
			p["rttMs"] = static_cast<int>(g.rtt_ms);
			p["controller"] = static_cast<int>(g.controller);
			p["avatar"] = AvatarId(g.avatar);
			p["avatarUrl"] = g.avatar.Valid() ? AvatarUrl(coplay->host->Avatar(g.avatar.hash)) : QString();
			players.append(p);
		}
	}
	{
		QMutexLocker locker(&coplay->mutex);
		coplay->players = players;
		coplay->mix_mode = static_cast<int>(coplay->host->GetMixMode());
	}
	emit CoPlayChanged();
}

void StreamSession::CoPlayOnEvent(ChiakiEvent *event)
{
	if(!coplay || !coplay->host)
		return;
	coplay::Host *host = coplay->host.get();
	switch(event->type)
	{
		case CHIAKI_EVENT_CONNECTED: {
			std::string error;
			if(host->Start(&error))
			{
				coplay->hosting = true;
				CHIAKI_LOGI(GetChiakiLog(), "Play Together: sharing this session on UDP port %u", host->Port());
			}
			else
			{
				CHIAKI_LOGE(GetChiakiLog(), "Play Together: cannot share this session: %s", error.c_str());
				emit CoPlayNotice(tr("This session is not shared with friends: %1").arg(QString::fromStdString(error)));
			}
			QMetaObject::invokeMethod(this, [this] { CoPlayUpdatePlayers(); }, Qt::QueuedConnection);
			break;
		}
		case CHIAKI_EVENT_QUIT:
			coplay->hosting = false;
			host->Stop();
			QMetaObject::invokeMethod(this, [this] { CoPlayUpdatePlayers(); }, Qt::QueuedConnection);
			break;
		case CHIAKI_EVENT_RUMBLE:
			host->SetRumble(event->rumble.left, event->rumble.right);
			break;
		case CHIAKI_EVENT_TRIGGER_EFFECTS:
			host->SetTriggerEffects(event->trigger_effects.type_left, event->trigger_effects.type_right,
				event->trigger_effects.left, event->trigger_effects.right);
			break;
		case CHIAKI_EVENT_LED_COLOR:
			host->SetLed(event->led_state);
			break;
		case CHIAKI_EVENT_PLAYER_INDEX:
			host->SetPlayerIndex(event->player_index);
			break;
		case CHIAKI_EVENT_HAPTIC_INTENSITY:
			host->SetHapticIntensity(static_cast<uint8_t>(event->intensity));
			break;
		case CHIAKI_EVENT_TRIGGER_INTENSITY:
			host->SetTriggerIntensity(static_cast<uint8_t>(event->intensity));
			break;
		case CHIAKI_EVENT_MOTION_RESET:
			host->NotifyMotionReset();
			break;
		default:
			break;
	}
}

bool StreamSession::CoPlayVideoSampleCb(uint8_t *buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void *user)
{
	auto self = static_cast<StreamSession *>(user);
	// Before decoding: the guests get the stream exactly as the console sent it
	if(self->coplay && self->coplay->host)
		self->coplay->host->PushVideo(buf, buf_size, frames_lost, frame_recovered);
#if CHIAKI_LIB_ENABLE_PI_DECODER
	if(self->pi_decoder)
		return chiaki_pi_decoder_video_sample_cb(buf, buf_size, self->pi_decoder);
#endif
	return chiaki_ffmpeg_decoder_video_sample_cb(buf, buf_size, frames_lost, frame_recovered, self->ffmpeg_decoder);
}

void StreamSession::CoPlayAudioHeaderCb(ChiakiAudioHeader *header, void *user)
{
	auto self = static_cast<StreamSession *>(user);
	if(self->coplay->host)
	{
		coplay::AudioHeader h;
		h.valid = true;
		h.channels = header->channels;
		h.bits = header->bits;
		h.rate = header->rate;
		h.frame_size = header->frame_size;
		h.unknown = header->unknown;
		self->coplay->host->SetAudioHeader(h);
	}
	self->coplay->opus_sink.header_cb(header, self->coplay->opus_sink.user);
}

void StreamSession::CoPlayAudioFrameCb(uint8_t *buf, size_t buf_size, void *user)
{
	auto self = static_cast<StreamSession *>(user);
	if(self->coplay->host && self->coplay->hosting)
		self->coplay->host->PushAudio(buf, buf_size);
	self->coplay->opus_sink.frame_cb(buf, buf_size, self->coplay->opus_sink.user);
}

void StreamSession::CoPlayOnHaptics(uint8_t *buf, size_t buf_size)
{
	if(coplay && coplay->host && coplay->hosting)
		coplay->host->PushHaptics(buf, buf_size);
}

// ------------------------------------------------------------------ guest

void StreamSession::CoPlayGuestAudioSettingsCb(uint32_t channels, uint32_t rate, void *user)
{
	// A console session blocks its audio thread until the output device is open. Here that
	// would be the thread Stop() waits for, so open it without waiting and let the frames
	// that arrive in between go.
	auto self = static_cast<StreamSession *>(user);
	self->coplay->audio_ready = false;
	QMetaObject::invokeMethod(self, [self, channels, rate] {
		self->InitAudio(channels, rate);
		if(self->coplay)
			self->coplay->audio_ready = true;
	}, Qt::QueuedConnection);
}

void StreamSession::CoPlayGuestAudioFrameCb(int16_t *buf, size_t samples_count, void *user)
{
	auto self = static_cast<StreamSession *>(user);
	if(self->coplay->audio_ready)
		self->PushAudioFrame(buf, samples_count);
}

static bool ValidAudioHeader(const coplay::AudioHeader &h)
{
	if(!h.valid || h.channels < 1 || h.channels > 2 || h.bits != 16)
		return false;
	if(h.rate != 8000 && h.rate != 12000 && h.rate != 16000 && h.rate != 24000 && h.rate != 48000)
		return false;
	return h.frame_size > 0 && h.frame_size <= 5760;
}

void StreamSession::CoPlayStartGuest()
{
	chiaki_opus_decoder_set_cb(&opus_decoder, CoPlayGuestAudioSettingsCb, CoPlayGuestAudioFrameCb, this);

	auto feed_decoder = [this](const uint8_t *data, size_t size, int32_t frames_lost, bool recovered) {
		// The decoder reads a little past the end of what it is given
		std::vector<uint8_t> &buf = coplay->video_buf;
		buf.resize(size + CHIAKI_VIDEO_BUFFER_PADDING_SIZE);
		memcpy(buf.data(), data, size);
		memset(buf.data() + size, 0, CHIAKI_VIDEO_BUFFER_PADDING_SIZE);
		chiaki_ffmpeg_decoder_video_sample_cb(buf.data(), size, frames_lost, recovered, ffmpeg_decoder);
	};

	coplay::ClientCallbacks callbacks;
	callbacks.on_video_config = [feed_decoder](const uint8_t *data, size_t size) {
		feed_decoder(data, size, 0, false);
	};
	callbacks.on_video_frame = [this, feed_decoder](const coplay::AssembledFrame &frame) {
		feed_decoder(frame.data.data(), frame.data.size(), static_cast<int32_t>(frame.frames_lost), frame.recovered);

		// The numbers shown in the stream overlay; they belong to our own thread
		if(frame.frames_lost)
		{
			int32_t lost = static_cast<int32_t>(frame.frames_lost);
			QMetaObject::invokeMethod(this, [this, lost] {
				frames_lost += lost;
				emit FramesLostChanged();
			}, Qt::QueuedConnection);
		}
		uint64_t now = coplay::NowMs();
		coplay->bitrate_window_bytes += frame.data.size();
		if(!coplay->bitrate_window_start_ms)
			coplay->bitrate_window_start_ms = now;
		else if(now - coplay->bitrate_window_start_ms >= 1000)
		{
			double mbit = coplay->bitrate_window_bytes * 8.0 / (now - coplay->bitrate_window_start_ms) / 1000.0;
			coplay->bitrate_window_start_ms = now;
			coplay->bitrate_window_bytes = 0;
			QMetaObject::invokeMethod(this, [this, mbit] {
				measured_bitrate = mbit;
				emit MeasuredBitrateChanged();
			}, Qt::QueuedConnection);
		}
	};
	callbacks.on_audio = [this](const uint8_t *data, size_t size) {
		if(coplay->applied_audio.valid)
			coplay->opus_sink.frame_cb(const_cast<uint8_t *>(data), size, coplay->opus_sink.user);
	};
	callbacks.on_haptics = [this](const uint8_t *data, size_t size) {
		PushHapticsFrame(const_cast<uint8_t *>(data), size);
	};
	// Runs on the receiving thread, like everything that touches coplay->participants
	auto rebuild_players = [this](uint8_t your_slot, int mix_mode) {
		QVariantList players;
		for(const coplay::Participant &p : coplay->participants)
		{
			QVariantMap m;
			m["slot"] = p.slot;
			m["name"] = QString::fromStdString(p.name);
			m["host"] = (p.flags & coplay::kParticipantHost) != 0;
			m["you"] = p.slot == your_slot;
			m["canControl"] = (p.flags & coplay::kParticipantCanControl) != 0;
			m["inControl"] = (p.flags & coplay::kParticipantInControl) != 0;
			m["controller"] = static_cast<int>(p.controller);
			// Only once the picture itself is here
			coplay::BlobData data = p.avatar.Valid() ? coplay->client->Avatar(p.avatar.hash) : nullptr;
			m["avatar"] = data ? AvatarId(p.avatar) : QString();
			m["avatarUrl"] = AvatarUrl(data);
			players.append(m);
		}
		bool changed;
		{
			QMutexLocker locker(&coplay->mutex);
			changed = players != coplay->players || coplay->mix_mode != mix_mode;
			coplay->players = players;
			coplay->mix_mode = mix_mode;
		}
		if(changed)
			emit CoPlayChanged();
	};
	callbacks.on_state = [this, rebuild_players](const coplay::SessionState &s) {
		// Sound format, once the host knows it
		if(ValidAudioHeader(s.audio) && !(s.audio == coplay->applied_audio))
		{
			ChiakiAudioHeader header;
			chiaki_audio_header_set(&header, s.audio.channels, s.audio.bits, s.audio.rate, s.audio.frame_size);
			header.unknown = s.audio.unknown;
			coplay->applied_audio = s.audio;
			coplay->opus_sink.header_cb(&header, coplay->opus_sink.user);
		}

		// Controller feedback: replay what changed as the events a console would have sent.
		// The intensities go first, they decide whether rumble and triggers are applied at all.
		const coplay::Feedback &f = s.feedback;
		coplay::Feedback &a = coplay->applied_feedback;
		ChiakiEvent event;
		if(f.haptic_intensity_seq != a.haptic_intensity_seq && f.haptic_intensity <= 3)
		{
			memset(&event, 0, sizeof(event));
			event.type = CHIAKI_EVENT_HAPTIC_INTENSITY;
			event.intensity = static_cast<ChiakiDualSenseEffectIntensity>(f.haptic_intensity);
			Event(&event);
		}
		if(f.trigger_intensity_seq != a.trigger_intensity_seq && f.trigger_intensity <= 3)
		{
			memset(&event, 0, sizeof(event));
			event.type = CHIAKI_EVENT_TRIGGER_INTENSITY;
			event.intensity = static_cast<ChiakiDualSenseEffectIntensity>(f.trigger_intensity);
			Event(&event);
		}
		if(f.led_seq != a.led_seq)
		{
			memset(&event, 0, sizeof(event));
			event.type = CHIAKI_EVENT_LED_COLOR;
			memcpy(event.led_state, f.led.data(), sizeof(event.led_state));
			Event(&event);
		}
		if(f.player_index_seq != a.player_index_seq)
		{
			memset(&event, 0, sizeof(event));
			event.type = CHIAKI_EVENT_PLAYER_INDEX;
			event.player_index = f.player_index;
			Event(&event);
		}
		// Not for resets that happened before we joined
		if(coplay->applied_any && f.motion_reset_seq != a.motion_reset_seq)
		{
			memset(&event, 0, sizeof(event));
			event.type = CHIAKI_EVENT_MOTION_RESET;
			Event(&event);
		}
		if(f.trigger_seq != a.trigger_seq)
		{
			memset(&event, 0, sizeof(event));
			event.type = CHIAKI_EVENT_TRIGGER_EFFECTS;
			event.trigger_effects.type_left = f.trigger_type_left;
			event.trigger_effects.type_right = f.trigger_type_right;
			memcpy(event.trigger_effects.left, f.trigger_left.data(), sizeof(event.trigger_effects.left));
			memcpy(event.trigger_effects.right, f.trigger_right.data(), sizeof(event.trigger_effects.right));
			Event(&event);
		}
		if(f.rumble_seq != a.rumble_seq)
		{
			memset(&event, 0, sizeof(event));
			event.type = CHIAKI_EVENT_RUMBLE;
			event.rumble.left = f.rumble_left;
			event.rumble.right = f.rumble_right;
			Event(&event);
		}
		a = f;
		coplay->applied_any = true;

		coplay->participants = s.participants;
		rebuild_players(s.your_slot, static_cast<int>(s.mix_mode));
	};
	callbacks.on_avatar = [this, rebuild_players](const coplay::BlobRef &) {
		// A picture arrived: the list can show it now
		int mode;
		{
			QMutexLocker locker(&coplay->mutex);
			mode = coplay->mix_mode;
		}
		rebuild_players(coplay->client->Welcome().slot, mode);
	};
	callbacks.on_inputs = [this](const coplay::InputsSnapshot &inputs) {
		QMutexLocker locker(&coplay->mutex);
		coplay->guest_inputs = inputs;
		coplay->have_guest_inputs = true;
	};
	callbacks.on_disconnected = [this](coplay::DisconnectReason reason) {
		switch(reason)
		{
			case coplay::DisconnectReason::HostEnded:
				emit CoPlayNotice(tr("The host ended the session."));
				break;
			case coplay::DisconnectReason::Kicked:
				emit CoPlayNotice(tr("The host removed you from the session."));
				break;
			case coplay::DisconnectReason::Timeout:
				emit CoPlayNotice(tr("The connection to the host was lost."));
				break;
		}
		if(coplay->guest_quit_sent.exchange(true))
			return;
		ChiakiEvent event;
		memset(&event, 0, sizeof(event));
		event.type = CHIAKI_EVENT_QUIT;
		event.quit.reason = CHIAKI_QUIT_REASON_STOPPED;
		Event(&event);
	};
	coplay->client->Start(callbacks);

	ChiakiEvent event;
	memset(&event, 0, sizeof(event));
	event.type = CHIAKI_EVENT_CONNECTED;
	Event(&event);
}

void StreamSession::CoPlayStopGuest()
{
	if(!IsCoPlayGuest())
		return;
	// Waits for the receiving thread; nothing from the host is processed after this
	coplay->client->Stop();
	if(coplay->guest_quit_sent.exchange(true))
		return;
	ChiakiEvent event;
	memset(&event, 0, sizeof(event));
	event.type = CHIAKI_EVENT_QUIT;
	event.quit.reason = CHIAKI_QUIT_REASON_STOPPED;
	Event(&event);
}

// ------------------------------------------------------------------ overlay

void StreamSession::CoPlayUpdateControllerKind()
{
	if(!coplay)
		return;
	coplay::ControllerKind kind = coplay::ControllerKind::None;
#if CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	for(auto controller : controllers)
	{
		coplay::ControllerKind k = coplay::ControllerKind::Other;
		if(controller->IsDualSense() || controller->IsDualSenseEdge())
			k = coplay::ControllerKind::DualSense;
		else if(controller->GetController())
		{
			switch(SDL_GameControllerGetType(controller->GetController()))
			{
				case SDL_CONTROLLER_TYPE_PS5:
					k = coplay::ControllerKind::DualSense;
					break;
				case SDL_CONTROLLER_TYPE_PS3:
				case SDL_CONTROLLER_TYPE_PS4:
					k = coplay::ControllerKind::DualShock4;
					break;
				case SDL_CONTROLLER_TYPE_XBOX360:
				case SDL_CONTROLLER_TYPE_XBOXONE:
					k = coplay::ControllerKind::Xbox;
					break;
				default:
					break;
			}
		}
		// With several controllers connected a DualSense wins: it is the one with all the features
		if(kind == coplay::ControllerKind::None || k == coplay::ControllerKind::DualSense)
			kind = k;
	}
#endif
	if(coplay->own_controller.exchange(static_cast<int>(kind)) == static_cast<int>(kind))
		return;
	if(coplay->host)
	{
		coplay->host->SetController(kind);
		CoPlayUpdatePlayers();
	}
	if(coplay->client)
		coplay->client->SetController(kind);
}

QByteArray StreamSession::CoPlayAvatarData(const QString &id)
{
	coplay::BlobHash hash;
	if(!coplay || !AvatarHash(id, hash))
		return QByteArray();
	coplay::BlobData data;
	if(coplay->host)
		data = coplay->host->Avatar(hash);
	else if(coplay->client)
		data = coplay->client->Avatar(hash);
	if(data)
		return QByteArray(reinterpret_cast<const char *>(data->data()), static_cast<int>(data->size()));
	if(coplay->own_avatar_ref.Valid() && hash == coplay->own_avatar_ref.hash)
		return QByteArray(reinterpret_cast<const char *>(coplay->own_avatar.data()), static_cast<int>(coplay->own_avatar.size()));
	return QByteArray();
}

QJsonObject StreamSession::OverlayState()
{
	QJsonObject state;
	if(!coplay || !connected)
	{
		state["active"] = false;
		return state;
	}
	state["active"] = true;

	QVariantList players;
	int mix_mode;
	coplay::PadState local;
	coplay::InputsSnapshot inputs;
	bool have_inputs = false;
	{
		QMutexLocker locker(&coplay->mutex);
		players = coplay->players;
		mix_mode = coplay->mix_mode;
		local = coplay->local_pad;
		if(coplay->client)
		{
			inputs = coplay->guest_inputs;
			have_inputs = coplay->have_guest_inputs;
		}
	}
	if(IsCoPlayHosting())
	{
		inputs = coplay->host->Inputs();
		have_inputs = true;
	}

	if(players.isEmpty())
	{
		// Playing alone: the overlay still shows our own controller
		QVariantMap me;
		me["slot"] = 0;
		me["name"] = coplay->own_name;
		me["host"] = false;
		me["you"] = true;
		me["canControl"] = true;
		me["inControl"] = true;
		me["controller"] = coplay->own_controller.load();
		me["avatar"] = AvatarId(coplay->own_avatar_ref);
		players.append(me);
		mix_mode = 0;
	}

	QJsonArray list;
	for(const QVariant &entry : std::as_const(players))
	{
		QVariantMap p = entry.toMap();
		QJsonObject o;
		int slot = p.value("slot").toInt();
		o["slot"] = slot;
		o["name"] = p.value("name").toString();
		o["host"] = p.value("host").toBool();
		o["you"] = p.value("you").toBool();
		o["canControl"] = p.value("canControl").toBool();
		o["inControl"] = p.value("inControl").toBool();
		o["controller"] = p.value("controller").toInt();
		o["avatar"] = p.value("avatar").toString();
		QJsonObject pad = PadJson(coplay::PadState());
		if(p.value("you").toBool())
			pad = PadJson(local); // our own without the detour over the host
		else if(have_inputs)
		{
			for(const coplay::PlayerInput &input : inputs.players)
			{
				if(input.slot == slot)
				{
					pad = PadJson(input);
					break;
				}
			}
		}
		o["pad"] = pad;
		list.append(o);
	}
	state["mixMode"] = mix_mode;
	state["players"] = list;
	state["combined"] = have_inputs ? PadJson(inputs.combined) : PadJson(local);
	return state;
}
