// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "devicecheck.h"

#include "coplaycamera.h"
#include "settings.h"
#include "voicechat.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr int kRate = 48000;
constexpr int kTestMs = 2 * 60 * 1000;
constexpr double kPi = 3.14159265358979323846;

/** One soft mallet note (a marimba's partials, the high ones dying first) added into `out` at `start_ms`. */
void AddNote(std::vector<float> &left, std::vector<float> &right, int start_ms, double freq, double gain, double pan)
{
	struct Partial { double ratio, amp, decay; };
	static const Partial partials[] = {
		{ 1.0, 1.0, 1.9 },
		{ 3.93, 0.16, 7.5 },
		{ 9.24, 0.05, 16.0 },
	};
	const size_t start = static_cast<size_t>(start_ms) * kRate / 1000;
	const size_t length = static_cast<size_t>(kRate * 1.6);
	const double attack = 0.006 * kRate;
	// Constant power across the sides
	const double gain_l = gain * std::cos(pan * kPi / 2.0);
	const double gain_r = gain * std::sin(pan * kPi / 2.0);
	for(size_t i = 0; i < length && start + i < left.size(); i++)
	{
		const double t = static_cast<double>(i) / kRate;
		double v = 0.0;
		for(const Partial &p : partials)
		{
			if(freq * p.ratio >= kRate / 2.0 - 2000.0)
				continue;
			v += p.amp * std::exp(-p.decay * t) * std::sin(2.0 * kPi * freq * p.ratio * t);
		}
		if(i < attack)
			v *= 0.5 - 0.5 * std::cos(kPi * i / attack);
		left[start + i] += static_cast<float>(v * gain_l);
		right[start + i] += static_cast<float>(v * gain_r);
	}
}

} // namespace

std::vector<int16_t> devicecheck::Chime(std::vector<int> *steps_ms)
{
	const int total_ms = 3200;
	std::vector<float> left(static_cast<size_t>(total_ms) * kRate / 1000), right(left.size());
	// Left: G4 then C5. Right: E5 then G5. Both: C major, softer, a little apart
	AddNote(left, right, 0, 392.00, 0.30, 0.0);
	AddNote(left, right, 240, 523.25, 0.30, 0.0);
	AddNote(left, right, 850, 659.25, 0.30, 1.0);
	AddNote(left, right, 1090, 783.99, 0.30, 1.0);
	AddNote(left, right, 1700, 523.25, 0.16, 0.25);
	AddNote(left, right, 1740, 659.25, 0.14, 0.75);
	AddNote(left, right, 1780, 783.99, 0.13, 0.4);
	AddNote(left, right, 1820, 1046.50, 0.11, 0.6);
	if(steps_ms)
		*steps_ms = { 0, 850, 1700, total_ms };

	float peak = 0.0f;
	for(size_t i = 0; i < left.size(); i++)
		peak = std::max({ peak, std::abs(left[i]), std::abs(right[i]) });
	// About -9 dBFS at its loudest: clear, never harsh
	const float scale = peak > 0.0f ? 0.35f / peak : 0.0f;
	std::vector<int16_t> out(left.size() * 2);
	for(size_t i = 0; i < left.size(); i++)
	{
		// The last 60 ms fade out, so the end never clicks
		const size_t left_over = left.size() - i;
		const float fade = left_over < kRate * 60 / 1000 ? static_cast<float>(left_over) / (kRate * 60 / 1000) : 1.0f;
		out[2 * i] = static_cast<int16_t>(std::clamp(left[i] * scale * fade, -1.0f, 1.0f) * 32767.0f);
		out[2 * i + 1] = static_cast<int16_t>(std::clamp(right[i] * scale * fade, -1.0f, 1.0f) * 32767.0f);
	}
	return out;
}

DeviceCheck::DeviceCheck(Settings *settings, QObject *parent) : QObject(parent), settings(settings)
{
	mic_poll.setInterval(50);
	connect(&mic_poll, &QTimer::timeout, this, [this]() {
		const int level = voice ? voice->OwnLevel() : 0;
		if(level == mic_level)
			return;
		mic_level = level;
		emit micLevelChanged();
	});
	mic_limit.setSingleShot(true);
	mic_limit.setInterval(kTestMs);
	connect(&mic_limit, &QTimer::timeout, this, &DeviceCheck::stopMic);

	speaker_steps.setSingleShot(true);
	connect(&speaker_steps, &QTimer::timeout, this, [this]() {
		const std::vector<int> &steps = speaker_steps_ms;
		speaker_step++;
		if(speaker_step >= static_cast<int>(steps.size()) - 1)
		{
			StopSpeakers();
			return;
		}
		speaker_side = speaker_step + 1;
		emit speakerChanged();
		speaker_steps.start(steps[speaker_step + 1] - steps[speaker_step]);
	});

	camera_limit.setSingleShot(true);
	camera_limit.setInterval(kTestMs);
	connect(&camera_limit, &QTimer::timeout, this, &DeviceCheck::stopCamera);
}

DeviceCheck::~DeviceCheck()
{
	stopMic();
	StopSpeakers();
	stopCamera();
}

int DeviceCheck::micGate() const
{
	// The voice chat's gate (an RMS of 160) on its own scale of -60..0 dBFS
	const double db = 20.0 * std::log10(160.0 / 32768.0);
	return static_cast<int>(std::lround((db + 60.0) / 60.0 * 100.0));
}

void DeviceCheck::startMic()
{
	stopMic();
	voice = std::make_unique<VoiceChat>(nullptr);
	// Only listening: nothing is ever sent from here
	voice->SetMuted(true);
	QString error;
	if(!voice->OpenMicrophone(settings->GetAudioInDevice(), &error))
	{
		voice.reset();
		mic_error = error.isEmpty() ? tr("The microphone could not be opened.") : error;
		emit micChanged();
		return;
	}
	mic_error.clear();
	mic_testing = true;
	mic_poll.start();
	mic_limit.start();
	emit micChanged();
}

void DeviceCheck::stopMic()
{
	mic_poll.stop();
	mic_limit.stop();
	const bool was = mic_testing;
	if(voice)
	{
		voice->CloseMicrophone();
		voice.reset();
	}
	mic_testing = false;
	if(mic_level)
	{
		mic_level = 0;
		emit micLevelChanged();
	}
	if(was)
		emit micChanged();
}

void DeviceCheck::playTestSound()
{
	StopSpeakers();
	const std::vector<int16_t> chime = devicecheck::Chime(&speaker_steps_ms);
	const std::vector<int> &steps = speaker_steps_ms;

	SDL_AudioSpec want;
	SDL_zero(want);
	want.freq = kRate;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = 1024;
	want.callback = nullptr; // queued
	SDL_AudioSpec have;
	const QString device = settings->GetAudioOutDevice();
	const QByteArray name = device.toUtf8();
	speaker_device = SDL_OpenAudioDevice(device.isEmpty() ? nullptr : name.constData(), 0, &want, &have, 0);
	if(!speaker_device && !device.isEmpty())
		speaker_device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
	if(!speaker_device)
	{
		speaker_error = QString::fromUtf8(SDL_GetError());
		if(speaker_error.isEmpty())
			speaker_error = tr("The speakers could not be opened.");
		emit speakerChanged();
		return;
	}
	speaker_error.clear();
	SDL_QueueAudio(speaker_device, chime.data(), static_cast<Uint32>(chime.size() * sizeof(int16_t)));
	SDL_PauseAudioDevice(speaker_device, 0);
	speaker_step = 0;
	speaker_side = 1;
	speaker_steps.start(steps[1] - steps[0]);
	emit speakerChanged();
}

void DeviceCheck::StopSpeakers()
{
	speaker_steps.stop();
	const bool was = speaker_device != 0;
	if(speaker_device)
	{
		SDL_CloseAudioDevice(speaker_device);
		speaker_device = 0;
	}
	speaker_side = 0;
	if(was)
		emit speakerChanged();
}

void DeviceCheck::startCamera()
{
	stopCamera();
	camera = new CoPlayCamera(this);
	connect(camera, &CoPlayCamera::Frame, this, [this](const QByteArray &, const QImage &image) {
		CoPlayCamProvider::Set(kCameraSlot, image);
		camera_frame++;
		emit cameraFrameChanged();
	});
	connect(camera, &CoPlayCamera::Failed, this, [this](const QString &message) {
		camera_error = message;
		stopCamera();
		emit cameraChanged();
	});
	camera_error.clear();
	camera_testing = true;
	camera_limit.start();
	emit cameraChanged();
	camera->Start(settings->GetCoPlayCameraDevice(), settings->GetCoPlayCameraQuality());
}

void DeviceCheck::stopCamera()
{
	camera_limit.stop();
	const bool was = camera_testing;
	if(camera)
	{
		camera->disconnect(this);
		camera->Stop();
		camera->deleteLater();
		camera = nullptr;
	}
	camera_testing = false;
	CoPlayCamProvider::Clear(kCameraSlot);
	if(was)
		emit cameraChanged();
}

void DeviceCheck::stopAll()
{
	stopMic();
	StopSpeakers();
	stopCamera();
}
