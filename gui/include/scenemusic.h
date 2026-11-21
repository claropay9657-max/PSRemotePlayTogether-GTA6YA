// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_SCENEMUSIC_H
#define CHIAKI_SCENEMUSIC_H

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>

#include <memory>

namespace sound { class Stream; }
class LiveScenes;

/**
 * The music of the stream scenes in the app's own live stream: Starting soon, Be right back
 * and Thanks for watching each have a loop of their own (res/sounds/music-*.flac) or the
 * player's own files, a switch and a level (LiveScenes' settings "music_<scene>_..."). While
 * one of them is on the stream its music plays into the stream's mix, fading in and out
 * over 1.5 s when the scene changes. A single FLAC or WAV loops without a gap; several own
 * files play in order (or shuffled) and start over. An own file that cannot be played is
 * skipped; when none can be, the app's own track plays and LiveScenes says so.
 *
 * The OBS scene pages play the same music themselves (res/overlay/scene.js).
 */
class SceneMusic : public QObject
{
	Q_OBJECT

	public:
		explicit SceneMusic(LiveScenes *scenes, QObject *parent = nullptr);
		~SceneMusic() override;

		/** The scenes that have music. */
		static bool HasMusic(const QString &scene);

	private:
		void Tick();
		/** Opens the music of a scene; false when there is none to play. */
		bool Open(const QString &scene);
		/** The next of the player's own files (or the same one again); false when none plays. */
		bool OpenNextOwn();
		bool OpenBuiltIn();
		/** The scene's music, as wanted now: switched on, its level 0..1. */
		double Wanted(QString *scene) const;

		QPointer<LiveScenes> scenes;
		QTimer timer;
		QString playing;
		std::unique_ptr<sound::Stream> stream;
		/** The player's own files in the order they play this round. */
		QStringList order;
		int order_at = -1;
		bool own = false;
		/** The level now, 0..1, moving towards `target` over 1.5 s. */
		double gain = 0.0;
		QByteArray builtin_file;
};

#endif // CHIAKI_SCENEMUSIC_H
