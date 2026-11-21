// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_STREAMSESSION_H
#define CHIAKI_STREAMSESSION_H

#include <chiaki/session.h>
#include <chiaki/opusdecoder.h>
#include <chiaki/opusencoder.h>
#include <chiaki/ffmpegdecoder.h>

#if CHIAKI_LIB_ENABLE_PI_DECODER
#include <chiaki/pidecoder.h>
#endif

#if CHIAKI_GUI_ENABLE_SETSU
#include <setsu.h>
#include <chiaki/orientation.h>
#endif

#if CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE
#include <sdeck.h>
#include <chiaki/orientation.h>
#endif

// Using Q_OS_MACOS instead of __APPLE__ doesn't work for the necessary enums to be included
#ifdef __APPLE__
#include <macMicPermission.h>
#endif

#include "exception.h"
#include "sessionlog.h"
#include "controllermanager.h"
#include "settings.h"

#include <atomic>
#include <memory>

#include <QObject>
#include <QVariantList>
#include <QJsonArray>
#include <QJsonObject>
#include <QImage>
#include <QMouseEvent>
#include <QMutex>
#include <QTimer>
#include <QQueue>
#include <QElapsedTimer>
#include <QThread>
#include <QWaitCondition>
#include <QAtomicInteger>
#if CHIAKI_GUI_ENABLE_SPEEX
#include <QQueue>
#include <speex/speex_echo.h>
#include <speex/speex_preprocess.h>
#endif

class QKeyEvent;
class Settings;

namespace coplay { class Client; class Host; struct ClientConfig; enum class DisconnectReason; struct ChatMessage; struct ChatEmbed; struct BlobRef; }
struct CoPlayState;
struct CoPlayChatState;
struct CoPlayRejoin;

class ChiakiException: public Exception
{
	public:
		explicit ChiakiException(const QString &msg) : Exception(msg) {};
};

#if CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE
typedef struct haptic_packet_t
{
   	int16_t haptic_packet[30];
    uint64_t timestamp;
} haptic_packet_t;

class SdeckHapticsWorker;
#endif

	struct StreamSessionConnectInfo
	{
		Settings *settings;
		QMap<Qt::Key, int> key_map;
		Decoder decoder;
		QString hw_decoder;
		AVBufferRef *hw_device_ctx;
		QString audio_out_device;
		QString audio_in_device;
		uint32_t log_level_mask;
		bool log_sanitize = true;
		QString log_file;
		ChiakiTarget target;
		QString host;
		QString nickname;
		QByteArray regist_key;
		QByteArray morning;
		QString initial_login_pin;
		ChiakiConnectVideoProfile video_profile;
		double packet_loss_max;
		unsigned int audio_buffer_size;
		int audio_volume;
		bool fullscreen;
		bool zoom;
		bool stretch;
		bool enable_keyboard;
		bool keyboard_controller_enabled;
		bool mouse_touch_enabled;
		bool enable_dualsense;
		bool auto_regist;
		float haptic_override;
		ChiakiDisableAudioVideo audio_video_disabled;
		RumbleHapticsIntensity rumble_haptics_intensity;
		bool buttons_by_pos;
		bool enable_idr_on_fec_failure;
		bool start_mic_unmuted;
		bool port_guessing_enabled;
		int port_guess_count;
		int port_guess_socket_count;
#if CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE
		bool vertical_sdeck;
		bool enable_steamdeck_haptics;
#endif
#if CHIAKI_GUI_ENABLE_SPEEX
		bool speech_processing_enabled;
		int32_t noise_suppress_level;
		int32_t echo_suppress_level;
#endif
		QString duid;
		QString psn_token;
		QString psn_account_id;
		uint16_t dpad_touch_increment;
		uint dpad_touch_shortcut1;
		uint dpad_touch_shortcut2;
		uint dpad_touch_shortcut3;
		uint dpad_touch_shortcut4;

		// Play Together. Sharing our own console session with guests:
		bool coplay_host_enabled = false;
		int coplay_port = 0;
		QString coplay_secret;
		QString coplay_name;
		QString coplay_address;
		QString coplay_stun_server;
		QString coplay_meet_server;
		/** Guests may put the console to sleep when they leave. */
		bool coplay_guests_may_sleep = false;
		/** How many friends may join at the same time. */
		int coplay_max_guests = 4;
		/** A public room: listed for everybody, with a key of its own (coplay_secret is that one). */
		bool coplay_public = false;
		QString coplay_room_title;
		QString coplay_room_lang;
		/** Send a small picture of the game to the list now and then (off in streamer mode). */
		bool coplay_room_preview = false;
		/** The game the console runs, as the host's discovery last saw it; may be empty. */
		QString coplay_room_game;
		QString coplay_room_title_id;
		/** 0 the host lets each in, 1 everybody watches first, 2 everybody plays. */
		int coplay_join_policy = 0;
		QString coplay_rooms_server;
		/** Let friends in who join from a web browser, and the page they open (empty: the built-in one). */
		bool coplay_web_guests = true;
		QString coplay_web_url;
		/** 0 = the port above the sharing port, -1 = any free one for each browser. */
		int coplay_web_port = 0;
		int coplay_mix_mode = 0;
		/** The player's picture as image file bytes; shown to the others and in the overlay. */
		QByteArray coplay_avatar;
		// Talking to and seeing the other players
		bool coplay_voice_enabled = true;
		bool coplay_voice_start_muted = false;
		/** Asks the others to leave us out of their live streams. */
		bool coplay_leave_out_of_streams = false;
		int coplay_voice_volume = 100;
		QString coplay_camera_device;
		int coplay_camera_quality = 1;
		bool coplay_camera_on_start = false;
		bool coplay_show_cams = true;
		/** Text chat: the writer's app looks at a link once and sends a preview along. */
		bool coplay_chat_previews = true;
		// Joining somebody else's session instead of connecting to a console.
		// The client has already completed its handshake with the host.
		std::shared_ptr<coplay::Client> coplay_client;
		/** What the client was made from, for joining again after a connection loss. */
		std::shared_ptr<const coplay::ClientConfig> coplay_client_config;
		/** Get a lost connection back without asking: the console's, or as a guest the host's. */
		bool auto_reconnect = false;

	StreamSessionConnectInfo() {}
	StreamSessionConnectInfo(
			Settings *settings,
			ChiakiTarget target,
			QString host,
			QString nickname,
			QByteArray regist_key,
			QByteArray morning,
			QString initial_login_pin,
			QString duid,
			bool auto_regist,
			bool fullscreen,
			bool zoom,
			bool stretch);
};

struct MicBuf
{
	int16_t *buf;
	uint32_t size_bytes;
	uint32_t current_byte;
};

class StreamSession : public QObject
{
	friend class StreamSessionPrivate;

	Q_OBJECT
	Q_PROPERTY(QString host READ GetHost CONSTANT)
	Q_PROPERTY(bool connected READ GetConnected NOTIFY ConnectedChanged)
	Q_PROPERTY(double measuredBitrate READ GetMeasuredBitrate NOTIFY MeasuredBitrateChanged)
	Q_PROPERTY(double averagePacketLoss READ GetAveragePacketLoss NOTIFY AveragePacketLossChanged)
	Q_PROPERTY(bool muted READ GetMuted WRITE SetMuted NOTIFY MutedChanged)
	Q_PROPERTY(bool cantDisplay READ GetCantDisplay NOTIFY CantDisplayChanged)
	Q_PROPERTY(int framesLost READ GetFramesLost NOTIFY FramesLostChanged)
	Q_PROPERTY(bool coplayGuest READ IsCoPlayGuest CONSTANT)
	Q_PROPERTY(bool coplayHosting READ IsCoPlayHosting NOTIFY CoPlayChanged)
	/** Guest: the host lets guests put the console to sleep when they leave. */
	Q_PROPERTY(bool coplayMaySleep READ CoPlayGuestMaySleep NOTIFY CoPlayChanged)
	// A public room: listed for everybody; who knocks, and who may come in
	Q_PROPERTY(bool coplayPublic READ GetCoPlayPublic NOTIFY CoPlayChanged)
	Q_PROPERTY(QString coplayRoomTitle READ GetCoPlayRoomTitle NOTIFY CoPlayChanged)
	Q_PROPERTY(QString coplayRoomGame READ GetCoPlayRoomGame NOTIFY CoPlayChanged)
	Q_PROPERTY(QString coplayRoomProblem READ GetCoPlayRoomProblem NOTIFY CoPlayChanged)
	Q_PROPERTY(int coplayJoinPolicy READ GetCoPlayJoinPolicy WRITE SetCoPlayJoinPolicy NOTIFY CoPlayChanged)
	Q_PROPERTY(bool coplayLocked READ GetCoPlayLocked WRITE SetCoPlayLocked NOTIFY CoPlayChanged)
	Q_PROPERTY(QVariantList coplayPending READ GetCoPlayPending NOTIFY CoPlayChanged)
	Q_PROPERTY(QString coplayInvite READ GetCoPlayInvite NOTIFY CoPlayChanged)
	Q_PROPERTY(QString coplayInviteLan READ GetCoPlayInviteLan NOTIFY CoPlayChanged)
	Q_PROPERTY(QString coplayInvitePublic READ GetCoPlayInvitePublic NOTIFY CoPlayChanged)
	Q_PROPERTY(QString coplayInviteIPv6 READ GetCoPlayInviteIPv6 NOTIFY CoPlayChanged)
	Q_PROPERTY(QString coplayInviteWeb READ GetCoPlayInviteWeb NOTIFY CoPlayChanged)
	Q_PROPERTY(QString coplayReachText READ GetCoPlayReachText NOTIFY CoPlayChanged)
	Q_PROPERTY(bool coplayReachGood READ GetCoPlayReachGood NOTIFY CoPlayChanged)
	Q_PROPERTY(QVariantList coplayPlayers READ GetCoPlayPlayers NOTIFY CoPlayChanged)
	Q_PROPERTY(int coplayMixMode READ GetCoPlayMixMode WRITE SetCoPlayMixMode NOTIFY CoPlayChanged)
	Q_PROPERTY(bool coplayVideoEnabled READ GetCoPlayVideoEnabled WRITE SetCoPlayVideoEnabled NOTIFY CoPlayChanged)
	// This computer shows no picture / plays no sound of the console in this session: the host plays
	// on the TV (controller only), or a guest joined with the controller only
	Q_PROPERTY(bool pictureOff READ GetPictureOff NOTIFY CoPlayChanged)
	Q_PROPERTY(bool soundOff READ GetSoundOff NOTIFY CoPlayChanged)
	Q_PROPERTY(bool coplayActive READ IsCoPlayActive NOTIFY CoPlayChanged)
	Q_PROPERTY(bool coplayVoiceOn READ GetCoPlayVoiceOn WRITE SetCoPlayVoiceOn NOTIFY CoPlayMediaChanged)
	Q_PROPERTY(bool coplayMicMuted READ GetCoPlayMicMuted WRITE SetCoPlayMicMuted NOTIFY CoPlayMediaChanged)
	Q_PROPERTY(bool coplayDeafened READ GetCoPlayDeafened WRITE SetCoPlayDeafened NOTIFY CoPlayMediaChanged)
	Q_PROPERTY(bool coplayCameraOn READ GetCoPlayCameraOn WRITE SetCoPlayCameraOn NOTIFY CoPlayMediaChanged)
	Q_PROPERTY(bool coplayCameraSupported READ GetCoPlayCameraSupported CONSTANT)
	Q_PROPERTY(QVariantMap coplayTalking READ GetCoPlayTalking NOTIFY CoPlayTalkingChanged)
	Q_PROPERTY(int coplayCamSerial READ GetCoPlayCamSerial NOTIFY CoPlayCamChanged)
	Q_PROPERTY(bool coplayReconnecting READ IsCoPlayReconnecting NOTIFY CoPlayReconnectingChanged)
	Q_PROPERTY(QVariantList coplayChat READ GetCoPlayChat NOTIFY CoPlayChatChanged)
	Q_PROPERTY(int coplayChatUnread READ GetCoPlayChatUnread NOTIFY CoPlayChatChanged)

	private:
		SessionLog log;
		ChiakiSession session;
		ChiakiOpusDecoder opus_decoder;
		ChiakiOpusEncoder opus_encoder;
		bool connected;
		bool muted;
		bool mic_connected;
		bool port_guessing_enabled;
		int port_guess_count;
		int port_guess_socket_count;
#ifdef Q_OS_MACOS
		bool mic_authorization;
#endif
		bool allow_unmute;
		int input_block;
		QString host;
		int audio_volume;
		double measured_bitrate = 0;
		double average_packet_loss = 0;
		int32_t frames_lost = 0;
		int32_t pending_frames_lost = 0;
		double packet_loss_max = 0;
		QList<double> packet_loss_history;
		QAtomicInteger<quint64> decoder_flush_generation{0};
		bool cant_display = false;
		int haptics_handheld;
		float rumble_multiplier;
		int ps5_rumble_intensity;
		int ps5_trigger_intensity;
		uint8_t led_color[3];
		uint8_t player_index;
		QHash<int, Controller *> controllers;
#if CHIAKI_GUI_ENABLE_SETSU
		Setsu *setsu;
		QMap<QPair<QString, SetsuTrackingId>, uint8_t> setsu_ids;
		ChiakiControllerState setsu_state;
		SetsuDevice *setsu_motion_device;
		ChiakiOrientationTracker orient_tracker;
		ChiakiAccelNewZero setsu_accel_zero, setsu_real_accel;
		bool orient_dirty;
#endif

#if CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE
		SDeck *sdeck;
		QMutex sdeck_mutex;
		QThread *sdeck_haptics_thread = nullptr;
		SdeckHapticsWorker *sdeck_haptics_worker = nullptr;
		ChiakiControllerState sdeck_state;
		bool enable_steamdeck_haptics;
		ChiakiOrientationTracker sdeck_orient_tracker;
		ChiakiAccelNewZero sdeck_accel_zero, sdeck_real_accel;
		bool sdeck_orient_dirty;
		bool vertical_sdeck;
#endif
		QQueue<uint16_t> rumble_haptics;
		bool rumble_haptics_connected;
		bool rumble_haptics_on;
		float PS_TOUCHPAD_MAX_X, PS_TOUCHPAD_MAX_Y;
		ChiakiControllerState keyboard_state;
		ChiakiControllerState touch_state;
		QMap<int, uint8_t> touch_tracker;
		int8_t mouse_touch_id;
		ChiakiControllerState dpad_touch_state;
		uint16_t dpad_touch_increment;
		float trigger_override;
		float haptic_override;
		bool keyboard_controller_enabled;
		bool mouse_touch_enabled;
		bool dpad_regular;
		bool dpad_regular_touch_switched;
		uint dpad_touch_shortcut1;
		uint dpad_touch_shortcut2;
		uint dpad_touch_shortcut3;
		uint dpad_touch_shortcut4;
		int8_t dpad_touch_id;
		QPair<uint16_t, uint16_t> dpad_touch_value;
		QTimer *dpad_touch_timer, *dpad_touch_stop_timer;
		QElapsedTimer double_tap_timer;
		RumbleHapticsIntensity rumble_haptics_intensity;
		bool start_mic_unmuted;
		bool session_started;

		// Getting a lost console back is the work of whoever owns the session (see QmlBackend);
		// the session says when that is worth a try
		bool console_auto_reconnect = false;
		bool reconnect_attempt = false;
		std::atomic<bool> reconnect_wanted{false};
		std::atomic<bool> stop_requested{false};
		std::atomic<bool> video_stalled{false};
		std::atomic<uint64_t> last_video_ms{0};
		// What the user switched off for this computer (Settings::GetAudioVideoDisabled()) in a
		// console session. Where friends can join, the console still sends both for them and only
		// this computer leaves them out (local_*_off); otherwise the library does not take them
		// from the console in the first place.
		bool console_video_off = false;
		bool console_audio_off = false;
		std::atomic<bool> local_video_off{false};
		std::atomic<bool> local_audio_off{false};
		void CheckVideoStalled();

		ChiakiFfmpegDecoder *ffmpeg_decoder;
		void TriggerFfmpegFrameAvailable();
#if CHIAKI_LIB_ENABLE_PI_DECODER
		ChiakiPiDecoder *pi_decoder;
#endif

		QString audio_out_device_name;
		QString audio_in_device_name;
		SDL_AudioDeviceID audio_out;
		SDL_AudioDeviceID audio_in;
		size_t audio_out_sample_size;
		unsigned int audio_out_channels = 2;
		unsigned int audio_out_rate = 48000;
		QMutex audio_out_ring_mutex;
		QByteArray audio_out_ring_buf;
		size_t audio_out_ring_read_pos = 0;
		size_t audio_out_ring_write_pos = 0;
		size_t audio_out_ring_fill = 0;
		bool audio_out_overflow_logged = false;
		QMutex audio_out_drain_mutex;
		QWaitCondition audio_out_drain_wait;
		QThread *audio_out_drain_thread = nullptr;
		bool audio_out_drain_thread_running = false;
		bool audio_out_drain_requested = false;
		size_t haptics_buffer_size;
		unsigned int audio_buffer_size;
		ChiakiHolepunchSession holepunch_session;
#if CHIAKI_GUI_ENABLE_SPEEX
		SpeexEchoState *echo_state;
		SpeexPreprocessState *preprocess_state;
		bool speech_processing_enabled;
		SDL_AudioCVT mic_speex_cvt;
		SDL_AudioCVT echo_speex_cvt;
		uint8_t *echo_resampler_buf, *mic_resampler_buf;
		QMutex echo_to_cancel_mutex;
		QQueue<QByteArray> echo_to_cancel;
#endif
		SDL_AudioDeviceID haptics_output;
		// A connected controller restarts the retries; older ones stop
		quint64 haptics_retry_generation = 0;
		size_t haptics_retry_attempt = 0;
		SDL_AudioCVT haptics_cvt;
		uint8_t *haptics_resampler_buf;
		MicBuf mic_buf;
		QMutex mic_ring_mutex;
		QByteArray mic_ring_buf;
		size_t mic_ring_read_pos = 0;
		size_t mic_ring_write_pos = 0;
		size_t mic_ring_fill = 0;
		bool mic_ring_drain_queued = false;
		bool mic_ring_overflow_logged = false;
		QAtomicInteger<bool> mic_active = true;
		QMap<Qt::Key, int> key_map;
		QElapsedTimer connect_timer;

		void StartAudioOutDrainThread();
		void StopAudioOutDrainThread();
		void AudioOutDrainThreadMain();
		void QueueAudioOutData(const QByteArray &audio_data);
		void DrainAudioOutRingBuffer();
		void PushAudioFrame(int16_t *buf, size_t samples_count);
		void PushHapticsFrame(uint8_t *buf, size_t buf_size);
		void CantDisplayMessage(bool cant_display);
		ChiakiErrorCode InitiatePsnConnection(QString psn_token);
#ifdef Q_OS_MACOS
		void SetMicAuthorization(Authorization authorization);
#endif
#if CHIAKI_GUI_ENABLE_SETSU
		void HandleSetsuEvent(SetsuEvent *event);
#endif
#if CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE
		void HandleSDeckEvent(SDeckEvent *event);
#endif
		// Play Together, see streamsession_coplay.cpp
		CoPlayState *coplay = nullptr;
		CoPlayChatState *coplay_chat = nullptr;
		void CoPlayInit(const StreamSessionConnectInfo &connect_info);
		void CoPlayStopThreads();
		void CoPlayFini();
		void CoPlayStartGuest();
		void CoPlayStopGuest(bool sleep_console = false);
		void CoPlayStopHost();
		void CoPlayStartRoom(const StreamSessionConnectInfo &connect_info, size_t max_guests);
		void CoPlayUpdateRoom();
		void CoPlayFindCover();
		/** Set when the list of public rooms wants a picture of the game; the frame thread takes it. */
		QAtomicInteger<int> room_picture_wanted = 0;
		/** The Twitch channel for the public room list, kept for a room that starts later. */
		QString coplay_room_twitch;
		void CoPlayUpdatePending();
		void CoPlayGuestLost(coplay::DisconnectReason reason);
		void CoPlayGuestQuit();
		void CoPlayBeginRejoin(coplay::DisconnectReason reason);
		void CoPlayRejoinFinished(const std::shared_ptr<CoPlayRejoin> &job, const std::shared_ptr<coplay::Client> &client, const QString &error);
		void CoPlayCancelRejoin();
		void CoPlayOnEvent(ChiakiEvent *event);
		// Text chat, see streamsession_chat.cpp
		void CoPlayChatInit(bool previews);
		void CoPlayChatFini();
		QVariantMap CoPlayChatEntry(const coplay::ChatMessage &m, bool mine);
		void CoPlayChatAdd(const QVariantMap &entry, bool notify);
		void CoPlayChatSend(const QString &local_key, const QString &text, const coplay::ChatEmbed &embed, const QByteArray &image);
		void CoPlayOnChat(const coplay::ChatMessage &m);
		void CoPlayOnChatImage(const coplay::BlobRef &ref);
		/** The connection to the host, whichever it is right now (see joining again). */
		std::shared_ptr<coplay::Client> CoPlayClient() const;
		/** The sharing side, if we share. */
		coplay::Host *CoPlayHost() const;
		/** The name the others know us by. */
		QString CoPlayOwnName() const;
		void CoPlayOnSharingStarted();
		QString CoPlayInviteFor(const QString &host) const;
		void CoPlayOnHaptics(uint8_t *buf, size_t buf_size);
		void CoPlayUpdatePlayers();
		void CoPlayUpdateControllerKind();
		void CoPlayStartMedia();
		void CoPlayOpenMicrophone();
		void CoPlayUpdateMedia();
		void CoPlaySyncPlayers(const QVariantList &players);
		void CoPlayOnVoice(int slot, const uint8_t *data, size_t size);
		void CoPlayOnCamFrame(int slot, const QByteArray &jpeg);
		void CoPlayMediaTick();
		int CoPlayOwnSlot() const;
		void SendControllerState(ChiakiControllerState *state);
		static bool CoPlayVideoSampleCb(uint8_t *buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void *user);
		static void CoPlayAudioHeaderCb(ChiakiAudioHeader *header, void *user);
		static void CoPlayAudioFrameCb(uint8_t *buf, size_t buf_size, void *user);
		static void CoPlayGuestAudioSettingsCb(uint32_t channels, uint32_t rate, void *user);
		static void CoPlayGuestAudioFrameCb(int16_t *buf, size_t samples_count, void *user);

		void AdjustAdaptiveTriggerPacket(uint8_t *buf, uint8_t type);
		void WaitHaptics();
		void RetryHaptics(quint64 generation);

	private slots:
		void InitAudio(unsigned int channels, unsigned int rate);
		void InitMic(unsigned int channels, unsigned int rate);
		void InitHaptics();
		void Event(ChiakiEvent *event);
		/** A first connection that failed is tried again, after a pause. */
		void RetryStart();
		void DisconnectHaptics();
		void ConnectHaptics();
#if CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE
		void ConnectSdeckHaptics();
		void StopSdeckHaptics();
#endif
		void QueueRumbleHaptics(uint16_t strength);
		void ConnectRumbleHaptics();

	public:
		explicit StreamSession(const StreamSessionConnectInfo &connect_info, QObject *parent = nullptr);
		~StreamSession();

		bool IsConnected()	{ return connected; }
		bool IsConnecting()	{ return connect_timer.isValid(); }

		void Start();
		void Stop();
		void GoToBed();
	Q_INVOKABLE bool RequestIDR();
		void ToggleMute();
		void SetLoginPIN(const QString &pin);
		void GoHome();
		QString GetHost() { return host; }
		bool GetConnected() { return connected; }
		double GetMeasuredBitrate()	{ return measured_bitrate; }
		double GetAveragePacketLoss()	{ return average_packet_loss; }
		quint64 DecoderFlushGeneration() const { return decoder_flush_generation.loadRelaxed(); }
		int GetFramesLost()		{ return frames_lost; }
		bool GetMuted()	{ return muted; }
		void SetMuted(bool enable)	{ if (enable != muted) ToggleMute(); }
		void SetAudioVolume(int volume) { audio_volume = volume; }
		bool GetCantDisplay()	{ return cant_display; }
		bool IsCoPlayGuest() const;
		/** A guest whose host lets guests put the console to sleep when they leave. */
		bool CoPlayGuestMaySleep() const;
		bool IsCoPlayHosting() const;
		QString GetCoPlayInvite() const;
		/** With this computer's address on the local network. */
		QString GetCoPlayInviteLan() const;
		/** With its address on the internet; empty until that is known. */
		QString GetCoPlayInvitePublic() const;
		/** With its global IPv6 address; empty if it has none. */
		QString GetCoPlayInviteIPv6() const;
		/** The link for a friend's web browser; empty if this session takes no browsers. */
		QString GetCoPlayInviteWeb() const;
		/** While sharing: whether friends elsewhere get in without a forwarded port, in a sentence. Empty when there is nothing to say. */
		QString GetCoPlayReachText() const;
		/** Whether that sentence is good news. */
		bool GetCoPlayReachGood() const;
		QVariantList GetCoPlayPlayers() const;
		int GetCoPlayMixMode() const;
		void SetCoPlayMixMode(int mode);
		bool GetCoPlayVideoEnabled() const;
		void SetCoPlayVideoEnabled(bool enabled);
		bool GetPictureOff() const;
		bool GetSoundOff() const;
		/** A guest that lost the host and is joining again by itself. */
		bool IsCoPlayReconnecting() const;
		/** What to tell the user while that goes on. */
		QString CoPlayReconnectStatus() const;
		/**
		 * This session is an attempt to get a lost one back: if it cannot connect it says so
		 * at once instead of trying again by itself. Call before Start().
		 */
		void SetReconnectAttempt(bool attempt)	{ reconnect_attempt = attempt; }
		/**
		 * Valid from SessionQuit on: the connection to the console was lost (rather than ended)
		 * and connecting again is worth a try. Guests of a shared session are told to wait.
		 */
		bool ReconnectWanted() const	{ return reconnect_wanted; }
		/** Stop() or GoToBed() was called: whatever ends the session now, the user wanted it to end. */
		bool StopRequested() const	{ return stop_requested; }
		QVariantList GetCoPlayChat() const;
		/** The latest messages for the OBS overlay. */
		QJsonArray OverlayChat() const;
		int GetCoPlayChatUnread() const;
		/** Writes in the chat. A link in it gets a preview, made by this app. */
		Q_INVOKABLE void coplaySendChat(const QString &text);
		Q_INVOKABLE void coplayMarkChatRead();
		Q_INVOKABLE void coplayKick(int slot);
		/** Removes a guest and keeps it out for the rest of the session. */
		Q_INVOKABLE void coplayBlock(int slot);
		/** Lets somebody who knocks in, or not (id from coplayPending). */
		Q_INVOKABLE void coplayAdmit(const QString &id, bool allow);
		bool GetCoPlayPublic() const;
		QString GetCoPlayRoomTitle() const;
		QString GetCoPlayRoomGame() const;
		/** The page for browsers (no invite in it); empty when we do not host. */
		QString GetCoPlayWebPage() const;
		/** We host and friends may join from a web browser. */
		bool GetCoPlayWebGuestsOn() const;
		QString GetCoPlayRoomProblem() const;
		int GetCoPlayJoinPolicy() const;
		void SetCoPlayJoinPolicy(int policy);
		bool GetCoPlayLocked() const;
		void SetCoPlayLocked(bool locked);
		QVariantList GetCoPlayPending() const;
		/** The game the console runs, for the list of public rooms. */
		void SetCoPlayRoomGame(const QString &game, const QString &title_id);
		/** The host's Twitch channel for the public room list, or empty: not streaming, or not shown. */
		void SetCoPlayRoomStream(const QString &twitch_login);
		/** True once for each picture of the game the list of public rooms wants (any thread). */
		bool TakeRoomPictureRequest() { return room_picture_wanted.fetchAndStoreRelaxed(0) != 0; }
		/** A picture of the game (a small JPEG) for the list of public rooms. */
		void CoPlayRoomPicture(const QByteArray &jpeg);
		Q_INVOKABLE void coplaySetCanControl(int slot, bool allowed);
		Q_INVOKABLE void coplayCopyInvite();
		Q_INVOKABLE void coplayCopyInviteLan();
		Q_INVOKABLE void coplayCopyInvitePublic();
		Q_INVOKABLE void coplayCopyInviteIPv6();
		Q_INVOKABLE void coplayCopyInviteWeb();
		/** Sharing our session, or taking part in somebody else's. */
		bool IsCoPlayActive() const;
		bool GetCoPlayVoiceOn() const;
		void SetCoPlayVoiceOn(bool on);
		bool GetCoPlayMicMuted() const;
		void SetCoPlayMicMuted(bool muted);
		bool GetCoPlayDeafened() const;
		void SetCoPlayDeafened(bool deafened);
		bool GetCoPlayCameraOn() const;
		void SetCoPlayCameraOn(bool on);
		bool GetCoPlayCameraSupported() const;
		/** Who is talking right now: slot (as text) -> true. */
		QVariantMap GetCoPlayTalking() const;
		/** Changes with every new webcam picture, of anybody. */
		int GetCoPlayCamSerial() const;
		/** Stops playing one player on this computer only. */
		Q_INVOKABLE void coplaySetPlayerMuted(int slot, bool muted);
		/** Host only: stops passing a guest's microphone on to everybody. */
		Q_INVOKABLE void coplaySetSilenced(int slot, bool silenced);
		/** A player's latest webcam picture (JPEG) and a number that changes with each one. */
		QByteArray CoPlayCamJpeg(int slot, quint64 *serial = nullptr);
		/** The conversation for the stream overlay; see VoiceChat::SetTap(). */
		void CoPlaySetOverlayAudio(bool enabled, bool include_own);
		QByteArray CoPlayReadOverlayAudio();
		/** Our live stream gets the voices and our own microphone, as asked (both false: neither). */
		void CoPlaySetLiveAudio(bool voices, bool own_mic);
		/** We are on air (or not any more): the others see it next to our name. */
		void CoPlaySetLive(bool live);
		/** Asks everybody else to leave us out of their live streams (voice, webcam, name). */
		void CoPlaySetLeaveMeOut(bool leave_out);
		void CoPlayMicPermissionResult(bool granted);
		/** Everything the stream overlay shows: players, who is in control, what everybody presses. */
		QJsonObject OverlayState();
		/**
		 * What everybody presses right now, for drawing controllers on the game picture:
		 * one entry per player slot ("0", "1", ...) plus "combined", each with
		 * b (buttons), l2, r2, lx, ly, rx, ry.
		 */
		Q_INVOKABLE QVariantMap coplayPadStates();
		/** Ourselves as an entry of the player list, for when nobody else is in the session. */
		Q_INVOKABLE QVariantMap coplayOwnPlayer();
		/** As a guest: ask the host for everybody's inputs (only needed while something shows them). */
		Q_INVOKABLE void coplaySetWantInputs(bool wanted);
		/** A player's picture by the id the overlay state gives; empty if unknown. */
		QByteArray CoPlayAvatarData(const QString &id);
		ChiakiErrorCode ConnectPsnConnection(QString duid, bool ps5);
		void CancelPsnConnection(bool stop_thread);

		ChiakiLog *GetChiakiLog()				{ return log.GetChiakiLog(); }
		QList<Controller *> GetControllers()	{ return controllers.values(); }
		ChiakiFfmpegDecoder *GetFfmpegDecoder()	{ return ffmpeg_decoder; }
#if CHIAKI_LIB_ENABLE_PI_DECODER
		ChiakiPiDecoder *GetPiDecoder()	{ return pi_decoder; }
#endif
		void HandleKeyboardEvent(QKeyEvent *event);
		void HandleTouchEvent(QTouchEvent *event, qreal width, qreal height);
		void HandleDpadTouchEvent(ChiakiControllerState *state, bool placeholder = false);
		void HandleMouseReleaseEvent(QMouseEvent *event);
		void HandleMousePressEvent(QMouseEvent *event);
		void HandleMouseMoveEvent(QMouseEvent *event, qreal width, qreal height);
#if CHIAKI_GUI_ENABLE_SPEEX
		bool ProcessMicFrame(int16_t *echo_buf);
#endif
		void QueueMicData(const uint8_t *micdata, size_t micdata_size);
		void DrainMicRingBuffer();
		void ReadMic(const QByteArray &micdata);

		void BlockInput(bool block) { input_block = block ? 1 : 2; SendFeedbackState(); }

	signals:
		void FfmpegFrameAvailable();
		void RumbleHapticPushed(uint16_t strength);
#if CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE
		void SdeckHapticPushed(haptic_packet_t packetl, haptic_packet_t packetr);
#endif
		void DualSenseIntensityChanged(uint8_t intensity);
		void SessionQuit(ChiakiQuitReason reason, const QString &reason_str);
		void LoginPINRequested(bool incorrect);
		void DataHolepunchProgress(bool finished);
		void AutoRegistSucceeded(const ChiakiRegisteredHost &host);
		void NicknameReceived(QString nickname);
		void ConnectedChanged();
		void MeasuredBitrateChanged();
		void AveragePacketLossChanged();
		void FramesLostChanged();
		void MutedChanged();
		void CantDisplayChanged(bool cant_display);
		void FecFailure();
		void CoPlayChanged();
		void CoPlayMediaChanged();
		void CoPlayTalkingChanged();
		void CoPlayCamChanged();
		void CoPlayReconnectingChanged();
		void CoPlayChatChanged();
		/**
		 * Somebody else wrote something new (not what was said before we came). Named like a QML
		 * signal on purpose: a handler in QML ("onCoplayChatMessage") only finds a signal whose name
		 * starts with a small letter, and the notes on the game picture were never shown while this
		 * was "CoPlayChatMessage".
		 */
		void coplayChatMessage(const QVariantMap &message);
		/** Something the user should be told, e.g. that the port for sharing is taken. */
		void CoPlayNotice(const QString &text);
		/** Host: a guest left and asked for the console to be put to sleep, and guests may. */
		void CoPlaySleepRequested(const QString &name);
		/** Host: somebody knocks on the public room (QML: onCoplayKnock). */
		void coplayKnock(const QString &name);

	private slots:
		void UpdateGamepads();
		void DpadSendFeedbackState();
		void SendFeedbackState();
};

Q_DECLARE_METATYPE(ChiakiQuitReason)
#if CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE
Q_DECLARE_METATYPE(haptic_packet_t)
#endif

#endif // CHIAKI_STREAMSESSION_H
