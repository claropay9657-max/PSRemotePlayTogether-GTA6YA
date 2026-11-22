// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_INTROSOUND_H
#define CHIAKI_INTROSOUND_H

#include "sounddecoder.h"

#include <QObject>

#include <SDL.h>

class Settings;

/**
 * The sound of the intro film (gui/src/qml/intro): one piece as long as the film
 * (res/sounds/intro.flac, made by scripts/sounds/make_intro_sound.py), played in step with it.
 * The film says where it is whenever it starts, goes on or jumps (play), and when it pauses or
 * is left (stop); between two such moments both simply run, each on its own clock.
 *
 * Nothing is loaded until the film is first played, and release() gives it back.
 */
class IntroSound : public QObject
{
	Q_OBJECT
	/** Switched off in the film, and remembered. */
	Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)

	public:
		explicit IntroSound(Settings *settings, QObject *parent = nullptr);
		~IntroSound();

		void SetSettings(Settings *settings) { this->settings = settings; }

		bool muted() const;
		void setMuted(bool muted);

		/** Plays from `seconds` into the film on. The first call reads the sound, which takes a moment. */
		Q_INVOKABLE void play(double seconds);
		Q_INVOKABLE void stop();
		/** The film is closed: the sound is not needed any more. */
		Q_INVOKABLE void release();

	signals:
		void mutedChanged();

	private:
		bool Load();

		Settings *settings;
		sound::Pcm pcm;
		bool tried = false;
		SDL_AudioDeviceID device = 0;
};

#endif // CHIAKI_INTROSOUND_H
