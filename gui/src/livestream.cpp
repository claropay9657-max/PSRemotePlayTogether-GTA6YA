// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "livestream.h"

#include <QLoggingCategory>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

Q_DECLARE_LOGGING_CATEGORY(chiakiGui)

namespace live {

namespace {

constexpr int kRate = 48000;
constexpr size_t kFifoSamples = kRate;           // one second of sound waits at most
constexpr size_t kPrimeSamples = kRate * 60 / 1000; // start a part once 60 ms are there
constexpr size_t kMaxSamples = kRate * 400 / 1000;  // more than this waiting: catch up
/** More than this waits to be sent: leave pictures out until the connection catches up. */
constexpr int64_t kMaxBacklogMs = 2500;
/** Tries before giving up when the server was never reached in the first place. */
constexpr int kFirstTries = 3;
/** Tries before giving up after the connection was lost. */
constexpr int kReconnectTries = 20;

int64_t NowUs()
{
	return std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string AvError(int err)
{
	char buf[AV_ERROR_MAX_STRING_SIZE] = {};
	av_strerror(err, buf, sizeof(buf));
	return buf;
}

struct EncoderChoice
{
	const char *name;
	/** What it gets the picture in. */
	AVPixelFormat input;
	/** A hardware device the picture is uploaded to first, or none. */
	AVHWDeviceType device;
	/** Extra options: "key=value:key=value". */
	const char *options;
};

// Hardware first, then what the system or FFmpeg has in software.
const EncoderChoice kEncoders[] = {
#if defined(__APPLE__)
	{ "h264_videotoolbox", AV_PIX_FMT_NV12, AV_HWDEVICE_TYPE_NONE, "realtime=1:allow_sw=1" },
#elif defined(_WIN32)
	{ "h264_nvenc", AV_PIX_FMT_NV12, AV_HWDEVICE_TYPE_NONE, "preset=p5:tune=hq:rc=cbr" },
	{ "h264_amf", AV_PIX_FMT_NV12, AV_HWDEVICE_TYPE_NONE, "usage=transcoding:rc=cbr:quality=balanced" },
	{ "h264_mf", AV_PIX_FMT_NV12, AV_HWDEVICE_TYPE_NONE, "hw_encoding=1:rate_control=cbr:scenario=live_streaming" },
	// Windows' own encoder in software
	{ "h264_mf", AV_PIX_FMT_NV12, AV_HWDEVICE_TYPE_NONE, "rate_control=cbr:scenario=live_streaming" },
#else
	{ "h264_nvenc", AV_PIX_FMT_NV12, AV_HWDEVICE_TYPE_NONE, "preset=p5:tune=hq:rc=cbr" },
	{ "h264_vaapi", AV_PIX_FMT_NV12, AV_HWDEVICE_TYPE_VAAPI, "rc_mode=CBR" },
#endif
	{ "libx264", AV_PIX_FMT_YUV420P, AV_HWDEVICE_TYPE_NONE, "preset=veryfast:x264-params=nal-hrd=cbr" },
	{ "libopenh264", AV_PIX_FMT_YUV420P, AV_HWDEVICE_TYPE_NONE, "rc_mode=bitrate:allow_skip_frames=0" },
};

bool IsRtmps(const std::string &url)
{
	return url.rfind("rtmps://", 0) == 0;
}

} // namespace

/**
 * While it exists, network calls are cut short (Engine::abort_io) once Stop() is asked
 * for, or, with a limit, once that many milliseconds have passed.
 */
struct Engine::Watchdog
{
	Engine *self;
	std::thread t;
	std::mutex m;
	std::condition_variable cv;
	bool done = false;

	Watchdog(Engine *self, int64_t limit_ms) : self(self)
	{
		t = std::thread([this, limit_ms] {
			const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(limit_ms < 0 ? 0 : limit_ms);
			std::unique_lock<std::mutex> lk(m);
			while(!done)
			{
				if(limit_ms < 0 ? this->self->stop.load() : std::chrono::steady_clock::now() >= until)
				{
					this->self->abort_io = true;
					break;
				}
				cv.wait_for(lk, std::chrono::milliseconds(50));
			}
		});
	}
	~Watchdog()
	{
		{
			std::lock_guard<std::mutex> lk(m);
			done = true;
		}
		cv.notify_all();
		t.join();
	}
};

const char *StateName(State state)
{
	switch(state)
	{
		case State::Off: return "off";
		case State::Connecting: return "connecting";
		case State::Live: return "live";
		case State::Reconnecting: return "reconnecting";
		case State::Failed: return "failed";
	}
	return "off";
}

// ------------------------------------------------------------------ sound

void Engine::Fifo::Reset()
{
	ring.assign(kFifoSamples * 2, 0.0f);
	read_pos = 0;
	fill = 0;
	primed = false;
	phase = 0.0;
	last[0] = last[1] = 0.0f;
}

void Engine::Fifo::WriteSample(float l, float r)
{
	if(ring.empty())
		return;
	if(fill == kFifoSamples)
	{
		read_pos = (read_pos + 1) % kFifoSamples;
		fill--;
	}
	size_t at = (read_pos + fill) % kFifoSamples;
	ring[at * 2] = l;
	ring[at * 2 + 1] = r;
	fill++;
}

void Engine::Fifo::MixInto(float *out, size_t count, float gain)
{
	if(ring.empty())
		return;
	// Its clock runs a little faster than ours, or it came in a burst: catch up
	if(fill > kMaxSamples)
	{
		size_t drop = fill - kPrimeSamples;
		read_pos = (read_pos + drop) % kFifoSamples;
		fill -= drop;
	}
	if(!primed)
	{
		if(fill < kPrimeSamples)
			return;
		primed = true;
	}
	size_t take = std::min(count, fill);
	for(size_t i = 0; i < take; i++)
	{
		size_t at = (read_pos + i) % kFifoSamples;
		out[i * 2] += ring[at * 2] * gain;
		out[i * 2 + 1] += ring[at * 2 + 1] * gain;
	}
	read_pos = (read_pos + take) % kFifoSamples;
	fill -= take;
	if(take < count)
		primed = false; // ran dry: wait for a little reserve again
}

void Engine::PushGameAudio(const int16_t *pcm, size_t frames, int channels, int rate)
{
	if(!running || !pcm || channels < 1 || rate < 8000)
		return;
	std::lock_guard<std::mutex> locker(audio_mutex);
	const double step = static_cast<double>(rate) / kRate;
	for(size_t i = 0; i < frames; i++)
	{
		const float l = pcm[i * channels] / 32768.0f;
		const float r = channels > 1 ? pcm[i * channels + 1] / 32768.0f : l;
		if(rate == kRate)
		{
			game.WriteSample(l, r);
			continue;
		}
		// Between the previous sample and this one, as many of ours as fall in there
		while(game.phase < 1.0)
		{
			const float t = static_cast<float>(game.phase);
			game.WriteSample(game.last[0] + (l - game.last[0]) * t, game.last[1] + (r - game.last[1]) * t);
			game.phase += step;
		}
		game.phase -= 1.0;
		game.last[0] = l;
		game.last[1] = r;
	}
}

void Engine::PushVoices(const int16_t *mono, size_t samples)
{
	if(!running || !mono)
		return;
	std::lock_guard<std::mutex> locker(audio_mutex);
	for(size_t i = 0; i < samples; i++)
	{
		const float v = mono[i] / 32768.0f;
		voices.WriteSample(v, v);
	}
}

void Engine::PushOwnVoice(const int16_t *mono, size_t samples)
{
	if(!running || !mono)
		return;
	std::lock_guard<std::mutex> locker(audio_mutex);
	for(size_t i = 0; i < samples; i++)
	{
		const float v = mono[i] / 32768.0f;
		own.WriteSample(v, v);
	}
}

void Engine::SetLevels(int game_percent, int voices_percent, int mic_percent)
{
	game_level = std::clamp(game_percent, 0, 200);
	voices_level = std::clamp(voices_percent, 0, 200);
	mic_level = std::clamp(mic_percent, 0, 200);
}

// ------------------------------------------------------------------ pictures

bool Engine::WantsFrame(int64_t now_us)
{
	if(!running)
		return false;
	const int64_t interval = 1000000 / std::max(1, fps.load());
	int64_t next = next_capture_us.load();
	// A little early is fine: the display draws when it likes, the encoder takes the newest
	if(now_us < next - interval / 4)
		return false;
	next = now_us - next > interval ? now_us + interval : next + interval;
	next_capture_us = next;
	return true;
}

void Engine::PushFrame(const uint8_t *y, int y_stride, const uint8_t *uv, int uv_stride, int frame_width, int frame_height)
{
	if(!running || frame_width != width.load() || frame_height != height.load())
		return;
	std::lock_guard<std::mutex> locker(video_mutex);
	const size_t w = static_cast<size_t>(frame_width);
	const size_t h = static_cast<size_t>(frame_height);
	video_y.resize(w * h);
	video_uv.resize(w * (h / 2));
	for(size_t row = 0; row < h; row++)
		memcpy(video_y.data() + row * w, y + row * y_stride, w);
	for(size_t row = 0; row < h / 2; row++)
		memcpy(video_uv.data() + row * w, uv + row * uv_stride, w);
	have_video = true;
}

void Engine::FillBlack()
{
	const size_t w = static_cast<size_t>(width.load());
	const size_t h = static_cast<size_t>(height.load());
	video_y.assign(w * h, 16);
	video_uv.assign(w * (h / 2), 128);
}

// ------------------------------------------------------------------ start and stop

Engine &Engine::Get()
{
	static Engine engine;
	return engine;
}

Engine::~Engine()
{
	Stop();
}

bool Engine::Start(const Config &new_config)
{
	// Still running, or the last stream is still saying goodbye to its server
	if(running)
		return false;
	if(thread.joinable())
		thread.join();
	config = new_config;
	config.width = std::max(320, config.width) & ~1;
	config.height = std::max(180, config.height) & ~1;
	config.fps = std::clamp(config.fps, 10, 60);
	width = config.width;
	height = config.height;
	fps = config.fps;
	next_capture_us = 0;
	{
		std::lock_guard<std::mutex> locker(video_mutex);
		have_video = false;
		FillBlack();
	}
	{
		std::lock_guard<std::mutex> locker(audio_mutex);
		game.Reset();
		voices.Reset();
		own.Reset();
	}
	{
		std::lock_guard<std::mutex> locker(stats_mutex);
		stats = Stats();
		stats.state = State::Connecting;
	}
	chosen_encoder.clear();
	stop = false;
	running = true;
	thread = std::thread(&Engine::Main, this);
	return true;
}

void Engine::RequestStop()
{
	stop = true;
	wait_cv.notify_all();
	send_cv.notify_all();
}

void Engine::Stop()
{
	RequestStop();
	if(thread.joinable())
		thread.join();
	running = false;
}

void Engine::SetState(State state)
{
	std::lock_guard<std::mutex> locker(stats_mutex);
	stats.state = state;
	if(state == State::Live)
		stats.error.clear();
}

void Engine::SetError(const std::string &error)
{
	std::lock_guard<std::mutex> locker(stats_mutex);
	stats.error = error;
}

Stats Engine::GetStats() const
{
	std::lock_guard<std::mutex> locker(stats_mutex);
	Stats out = stats;
	if(out.state == State::Live && live_since_us > 0)
		out.live_seconds = (NowUs() - live_since_us) / 1000000;
	return out;
}

int Engine::InterruptCb(void *opaque)
{
	auto self = static_cast<Engine *>(opaque);
	return self->abort_io.load() ? 1 : 0;
}

// ------------------------------------------------------------------ the encoder thread

void Engine::Main()
{
	qCInfo(chiakiGui).noquote() << "Live: going live to" << QString::fromStdString(config.shown_url)
		<< QStringLiteral("(%1x%2, %3 fps, %4 + %5 kbit/s)").arg(config.width).arg(config.height)
			.arg(config.fps).arg(config.video_kbps).arg(config.audio_kbps);
	bool was_live = false;
	int failures = 0;
	while(!stop)
	{
		SetState(was_live ? State::Reconnecting : State::Connecting);
		std::string error;
		if(Connect(&error))
		{
			failures = 0;
			was_live = true;
			SetState(State::Live);
			qCInfo(chiakiGui).noquote() << "Live: on air with" << QString::fromStdString(chosen_encoder);
			RunLive();
			const bool lost = !stop;
			Disconnect(!lost);
			if(!lost)
				break;
			{
				std::lock_guard<std::mutex> locker(stats_mutex);
				stats.reconnects++;
			}
			qCWarning(chiakiGui) << "Live: the connection was lost, making it again";
		}
		else
		{
			Disconnect(false);
			if(stop)
				break;
			failures++;
			SetError(error);
			qCWarning(chiakiGui).noquote() << "Live: could not go live:" << QString::fromStdString(error);
			if(failures >= (was_live ? kReconnectTries : kFirstTries) || chosen_encoder.empty())
			{
				SetState(State::Failed);
				running = false;
				return;
			}
		}
		// Wait a little longer after each failed try: 2, 4, 8, 15, 15, ... seconds
		const int wait_s = std::min(15, 1 << std::min(failures + 1, 4));
		std::unique_lock<std::mutex> lk(wait_mutex);
		wait_cv.wait_for(lk, std::chrono::seconds(wait_s), [this] { return stop.load(); });
	}
	SetState(State::Off);
	qCInfo(chiakiGui) << "Live: the stream has ended";
	running = false;
}

bool Engine::OpenVideoEncoder(std::string *error)
{
	const char *forced = getenv("PSRPT_LIVE_ENCODER");
	std::string tried;
	for(const EncoderChoice &choice : kEncoders)
	{
		if(forced && *forced && strcmp(forced, choice.name) != 0)
			continue;
		// Once one worked, a new connection goes straight to it
		if(!chosen_encoder.empty() && chosen_encoder != choice.name)
			continue;
		const AVCodec *codec = avcodec_find_encoder_by_name(choice.name);
		if(!codec)
			continue;
		AVCodecContext *ctx = avcodec_alloc_context3(codec);
		if(!ctx)
			continue;
		ctx->width = config.width;
		ctx->height = config.height;
		ctx->time_base = AVRational{1, config.fps};
		ctx->framerate = AVRational{config.fps, 1};
		ctx->sample_aspect_ratio = AVRational{1, 1};
		// Twitch wants a full picture every two seconds and no B-frames make it simpler for everybody
		ctx->gop_size = config.fps * 2;
		ctx->max_b_frames = 0;
		ctx->bit_rate = static_cast<int64_t>(config.video_kbps) * 1000;
		ctx->rc_max_rate = ctx->bit_rate;
		ctx->rc_buffer_size = static_cast<int>(ctx->bit_rate);
		ctx->profile = AV_PROFILE_H264_HIGH;
		ctx->color_range = AVCOL_RANGE_MPEG;
		ctx->colorspace = AVCOL_SPC_BT709;
		ctx->color_primaries = AVCOL_PRI_BT709;
		ctx->color_trc = AVCOL_TRC_BT709;
		ctx->pix_fmt = choice.input;
		ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER; // FLV keeps the H.264 setup in its header
		if(strcmp(choice.name, "libopenh264") == 0)
			ctx->profile = AV_PROFILE_H264_CONSTRAINED_BASELINE;

		AVBufferRef *device = nullptr;
		if(choice.device != AV_HWDEVICE_TYPE_NONE)
		{
			if(av_hwdevice_ctx_create(&device, choice.device, nullptr, nullptr, 0) < 0)
			{
				avcodec_free_context(&ctx);
				continue;
			}
			AVBufferRef *frames = av_hwframe_ctx_alloc(device);
			if(!frames)
			{
				av_buffer_unref(&device);
				avcodec_free_context(&ctx);
				continue;
			}
			auto fc = reinterpret_cast<AVHWFramesContext *>(frames->data);
			fc->format = choice.device == AV_HWDEVICE_TYPE_VAAPI ? AV_PIX_FMT_VAAPI : AV_PIX_FMT_NONE;
			fc->sw_format = choice.input;
			fc->width = config.width;
			fc->height = config.height;
			fc->initial_pool_size = 8;
			if(fc->format == AV_PIX_FMT_NONE || av_hwframe_ctx_init(frames) < 0)
			{
				av_buffer_unref(&frames);
				av_buffer_unref(&device);
				avcodec_free_context(&ctx);
				continue;
			}
			ctx->pix_fmt = fc->format;
			ctx->hw_frames_ctx = frames;
		}

		AVDictionary *opts = nullptr;
		av_dict_parse_string(&opts, choice.options, "=", ":", 0);
		int err = avcodec_open2(ctx, codec, &opts);
		av_dict_free(&opts);
		if(err < 0)
		{
			tried += std::string(tried.empty() ? "" : ", ") + choice.name + " (" + AvError(err) + ")";
			av_buffer_unref(&device);
			avcodec_free_context(&ctx);
			continue;
		}
		vctx = ctx;
		hw_device = device;
		hw_upload = device != nullptr;
		planar_input = choice.input == AV_PIX_FMT_YUV420P;
		chosen_encoder = choice.name;
		{
			std::lock_guard<std::mutex> locker(stats_mutex);
			stats.encoder = chosen_encoder;
		}

		vframe = av_frame_alloc();
		vframe->format = choice.input;
		vframe->width = config.width;
		vframe->height = config.height;
		vframe->color_range = AVCOL_RANGE_MPEG;
		vframe->colorspace = AVCOL_SPC_BT709;
		vframe->color_primaries = AVCOL_PRI_BT709;
		vframe->color_trc = AVCOL_TRC_BT709;
		if(av_frame_get_buffer(vframe, 0) < 0)
		{
			*error = "Out of memory for the picture.";
			return false;
		}
		return true;
	}
	if(!tried.empty())
		qCWarning(chiakiGui).noquote() << "Live: no H.264 encoder would open:" << QString::fromStdString(tried);
	*error = "This computer has no H.264 encoder the app can use.";
	chosen_encoder.clear();
	return false;
}

bool Engine::OpenAudioEncoder(std::string *error)
{
	const AVCodec *codec = avcodec_find_encoder_by_name("aac");
	if(!codec)
	{
		*error = "This build has no AAC encoder.";
		return false;
	}
	actx = avcodec_alloc_context3(codec);
	actx->sample_fmt = AV_SAMPLE_FMT_FLTP;
	actx->sample_rate = kRate;
	actx->time_base = AVRational{1, kRate};
	AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
	av_channel_layout_copy(&actx->ch_layout, &stereo);
	actx->bit_rate = static_cast<int64_t>(config.audio_kbps) * 1000;
	actx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	int err = avcodec_open2(actx, codec, nullptr);
	if(err < 0)
	{
		*error = "The AAC encoder would not open: " + AvError(err);
		return false;
	}
	aframe = av_frame_alloc();
	aframe->format = AV_SAMPLE_FMT_FLTP;
	aframe->sample_rate = kRate;
	aframe->nb_samples = actx->frame_size > 0 ? actx->frame_size : 1024;
	av_channel_layout_copy(&aframe->ch_layout, &stereo);
	if(av_frame_get_buffer(aframe, 0) < 0)
	{
		*error = "Out of memory for the sound.";
		return false;
	}
	return true;
}

bool Engine::Connect(std::string *error)
{
	abort_io = false;
	connection_lost = false;
	queued_bytes = 0;
	sender_done = false;
	sender_exited = false;

	int err = avformat_alloc_output_context2(&out, nullptr, "flv", config.url.c_str());
	if(err < 0 || !out)
	{
		*error = "This build cannot write FLV: " + AvError(err);
		return false;
	}
	out->interrupt_callback.callback = &Engine::InterruptCb;
	out->interrupt_callback.opaque = this;

	if(!OpenVideoEncoder(error) || !OpenAudioEncoder(error))
		return false;

	vstream = avformat_new_stream(out, nullptr);
	astream = avformat_new_stream(out, nullptr);
	if(!vstream || !astream)
	{
		*error = "Out of memory.";
		return false;
	}
	avcodec_parameters_from_context(vstream->codecpar, vctx);
	vstream->time_base = vctx->time_base;
	vstream->avg_frame_rate = vctx->framerate;
	avcodec_parameters_from_context(astream->codecpar, actx);
	astream->time_base = actx->time_base;
	av_dict_set(&out->metadata, "encoder", "PSRemotePlayTogether", 0);

	// Stop() while the server does not answer: the interrupt callback gets us out
	Watchdog watch(this, -1);

	AVDictionary *opts = nullptr;
	// A connection that takes no data for this long is treated as lost (microseconds)
	av_dict_set(&opts, "rw_timeout", "15000000", 0);
	av_dict_set(&opts, "rtmp_live", "live", 0);
	if(IsRtmps(config.url))
	{
		// Never hand the stream key to a server that cannot prove who it is
		av_dict_set(&opts, "tls_verify", "1", 0);
		if(!config.ca_file.empty())
			av_dict_set(&opts, "ca_file", config.ca_file.c_str(), 0);
	}
	err = 0;
	if(!(out->oformat->flags & AVFMT_NOFILE))
		err = avio_open2(&out->pb, config.url.c_str(), AVIO_FLAG_WRITE, &out->interrupt_callback, &opts);
	av_dict_free(&opts);
	if(err < 0)
	{
		if(err == AVERROR(ECONNREFUSED))
			*error = "The server refused the connection. Check the address.";
		else if(err == AVERROR_EXIT)
			*error = "Stopped.";
		else
			*error = "The server did not take the stream (" + AvError(err) + "). Check the address and the stream key.";
		return false;
	}
	err = avformat_write_header(out, nullptr);
	if(err < 0)
	{
		*error = "The server did not take the stream (" + AvError(err) + ").";
		return false;
	}
	if(stop)
		return false;

	live_since_us = NowUs();
	bytes_sent = 0;
	rate_window_us = live_since_us;
	rate_window_bytes = 0;
	sender = std::thread(&Engine::SendMain, this);
	return true;
}

void Engine::RunLive()
{
	const int64_t t0 = NowUs();
	int64_t frame_index = 0;
	int64_t audio_samples = 0;
	const int frame_samples = aframe->nb_samples;
	mix_buf.assign(static_cast<size_t>(frame_samples) * 2, 0.0f);

	while(!stop && !connection_lost)
	{
		const int64_t now = NowUs() - t0;
		const int64_t video_due = frame_index * 1000000 / config.fps;
		const int64_t audio_due = (audio_samples + frame_samples) * 1000000 / kRate;

		// Held up for more than a second (the computer was asleep, say): carry on from now
		if(now - video_due > 1000000)
		{
			const int64_t skip_to = now * config.fps / 1000000;
			{
				std::lock_guard<std::mutex> locker(stats_mutex);
				stats.dropped += static_cast<uint64_t>(skip_to - frame_index);
			}
			frame_index = skip_to;
			continue;
		}
		if(now - audio_due > 1000000)
		{
			audio_samples = now * kRate / 1000000 / frame_samples * frame_samples;
			continue;
		}

		if(now >= video_due && video_due <= audio_due)
		{
			EncodeVideo(frame_index++);
		}
		else if(now >= audio_due)
		{
			EncodeAudio(audio_samples);
			audio_samples += frame_samples;
		}
		else
		{
			const int64_t wake = std::min(video_due, audio_due);
			std::unique_lock<std::mutex> lk(wait_mutex);
			wait_cv.wait_for(lk, std::chrono::microseconds(std::max<int64_t>(500, wake - now)),
				[this] { return stop.load() || connection_lost.load(); });
		}

		// The rate shown to the player, over the last two seconds
		const int64_t t = NowUs();
		if(t - rate_window_us >= 2000000)
		{
			const int64_t sent = bytes_sent.load();
			const int kbps = static_cast<int>((sent - rate_window_bytes) * 8 * 1000 / std::max<int64_t>(1, t - rate_window_us));
			rate_window_us = t;
			rate_window_bytes = sent;
			std::lock_guard<std::mutex> locker(stats_mutex);
			stats.kbps = kbps;
		}
	}
}

int64_t Engine::BacklogMs()
{
	std::lock_guard<std::mutex> locker(send_mutex);
	const int kbps = std::max(1, config.video_kbps + config.audio_kbps);
	return queued_bytes * 8 / kbps;
}

void Engine::EncodeVideo(int64_t index)
{
	if(BacklogMs() > kMaxBacklogMs)
	{
		std::lock_guard<std::mutex> locker(stats_mutex);
		stats.dropped++;
		return;
	}
	if(av_frame_make_writable(vframe) < 0)
		return;
	{
		std::lock_guard<std::mutex> locker(video_mutex);
		const int w = config.width;
		const int h = config.height;
		if(video_y.size() < static_cast<size_t>(w) * h)
			FillBlack();
		for(int row = 0; row < h; row++)
			memcpy(vframe->data[0] + row * vframe->linesize[0], video_y.data() + static_cast<size_t>(row) * w, w);
		if(!planar_input)
		{
			for(int row = 0; row < h / 2; row++)
				memcpy(vframe->data[1] + row * vframe->linesize[1], video_uv.data() + static_cast<size_t>(row) * w, w);
		}
		else
		{
			// NV12 -> YUV420P
			for(int row = 0; row < h / 2; row++)
			{
				const uint8_t *src = video_uv.data() + static_cast<size_t>(row) * w;
				uint8_t *u = vframe->data[1] + row * vframe->linesize[1];
				uint8_t *v = vframe->data[2] + row * vframe->linesize[2];
				for(int x = 0; x < w / 2; x++)
				{
					u[x] = src[x * 2];
					v[x] = src[x * 2 + 1];
				}
			}
		}
	}
	vframe->pts = index;

	AVFrame *send = vframe;
	AVFrame *hw = nullptr;
	if(hw_upload)
	{
		hw = av_frame_alloc();
		if(av_hwframe_get_buffer(vctx->hw_frames_ctx, hw, 0) < 0 || av_hwframe_transfer_data(hw, vframe, 0) < 0)
		{
			av_frame_free(&hw);
			std::lock_guard<std::mutex> locker(stats_mutex);
			stats.dropped++;
			return;
		}
		hw->pts = index;
		send = hw;
	}
	int err = avcodec_send_frame(vctx, send);
	av_frame_free(&hw);
	if(err < 0 && err != AVERROR(EAGAIN))
	{
		qCWarning(chiakiGui).noquote() << "Live: the encoder failed:" << QString::fromStdString(AvError(err));
		connection_lost = true;
		return;
	}
	Drain(vctx, vstream);
	std::lock_guard<std::mutex> locker(stats_mutex);
	stats.frames++;
}

void Engine::EncodeAudio(int64_t first_sample)
{
	const size_t count = static_cast<size_t>(aframe->nb_samples);
	std::fill(mix_buf.begin(), mix_buf.end(), 0.0f);
	{
		std::lock_guard<std::mutex> locker(audio_mutex);
		game.MixInto(mix_buf.data(), count, game_level / 100.0f);
		voices.MixInto(mix_buf.data(), count, voices_level / 100.0f);
		own.MixInto(mix_buf.data(), count, mic_level / 100.0f);
	}
	if(av_frame_make_writable(aframe) < 0)
		return;
	float *l = reinterpret_cast<float *>(aframe->data[0]);
	float *r = reinterpret_cast<float *>(aframe->data[1]);
	for(size_t i = 0; i < count; i++)
	{
		l[i] = std::clamp(mix_buf[i * 2], -1.0f, 1.0f);
		r[i] = std::clamp(mix_buf[i * 2 + 1], -1.0f, 1.0f);
	}
	aframe->pts = first_sample;
	int err = avcodec_send_frame(actx, aframe);
	if(err < 0 && err != AVERROR(EAGAIN))
	{
		qCWarning(chiakiGui).noquote() << "Live: the sound encoder failed:" << QString::fromStdString(AvError(err));
		connection_lost = true;
		return;
	}
	Drain(actx, astream);
}

void Engine::Drain(AVCodecContext *ctx, AVStream *stream)
{
	for(;;)
	{
		AVPacket *pkt = av_packet_alloc();
		int err = avcodec_receive_packet(ctx, pkt);
		if(err < 0)
		{
			av_packet_free(&pkt);
			return;
		}
		av_packet_rescale_ts(pkt, ctx->time_base, stream->time_base);
		pkt->stream_index = stream->index;
		Queue(pkt);
	}
}

void Engine::Queue(AVPacket *pkt)
{
	{
		std::lock_guard<std::mutex> locker(send_mutex);
		queued_bytes += pkt->size;
		send_queue.push_back(pkt);
	}
	send_cv.notify_one();
}

// ------------------------------------------------------------------ the sender thread

void Engine::SendMain()
{
	for(;;)
	{
		AVPacket *pkt = nullptr;
		{
			std::unique_lock<std::mutex> lk(send_mutex);
			send_cv.wait(lk, [this] { return !send_queue.empty() || sender_done || abort_io.load(); });
			if(abort_io || send_queue.empty())
				break;
			pkt = send_queue.front();
			send_queue.pop_front();
			queued_bytes -= pkt->size;
		}
		const int size = pkt->size;
		int err = av_interleaved_write_frame(out, pkt);
		av_packet_free(&pkt);
		if(err < 0)
		{
			if(!abort_io)
			{
				SetError("The connection to the server was lost (" + AvError(err) + ").");
				qCWarning(chiakiGui).noquote() << "Live: sending failed:" << QString::fromStdString(AvError(err));
			}
			connection_lost = true;
			wait_cv.notify_all();
			break;
		}
		bytes_sent += size;
	}
	std::lock_guard<std::mutex> locker(send_mutex);
	sender_done = true;
	sender_exited = true;
	send_cv.notify_all();
}

void Engine::Disconnect(bool graceful)
{
	// Saying goodbye to the server may take a moment, but never long
	Watchdog limit(this, graceful ? 5000 : 0);
	if(sender.joinable())
	{
		if(graceful)
		{
			// What the encoders still hold, then give the sender a few seconds to get it all out
			avcodec_send_frame(vctx, nullptr);
			Drain(vctx, vstream);
			avcodec_send_frame(actx, nullptr);
			Drain(actx, astream);
			std::unique_lock<std::mutex> lk(send_mutex);
			sender_done = true;
			send_cv.notify_all();
			send_cv.wait(lk, [this] { return sender_exited || abort_io.load(); });
		}
		else
		{
			abort_io = true;
		}
		send_cv.notify_all();
		sender.join();
		if(graceful && !abort_io && !connection_lost)
			av_write_trailer(out);
	}
	{
		std::lock_guard<std::mutex> locker(send_mutex);
		for(AVPacket *pkt : send_queue)
			av_packet_free(&pkt);
		send_queue.clear();
		queued_bytes = 0;
	}
	if(out)
	{
		if(out->pb && !(out->oformat->flags & AVFMT_NOFILE))
			avio_closep(&out->pb);
		avformat_free_context(out);
		out = nullptr;
	}
	vstream = nullptr;
	astream = nullptr;
	avcodec_free_context(&vctx);
	avcodec_free_context(&actx);
	av_frame_free(&vframe);
	av_frame_free(&aframe);
	av_buffer_unref(&hw_device);
	hw_upload = false;
	live_since_us = 0;
	std::lock_guard<std::mutex> locker(stats_mutex);
	stats.kbps = 0;
}

} // namespace live
