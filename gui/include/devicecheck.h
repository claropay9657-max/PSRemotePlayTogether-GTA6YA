// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_DEVICECHECK_H
#define CHIAKI_DEVICECHECK_H

#include <QObject>
#include <QString>
#include <QTimer>

#include <SDL.h>

#include <memory>
#include <vector>

class Settings;
class VoiceChat;
class CoPlayCamera;

/**
 * Trying the microphone, the speakers and the webcam outside a session, on the settings'
 * Voice & webcam page: how loud the microphone hears you (with the level from which the others
 * hear you), a chime on the left and then on the right of the chosen speakers, and the
 * webcam's picture as it would be sent ("image://coplaycam/1000/<cameraFrame>").
 *
 * Each test stops by itself after two minutes, when the page closes and when a session starts:
 * while playing, the session has the devices.
 */
class DeviceCheck : public QObject
{
	Q_OBJECT
	Q_PROPERTY(bool micTesting READ micTesting NOTIFY micChanged)
	/** 0..100, as the stream menu shows a player's voice. */
	Q_PROPERTY(int micLevel READ micLevel NOTIFY micLevelChanged)
	/** From this level on the others hear you (the voice chat's gate). */
	Q_PROPERTY(int micGate READ micGate CONSTANT)
	Q_PROPERTY(QString micError READ micError NOTIFY micChanged)
	Q_PROPERTY(bool speakerTesting READ speakerTesting NOTIFY speakerChanged)
	/** Where the chime plays now: 0 nowhere, 1 left, 2 right, 3 both. */
	Q_PROPERTY(int speakerSide READ speakerSide NOTIFY speakerChanged)
	Q_PROPERTY(QString speakerError READ speakerError NOTIFY speakerChanged)
	Q_PROPERTY(bool cameraTesting READ cameraTesting NOTIFY cameraChanged)
	/** Counts the pictures shown, for the image address. */
	Q_PROPERTY(int cameraFrame READ cameraFrame NOTIFY cameraFrameChanged)
	Q_PROPERTY(QString cameraError READ cameraError NOTIFY cameraChanged)
	Q_PROPERTY(int cameraSlot READ cameraSlot CONSTANT)

	public:
		/** The webcam picture's place in CoPlayCamProvider, away from the players' slots. */
		static constexpr int kCameraSlot = 1000;

		explicit DeviceCheck(Settings *settings, QObject *parent = nullptr);
		~DeviceCheck();

		void SetSettings(Settings *settings) { this->settings = settings; }

		bool micTesting() const { return mic_testing; }
		int micLevel() const { return mic_level; }
		int micGate() const;
		QString micError() const { return mic_error; }
		bool speakerTesting() const { return speaker_device != 0; }
		int speakerSide() const { return speaker_side; }
		QString speakerError() const { return speaker_error; }
		bool cameraTesting() const { return camera_testing; }
		int cameraFrame() const { return camera_frame; }
		QString cameraError() const { return camera_error; }
		int cameraSlot() const { return kCameraSlot; }

		Q_INVOKABLE void startMic();
		Q_INVOKABLE void stopMic();
		Q_INVOKABLE void playTestSound();
		Q_INVOKABLE void startCamera();
		Q_INVOKABLE void stopCamera();
		Q_INVOKABLE void stopAll();

	signals:
		void micChanged();
		void micLevelChanged();
		void speakerChanged();
		void cameraChanged();
		void cameraFrameChanged();

	private:
		void StopSpeakers();

		Settings *settings;

		std::unique_ptr<VoiceChat> voice;
		bool mic_testing = false;
		int mic_level = 0;
		QString mic_error;
		QTimer mic_poll;
		QTimer mic_limit;

		SDL_AudioDeviceID speaker_device = 0;
		int speaker_side = 0;
		QString speaker_error;
		QTimer speaker_steps;
		std::vector<int> speaker_steps_ms;
		int speaker_step = 0;

		CoPlayCamera *camera = nullptr;
		bool camera_testing = false;
		int camera_frame = 0;
		QString camera_error;
		QTimer camera_limit;
};

namespace devicecheck {
/**
 * The chime of the speaker test: 48 kHz, stereo, signed 16 bit. Two soft mallet notes on the
 * left, two on the right, then a chord on both. `steps_ms` gets when each part starts.
 */
std::vector<int16_t> Chime(std::vector<int> *steps_ms = nullptr);
} // namespace devicecheck

#endif // CHIAKI_DEVICECHECK_H
