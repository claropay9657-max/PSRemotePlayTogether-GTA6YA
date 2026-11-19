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
#include <coplayaddress.h>
#include <coplaycamera.h>
#include <voicechat.h>

#include <chiaki/time.h>
#include <chiaki/video.h>

#include <coplay/client.h>
#include <coplay/host.h>
#include <coplay/invite.h>
#include <coplay/socket.h>

#include <QClipboard>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

/** One try to find the host again; it is asked every quarter of a second during this time. */
static const uint32_t kRejoinAttemptMs = 2000;
/** A guest that could not join again after this long gives up. */
static const uint64_t kRejoinTimeoutMs = 90 * 1000;
/** Ending a session waits this long at most for the thread that is joining again. */
static const int kRejoinCancelWaitMs = 300;
/**
 * A host that is getting its console back needs a moment to take its old session apart;
 * asked at once, it may still answer and then go silent.
 */
static const int kRejoinHostBackDelayMs = 1500;

/**
 * A guest joining again after it lost the host. Shared between the session and the
 * thread that looks for the host: that thread is never waited for longer than a moment
 * (looking up a host name can take long), so it may outlive the session.
 */
struct CoPlayRejoin
{
	std::mutex mutex; // everything below
	std::condition_variable changed;
	bool cancelled = false;
	bool finished = false;
	std::shared_ptr<coplay::Client> attempt; // the one that is connecting right now
};

struct CoPlayState
{
	// host
	std::unique_ptr<coplay::Host> host;
	std::atomic<bool> hosting{false};
	QString host_name;

	// guest
	// The connection to the host. Joining again replaces it, on the session's own thread and
	// while no receiving thread runs; any other thread takes its reference with client_mutex.
	std::shared_ptr<coplay::Client> client;
	QMutex client_mutex;
	std::shared_ptr<const coplay::ClientConfig> client_config;
	coplay::ClientCallbacks client_callbacks;
	bool auto_rejoin = false;
	std::shared_ptr<CoPlayRejoin> rejoin; // session's thread; set while joining again
	QString rejoin_status;
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
	// What the invites are made of; set once before sharing starts
	coplay::Secret invite_secret{};
	uint16_t invite_port = 0;
	QString invite_address; // what the user wants in the invite, if anything
	QString invite_lan;
	QString stun_server;
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
	std::atomic<int> own_battery{255};
	int battery_ticks = 0;

	// Talking to and seeing each other. The voice engine lives as long as the session,
	// so the network threads can always hand it what they receive.
	std::unique_ptr<VoiceChat> voice;
	CoPlayCamera *camera = nullptr; // owned by the session object
	QTimer *media_timer = nullptr;
	bool media_started = false;
	bool voice_wanted = true;
	bool camera_on_start = false;
	bool show_cams = true;
	QString mic_device;
	QString speaker_device;
	QString camera_device;
	int camera_quality = 1;
	std::atomic<bool> voice_on{false};
	std::atomic<bool> mic_muted{false};
	std::atomic<bool> deafened{false};
	std::atomic<bool> camera_on{false};
	bool mic_permission_pending = false;
	QVariantMap talking;
	QMutex cam_mutex; // cam_jpeg, cam_serial
	QHash<int, QByteArray> cam_jpeg;
	QHash<int, quint64> cam_serial;
	std::atomic<int> cam_serial_total{0};
};

/** What a player's flags say, for the player list. */
static void MediaToMap(QVariantMap &m, uint8_t media)
{
	m["voice"] = (media & coplay::kMediaVoice) != 0;
	m["muted"] = (media & coplay::kMediaMuted) != 0;
	m["camera"] = (media & coplay::kMediaCamera) != 0;
	m["silenced"] = (media & coplay::kMediaSilenced) != 0;
}

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
	CoPlayChatInit(connect_info.coplay_chat_previews);
	chiaki_opus_decoder_get_sink(&opus_decoder, &coplay->opus_sink);
	coplay->mix_mode = connect_info.coplay_mix_mode;
	coplay->own_name = connect_info.coplay_name.trimmed();
	if(!connect_info.coplay_avatar.isEmpty() && connect_info.coplay_avatar.size() <= static_cast<int>(coplay::kMaxAvatarSize))
	{
		coplay->own_avatar.assign(connect_info.coplay_avatar.constBegin(), connect_info.coplay_avatar.constEnd());
		coplay->own_avatar_ref = coplay::MakeBlobRef(coplay->own_avatar);
	}

	coplay->voice = std::make_unique<VoiceChat>(GetChiakiLog());
	coplay->voice->SetVolume(connect_info.coplay_voice_volume);
	coplay->voice_wanted = connect_info.coplay_voice_enabled;
	coplay->mic_muted = connect_info.coplay_voice_start_muted;
	coplay->camera_on_start = connect_info.coplay_camera_on_start;
	coplay->show_cams = connect_info.coplay_show_cams;
	coplay->camera_device = connect_info.coplay_camera_device;
	coplay->camera_quality = connect_info.coplay_camera_quality;
	coplay->mic_device = connect_info.audio_in_device;
	coplay->speaker_device = connect_info.audio_out_device;

	coplay->client = connect_info.coplay_client;
	if(coplay->client)
	{
		coplay->client_config = connect_info.coplay_client_config;
		coplay->auto_rejoin = connect_info.auto_reconnect && coplay->client_config;
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
	callbacks.on_voice = [this](uint8_t slot, const uint8_t *data, size_t size) {
		CoPlayOnVoice(slot, data, size);
	};
	callbacks.on_cam_frame = [this](uint8_t slot, const std::vector<uint8_t> &jpeg) {
		CoPlayOnCamFrame(slot, QByteArray(reinterpret_cast<const char *>(jpeg.data()), static_cast<int>(jpeg.size())));
	};
	callbacks.on_chat = [this](const coplay::ChatMessage &m) {
		QMetaObject::invokeMethod(this, [this, m] { CoPlayOnChat(m); }, Qt::QueuedConnection);
	};
	callbacks.on_chat_image = [this](const coplay::BlobRef &ref) {
		QMetaObject::invokeMethod(this, [this, ref] { CoPlayOnChatImage(ref); }, Qt::QueuedConnection);
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

	coplay->invite_secret = config.secret;
	coplay->invite_port = config.port;
	coplay->invite_address = connect_info.coplay_address.trimmed();
	coplay->stun_server = connect_info.coplay_stun_server;
	QString local = QString::fromStdString(coplay::GuessLocalAddress());
	coplay->invite_lan = CoPlayInviteFor(local.isEmpty() ? QStringLiteral("127.0.0.1") : local);
	coplay->invite = coplay->invite_address.isEmpty() ? coplay->invite_lan : CoPlayInviteFor(coplay->invite_address);
	// The invite for friends elsewhere appears when the address on the internet is found
	connect(CoPlayPublicAddress::Instance(), &CoPlayPublicAddress::Changed, this, &StreamSession::CoPlayChanged);
}

/** The invite to this session under the given address; none without an address. */
QString StreamSession::CoPlayInviteFor(const QString &host) const
{
	if(!coplay || !coplay->host || host.isEmpty())
		return QString();
	coplay::Invite invite;
	invite.host = host.toStdString();
	invite.port = coplay->invite_port;
	invite.secret = coplay->invite_secret;
	return QString::fromStdString(coplay::EncodeInvite(invite));
}

void StreamSession::CoPlayStopThreads()
{
	if(!coplay)
		return;
	// First what sends through them: the microphone (its audio thread) and the camera
	if(coplay->voice)
		coplay->voice->CloseMicrophone();
	if(coplay->camera)
		coplay->camera->Stop();
	// Both calls return only after the respective thread has ended, so that none of
	// its callbacks runs while the rest of the session is taken apart
	CoPlayStopHost();
	CoPlayCancelRejoin();
	if(coplay->client)
		coplay->client->Stop();
	if(coplay->voice)
		coplay->voice->CloseSpeakers();
}

void StreamSession::CoPlayStopHost()
{
	if(!coplay)
		return;
	coplay->hosting = false;
	if(!coplay->host)
		return;
	// Guests wait for a host that is getting its console back, and leave one that is done
	if(reconnect_wanted)
		coplay->host->StopForReconnect();
	else
		coplay->host->Stop();
}

void StreamSession::CoPlayFini()
{
	CoPlayStopThreads();
	CoPlayCamProvider::ClearAll();
	delete coplay;
	coplay = nullptr;
	CoPlayChatFini();
}

coplay::Host *StreamSession::CoPlayHost() const
{
	return coplay ? coplay->host.get() : nullptr;
}

QString StreamSession::CoPlayOwnName() const
{
	if(!coplay)
		return QString();
	if(!coplay->own_name.isEmpty())
		return coplay->own_name;
	return IsCoPlayGuest() ? tr("You") : coplay->host_name;
}

std::shared_ptr<coplay::Client> StreamSession::CoPlayClient() const
{
	if(!coplay)
		return nullptr;
	QMutexLocker locker(&coplay->client_mutex);
	return coplay->client;
}


bool StreamSession::IsCoPlayActive() const
{
	return IsCoPlayGuest() || IsCoPlayHosting();
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

QString StreamSession::GetCoPlayInviteLan() const
{
	if(!IsCoPlayHosting())
		return QString();
	return coplay->invite_lan;
}

QString StreamSession::GetCoPlayInvitePublic() const
{
	if(!IsCoPlayHosting())
		return QString();
	// As in the settings: an address the user wrote down is the one for friends elsewhere,
	// otherwise the one that was looked up, which the settings and every session share
	if(!coplay->invite_address.isEmpty())
		return CoPlayInviteFor(coplay->invite_address);
	return CoPlayInviteFor(CoPlayPublicAddress::Instance()->Address());
}

QString StreamSession::GetCoPlayInviteIPv6() const
{
	if(!IsCoPlayHosting())
		return QString();
	return CoPlayInviteFor(QString::fromStdString(coplay::GuessGlobalIPv6Address()));
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
	uint8_t others = coplay->client->Want() & (coplay::kWantInputs | coplay::kWantVoice | coplay::kWantCams);
	coplay->client->SetWant(static_cast<uint8_t>((enabled ? coplay::kWantAll : coplay::kWantHaptics) | others));
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

void StreamSession::coplayCopyInviteLan()
{
	QString invite = GetCoPlayInviteLan();
	if(!invite.isEmpty())
		QGuiApplication::clipboard()->setText(invite);
}

void StreamSession::coplayCopyInvitePublic()
{
	QString invite = GetCoPlayInvitePublic();
	if(!invite.isEmpty())
		QGuiApplication::clipboard()->setText(invite);
}

void StreamSession::coplayCopyInviteIPv6()
{
	QString invite = GetCoPlayInviteIPv6();
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
		me["battery"] = coplay->own_battery.load();
		me["avatar"] = AvatarId(coplay->own_avatar_ref);
		me["avatarUrl"] = AvatarUrl(coplay->host->Avatar(coplay->own_avatar_ref.hash));
		MediaToMap(me, coplay->host->LocalMedia());
		me["mutedHere"] = false;
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
			p["battery"] = static_cast<int>(g.battery);
			p["avatar"] = AvatarId(g.avatar);
			p["avatarUrl"] = g.avatar.Valid() ? AvatarUrl(coplay->host->Avatar(g.avatar.hash)) : QString();
			MediaToMap(p, g.media);
			p["mutedHere"] = coplay->voice->PlayerMuted(g.slot);
			players.append(p);
		}
	}
	{
		QMutexLocker locker(&coplay->mutex);
		coplay->players = players;
		coplay->mix_mode = static_cast<int>(coplay->host->GetMixMode());
	}
	CoPlaySyncPlayers(players);
	emit CoPlayChanged();
}

/** On our own thread, once the session is open to guests. */
void StreamSession::CoPlayOnSharingStarted()
{
	if(!IsCoPlayHosting())
		return;
	// Friends elsewhere need this computer's address on the internet in their invite.
	// Sharing is the user's word that it may be asked for; nothing to ask if they wrote one down.
	if(coplay->invite_address.isEmpty())
		CoPlayPublicAddress::Instance()->LookupIfStale(coplay->stun_server);
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
				QMetaObject::invokeMethod(this, [this] { CoPlayStartMedia(); }, Qt::QueuedConnection);
				QMetaObject::invokeMethod(this, [this] { CoPlayOnSharingStarted(); }, Qt::QueuedConnection);
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
			CoPlayStopHost();
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
	// For noticing a console that stays connected but stopped sending, see CheckVideoStalled()
	self->last_video_ms = chiaki_time_now_monotonic_ms();
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
			m["battery"] = static_cast<int>(p.battery);
			// Only once the picture itself is here
			coplay::BlobData data = p.avatar.Valid() ? coplay->client->Avatar(p.avatar.hash) : nullptr;
			m["avatar"] = data ? AvatarId(p.avatar) : QString();
			m["avatarUrl"] = AvatarUrl(data);
			MediaToMap(m, p.media);
			m["mutedHere"] = coplay->voice->PlayerMuted(p.slot);
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
		{
			QMetaObject::invokeMethod(this, [this, players] { CoPlaySyncPlayers(players); }, Qt::QueuedConnection);
			emit CoPlayChanged();
		}
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
	callbacks.on_voice = [this](uint8_t slot, const uint8_t *data, size_t size) {
		CoPlayOnVoice(slot, data, size);
	};
	callbacks.on_cam_frame = [this](uint8_t slot, const std::vector<uint8_t> &jpeg) {
		CoPlayOnCamFrame(slot, QByteArray(reinterpret_cast<const char *>(jpeg.data()), static_cast<int>(jpeg.size())));
	};
	callbacks.on_chat = [this](const coplay::ChatMessage &m) {
		QMetaObject::invokeMethod(this, [this, m] { CoPlayOnChat(m); }, Qt::QueuedConnection);
	};
	callbacks.on_chat_image = [this](const coplay::BlobRef &ref) {
		QMetaObject::invokeMethod(this, [this, ref] { CoPlayOnChatImage(ref); }, Qt::QueuedConnection);
	};
	callbacks.on_disconnected = [this](coplay::DisconnectReason reason) {
		// On the receiving thread, which ends as soon as this returns
		bool lost = reason == coplay::DisconnectReason::Timeout || reason == coplay::DisconnectReason::HostReconnecting;
		if(lost && coplay->auto_rejoin)
		{
			// The session stays and joins again. Not from here: that replaces the client
			// this thread belongs to.
			QMetaObject::invokeMethod(this, [this, reason] { CoPlayBeginRejoin(reason); }, Qt::QueuedConnection);
			return;
		}
		CoPlayGuestLost(reason);
	};
	// Kept: a connection that replaces this one is started with the same
	coplay->client_callbacks = callbacks;
	coplay->client->Start(callbacks);

	ChiakiEvent event;
	memset(&event, 0, sizeof(event));
	event.type = CHIAKI_EVENT_CONNECTED;
	Event(&event);
	CoPlayStartMedia();
}

void StreamSession::CoPlayStopGuest()
{
	if(!IsCoPlayGuest())
		return;
	CoPlayCancelRejoin();
	// Waits for the receiving thread; nothing from the host is processed after this
	coplay->client->Stop();
	CoPlayGuestQuit();
}

void StreamSession::CoPlayGuestLost(coplay::DisconnectReason reason)
{
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
		case coplay::DisconnectReason::HostReconnecting:
			emit CoPlayNotice(tr("The host lost the connection to its console."));
			break;
	}
	CoPlayGuestQuit();
}

void StreamSession::CoPlayGuestQuit()
{
	if(coplay->guest_quit_sent.exchange(true))
		return;
	ChiakiEvent event;
	memset(&event, 0, sizeof(event));
	event.type = CHIAKI_EVENT_QUIT;
	event.quit.reason = CHIAKI_QUIT_REASON_STOPPED;
	Event(&event);
}

// ------------------------------------------------------------------ guest: joining again

bool StreamSession::IsCoPlayReconnecting() const
{
	return coplay && coplay->rejoin;
}

QString StreamSession::CoPlayReconnectStatus() const
{
	return coplay && coplay->rejoin ? coplay->rejoin_status : QString();
}

void StreamSession::CoPlayBeginRejoin(coplay::DisconnectReason reason)
{
	// On the session's thread, asked for by the receiving thread of the connection that broke
	if(!IsCoPlayGuest() || coplay->guest_quit_sent || coplay->rejoin)
		return;
	bool host_reconnecting = reason == coplay::DisconnectReason::HostReconnecting;
	CHIAKI_LOGW(GetChiakiLog(), "Play Together: %s, joining again",
		host_reconnecting ? "the host is getting its console back" : "the connection to the host was lost");

	// Its receiving thread ends by itself after a loss. Once this has waited for it, nothing
	// from the host is processed until the new connection is started.
	coplay->client->Stop();

	// Nobody tells the controllers to stop any more, and whatever the host says about them
	// once we are back is news
	for(auto controller : controllers)
	{
		const uint8_t clear_effect[10] = { 0 };
		controller->SetTriggerEffects(0x05, clear_effect, 0x05, clear_effect);
		controller->SetRumble(0, 0);
	}
	coplay->applied_feedback = coplay::Feedback();
	coplay->applied_any = false;

	coplay::ClientConfig config = *coplay->client_config;
	config.want = coplay->client->Want();
	config.media = coplay->client->Media();
	config.controller = static_cast<coplay::ControllerKind>(coplay->own_controller.load());
	config.connect_timeout_ms = kRejoinAttemptMs;

	auto job = std::make_shared<CoPlayRejoin>();
	coplay->rejoin = job;
	coplay->rejoin_status = host_reconnecting
		? tr("The host lost its console — waiting for it to come back…")
		: tr("Connection to the host lost — reconnecting…");
	emit CoPlayReconnectingChanged();

	// Looking the host up and waiting for its answer block, so that is not for this thread.
	// The one doing it touches the session in a single place, see below.
	std::thread([this, job, config, host_reconnecting] {
		std::shared_ptr<coplay::Client> joined;
		std::string error;
		uint64_t started_ms = coplay::NowMs();
		std::unique_lock<std::mutex> lock(job->mutex);
		if(host_reconnecting)
			job->changed.wait_for(lock, std::chrono::milliseconds(kRejoinHostBackDelayMs), [&job] { return job->cancelled; });
		while(!job->cancelled)
		{
			auto attempt_started = std::chrono::steady_clock::now();
			auto client = std::make_shared<coplay::Client>(config);
			job->attempt = client; // for CancelConnect()
			lock.unlock();
			bool ok = client->Connect(&error);
			lock.lock();
			job->attempt.reset();
			if(job->cancelled)
				break; // should it have got through all the same, it says goodbye as it goes
			if(ok)
			{
				joined = client;
				break;
			}
			if(coplay::NowMs() - started_ms >= kRejoinTimeoutMs)
				break;
			// An error that came at once (no network, for one) must not make this a busy loop
			job->changed.wait_until(lock, attempt_started + std::chrono::milliseconds(kRejoinAttemptMs),
				[&job] { return job->cancelled; });
		}
		// Whoever ends the session cancels first, and needs this lock for it: if it is not
		// cancelled, the session is still there to be told
		if(!job->cancelled)
		{
			QString message = QString::fromStdString(error);
			QMetaObject::invokeMethod(this, [this, job, joined, message] {
				CoPlayRejoinFinished(job, joined, message);
			}, Qt::QueuedConnection);
		}
		job->finished = true;
		job->changed.notify_all();
	}).detach();
}

void StreamSession::CoPlayRejoinFinished(const std::shared_ptr<CoPlayRejoin> &job,
		const std::shared_ptr<coplay::Client> &client, const QString &error)
{
	if(!coplay || coplay->rejoin != job)
	{
		// Cancelled while this was on its way
		if(client)
			client->Stop();
		return;
	}
	coplay->rejoin.reset();
	coplay->rejoin_status.clear();
	emit CoPlayReconnectingChanged();

	if(!client)
	{
		CHIAKI_LOGE(GetChiakiLog(), "Play Together: could not join again: %s", qPrintable(error));
		emit CoPlayNotice(tr("The connection to the host was lost."));
		CoPlayGuestQuit();
		return;
	}
	if(client->Welcome().stream.codec != coplay->client->Welcome().stream.codec)
	{
		// The decoder was set up for the other one
		CHIAKI_LOGE(GetChiakiLog(), "Play Together: the host came back with a different video codec");
		client->Stop();
		emit CoPlayNotice(tr("The host changed its video settings. Please join again."));
		CoPlayGuestQuit();
		return;
	}

	// What was switched while we were away went to the old connection
	client->SetWant(coplay->client->Want());
	client->SetMedia(coplay->client->Media());
	client->SetController(static_cast<coplay::ControllerKind>(coplay->own_controller.load()));
	{
		QMutexLocker locker(&coplay->client_mutex);
		coplay->client = client;
	}
	{
		QMutexLocker locker(&coplay->mutex);
		coplay->have_guest_inputs = false;
	}
	CHIAKI_LOGI(GetChiakiLog(), "Play Together: joined again at %s as player %u",
		client->HostAddress().c_str(), static_cast<unsigned>(client->Welcome().slot));
	// The picture comes back by itself: a new connection starts with a key frame
	client->Start(coplay->client_callbacks);
	emit CoPlayChanged();
}

void StreamSession::CoPlayCancelRejoin()
{
	if(!coplay || !coplay->rejoin)
		return;
	std::shared_ptr<CoPlayRejoin> job = coplay->rejoin;
	coplay->rejoin.reset();
	coplay->rejoin_status.clear();
	{
		std::unique_lock<std::mutex> lock(job->mutex);
		job->cancelled = true;
		if(job->attempt)
			job->attempt->CancelConnect();
		job->changed.notify_all();
		// It notices within a few milliseconds, unless it is stuck looking up the host's
		// name: then it is left behind and ends by itself, without touching the session
		job->changed.wait_for(lock, std::chrono::milliseconds(kRejoinCancelWaitMs), [&job] { return job->finished; });
	}
	emit CoPlayReconnectingChanged();
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
	// The charge of the controller that is running out first; on a cable if all are
	int battery = 255;
#if CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER
	for(auto controller : controllers)
	{
		int level = controller->GetBattery();
		if(level == 255)
			continue;
		if(battery == 255 || (level <= 100 && (battery > 100 || level < battery)))
			battery = level;
	}
#endif
	bool kind_changed = coplay->own_controller.exchange(static_cast<int>(kind)) != static_cast<int>(kind);
	bool battery_changed = coplay->own_battery.exchange(battery) != battery;
	if(!kind_changed && !battery_changed)
		return;
	if(coplay->host)
	{
		coplay->host->SetController(kind);
		coplay->host->SetBattery(static_cast<uint8_t>(battery));
		CoPlayUpdatePlayers();
	}
	if(coplay->client)
	{
		coplay->client->SetController(kind);
		coplay->client->SetBattery(static_cast<uint8_t>(battery));
	}
	if(!coplay->host && !coplay->client)
		emit CoPlayChanged(); // playing alone: the display on the game picture shows it too
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
		me["battery"] = coplay->own_battery.load();
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
		o["battery"] = p.value("battery", 255).toInt();
		o["avatar"] = p.value("avatar").toString();
		o["voice"] = p.value("voice").toBool();
		o["muted"] = p.value("muted").toBool() || p.value("silenced").toBool();
		o["camera"] = p.value("camera").toBool();
		bool you = p.value("you").toBool();
		o["talking"] = you ? coplay->voice->OwnSpeaking() : coplay->voice->Level(slot) > 12;
		{
			QMutexLocker cam_locker(&coplay->cam_mutex);
			o["cam"] = static_cast<qint64>(coplay->cam_serial.value(slot, 0));
		}
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

// ------------------------------------------------------------------ voice and webcams

#ifdef Q_OS_MACOS
static void CoPlayMicPermissionCb(Authorization authorization, void *user)
{
	// Arrives on whatever thread the system likes, and as late as the user answers the
	// system's question: the session may be gone by then, hence the guarded pointer,
	// which is only looked at on the session's own thread
	auto guard = static_cast<QPointer<StreamSession> *>(user);
	bool granted = authorization == AUTHORIZED;
	QMetaObject::invokeMethod(qApp, [guard, granted] {
		if(*guard)
			(*guard)->CoPlayMicPermissionResult(granted);
		delete guard;
	}, Qt::QueuedConnection);
}
#endif

int StreamSession::CoPlayOwnSlot() const
{
	return IsCoPlayGuest() ? coplay->client->Welcome().slot : 0;
}

void StreamSession::CoPlayStartMedia()
{
	if(!coplay || coplay->media_started || !IsCoPlayActive())
		return;
	coplay->media_started = true;

	coplay->voice->SetSendCallback([this](const uint8_t *data, size_t size) {
		// On the microphone's thread; both ends outlive the microphone (see CoPlayStopThreads)
		std::shared_ptr<coplay::Client> client;
		{
			QMutexLocker locker(&coplay->client_mutex);
			client = coplay->client;
		}
		if(client)
			client->SendVoice(data, size);
		else if(coplay->host && coplay->hosting)
			coplay->host->PushVoice(data, size);
	});

	coplay->camera = new CoPlayCamera(this);
	connect(coplay->camera, &CoPlayCamera::Frame, this, [this](const QByteArray &jpeg, const QImage &image) {
		if(!coplay || !coplay->camera_on)
			return;
		const uint8_t *data = reinterpret_cast<const uint8_t *>(jpeg.constData());
		if(coplay->client)
			coplay->client->SendCamFrame(data, static_cast<size_t>(jpeg.size()));
		else if(coplay->host && coplay->hosting)
			coplay->host->PushCamFrame(data, static_cast<size_t>(jpeg.size()));
		// And into our own tile, without decoding what we just encoded
		int slot = CoPlayOwnSlot();
		{
			QMutexLocker locker(&coplay->cam_mutex);
			coplay->cam_jpeg[slot] = jpeg;
			coplay->cam_serial[slot]++;
		}
		CoPlayCamProvider::Set(slot, image);
		coplay->cam_serial_total++;
		emit CoPlayCamChanged();
	});
	connect(coplay->camera, &CoPlayCamera::Failed, this, [this](const QString &message) {
		if(!coplay)
			return;
		coplay->camera_on = false;
		CoPlayUpdateMedia();
		emit CoPlayNotice(tr("Webcam: %1").arg(message));
	});

	coplay->media_timer = new QTimer(this);
	coplay->media_timer->setInterval(100);
	connect(coplay->media_timer, &QTimer::timeout, this, &StreamSession::CoPlayMediaTick);
	coplay->media_timer->start();

	// For trying things out without a camera (and without its permission prompt)
	if(qEnvironmentVariableIsSet("COPLAY_TEST_CAMERA"))
	{
		coplay->camera_device = CoPlayCamera::TestDevice();
		coplay->camera_on_start = true;
	}

	if(coplay->voice_wanted)
		SetCoPlayVoiceOn(true);
	if(coplay->camera_on_start)
		SetCoPlayCameraOn(true);
	CoPlayUpdateMedia();
}

bool StreamSession::GetCoPlayVoiceOn() const
{
	return coplay && coplay->voice_on;
}

void StreamSession::SetCoPlayVoiceOn(bool on)
{
	if(!coplay || !coplay->media_started || coplay->voice_on == on)
		return;
	if(!on)
	{
		coplay->voice_on = false;
		coplay->voice->CloseMicrophone();
		coplay->voice->CloseSpeakers();
		for(int slot = 0; slot < VoiceChat::kMaxPlayers; slot++)
			coplay->voice->ForgetPlayer(slot);
		if(coplay->client)
			coplay->client->SetWant(static_cast<uint8_t>(coplay->client->Want() & ~coplay::kWantVoice));
		CoPlayUpdateMedia();
		return;
	}

	QString error;
	if(!coplay->voice->OpenSpeakers(coplay->speaker_device, &error))
	{
		CHIAKI_LOGE(GetChiakiLog(), "Voice chat: cannot open the speakers: %s", qPrintable(error));
		emit CoPlayNotice(tr("Voice chat: the audio output could not be opened (%1).").arg(error));
		return;
	}
	coplay->voice->SetDeafened(coplay->deafened);
	coplay->voice->SetMuted(coplay->mic_muted);
	coplay->voice_on = true;
	if(coplay->client)
		coplay->client->SetWant(static_cast<uint8_t>(coplay->client->Want() | coplay::kWantVoice));
	CoPlayUpdateMedia();

	// Listening works from here on; talking needs the microphone, and on macOS the user's consent
	if(qEnvironmentVariableIsSet("COPLAY_TEST_VOICE"))
	{
		CoPlayMicPermissionResult(true);
		return;
	}
#ifdef Q_OS_MACOS
	if(!coplay->mic_permission_pending)
	{
		coplay->mic_permission_pending = true;
		macMicPermission(CoPlayMicPermissionCb, new QPointer<StreamSession>(this));
	}
#else
	CoPlayMicPermissionResult(true);
#endif
}

void StreamSession::CoPlayMicPermissionResult(bool granted)
{
	if(!coplay)
		return;
	coplay->mic_permission_pending = false;
	if(!coplay->voice_on)
		return;
	if(!granted)
	{
		emit CoPlayNotice(tr("Voice chat: this app is not allowed to use the microphone, so you can listen but not talk. You can change that in the system's privacy settings."));
		CoPlayUpdateMedia();
		return;
	}
	CoPlayOpenMicrophone();
}

void StreamSession::CoPlayOpenMicrophone()
{
	QString error;
	QString device = qEnvironmentVariableIsSet("COPLAY_TEST_VOICE") ? QStringLiteral("test") : coplay->mic_device;
	if(!coplay->voice->OpenMicrophone(device, &error))
	{
		CHIAKI_LOGE(GetChiakiLog(), "Voice chat: cannot open the microphone: %s", qPrintable(error));
		emit CoPlayNotice(tr("Voice chat: the microphone could not be opened (%1). You can listen but not talk.").arg(error));
	}
	CoPlayUpdateMedia();
}

bool StreamSession::GetCoPlayMicMuted() const
{
	return coplay && coplay->mic_muted;
}

void StreamSession::SetCoPlayMicMuted(bool muted)
{
	if(!coplay || coplay->mic_muted == muted)
		return;
	coplay->mic_muted = muted;
	coplay->voice->SetMuted(muted);
	CoPlayUpdateMedia();
}

bool StreamSession::GetCoPlayDeafened() const
{
	return coplay && coplay->deafened;
}

void StreamSession::SetCoPlayDeafened(bool deafened)
{
	if(!coplay || coplay->deafened == deafened)
		return;
	coplay->deafened = deafened;
	coplay->voice->SetDeafened(deafened);
	emit CoPlayMediaChanged();
}

bool StreamSession::GetCoPlayCameraSupported() const
{
	return CoPlayCamera::Supported();
}

bool StreamSession::GetCoPlayCameraOn() const
{
	return coplay && coplay->camera_on;
}

void StreamSession::SetCoPlayCameraOn(bool on)
{
	if(!coplay || !coplay->media_started || !coplay->camera || coplay->camera_on == on)
		return;
	coplay->camera_on = on;
	if(on)
		coplay->camera->Start(coplay->camera_device, coplay->camera_quality); // reports back through Failed
	else
	{
		coplay->camera->Stop();
		int slot = CoPlayOwnSlot();
		{
			QMutexLocker locker(&coplay->cam_mutex);
			coplay->cam_jpeg.remove(slot);
			coplay->cam_serial.remove(slot);
		}
		CoPlayCamProvider::Clear(slot);
		coplay->cam_serial_total++;
		emit CoPlayCamChanged();
	}
	CoPlayUpdateMedia();
}

void StreamSession::CoPlayUpdateMedia()
{
	if(!coplay)
		return;
	uint8_t media = 0;
	if(coplay->voice_on && coplay->voice->MicrophoneOpen())
		media |= coplay::kMediaVoice;
	if(coplay->voice_on && coplay->mic_muted)
		media |= coplay::kMediaMuted;
	if(coplay->camera_on)
		media |= coplay::kMediaCamera;
	if(coplay->client)
		coplay->client->SetMedia(media);
	else if(coplay->host)
	{
		coplay->host->SetLocalMedia(media);
		CoPlayUpdatePlayers();
	}
	emit CoPlayMediaChanged();
}

void StreamSession::coplaySetPlayerMuted(int slot, bool muted)
{
	if(!coplay || slot < 0 || slot >= VoiceChat::kMaxPlayers)
		return;
	coplay->voice->SetPlayerMuted(slot, muted);
	{
		QMutexLocker locker(&coplay->mutex);
		for(QVariant &entry : coplay->players)
		{
			QVariantMap m = entry.toMap();
			if(m.value("slot").toInt() == slot)
			{
				m["mutedHere"] = muted;
				entry = m;
			}
		}
	}
	emit CoPlayChanged();
}

void StreamSession::coplaySetSilenced(int slot, bool silenced)
{
	if(!coplay || !coplay->host || slot < 1 || slot > 255)
		return;
	coplay->host->SetGuestSilenced(static_cast<uint8_t>(slot), silenced);
}

void StreamSession::CoPlayOnVoice(int slot, const uint8_t *data, size_t size)
{
	// On a network thread
	if(coplay->voice_on)
		coplay->voice->PushVoice(slot, data, size);
}

void StreamSession::CoPlayOnCamFrame(int slot, const QByteArray &jpeg)
{
	// On a network thread; decoding the picture is work for ours
	if(!coplay->show_cams)
		return;
	QMetaObject::invokeMethod(this, [this, slot, jpeg] {
		if(!coplay)
			return;
		QImage image = QImage::fromData(jpeg, "JPEG");
		if(image.isNull())
			return;
		{
			QMutexLocker locker(&coplay->cam_mutex);
			coplay->cam_jpeg[slot] = jpeg;
			coplay->cam_serial[slot]++;
		}
		CoPlayCamProvider::Set(slot, image);
		coplay->cam_serial_total++;
		emit CoPlayCamChanged();
	}, Qt::QueuedConnection);
}

void StreamSession::CoPlaySyncPlayers(const QVariantList &players)
{
	// Whoever left or switched the webcam off must not stay on screen as a frozen picture,
	// and whoever takes over a place must not inherit the old voice
	if(!coplay)
		return;
	bool present[VoiceChat::kMaxPlayers] = {};
	bool camera[VoiceChat::kMaxPlayers] = {};
	for(const QVariant &entry : players)
	{
		QVariantMap m = entry.toMap();
		int slot = m.value("slot").toInt();
		if(slot < 0 || slot >= VoiceChat::kMaxPlayers)
			continue;
		present[slot] = true;
		camera[slot] = m.value("camera").toBool();
	}
	bool cams_changed = false;
	for(int slot = 0; slot < VoiceChat::kMaxPlayers; slot++)
	{
		if(!present[slot])
			coplay->voice->ForgetPlayer(slot);
		if(!camera[slot])
		{
			QMutexLocker locker(&coplay->cam_mutex);
			if(coplay->cam_jpeg.remove(slot))
			{
				coplay->cam_serial.remove(slot);
				CoPlayCamProvider::Clear(slot);
				cams_changed = true;
			}
		}
	}
	if(cams_changed)
	{
		coplay->cam_serial_total++;
		emit CoPlayCamChanged();
	}
}

void StreamSession::CoPlayMediaTick()
{
	if(!coplay)
		return;
	// Every half minute: has a controller's charge changed?
	if(++coplay->battery_ticks >= 300)
	{
		coplay->battery_ticks = 0;
		CoPlayUpdateControllerKind();
	}
	QVariantMap talking;
	int own = CoPlayOwnSlot();
	if(coplay->voice_on)
	{
		for(int slot = 0; slot < VoiceChat::kMaxPlayers; slot++)
		{
			bool on = slot == own ? coplay->voice->OwnSpeaking() : coplay->voice->Level(slot) > 12;
			if(on)
				talking[QString::number(slot)] = true;
		}
	}
	if(talking != coplay->talking)
	{
		coplay->talking = talking;
		emit CoPlayTalkingChanged();
	}
}

QVariantMap StreamSession::GetCoPlayTalking() const
{
	return coplay ? coplay->talking : QVariantMap();
}

int StreamSession::GetCoPlayCamSerial() const
{
	return coplay ? coplay->cam_serial_total.load() : 0;
}

QByteArray StreamSession::CoPlayCamJpeg(int slot, quint64 *serial)
{
	if(!coplay)
		return QByteArray();
	QMutexLocker locker(&coplay->cam_mutex);
	if(serial)
		*serial = coplay->cam_serial.value(slot, 0);
	return coplay->cam_jpeg.value(slot);
}

void StreamSession::CoPlaySetOverlayAudio(bool enabled, bool include_own)
{
	if(coplay)
		coplay->voice->SetTap(enabled, include_own);
}

QByteArray StreamSession::CoPlayReadOverlayAudio()
{
	return coplay ? coplay->voice->ReadTap() : QByteArray();
}

QVariantMap StreamSession::coplayPadStates()
{
	QVariantMap out;
	QJsonObject state = OverlayState();
	if(!state.value("active").toBool())
		return out;
	const QJsonArray players = state.value("players").toArray();
	for(const QJsonValue &v : players)
	{
		QJsonObject p = v.toObject();
		out.insert(QString::number(p.value("slot").toInt()), p.value("pad").toObject().toVariantMap());
	}
	out.insert("combined", state.value("combined").toObject().toVariantMap());
	return out;
}

QVariantMap StreamSession::coplayOwnPlayer()
{
	QVariantMap me;
	if(!coplay)
		return me;
	me["slot"] = 0;
	me["name"] = coplay->own_name;
	me["host"] = false;
	me["you"] = true;
	me["canControl"] = true;
	me["inControl"] = true;
	me["controller"] = coplay->own_controller.load();
	me["battery"] = coplay->own_battery.load();
	me["avatar"] = AvatarId(coplay->own_avatar_ref);
	coplay::BlobData data;
	if(coplay->own_avatar_ref.Valid())
		data = std::make_shared<const std::vector<uint8_t>>(coplay->own_avatar);
	me["avatarUrl"] = AvatarUrl(data);
	return me;
}

void StreamSession::coplaySetWantInputs(bool wanted)
{
	if(!IsCoPlayGuest())
		return;
	uint8_t want = coplay->client->Want();
	uint8_t updated = static_cast<uint8_t>(wanted ? (want | coplay::kWantInputs) : (want & ~coplay::kWantInputs));
	if(updated != want)
		coplay->client->SetWant(updated);
}
