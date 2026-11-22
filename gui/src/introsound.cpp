// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "introsound.h"

#include "settings.h"

#include <QFile>
#include <QLoggingCategory>

#include <algorithm>

Q_DECLARE_LOGGING_CATEGORY(chiakiGui)

IntroSound::IntroSound(Settings *settings, QObject *parent) : QObject(parent), settings(settings)
{
}

IntroSound::~IntroSound()
{
	release();
}

bool IntroSound::muted() const
{
	return !settings->GetIntroSound();
}

void IntroSound::setMuted(bool muted)
{
	if(muted == this->muted())
		return;
	settings->SetIntroSound(!muted);
	if(muted)
		stop();
	emit mutedChanged();
}

bool IntroSound::Load()
{
	if(tried)
		return !pcm.empty();
	tried = true;
	QFile file(QStringLiteral(":/sounds/intro.flac"));
	QString error;
	if(!file.open(QIODevice::ReadOnly) || !sound::DecodeAll(file.readAll(), &pcm, &error))
	{
		qCWarning(chiakiGui).noquote() << "Intro film: its sound cannot be played:" << error;
		pcm.clear();
		return false;
	}
	return true;
}

void IntroSound::play(double seconds)
{
	if(muted() || !Load())
		return;
	if(!device)
	{
		SDL_AudioSpec want;
		SDL_zero(want);
		want.freq = sound::kRate;
		want.format = AUDIO_S16SYS;
		want.channels = 2;
		want.samples = 1024;
		want.callback = nullptr; // queued
		SDL_AudioSpec have;
		const QByteArray name = settings->GetAudioOutDevice().toUtf8();
		device = SDL_OpenAudioDevice(name.isEmpty() ? nullptr : name.constData(), 0, &want, &have, 0);
		if(!device && !name.isEmpty())
			device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
		if(!device)
		{
			qCWarning(chiakiGui) << "Intro film: no sound, the speakers could not be opened:" << SDL_GetError();
			return;
		}
	}
	SDL_PauseAudioDevice(device, 1);
	SDL_ClearQueuedAudio(device);
	const size_t frames = pcm.size() / 2;
	const size_t from = std::min(frames, static_cast<size_t>(std::max(0.0, seconds) * sound::kRate));
	if(from < frames)
		SDL_QueueAudio(device, pcm.data() + from * 2, static_cast<Uint32>((frames - from) * 2 * sizeof(int16_t)));
	SDL_PauseAudioDevice(device, 0);
}

void IntroSound::stop()
{
	if(!device)
		return;
	SDL_PauseAudioDevice(device, 1);
	SDL_ClearQueuedAudio(device);
}

void IntroSound::release()
{
	if(device)
	{
		SDL_CloseAudioDevice(device);
		device = 0;
	}
	sound::Pcm().swap(pcm);
	tried = false;
}
