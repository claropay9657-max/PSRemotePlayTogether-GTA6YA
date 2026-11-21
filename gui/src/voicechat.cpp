// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "voicechat.h"
#include "livestream.h"

#include <chiaki/log.h>

#include <QDateTime>
#include <QMutexLocker>

#include <opus/opus.h>

#if CHIAKI_GUI_ENABLE_SPEEX
#include <speex/speex_preprocess.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

/** A player is heard once this much is buffered: rides out jitter between packets. */
static const size_t kStartSamples = VoiceChat::kFrameSamples * 3;
/** More than this buffered means we fell behind; skip ahead to keep the delay short. */
static const size_t kMaxBufferedSamples = VoiceChat::kRate * 3 / 10;
static const size_t kCatchUpSamples = VoiceChat::kFrameSamples * 4;
static const size_t kRingSamples = VoiceChat::kRate;
/** Below this the microphone counts as silent (about -46 dBFS). */
static const double kGateRms = 160.0;
/** Keep sending this long after the last loud frame, so word endings are not cut off. */
static const qint64 kHangoverMs = 450;
static const qint64 kLevelHoldMs = 250;
static const int kTapMaxBytes = VoiceChat::kRate * 2 * 2; // two seconds
static const double kPi = 3.14159265358979323846;

static qint64 NowMs()
{
	return QDateTime::currentMSecsSinceEpoch();
}

static int LevelOf(const int16_t *samples, size_t count, double *rms_out = nullptr)
{
	if(!count)
		return 0;
	double sum = 0.0;
	for(size_t i = 0; i < count; i++)
		sum += static_cast<double>(samples[i]) * samples[i];
	double rms = std::sqrt(sum / count);
	if(rms_out)
		*rms_out = rms;
	if(rms < 1.0)
		return 0;
	// -60 dBFS .. 0 dBFS onto 0 .. 100
	double db = 20.0 * std::log10(rms / 32768.0);
	return static_cast<int>(std::clamp((db + 60.0) / 60.0 * 100.0, 0.0, 100.0));
}

VoiceChat::VoiceChat(ChiakiLog *log) : log(log)
{
	capture_buf.resize(kFrameSamples);
	own_ring.resize(kRingSamples);
}

VoiceChat::~VoiceChat()
{
	CloseMicrophone();
	CloseSpeakers();
	for(Player &p : players)
	{
		if(p.decoder)
			opus_decoder_destroy(p.decoder);
	}
}

// ---------------------------------------------------------------- microphone

bool VoiceChat::OpenMicrophone(const QString &device, QString *error)
{
	CloseMicrophone();

	int opus_error = 0;
	encoder = opus_encoder_create(kRate, 1, OPUS_APPLICATION_VOIP, &opus_error);
	if(!encoder)
	{
		if(error)
			*error = QString::fromLatin1(opus_strerror(opus_error));
		return false;
	}
	opus_encoder_ctl(encoder, OPUS_SET_BITRATE(32000));
	opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(1));
	opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(5));
	opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));

#if CHIAKI_GUI_ENABLE_SPEEX
	SpeexPreprocessState *state = speex_preprocess_state_init(kFrameSamples, kRate);
	if(state)
	{
		int on = 1;
		speex_preprocess_ctl(state, SPEEX_PREPROCESS_SET_DENOISE, &on);
		speex_preprocess_ctl(state, SPEEX_PREPROCESS_SET_AGC, &on);
		preprocess = state;
	}
#endif

	capture_fill = 0;
	loud_until_ms = 0;

	if(device == QLatin1String("test"))
	{
		// For trying things out without a microphone (and without its permission prompt)
		test_running = true;
		test_thread = std::thread(&VoiceChat::TestVoiceMain, this);
		mic_open = true;
		CHIAKI_LOGI(log, "Voice chat: sending a test voice instead of a microphone");
		return true;
	}

	SDL_AudioSpec want;
	SDL_zero(want);
	want.freq = kRate;
	want.format = AUDIO_S16SYS;
	want.channels = 1;
	want.samples = kFrameSamples;
	want.callback = CaptureCallback;
	want.userdata = this;
	SDL_AudioSpec have;
	QByteArray name = device.toUtf8();
	mic_device = SDL_OpenAudioDevice(device.isEmpty() ? nullptr : name.constData(), 1, &want, &have, 0);
	if(!mic_device && !device.isEmpty())
	{
		CHIAKI_LOGW(log, "Voice chat: cannot open microphone '%s' (%s), trying the default one", name.constData(), SDL_GetError());
		mic_device = SDL_OpenAudioDevice(nullptr, 1, &want, &have, 0);
	}
	if(!mic_device)
	{
		if(error)
			*error = QString::fromUtf8(SDL_GetError());
		CloseMicrophone();
		return false;
	}
	mic_open = true;
	SDL_PauseAudioDevice(mic_device, 0);
	CHIAKI_LOGI(log, "Voice chat: microphone open");
	return true;
}

void VoiceChat::CloseMicrophone()
{
	mic_open = false;
	if(test_running.exchange(false) && test_thread.joinable())
		test_thread.join();
	if(mic_device)
	{
		// Returns only after the callback is done with the encoder
		SDL_CloseAudioDevice(mic_device);
		mic_device = 0;
	}
	if(encoder)
	{
		opus_encoder_destroy(encoder);
		encoder = nullptr;
	}
#if CHIAKI_GUI_ENABLE_SPEEX
	if(preprocess)
	{
		speex_preprocess_state_destroy(static_cast<SpeexPreprocessState *>(preprocess));
		preprocess = nullptr;
	}
#endif
	own_level = 0;
}

void VoiceChat::CaptureCallback(void *user, Uint8 *stream, int len)
{
	static_cast<VoiceChat *>(user)->OnCaptured(reinterpret_cast<const int16_t *>(stream), static_cast<size_t>(len) / sizeof(int16_t));
}

void VoiceChat::OnCaptured(const int16_t *samples, size_t count)
{
	while(count)
	{
		size_t n = std::min(count, static_cast<size_t>(kFrameSamples) - capture_fill);
		memcpy(capture_buf.data() + capture_fill, samples, n * sizeof(int16_t));
		capture_fill += n;
		samples += n;
		count -= n;
		if(capture_fill == kFrameSamples)
		{
			capture_fill = 0;
			ProcessFrame(capture_buf.data());
		}
	}
}

void VoiceChat::ProcessFrame(int16_t *frame)
{
#if CHIAKI_GUI_ENABLE_SPEEX
	if(preprocess)
		speex_preprocess_run(static_cast<SpeexPreprocessState *>(preprocess), frame);
#endif
	qint64 now = NowMs();
	double rms = 0.0;
	int level = LevelOf(frame, kFrameSamples, &rms);
	own_level = level;
	own_level_ms = now;
	if(rms >= kGateRms)
		loud_until_ms = now + kHangoverMs;
	if(mic_muted || now > loud_until_ms || !encoder)
		return;

	uint8_t packet[600];
	opus_int32 size = opus_encode(encoder, frame, kFrameSamples, packet, sizeof(packet));
	if(size <= 2)
		return; // nothing worth sending (Opus' own way of saying "silence")
	own_sent_ms = now;
	if(send_callback)
		send_callback(packet, static_cast<size_t>(size));
	// What the others hear of us goes into our own live stream too, if wanted
	if(stream_own)
		live::Engine::Get().PushOwnVoice(frame, kFrameSamples);

	QMutexLocker locker(&own_mutex);
	if(!tap_include_own)
		return;
	for(int i = 0; i < kFrameSamples; i++)
	{
		if(own_fill == own_ring.size())
		{
			own_read_pos = (own_read_pos + 1) % own_ring.size();
			own_fill--;
		}
		own_ring[(own_read_pos + own_fill) % own_ring.size()] = frame[i];
		own_fill++;
	}
}

void VoiceChat::TestVoiceMain()
{
	// Short notes with pauses in between, at a pitch that differs from app to app
	const double base = 330.0 + 110.0 * (static_cast<unsigned>(NowMs()) % 4);
	std::vector<int16_t> frame(kFrameSamples);
	double phase = 0.0;
	uint64_t n = 0;
	auto next = std::chrono::steady_clock::now();
	while(test_running)
	{
		double second = static_cast<double>(n) * kFrameSamples / kRate;
		double in_bar = std::fmod(second, 1.6);
		bool sounding = in_bar < 0.5;
		double freq = base * (static_cast<int>(second / 1.6) % 2 ? 1.25 : 1.0);
		for(int i = 0; i < kFrameSamples; i++)
		{
			phase += 2.0 * kPi * freq / kRate;
			frame[i] = sounding ? static_cast<int16_t>(7000.0 * std::sin(phase)) : 0;
		}
		n++;
		ProcessFrame(frame.data());
		next += std::chrono::milliseconds(20);
		std::this_thread::sleep_until(next);
	}
}

int VoiceChat::OwnLevel() const
{
	return NowMs() - own_level_ms > kLevelHoldMs ? 0 : own_level.load();
}

bool VoiceChat::OwnSpeaking() const
{
	return NowMs() - own_sent_ms <= kLevelHoldMs;
}

// ---------------------------------------------------------------- speakers

bool VoiceChat::OpenSpeakers(const QString &device, QString *error)
{
	CloseSpeakers();
	SDL_AudioSpec want;
	SDL_zero(want);
	want.freq = kRate;
	want.format = AUDIO_S16SYS;
	want.channels = 1;
	want.samples = kFrameSamples / 2;
	want.callback = PlaybackCallback;
	want.userdata = this;
	SDL_AudioSpec have;
	QByteArray name = device.toUtf8();
	speaker_device = SDL_OpenAudioDevice(device.isEmpty() ? nullptr : name.constData(), 0, &want, &have, 0);
	if(!speaker_device && !device.isEmpty())
		speaker_device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
	if(!speaker_device)
	{
		if(error)
			*error = QString::fromUtf8(SDL_GetError());
		return false;
	}
	SDL_PauseAudioDevice(speaker_device, 0);
	return true;
}

void VoiceChat::CloseSpeakers()
{
	if(speaker_device)
	{
		SDL_CloseAudioDevice(speaker_device);
		speaker_device = 0;
	}
}

void VoiceChat::SetVolume(int percent)
{
	volume = std::clamp(percent, 0, 200);
}

void VoiceChat::SetPlayerMuted(int slot, bool muted)
{
	if(slot < 0 || slot >= kMaxPlayers)
		return;
	QMutexLocker locker(&play_mutex);
	players[slot].muted = muted;
}

bool VoiceChat::PlayerMuted(int slot) const
{
	if(slot < 0 || slot >= kMaxPlayers)
		return false;
	QMutexLocker locker(&play_mutex);
	return players[slot].muted;
}

void VoiceChat::PushVoice(int slot, const uint8_t *data, size_t size)
{
	if(slot < 0 || slot >= kMaxPlayers || !data || !size)
		return;
	int16_t pcm[kFrameSamples * 6]; // Opus frames are at most 120 ms
	QMutexLocker locker(&play_mutex);
	Player &p = players[slot];
	if(!p.decoder)
	{
		int opus_error = 0;
		p.decoder = opus_decoder_create(kRate, 1, &opus_error);
		if(!p.decoder)
			return;
		p.ring.assign(kRingSamples, 0);
		p.read_pos = 0;
		p.fill = 0;
		p.playing = false;
	}
	int samples = opus_decode(p.decoder, data, static_cast<opus_int32>(size), pcm, kFrameSamples * 6, 0);
	if(samples <= 0)
		return;
	p.level = LevelOf(pcm, static_cast<size_t>(samples));
	p.level_ms = NowMs();
	for(int i = 0; i < samples; i++)
	{
		if(p.fill == p.ring.size())
		{
			p.read_pos = (p.read_pos + 1) % p.ring.size();
			p.fill--;
		}
		p.ring[(p.read_pos + p.fill) % p.ring.size()] = pcm[i];
		p.fill++;
	}
	if(p.fill > kMaxBufferedSamples)
	{
		size_t drop = p.fill - kCatchUpSamples;
		p.read_pos = (p.read_pos + drop) % p.ring.size();
		p.fill -= drop;
	}
}

void VoiceChat::ForgetPlayer(int slot)
{
	if(slot < 0 || slot >= kMaxPlayers)
		return;
	QMutexLocker locker(&play_mutex);
	Player &p = players[slot];
	if(p.decoder)
	{
		opus_decoder_destroy(p.decoder);
		p.decoder = nullptr;
	}
	p.fill = 0;
	p.playing = false;
	p.muted = false;
	p.level = 0;
}

int VoiceChat::Level(int slot) const
{
	if(slot < 0 || slot >= kMaxPlayers)
		return 0;
	const Player &p = players[slot];
	return NowMs() - p.level_ms > kLevelHoldMs ? 0 : p.level.load();
}

void VoiceChat::PlaybackCallback(void *user, Uint8 *stream, int len)
{
	static_cast<VoiceChat *>(user)->Mix(reinterpret_cast<int16_t *>(stream), static_cast<size_t>(len) / sizeof(int16_t));
}

void VoiceChat::Mix(int16_t *out, size_t count)
{
	QMutexLocker locker(&play_mutex);
	mix_buf.assign(count, 0);
	// Our live stream hears what we hear, without whoever asked to be left out of streams
	const bool to_stream = stream_voices;
	const uint32_t excluded = stream_excluded;
	if(to_stream)
		stream_buf.assign(count, 0);
	for(int slot = 0; slot < kMaxPlayers; slot++)
	{
		Player &p = players[slot];
		if(!p.decoder)
			continue;
		if(!p.playing)
		{
			if(p.fill < kStartSamples)
				continue;
			p.playing = true;
		}
		size_t take = std::min(count, p.fill);
		if(!p.muted)
		{
			for(size_t i = 0; i < take; i++)
				mix_buf[i] += p.ring[(p.read_pos + i) % p.ring.size()];
			if(to_stream && !(excluded & (1u << slot)))
			{
				for(size_t i = 0; i < take; i++)
					stream_buf[i] += p.ring[(p.read_pos + i) % p.ring.size()];
			}
		}
		p.read_pos = (p.read_pos + take) % p.ring.size();
		p.fill -= take;
		if(take < count)
			p.playing = false; // ran dry: wait for a little reserve again
	}

	auto clip = [](int32_t v) { return static_cast<int16_t>(std::clamp(v, -32768, 32767)); };

	if(to_stream)
	{
		stream_out.resize(count);
		for(size_t i = 0; i < count; i++)
			stream_out[i] = clip(stream_buf[i]);
		live::Engine::Get().PushVoices(stream_out.data(), count);
	}

	if(tap_enabled)
	{
		if(tap_include_own)
		{
			QMutexLocker own_locker(&own_mutex);
			size_t take = std::min(count, own_fill);
			// Our own voice goes to the overlay only, never to our own speakers
			int old = tap.size();
			tap.resize(old + static_cast<int>(count * sizeof(int16_t)));
			int16_t *dst = reinterpret_cast<int16_t *>(tap.data() + old);
			for(size_t i = 0; i < count; i++)
			{
				int32_t v = mix_buf[i];
				if(i < take)
					v += own_ring[(own_read_pos + i) % own_ring.size()];
				dst[i] = clip(v);
			}
			own_read_pos = (own_read_pos + take) % own_ring.size();
			own_fill -= take;
		}
		else
		{
			int old = tap.size();
			tap.resize(old + static_cast<int>(count * sizeof(int16_t)));
			int16_t *dst = reinterpret_cast<int16_t *>(tap.data() + old);
			for(size_t i = 0; i < count; i++)
				dst[i] = clip(mix_buf[i]);
		}
		if(tap.size() > kTapMaxBytes)
			tap.remove(0, tap.size() - kTapMaxBytes); // nobody is reading
	}

	if(deafened)
	{
		memset(out, 0, count * sizeof(int16_t));
		return;
	}
	int vol = volume;
	for(size_t i = 0; i < count; i++)
		out[i] = clip(mix_buf[i] * vol / 100);
}

void VoiceChat::SetTap(bool enabled, bool include_own)
{
	{
		QMutexLocker locker(&play_mutex);
		tap_enabled = enabled;
		if(!enabled)
			tap.clear();
		QMutexLocker own_locker(&own_mutex);
		tap_include_own = enabled && include_own;
		if(!tap_include_own)
			own_fill = 0;
	}
}

void VoiceChat::SetStreamTap(bool voices, bool own)
{
	stream_voices = voices;
	stream_own = own;
}

QByteArray VoiceChat::ReadTap()
{
	QMutexLocker locker(&play_mutex);
	QByteArray out;
	out.swap(tap);
	return out;
}
