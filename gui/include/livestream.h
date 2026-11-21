// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_LIVESTREAM_H
#define CHIAKI_LIVESTREAM_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" {
struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct AVFrame;
struct AVPacket;
struct AVBufferRef;
}

/**
 * Going live from the app: what this player sees and hears goes to Twitch (or any
 * RTMP(S) server) straight from this computer. Nothing passes through a server of ours.
 *
 * The stream window draws the picture a second time at stream size (game picture and
 * the display on top of it, never the menus) and hands it over here as NV12; the game
 * sound and the voices come from where the app plays them. One thread encodes on a
 * steady clock (H.264 + AAC; the last picture is repeated when none came in time), a
 * second one sends (FLV over RTMP or RTMPS), so that a slow upload leaves frames out
 * instead of holding up the encoder. A lost connection is made again by itself.
 *
 * There is one engine for the whole app (Engine::Get()); everything that feeds it can
 * call it at any time, it ignores what comes while it is not running.
 */
namespace live {

enum class State
{
	Off,
	Connecting,
	Live,
	/** Was live, lost the connection and is making it again. */
	Reconnecting,
	/** Gave up; Stats::error says why. */
	Failed
};

struct Config
{
	/** Where to send to, with the stream key. Never logged or shown. */
	std::string url;
	/** The same without the key, for the log. */
	std::string shown_url;
	int width = 1280;
	int height = 720;
	int fps = 30;
	int video_kbps = 4500;
	int audio_kbps = 160;
	/** Certificates to check an RTMPS server with; empty: whatever the TLS library finds itself. */
	std::string ca_file;
};

struct Stats
{
	State state = State::Off;
	/** What actually went out over the last seconds, picture and sound. */
	int kbps = 0;
	/** Pictures sent since going live. */
	uint64_t frames = 0;
	/** Pictures left out because the connection could not keep up. */
	uint64_t dropped = 0;
	/** Seconds the current connection is up. */
	int64_t live_seconds = 0;
	int reconnects = 0;
	/** The H.264 encoder in use, e.g. "h264_videotoolbox". */
	std::string encoder;
	/** The last problem, in words for the player; empty if there is none. */
	std::string error;
};

class Engine
{
	public:
		static Engine &Get();

		/** Starts going live; false if it is already running. */
		bool Start(const Config &config);
		/** Ends the stream and waits for it (a few seconds at most); safe to call at any time. */
		void Stop();
		/** Ends the stream without waiting: it says goodbye to the server in the background. */
		void RequestStop();
		bool Running() const { return running.load(); }

		/**
		 * The render thread asks this before drawing a picture for the stream: true when
		 * the next one is due. Pictures come at most at the stream's frame rate.
		 */
		bool WantsFrame(int64_t now_us);
		int Width() const { return width.load(); }
		int Height() const { return height.load(); }
		/** One picture, NV12 (BT.709, limited range) at Width() x Height(). Any thread. */
		void PushFrame(const uint8_t *y, int y_stride, const uint8_t *uv, int uv_stride, int frame_width, int frame_height);

		/** The game sound as played here: interleaved signed 16 bit. Any thread. */
		void PushGameAudio(const int16_t *pcm, size_t frames, int channels, int rate);
		/** The other players' voices as heard here: 48 kHz mono. Any thread. */
		void PushVoices(const int16_t *mono, size_t samples);
		/** Our own microphone while it is sending: 48 kHz mono. Any thread. */
		void PushOwnVoice(const int16_t *mono, size_t samples);
		/** A sound of our own (an alert): played out whole, after anything still playing. Any thread. */
		void PushEffects(const int16_t *pcm, size_t frames, int channels, int rate);
		/**
		 * Background music (a scene's loop): 48 kHz stereo, already at its level and faded.
		 * The player keeps a little ahead, MusicQueued() says how much is waiting. Any thread.
		 */
		void PushMusic(const int16_t *pcm, size_t frames);
		size_t MusicQueued();
		/** Loudness of each part in the stream, percent (0..200). */
		void SetLevels(int game_percent, int voices_percent, int mic_percent);

		Stats GetStats() const;

	private:
		struct Watchdog;

		Engine() = default;
		~Engine();
		Engine(const Engine &) = delete;
		Engine &operator=(const Engine &) = delete;

		/** Stereo float samples at 48 kHz, written by one part of the app and read by the encoder. */
		struct Fifo
		{
			std::vector<float> ring; // interleaved stereo
			size_t capacity = 0;     // in samples (two floats each)
			size_t read_pos = 0;
			size_t fill = 0;
			bool primed = false;
			/** Sounds that come whole (alerts): no waiting for a reserve, no catching up. */
			bool whole = false;
			// a simple resampler for sound that does not come at 48 kHz
			double phase = 0.0;
			float last[2] = {0.0f, 0.0f};

			void Reset(size_t samples);
			void WriteSample(float l, float r);
			/** Adds `count` samples, multiplied by `gain`, to `out`; pads with silence if it runs dry. */
			void MixInto(float *out, size_t count, float gain);
		};

		void Main();
		bool Connect(std::string *error);
		void RunLive();
		void Disconnect(bool graceful);
		bool OpenVideoEncoder(std::string *error);
		bool OpenAudioEncoder(std::string *error);
		void EncodeVideo(int64_t index);
		void EncodeAudio(int64_t first_sample);
		void Drain(AVCodecContext *ctx, AVStream *stream);
		void Queue(AVPacket *pkt);
		void SendMain();
		int64_t BacklogMs();
		void SetState(State state);
		void SetError(const std::string &error);
		void FillBlack();
		static int InterruptCb(void *opaque);

		Config config;
		std::thread thread;
		std::atomic<bool> running{false};
		std::atomic<bool> stop{false};
		std::mutex wait_mutex;
		std::condition_variable wait_cv;

		// pictures
		std::atomic<int> width{1280};
		std::atomic<int> height{720};
		std::atomic<int> fps{30};
		std::atomic<int64_t> next_capture_us{0};
		std::mutex video_mutex;
		std::vector<uint8_t> video_y;
		std::vector<uint8_t> video_uv;
		bool have_video = false;

		// sound
		std::mutex audio_mutex;
		Fifo game;
		Fifo voices;
		Fifo own;
		Fifo effects;
		Fifo music;
		std::atomic<int> game_level{100};
		std::atomic<int> voices_level{100};
		std::atomic<int> mic_level{100};
		std::vector<float> mix_buf;

		// the connection, only touched by the encoder thread (and the sender while it runs)
		AVFormatContext *out = nullptr;
		AVCodecContext *vctx = nullptr;
		AVCodecContext *actx = nullptr;
		AVStream *vstream = nullptr;
		AVStream *astream = nullptr;
		AVFrame *vframe = nullptr;
		AVFrame *aframe = nullptr;
		AVBufferRef *hw_device = nullptr;
		bool hw_upload = false;
		bool planar_input = false; // the encoder wants YUV420P instead of NV12
		std::string chosen_encoder;
		std::atomic<bool> abort_io{false};
		std::atomic<bool> connection_lost{false};

		// what waits to be sent
		std::thread sender;
		std::mutex send_mutex;
		std::condition_variable send_cv;
		std::deque<AVPacket *> send_queue;
		int64_t queued_bytes = 0;
		bool sender_done = false;
		bool sender_exited = false;

		// numbers for the player
		mutable std::mutex stats_mutex;
		Stats stats;
		int64_t live_since_us = 0;
		std::atomic<int64_t> bytes_sent{0};
		int64_t rate_window_us = 0;
		int64_t rate_window_bytes = 0;
};

const char *StateName(State state);

} // namespace live

#endif // CHIAKI_LIVESTREAM_H
