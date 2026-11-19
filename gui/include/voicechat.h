// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_VOICECHAT_H
#define CHIAKI_VOICECHAT_H

#include <QByteArray>
#include <QMutex>
#include <QString>

#include <SDL.h>

#include <atomic>
#include <functional>
#include <thread>
#include <vector>

struct OpusEncoder;
struct OpusDecoder;
typedef struct chiaki_log_t ChiakiLog;

/**
 * The players of a shared session talking to each other.
 *
 * Sending: the microphone is recorded, quiet stretches are left out and the
 * rest goes out as Opus frames of 20 ms through the send callback.
 *
 * Receiving: every other player's Opus frames are decoded into a short buffer
 * of their own and mixed for the speakers. The same mix is offered to the
 * stream overlay (the "tap"), so that the conversation can end up in OBS.
 *
 * It does not know about the network; the session feeds it and sends for it.
 */
class VoiceChat
{
	public:
		static constexpr int kRate = 48000;
		static constexpr int kFrameSamples = 960; // 20 ms
		static constexpr int kMaxPlayers = 9;

		explicit VoiceChat(ChiakiLog *log);
		~VoiceChat();
		VoiceChat(const VoiceChat &) = delete;
		VoiceChat &operator=(const VoiceChat &) = delete;

		/** Gets every Opus frame worth sending. Called on an audio thread; set before opening the microphone. */
		void SetSendCallback(std::function<void(const uint8_t *, size_t)> callback) { send_callback = std::move(callback); }

		/** @param device empty for the system's default; "test" for a made-up voice instead of a microphone */
		bool OpenMicrophone(const QString &device, QString *error);
		void CloseMicrophone();
		bool MicrophoneOpen() const { return mic_open; }
		/** Muted: still recording (for the level meter), but nothing is sent. */
		void SetMuted(bool muted) { mic_muted = muted; }
		bool Muted() const { return mic_muted; }

		bool OpenSpeakers(const QString &device, QString *error);
		void CloseSpeakers();
		bool SpeakersOpen() const { return speaker_device != 0; }
		/** Deafened: nothing is played, but the stream overlay still gets the conversation. */
		void SetDeafened(bool deafened) { this->deafened = deafened; }
		bool Deafened() const { return deafened; }
		/** 0..200 percent. */
		void SetVolume(int percent);
		/** Stops playing one player here, for the others nothing changes. */
		void SetPlayerMuted(int slot, bool muted);
		bool PlayerMuted(int slot) const;

		/** One Opus frame of another player. Any thread. */
		void PushVoice(int slot, const uint8_t *data, size_t size);
		/** A player left: whoever gets the place next starts fresh. */
		void ForgetPlayer(int slot);

		/** How loud a player is right now, 0..100; 0 again shortly after they stop. */
		int Level(int slot) const;
		int OwnLevel() const;
		/** True while our own voice is being sent. */
		bool OwnSpeaking() const;

		/** Switches the copy of the conversation for the stream overlay on or off. */
		void SetTap(bool enabled, bool include_own);
		/** What was said since the last call: 48 kHz, mono, signed 16 bit little endian. */
		QByteArray ReadTap();

	private:
		struct Player
		{
			OpusDecoder *decoder = nullptr;
			std::vector<int16_t> ring;
			size_t read_pos = 0;
			size_t fill = 0;
			bool playing = false;
			bool muted = false;
			std::atomic<int> level{0};
			std::atomic<qint64> level_ms{0};
		};

		static void CaptureCallback(void *user, Uint8 *stream, int len);
		static void PlaybackCallback(void *user, Uint8 *stream, int len);
		void OnCaptured(const int16_t *samples, size_t count);
		void ProcessFrame(int16_t *frame);
		void Mix(int16_t *out, size_t count);
		void TestVoiceMain();

		ChiakiLog *log;
		std::function<void(const uint8_t *, size_t)> send_callback;

		// microphone
		SDL_AudioDeviceID mic_device = 0;
		std::atomic<bool> mic_open{false};
		std::atomic<bool> mic_muted{false};
		OpusEncoder *encoder = nullptr;
		std::vector<int16_t> capture_buf;
		size_t capture_fill = 0;
		std::atomic<int> own_level{0};
		std::atomic<qint64> own_level_ms{0};
		std::atomic<qint64> own_sent_ms{0};
		qint64 loud_until_ms = 0;
		void *preprocess = nullptr; // SpeexPreprocessState, if built with it
		std::thread test_thread;
		std::atomic<bool> test_running{false};

		// speakers
		SDL_AudioDeviceID speaker_device = 0;
		std::atomic<bool> deafened{false};
		std::atomic<int> volume{100};
		mutable QMutex play_mutex; // players, tap
		Player players[kMaxPlayers];
		std::vector<int32_t> mix_buf;

		// what goes to the stream overlay
		bool tap_enabled = false;
		bool tap_include_own = false;
		QByteArray tap;
		QMutex own_mutex; // own_ring
		std::vector<int16_t> own_ring;
		size_t own_read_pos = 0;
		size_t own_fill = 0;
};

#endif // CHIAKI_VOICECHAT_H
